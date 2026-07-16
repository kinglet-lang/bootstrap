#!/usr/bin/env python3
"""Emit reviewdog RDJSONL diagnostics from an LLM PR review.

This script is intentionally safe for pull_request_target workflows: it reads PR
metadata, patches, and file contents through the GitHub API, but it never checks
out or executes code from the pull request head.
"""

from __future__ import annotations

import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any


MAX_FILES = int(os.getenv("AI_REVIEWDOG_MAX_FILES", "40"))
MAX_PATCH_CHARS = int(os.getenv("AI_REVIEWDOG_MAX_PATCH_CHARS", "50000"))
MAX_FILE_CHARS = int(os.getenv("AI_REVIEWDOG_MAX_FILE_CHARS", "12000"))
MIN_BLOCKING_CONFIDENCE = float(os.getenv("AI_REVIEWDOG_MIN_BLOCKING_CONFIDENCE", "0.75"))
INCLUDE_WARNINGS = os.getenv("AI_REVIEWDOG_INCLUDE_WARNINGS", "false").lower() == "true"
MODEL = os.getenv("DEEPSEEK_MODEL", "deepseek-chat")
DEEPSEEK_BASE_URL = os.getenv("DEEPSEEK_BASE_URL", "https://api.deepseek.com")

UNCERTAIN_RE = re.compile(
    r"\b(not shown|not visible|not clear|cannot confirm|can't confirm|verify|confirm|"
    r"if (this|there|it|already)|assuming|appears to|seems to|may|might|could)\b",
    re.IGNORECASE,
)

FALSE_POSITIVE_RE = re.compile(
    r"\b(kl_release.*KirOpcode|KirOpcode.*kl_release|"
    r"read/write.*close fd|close fd.*read/write|"
    r"io::reader.*close|io::writer.*sync|"
    r"reader.*writer.*close|writer.*sync|"
    r"\$.*invalid.*LLVM|LLVM.*\$.*invalid|"
    r"0-on-error.*ambig|ambig.*0-on-error|"
    r"conflate[s]?.*EOF.*error|EOF.*error.*ambig)\b",
    re.IGNORECASE,
)


@dataclass
class PullRequest:
    number: int
    title: str
    body: str
    base_sha: str
    head_sha: str
    head_repo_full_name: str


@dataclass
class ChangedFile:
    path: str
    status: str
    patch: str
    raw_url: str | None
    changed_lines: set[int]
    head_text: str | None = None


def log(message: str) -> None:
    print(f"ai-reviewdog: {message}", file=sys.stderr)


def request_json(url: str, *, token: str | None = None, method: str = "GET", data: Any = None) -> Any:
    body = None if data is None else json.dumps(data).encode("utf-8")
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "kinglet-ai-reviewdog",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if data is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.loads(resp.read().decode("utf-8"))


def request_text(url: str, *, token: str | None = None) -> str:
    headers = {"User-Agent": "kinglet-ai-reviewdog"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read().decode("utf-8", "replace")


def github_api(path: str) -> str:
    return "https://api.github.com" + path


def parse_changed_lines(patch: str) -> set[int]:
    """Return new-file line numbers touched by a unified patch."""
    lines: set[int] = set()
    new_line: int | None = None
    for line in patch.splitlines():
        if line.startswith("@@"):
            m = re.search(r"\+(\d+)(?:,(\d+))?", line)
            if not m:
                new_line = None
                continue
            new_line = int(m.group(1))
            continue
        if new_line is None:
            continue
        if line.startswith("+") and not line.startswith("+++"):
            lines.add(new_line)
            new_line += 1
        elif line.startswith("-") and not line.startswith("---"):
            continue
        else:
            new_line += 1
    return lines


def load_pr(repo: str, pr_number: int, token: str) -> PullRequest:
    pr = request_json(github_api(f"/repos/{repo}/pulls/{pr_number}"), token=token)
    return PullRequest(
        number=pr_number,
        title=pr.get("title") or "",
        body=pr.get("body") or "",
        base_sha=pr["base"]["sha"],
        head_sha=pr["head"]["sha"],
        head_repo_full_name=pr["head"]["repo"]["full_name"],
    )


def load_files(repo: str, pr_number: int, token: str) -> list[ChangedFile]:
    files: list[ChangedFile] = []
    page = 1
    while True:
        url = github_api(f"/repos/{repo}/pulls/{pr_number}/files?per_page=100&page={page}")
        batch = request_json(url, token=token)
        if not batch:
            break
        for item in batch:
            patch = item.get("patch") or ""
            if not patch:
                continue
            path = item["filename"]
            if path.startswith(("vendor/", "third_party/", "tools/llvm/")):
                continue
            files.append(
                ChangedFile(
                    path=path,
                    status=item.get("status", "modified"),
                    patch=patch,
                    raw_url=item.get("raw_url"),
                    changed_lines=parse_changed_lines(patch),
                )
            )
        if len(batch) < 100:
            break
        page += 1
    return files[:MAX_FILES]


def fill_head_contents(files: list[ChangedFile], token: str) -> None:
    for file in files:
        if not file.raw_url or file.status == "removed":
            continue
        try:
            text = request_text(file.raw_url, token=token)
        except Exception as exc:  # noqa: BLE001 - best-effort context only.
            log(f"could not fetch {file.path}: {exc}")
            continue
        if "\0" in text:
            continue
        file.head_text = text[:MAX_FILE_CHARS]


def load_repo_context() -> str:
    chunks: list[str] = []
    for path in ("AGENTS.md", "CONTRIBUTING.md"):
        try:
            with open(path, "r", encoding="utf-8") as fh:
                text = fh.read()
        except FileNotFoundError:
            continue
        chunks.append(f"## {path}\n{text[:6000]}")
    chunks.append(
        "## Kinglet project review facts\n"
        "- Bootstrap is the C++20 reference compiler for Kinglet.\n"
        "- io::reader is a capability for read; it does not imply close().\n"
        "- io::writer is a capability for write; it does not imply sync() or close().\n"
        "- fs::file is a resource type; passing it by value transfers ownership.\n"
        "- kl_release handles runtime KlKind values, not KIR opcodes.\n"
        "- Do not mark a missing switch/lowering/release case as blocking merely because it is not visible in the diff.\n"
        "- LLVM symbol names are created through LLVM APIs; '$' in an internal function name is not by itself a blocker.\n"
        "- Current file read/write APIs intentionally use 0 for EOF/error until a richer error model exists.\n"
    )
    return "\n\n".join(chunks)


def build_prompt(pr: PullRequest, files: list[ChangedFile], repo_context: str) -> list[dict[str, str]]:
    rendered_files: list[str] = []
    patch_chars = 0
    for file in files:
        if patch_chars >= MAX_PATCH_CHARS:
            break
        patch = file.patch[: max(0, MAX_PATCH_CHARS - patch_chars)]
        patch_chars += len(patch)
        rendered = [
            f"### {file.path}",
            f"status: {file.status}",
            f"changed_lines: {sorted(file.changed_lines)[:200]}",
            "```diff",
            patch,
            "```",
        ]
        if file.head_text:
            rendered.extend(["```current-file", file.head_text, "```"])
        rendered_files.append("\n".join(rendered))

    system = (
        "You are a conservative code-review diagnostic engine for the Kinglet compiler. "
        "Return only high-confidence findings that are directly actionable on changed lines. "
        "Prefer silence over speculative comments. You are not the final publisher; a verifier will discard weak findings."
    )
    user = f"""
Review PR #{pr.number}: {pr.title}

PR body:
{pr.body[:4000]}

Repository context:
{repo_context}

Changed files:
{chr(10).join(rendered_files)}

Output a JSON array only. Each item must have:
- file: string, path exactly as shown
- line: integer, new-file line number on a changed line
- severity: one of "blocking", "should", "info", "nit"
- confidence: number from 0 to 1
- claim: concise problem statement
- evidence: concrete evidence from the diff/current-file
- rule: short rule id, e.g. "memory-safety" or "missing-test"

Severity rules:
- "blocking" only for defects that are highly likely to break correctness, safety, CI, or documented project semantics.
- Do not use blocking for missing context, style preferences, or anything phrased as "verify/confirm/if".
- If the code may be correct in files or commits not shown here, omit the finding.
- Do not ask for close() on io::reader or sync()/close() on io::writer.
- If there are no high-confidence findings, return [].
"""
    return [{"role": "system", "content": system}, {"role": "user", "content": user}]


def call_deepseek(messages: list[dict[str, str]], api_key: str) -> list[dict[str, Any]]:
    payload = {
        "model": MODEL,
        "messages": messages,
        "temperature": 0.1,
        "response_format": {"type": "json_object"},
    }
    url = DEEPSEEK_BASE_URL.rstrip("/") + "/chat/completions"
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    content = data["choices"][0]["message"]["content"]
    parsed = json.loads(content)
    if isinstance(parsed, list):
        return parsed
    if isinstance(parsed, dict):
        for key in ("findings", "diagnostics", "issues", "results"):
            if isinstance(parsed.get(key), list):
                return parsed[key]
    return []


def normalize_severity(value: Any) -> str:
    sev = str(value or "info").strip().lower().replace("_", "-")
    if sev in {"blocker", "blocking", "error", "critical"}:
        return "blocking"
    if sev in {"should", "should-fix", "warning", "warn"}:
        return "should"
    if sev in {"nit", "nitpick"}:
        return "nit"
    return "info"


def verify_findings(findings: list[dict[str, Any]], files: list[ChangedFile]) -> list[dict[str, Any]]:
    by_path = {f.path: f for f in files}
    verified: list[dict[str, Any]] = []
    for item in findings:
        path = str(item.get("file") or item.get("path") or "")
        file = by_path.get(path)
        if not file:
            continue
        raw_line = item.get("line")
        if raw_line is None:
            continue
        try:
            line = int(raw_line)
        except (TypeError, ValueError):
            continue
        if line not in file.changed_lines:
            continue

        severity = normalize_severity(item.get("severity"))
        text = "\n".join(str(item.get(k, "")) for k in ("claim", "evidence", "message"))
        confidence = float(item.get("confidence") or 0.0)

        if FALSE_POSITIVE_RE.search(text):
            continue
        if UNCERTAIN_RE.search(text):
            continue
        if severity != "blocking" and not INCLUDE_WARNINGS:
            continue
        if severity == "blocking" and confidence < MIN_BLOCKING_CONFIDENCE:
            continue

        item["file"] = path
        item["line"] = line
        item["severity"] = severity
        item["confidence"] = confidence
        verified.append(item)
    return verified


def to_rdjsonl(findings: list[dict[str, Any]]) -> None:
    for item in findings:
        severity = "ERROR" if item["severity"] == "blocking" else "WARNING"
        claim = str(item.get("claim") or item.get("message") or "Review finding").strip()
        evidence = str(item.get("evidence") or "").strip()
        confidence = item.get("confidence", 0)
        message = f"{claim}\n\nEvidence: {evidence}\nConfidence: {confidence}"
        diagnostic = {
            "message": message,
            "location": {
                "path": item["file"],
                "range": {"start": {"line": item["line"]}},
            },
            "severity": severity,
            "source": {"name": "kinglet-ai-reviewdog"},
            "code": {"value": str(item.get("rule") or "ai-review")},
        }
        print(json.dumps(diagnostic, ensure_ascii=False))


def main() -> int:
    token = os.getenv("GITHUB_TOKEN") or os.getenv("REVIEWDOG_GITHUB_API_TOKEN")
    api_key = os.getenv("DEEPSEEK_API_KEY")
    repo = os.getenv("GITHUB_REPOSITORY")
    pr_number_raw = os.getenv("PR_NUMBER") or os.getenv("GITHUB_REF_NAME", "").split("/")[0]

    if not token or not repo or not pr_number_raw:
        log("missing GITHUB_TOKEN, GITHUB_REPOSITORY, or PR_NUMBER; emitting no diagnostics")
        return 0
    if not api_key:
        log("DEEPSEEK_API_KEY is not configured; emitting no diagnostics")
        return 0

    try:
        pr_number = int(pr_number_raw)
        pr = load_pr(repo, pr_number, token)
        files = load_files(repo, pr_number, token)
        fill_head_contents(files, token)
        if not files:
            log("no textual patches to review")
            return 0
        messages = build_prompt(pr, files, load_repo_context())
        findings = call_deepseek(messages, api_key)
        verified = verify_findings(findings, files)
        log(f"model_findings={len(findings)} verified_findings={len(verified)}")
        to_rdjsonl(verified)
        return 0
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")[:1000]
        log(f"HTTP error: {exc.code} {body}")
        return 0
    except Exception as exc:  # noqa: BLE001 - reviewer should not break CI by crashing.
        log(f"unexpected error: {exc}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
