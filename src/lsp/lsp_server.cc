#include "lsp/lsp_server.h"

#include "ast/ast.h"
#include "checker/type_checker.h"
#include "lexer/scanner.h"
#include "parser/parser.h"

#include <iostream>
#include <sstream>

namespace kinglet::lsp {

void Server::run() {
  while (true) {
    std::string msg = read_message();
    if (msg.empty()) break;
    std::size_t pos = 0;
    auto parsed = json::parse(msg, pos);
    handle_message(parsed);
  }
}

std::string Server::read_message() {
  // Read Content-Length: <N>\r\n\r\n<JSON>
  std::string line;
  long length = -1;
  while (std::getline(std::cin, line)) {
    if (line.empty() || line == "\r") continue;
    if (line.starts_with("Content-Length:")) {
      length = std::stol(line.substr(15));
      // Read the blank line separator
      std::getline(std::cin, line);
      break;
    }
  }
  if (length <= 0) return "";
  std::string content(static_cast<std::size_t>(length), '\0');
  std::cin.read(content.data(), length);
  return content;
}

void Server::write_message(const json::Value &msg) {
  std::string body = json::to_string(msg);
  std::ostringstream header;
  header << "Content-Length: " << body.size() << "\r\n\r\n";
  std::cout << header.str() << body << std::flush;
}

void Server::handle_message(const json::Value &msg) {
  if (!msg.is_object()) return;
  const auto &obj = msg.as_object();
  auto method_it = obj.find("method");
  if (method_it == obj.end()) return;

  const std::string &method = method_it->second.as_string();
  json::Value params;
  auto params_it = obj.find("params");
  if (params_it != obj.end()) params = params_it->second;

  // Get request id for response
  json::Value id = json::Value::null();
  auto id_it = obj.find("id");
  if (id_it != obj.end()) id = id_it->second;

  if (method == "initialize") {
    auto result = handle_initialize(params);
    json::Object response;
    response["jsonrpc"] = json::Value::string("2.0");
    response["id"] = id;
    response["result"] = result;
    write_message(json::Value(response));
  } else if (method == "textDocument/didOpen") {
    handle_did_open(params);
  } else if (method == "textDocument/didChange") {
    handle_did_change(params);
  } else if (method == "textDocument/completion") {
    auto result = handle_completion(params);
    json::Object response;
    response["jsonrpc"] = json::Value::string("2.0");
    response["id"] = id;
    response["result"] = result;
    write_message(json::Value(response));
  } else if (method == "shutdown") {
    json::Object response;
    response["jsonrpc"] = json::Value::string("2.0");
    response["id"] = id;
    response["result"] = json::Value::null();
    write_message(json::Value(response));
  } else if (method == "exit") {
    // Client will close the connection
  }
}

void Server::send_notification(const std::string &method, const json::Value &params) {
  json::Object msg;
  msg["jsonrpc"] = json::Value::string("2.0");
  msg["method"] = json::Value::string(method);
  msg["params"] = params;
  write_message(json::Value(msg));
}

json::Value Server::handle_initialize(const json::Value &) {
  json::Object capabilities;

  // Text document sync — we want full text on changes
  json::Object text_doc_sync;
  text_doc_sync["openClose"] = json::Value(true);
  text_doc_sync["change"] = json::Value::number(2); // Incremental
  capabilities["textDocumentSync"] = json::Value(text_doc_sync);

  // Diagnostic provider
  json::Object diagnostic_provider;
  diagnostic_provider["interFileDependencies"] = json::Value(false);
  diagnostic_provider["workspaceDiagnostics"] = json::Value(false);
  capabilities["diagnosticProvider"] = json::Value(diagnostic_provider);

  // Completion provider
  json::Object completion_provider;
  json::Array trigger_chars;
  trigger_chars.push_back(json::Value::string("."));
  trigger_chars.push_back(json::Value::string(":"));
  completion_provider["triggerCharacters"] = json::Value(trigger_chars);
  capabilities["completionProvider"] = json::Value(completion_provider);

  json::Object server_info;
  server_info["name"] = json::Value::string("kinglet");
  server_info["version"] = json::Value::string("0.1.0");

  json::Object result;
  result["capabilities"] = json::Value(capabilities);
  result["serverInfo"] = json::Value(server_info);
  return json::Value(result);
}

void Server::handle_did_open(const json::Value &params) {
  if (!params.is_object()) return;
  const auto &p = params.as_object();
  auto doc_it = p.find("textDocument");
  if (doc_it == p.end()) return;
  const auto &doc = doc_it->second.as_object();
  auto uri_it = doc.find("uri");
  auto text_it = doc.find("text");
  if (uri_it == doc.end() || text_it == doc.end()) return;

  TextDocument td;
  td.uri = uri_it->second.as_string();
  td.text = text_it->second.as_string();
  documents_.push_back(td);
  send_diagnostics(td);
}

void Server::handle_did_change(const json::Value &params) {
  if (!params.is_object()) return;
  const auto &p = params.as_object();
  auto doc_it = p.find("textDocument");
  auto changes_it = p.find("contentChanges");
  if (doc_it == p.end() || changes_it == p.end()) return;

  const auto &doc = doc_it->second.as_object();
  auto uri_it = doc.find("uri");
  if (uri_it == doc.end()) return;
  std::string uri = uri_it->second.as_string();

  const auto &changes = changes_it->second.as_array();
  if (changes.empty()) return;
  std::string new_text = changes.back().as_object().at("text").as_string();

  for (auto &td : documents_) {
    if (td.uri == uri) {
      td.text = std::move(new_text);
      send_diagnostics(td);
      return;
    }
  }
}

void Server::send_diagnostics(const TextDocument &doc) {
  std::vector<Diagnostic> diagnostics;

  // Run Kinglet pipeline
  Scanner scanner(doc.text);
  auto tokens = scanner.scan_tokens();

  bool has_lexer_error = false;
  for (const auto &token : tokens) {
    if (token.type == TokenType::ERROR) {
      diagnostics.push_back({token.line, token.column, std::string(token.lexeme)});
      has_lexer_error = true;
    }
  }

  if (!has_lexer_error) {
    Parser parser(tokens);
    auto parse_result = parser.parse();
    for (const auto &err : parse_result.errors) {
      diagnostics.push_back({err.line, err.column, err.message});
    }

    if (parse_result.errors.empty() && parse_result.program) {
      TypeChecker checker;
      auto type_result = checker.check(*parse_result.program);
      for (const auto &err : type_result.errors) {
        diagnostics.push_back({err.location.line, err.location.column, err.message});
      }
    }
  }

  // Build JSON diagnostic array
  json::Array items;
  for (const auto &d : diagnostics) {
    json::Object item;
    json::Object range;
    json::Object start;
    start["line"] = json::Value::number(d.line - 1);
    start["character"] = json::Value::number(d.column - 1);
    range["start"] = json::Value(start);
    json::Object end;
    end["line"] = json::Value::number(d.line - 1);
    end["character"] = json::Value::number(d.column);
    range["end"] = json::Value(end);
    item["range"] = json::Value(range);
    item["message"] = json::Value::string(d.message);
    item["severity"] = json::Value::number(1); // Error
    item["source"] = json::Value::string("kinglet");
    items.push_back(json::Value(item));
  }

  json::Object diag_params;
  json::Object diag_uri;
  diag_uri["uri"] = json::Value::string(doc.uri);
  diag_uri["diagnostics"] = json::Value(items);
  diag_params["textDocument"] = json::Value(diag_uri);

  send_notification("textDocument/publishDiagnostics", json::Value(diag_params));
}

json::Value Server::handle_completion(const json::Value &params) {
  json::Array items;

  // Find the document text
  if (!params.is_object()) return json::Value(items);
  const auto &p = params.as_object();
  auto doc_it = p.find("textDocument");
  auto pos_it = p.find("position");
  if (doc_it == p.end() || pos_it == p.end()) return json::Value(items);

  const auto &doc = doc_it->second.as_object();
  auto uri_it = doc.find("uri");
  if (uri_it == doc.end()) return json::Value(items);

  // Get current text
  std::string current_text;
  for (const auto &td : documents_) {
    if (td.uri == uri_it->second.as_string()) {
      current_text = td.text;
      break;
    }
  }

  // Parse the document to get scope info
  Scanner scanner(current_text);
  auto tokens = scanner.scan_tokens();
  bool has_error = false;
  for (const auto &t : tokens) {
    if (t.type == TokenType::ERROR) { has_error = true; break; }
  }

  // Always provide keyword + type completions
  const char *keywords[] = {
    "if", "else", "for", "while", "break", "continue", "return",
    "inspect", "using", "namespace", "const", "import", "export",
    "struct", "enum", "trait", "spawn", "select",
    "int", "float", "double", "bool", "string", "void", "byte", "auto",
    "true", "false", "null",
    "io"
  };

  for (const char *kw : keywords) {
    json::Object item;
    item["label"] = json::Value::string(kw);
    item["kind"] = json::Value::number(14); // Keyword
    item["insertText"] = json::Value::string(kw);
    items.push_back(json::Value(item));
  }

  // Add snippet completions
  struct Snippet { const char *label; const char *text; };
  const Snippet snippets[] = {
    {"inspect", "inspect (${1:expr}) {\n\t${2:_} => ${0:result}\n}"},
    {"for", "for (${1:int i = 0}; $1 < ${2:n}; $1 += ${3:1}) {\n\t${0}\n}"},
    {"if", "if (${1:cond}) {\n\t${0}\n}"},
    {"while", "while (${1:cond}) {\n\t${0}\n}"},
    {"fun", "int ${1:name}(${2:params}) {\n\t${0}\n}"},
    {"using namespace", "using namespace ${1:io};"},
  };

  for (const auto &s : snippets) {
    json::Object item;
    item["label"] = json::Value::string(s.label);
    item["kind"] = json::Value::number(15); // Snippet
    item["insertText"] = json::Value::string(s.text);
    item["insertTextFormat"] = json::Value::number(2); // Snippet format
    items.push_back(json::Value(item));
  }

  // Add variable scoping completions if we can parse the document
  if (!has_error) {
    Parser parser(tokens);
    auto result = parser.parse();
    if (result.errors.empty() && result.program) {
      // Collect declared names
      for (const auto &decl : result.program->declarations) {
        if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
          json::Object item;
          item["label"] = json::Value::string(func->name);
          item["kind"] = json::Value::number(3); // Function
          items.push_back(json::Value(item));
        }
      }
    }
  }

  return json::Value(items);
}

} // namespace kinglet::lsp
