#!/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HOOK_DIR="$ROOT/scripts/hooks"

chmod +x "$HOOK_DIR/pre-commit" "$HOOK_DIR/commit-msg" \
  "$HOOK_DIR/check_comments.py" "$HOOK_DIR/check_commit_msg.py"

git -C "$ROOT" config core.hooksPath scripts/hooks

echo "Installed git hooks -> $HOOK_DIR"
echo "  pre-commit  : comment / copyright checks on staged core files"
echo "  commit-msg  : conventional commit subject checks"
