// Copyright (c) 2026 Kinglet Language Developers
// SPDX-License-Identifier: MIT

#include "preen/emitter.h"

#include <cctype>
#include <sstream>

namespace kinglet::preen {

namespace {

std::string join(const std::vector<std::string> &parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      out += sep;
    }
    out += parts[i];
  }
  return out;
}

std::string comment_suffix(std::string_view trailing) {
  std::size_t start = std::string_view::npos;
  for (std::size_t i = 0; i + 1 < trailing.size(); ++i) {
    if (trailing[i] == '/' && trailing[i + 1] == '/') {
      start = i;
      break;
    }
    if (trailing[i] == '/' && trailing[i + 1] == '*') {
      start = i;
      break;
    }
  }
  if (start == std::string_view::npos) {
    return {};
  }
  std::string out(trailing.substr(start));
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
    out.pop_back();
  }
  if (out.size() >= 2 && out[0] == '/' && out[1] == '/') {
    return " " + out;
  }
  return out;
}

TokenType token_for_binary_op(ast::BinaryOp op) {
  switch (op) {
  case ast::BinaryOp::Add:
    return TokenType::PLUS;
  case ast::BinaryOp::Sub:
    return TokenType::MINUS;
  case ast::BinaryOp::Mul:
    return TokenType::STAR;
  case ast::BinaryOp::Div:
    return TokenType::SLASH;
  case ast::BinaryOp::Mod:
    return TokenType::PERCENT;
  case ast::BinaryOp::Eq:
    return TokenType::EQUAL_EQUAL;
  case ast::BinaryOp::Neq:
    return TokenType::BANG_EQUAL;
  case ast::BinaryOp::Lt:
    return TokenType::LESS;
  case ast::BinaryOp::Gt:
    return TokenType::GREATER;
  case ast::BinaryOp::Le:
    return TokenType::LESS_EQUAL;
  case ast::BinaryOp::Ge:
    return TokenType::GREATER_EQUAL;
  case ast::BinaryOp::And:
    return TokenType::AMP_AMP;
  case ast::BinaryOp::Or:
    return TokenType::PIPE_PIPE;
  case ast::BinaryOp::BitAnd:
    return TokenType::AMP;
  case ast::BinaryOp::BitOr:
    return TokenType::PIPE;
  case ast::BinaryOp::BitXor:
    return TokenType::CARET;
  case ast::BinaryOp::Shl:
    return TokenType::LESS_LESS;
  case ast::BinaryOp::Shr:
    return TokenType::GREATER_GREATER;
  }
  return TokenType::ERROR;
}

} // namespace

Emitter::Emitter(const FmtConfig &config, const TriviaMap *decl_trivia,
                 const TokenTriviaIndex *token_trivia, const SpanMap *spans,
                 const std::vector<Token> *tokens)
    : config_(config), decl_trivia_(decl_trivia), token_trivia_(token_trivia), spans_(spans),
      tokens_(tokens) {}

std::string Emitter::indent_str(int extra) const {
  return std::string(static_cast<std::size_t>((indent_level_ + extra) * config_.indent), ' ');
}

void Emitter::append(const std::string &text) { out_ += text; }

void Emitter::newline() { out_ += config_.newline_string(); }

void Emitter::append_line(const std::string &text) {
  append(indent_str());
  append(text);
  newline();
}

void Emitter::append_leading_trivia(int line) {
  if (decl_trivia_ == nullptr) {
    return;
  }
  const std::string leading = decl_trivia_->leading_for_line(line);
  if (!leading.empty()) {
    append(leading);
  }
}

std::string Emitter::leading_token(std::size_t index) const {
  if (token_trivia_ == nullptr) {
    return {};
  }
  return token_trivia_->at(index).leading;
}

std::string Emitter::trailing_token(std::size_t index) const {
  if (token_trivia_ == nullptr) {
    return {};
  }
  return token_trivia_->at(index).trailing;
}

std::string Emitter::gap_between(std::size_t from_after, std::size_t before,
                                 std::string_view canonical_if_blank) const {
  if (token_trivia_ == nullptr || before <= from_after) {
    return std::string(canonical_if_blank);
  }
  std::string out = trailing_token(from_after);
  for (std::size_t i = from_after + 1; i < before; ++i) {
    out += leading_token(i);
    out += trailing_token(i);
  }
  out += leading_token(before);
  if (is_blank(out)) {
    return std::string(canonical_if_blank);
  }
  return out;
}

bool Emitter::is_blank(const std::string &text) const {
  return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

std::size_t Emitter::find_token_between(std::size_t start, std::size_t end, TokenType type) const {
  if (tokens_ == nullptr) {
    return end;
  }
  for (std::size_t i = start; i < end && i < tokens_->size(); ++i) {
    if ((*tokens_)[i].type == type) {
      return i;
    }
  }
  return end;
}

std::string Emitter::stmt_trailing(const ast::Stmt &stmt) const {
  if (spans_ == nullptr || token_trivia_ == nullptr || !spans_->has(stmt)) {
    return {};
  }
  const TokenSpan span = spans_->span(stmt);
  if (span.end == 0) {
    return {};
  }
  return comment_suffix(trailing_token(span.end - 1));
}

bool Emitter::is_single_line_block(const ast::BlockStmt &block, int line) const {
  if (block.statements.empty()) {
    return false;
  }
  for (const ast::StmtPtr &stmt : block.statements) {
    if (stmt->location.line != line) {
      return false;
    }
  }
  return true;
}

std::string Emitter::emit_program(const ast::Program &program) {
  for (std::size_t i = 0; i < program.declarations.size(); ++i) {
    emit_top_level_decl(*program.declarations[i]);
    if (i + 1 < program.declarations.size()) {
      newline();
    }
  }
  if (!out_.empty() && out_.back() != '\n') {
    newline();
  }
  return out_;
}

void Emitter::emit_top_level_decl(const ast::Decl &decl) {
  append_leading_trivia(decl.location.line);
  append(emit_decl(decl, true));
}

std::string Emitter::emit_type(const ast::TypeExpr &type) {
  if (type.name == "Array" && type.type_args.size() == 1) {
    return emit_type(type.type_args[0]) + "[]";
  }
  if (type.type_args.empty()) {
    return type.name;
  }
  std::vector<std::string> args;
  args.reserve(type.type_args.size());
  for (const ast::TypeExpr &arg : type.type_args) {
    args.push_back(emit_type(arg));
  }
  return type.name + "<" + join(args, ", ") + ">";
}

std::string Emitter::emit_string_literal(const std::string &value) {
  std::string out = "\"";
  for (char c : value) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  out.push_back('"');
  return out;
}

std::string Emitter::emit_char_literal(int8_t value) {
  std::ostringstream out;
  out << '\'';
  if (value == '\\') {
    out << "\\\\";
  } else if (value == '\'') {
    out << "\\'";
  } else if (value == '\n') {
    out << "\\n";
  } else if (value == '\t') {
    out << "\\t";
  } else if (std::isprint(static_cast<unsigned char>(value))) {
    out << static_cast<char>(value);
  } else {
    out << "\\x" << std::hex << static_cast<int>(static_cast<unsigned char>(value));
  }
  out << '\'';
  return out.str();
}

std::string Emitter::emit_type_params(const std::vector<std::string> &params) {
  if (params.empty()) {
    return {};
  }
  return "<" + join(params, ", ") + ">";
}

std::string Emitter::emit_parameters(const std::vector<ast::Parameter> &params) {
  std::vector<std::string> parts;
  parts.reserve(params.size());
  for (const ast::Parameter &param : params) {
    parts.push_back(emit_type(param.type) + " " + param.name);
  }
  return join(parts, ", ");
}

std::string Emitter::emit_expr(const ast::Expr &expr) {
  if (const auto *lit = dynamic_cast<const ast::IntLiteralExpr *>(&expr)) {
    std::string out = std::to_string(lit->value);
    if (!lit->width_suffix.empty()) {
      out += "." + lit->width_suffix;
    }
    return out;
  }
  if (const auto *lit = dynamic_cast<const ast::FloatLiteralExpr *>(&expr)) {
    std::ostringstream out;
    out << lit->value;
    if (!lit->width_suffix.empty()) {
      out << "." << lit->width_suffix;
    }
    return out.str();
  }
  if (const auto *lit = dynamic_cast<const ast::StringLiteralExpr *>(&expr)) {
    return emit_string_literal(lit->value);
  }
  if (const auto *lit = dynamic_cast<const ast::CharLiteralExpr *>(&expr)) {
    return emit_char_literal(lit->value);
  }
  if (const auto *lit = dynamic_cast<const ast::BoolLiteralExpr *>(&expr)) {
    return lit->value ? "true" : "false";
  }
  if (dynamic_cast<const ast::NullLiteralExpr *>(&expr)) {
    return "null";
  }
  if (const auto *id = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    return id->name;
  }
  if (const auto *binding = dynamic_cast<const ast::BindingPattern *>(&expr)) {
    return binding->name;
  }
  if (const auto *pipe = dynamic_cast<const ast::PipeExpr *>(&expr)) {
    const std::string left = emit_expr(*pipe->left);
    const std::string right = emit_expr(*pipe->right);
    if (spans_ != nullptr && token_trivia_ != nullptr && spans_->has(*pipe->left) &&
        spans_->has(*pipe->right)) {
      const TokenSpan left_span = spans_->span(*pipe->left);
      const TokenSpan right_span = spans_->span(*pipe->right);
      if (left_span.end > 0 && right_span.start > left_span.end) {
        const std::size_t pipe_idx =
            find_token_between(left_span.end, right_span.start, TokenType::PIPE_GREATER);
        if (pipe_idx < right_span.start) {
          std::string prefix = gap_between(left_span.end - 1, pipe_idx, " ");
          std::string suffix = gap_between(pipe_idx, right_span.start, " ");
          return left + prefix + "|>" + suffix + right;
        }
      }
    }
    return left + " |> " + right;
  }
  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expr)) {
    return std::string(unary_op_name(unary->op)) + emit_expr(*unary->right);
  }
  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    const std::string left = emit_expr(*binary->left);
    const std::string right = emit_expr(*binary->right);
    const std::string op = binary_op_name(binary->op);
    if (spans_ != nullptr && token_trivia_ != nullptr && spans_->has(*binary->left) &&
        spans_->has(*binary->right)) {
      const TokenSpan left_span = spans_->span(*binary->left);
      const TokenSpan right_span = spans_->span(*binary->right);
      if (left_span.end > 0 && right_span.start > left_span.end) {
        const std::size_t op_idx = find_token_between(left_span.end, right_span.start,
                                                      token_for_binary_op(binary->op));
        if (op_idx < right_span.start) {
          std::string prefix = gap_between(left_span.end - 1, op_idx, " ");
          std::string suffix = gap_between(op_idx, right_span.start, " ");
          return left + prefix + op + suffix + right;
        }
      }
    }
    return left + " " + op + " " + right;
  }
  if (const auto *assign = dynamic_cast<const ast::AssignExpr *>(&expr)) {
    return assign->name + " " + assign_op_name(assign->op) + " " + emit_expr(*assign->value);
  }
  if (const auto *call = dynamic_cast<const ast::CallExpr *>(&expr)) {
    std::string callee = emit_expr(*call->callee);
    if (!call->type_args.empty()) {
      std::vector<std::string> args;
      for (const ast::TypeExpr &type_arg : call->type_args) {
        args.push_back(emit_type(type_arg));
      }
      callee += "<" + join(args, ", ") + ">";
    }
    std::vector<std::string> args;
    for (const ast::ExprPtr &arg : call->args) {
      args.push_back(emit_expr(*arg));
    }
    return callee + "(" + join(args, ", ") + ")";
  }
  if (const auto *cast = dynamic_cast<const ast::CastExpr *>(&expr)) {
    return "Cast<" + emit_type(cast->target_type) + ">(" + emit_expr(*cast->value) + ")";
  }
  if (const auto *ternary = dynamic_cast<const ast::TernaryExpr *>(&expr)) {
    return emit_expr(*ternary->condition) + " ? " + emit_expr(*ternary->then_expr) + " : " +
           emit_expr(*ternary->else_expr);
  }
  if (const auto *coalesce = dynamic_cast<const ast::NullCoalesceExpr *>(&expr)) {
    if (coalesce->err_binding.empty()) {
      return emit_expr(*coalesce->left) + " ?: " + emit_expr(*coalesce->right);
    }
    return emit_expr(*coalesce->left) + " ?: let " + coalesce->err_binding + " => " +
           emit_expr(*coalesce->right);
  }
  if (const auto *propagate = dynamic_cast<const ast::PropagateExpr *>(&expr)) {
    return emit_expr(*propagate->value) + "?";
  }
  if (const auto *ns = dynamic_cast<const ast::NamespaceAccessExpr *>(&expr)) {
    return ns->namespace_name + "::" + ns->member_name;
  }
  if (const auto *field = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    return emit_expr(*field->object) + "." + field->field_name;
  }
  if (const auto *field_assign = dynamic_cast<const ast::FieldAssignExpr *>(&expr)) {
    return emit_expr(*field_assign->object) + "." + field_assign->field_name + " = " +
           emit_expr(*field_assign->value);
  }
  if (const auto *index = dynamic_cast<const ast::IndexExpr *>(&expr)) {
    return emit_expr(*index->object) + "[" + emit_expr(*index->index) + "]";
  }
  if (const auto *index_assign = dynamic_cast<const ast::IndexAssignExpr *>(&expr)) {
    return emit_expr(*index_assign->object) + "[" + emit_expr(*index_assign->index) + "] = " +
           emit_expr(*index_assign->value);
  }
  if (const auto *array = dynamic_cast<const ast::ArrayLiteralExpr *>(&expr)) {
    std::vector<std::string> elems;
    for (const ast::ExprPtr &element : array->elements) {
      elems.push_back(emit_expr(*element));
    }
    return "[" + join(elems, ", ") + "]";
  }
  if (const auto *map = dynamic_cast<const ast::MapLiteralExpr *>(&expr)) {
    if (map->keys.empty()) {
      return "{}";
    }
    std::vector<std::string> entries;
    for (std::size_t i = 0; i < map->keys.size(); ++i) {
      entries.push_back(emit_expr(*map->keys[i]) + ": " + emit_expr(*map->values[i]));
    }
    return "{" + join(entries, ", ") + "}";
  }
  if (const auto *lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    std::vector<std::string> fields;
    for (const auto &field : lit->fields) {
      if (field.name.empty()) {
        fields.push_back(emit_expr(*field.value));
      } else {
        fields.push_back(field.name + ": " + emit_expr(*field.value));
      }
    }
    const std::string type_name = emit_type(lit->struct_type);
    return type_name + " { " + join(fields, ", ") + " }";
  }
  if (const auto *match = dynamic_cast<const ast::MatchExpr *>(&expr)) {
    std::ostringstream out;
    out << emit_expr(*match->value) << " match {";
    for (std::size_t i = 0; i < match->arms.size(); ++i) {
      out << "\n" << indent_str(1) << emit_expr(*match->arms[i].pattern) << " => ";
      out << emit_expr(*match->arms[i].body);
      if (match->arms[i].guard) {
        out << " when " << emit_expr(*match->arms[i].guard);
      }
      out << ",";
    }
    out << "\n" << indent_str() << "}";
    return out.str();
  }
  if (const auto *array_pat = dynamic_cast<const ast::ArrayPattern *>(&expr)) {
    std::vector<std::string> elems;
    for (const ast::ExprPtr &element : array_pat->elements) {
      elems.push_back(emit_expr(*element));
    }
    return "[" + join(elems, ", ") + "]";
  }
  if (const auto *enum_pat = dynamic_cast<const ast::EnumPattern *>(&expr)) {
    std::string out = enum_pat->enum_name + "::" + enum_pat->variant_name;
    if (!enum_pat->fields.empty()) {
      std::vector<std::string> fields;
      for (const ast::ExprPtr &field : enum_pat->fields) {
        fields.push_back(emit_expr(*field));
      }
      out += "(" + join(fields, ", ") + ")";
    }
    return out;
  }
  if (const auto *struct_pat = dynamic_cast<const ast::StructPattern *>(&expr)) {
    std::vector<std::string> fields;
    for (const auto &field : struct_pat->fields) {
      if (field.pattern) {
        fields.push_back(field.name + ": " + emit_expr(*field.pattern));
      } else {
        fields.push_back(field.name);
      }
    }
    return struct_pat->struct_name + " { " + join(fields, ", ") + " }";
  }
  return "/* unsupported expr */";
}

std::string Emitter::emit_block_body(const ast::BlockStmt &block) {
  std::ostringstream body;
  const int saved = indent_level_;
  indent_level_++;
  for (std::size_t i = 0; i < block.statements.size(); ++i) {
    body << indent_str(0) << emit_stmt(*block.statements[i], true);
    if (i + 1 < block.statements.size()) {
      body << config_.newline_string();
    }
  }
  indent_level_ = saved;
  return body.str();
}

std::string Emitter::emit_stmt(const ast::Stmt &stmt, bool block_child) {
  if (const auto *expr_stmt = dynamic_cast<const ast::ExprStmt *>(&stmt)) {
    std::string out = emit_expr(*expr_stmt->expr) + ";";
    if (block_child) {
      out += stmt_trailing(stmt);
    }
    return out;
  }
  if (const auto *ret = dynamic_cast<const ast::ReturnStmt *>(&stmt)) {
    if (ret->value) {
      return "return " + emit_expr(*ret->value) + ";";
    }
    return "return;";
  }
  if (const auto *var = dynamic_cast<const ast::VarDeclStmt *>(&stmt)) {
    std::string out;
    if (!var->storage.empty()) {
      out += var->storage + " ";
    }
    if (!var->type.name.empty() || !var->type.type_args.empty()) {
      out += emit_type(var->type) + " ";
    }
    out += var->name;
    if (var->init) {
      out += " = " + emit_expr(*var->init);
    }
    out += ";";
    if (block_child) {
      out += stmt_trailing(stmt);
    }
    return out;
  }
  if (const auto *unpack = dynamic_cast<const ast::UnpackDeclStmt *>(&stmt)) {
    std::string out = "let ";
    for (std::size_t i = 0; i < unpack->names.size(); ++i) {
      if (i > 0) {
        out += ", ";
      }
      out += unpack->names[i];
    }
    if (!unpack->rest_name.empty()) {
      if (!unpack->names.empty()) {
        out += ", ";
      }
      out += "..." + unpack->rest_name;
    }
    out += " = " + emit_expr(*unpack->init);
    return out + ";";
  }
  if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
    if (block_child) {
      return "{\n" + emit_block_body(*block) + "\n" + indent_str() + "}";
    }
    return "{\n" + emit_block_body(*block) + "\n}";
  }
  if (const auto *if_stmt = dynamic_cast<const ast::IfStmt *>(&stmt)) {
    std::string out = "if " + emit_expr(*if_stmt->condition) + " ";
    out += emit_stmt(*if_stmt->then_branch, true);
    if (if_stmt->else_branch) {
      out += " else " + emit_stmt(*if_stmt->else_branch, true);
    }
    return out;
  }
  if (const auto *guard = dynamic_cast<const ast::GuardStmt *>(&stmt)) {
    return "guard " + emit_expr(*guard->condition) + " else " +
           emit_stmt(*guard->else_body, true);
  }
  if (const auto *while_stmt = dynamic_cast<const ast::WhileStmt *>(&stmt)) {
    return "while " + emit_expr(*while_stmt->condition) + " " +
           emit_stmt(*while_stmt->body, true);
  }
  if (const auto *for_stmt = dynamic_cast<const ast::ForStmt *>(&stmt)) {
    std::string out = "for (";
    if (for_stmt->init) {
      std::string init = emit_stmt(*for_stmt->init);
      if (!init.empty() && init.back() == ';') {
        init.pop_back();
      }
      out += init;
    }
    out += "; ";
    if (for_stmt->condition) {
      out += emit_expr(*for_stmt->condition);
    }
    out += "; ";
    if (for_stmt->step) {
      std::string step = emit_stmt(*for_stmt->step);
      if (!step.empty() && step.back() == ';') {
        step.pop_back();
      }
      out += step;
    }
    out += ") " + emit_stmt(*for_stmt->body, true);
    return out;
  }
  if (dynamic_cast<const ast::BreakStmt *>(&stmt)) {
    return "break;";
  }
  if (dynamic_cast<const ast::ContinueStmt *>(&stmt)) {
    return "continue;";
  }
  if (const auto *try_stmt = dynamic_cast<const ast::TryCatchStmt *>(&stmt)) {
    std::ostringstream out;
    out << "try " << emit_stmt(*try_stmt->body, true);
    for (const ast::CatchArm &arm : try_stmt->catches) {
      out << " catch (" << emit_type(arm.error_type);
      if (!arm.binding_name.empty()) {
        out << " " << arm.binding_name;
      }
      out << ") " << emit_stmt(*arm.body, true);
    }
    return out.str();
  }
  return "/* unsupported stmt */;";
}

std::string Emitter::emit_decl(const ast::Decl &decl, bool top_level) {
  if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(&decl)) {
    if (using_decl->wildcard) {
      return "using " + using_decl->namespace_name + "::*;";
    }
    if (!using_decl->selected_symbols.empty()) {
      return "using " + using_decl->namespace_name + " { " +
             join(using_decl->selected_symbols, ", ") + " };";
    }
    if (using_decl->is_namespace) {
      return "using namespace " + using_decl->namespace_name + ";";
    }
    return "using " + using_decl->namespace_name + ";";
  }
  if (const auto *exported = dynamic_cast<const ast::ExportModuleDecl *>(&decl)) {
    return "export module " + exported->name + ";";
  }
  if (const auto *logical = dynamic_cast<const ast::LogicalImportDecl *>(&decl)) {
    return "import " + logical->module_id + ";";
  }
  if (const auto *import = dynamic_cast<const ast::ImportDecl *>(&decl)) {
    std::string out = "import { \"" + import->path + "\"";
    if (!import->alias.empty()) {
      out += " as " + import->alias;
    }
    out += " }";
    return out;
  }
  if (const auto *block = dynamic_cast<const ast::ImportBlockDecl *>(&decl)) {
    std::vector<std::string> lines;
    for (const ast::DeclPtr &item : block->imports) {
      if (const auto *imp = dynamic_cast<const ast::ImportDecl *>(item.get())) {
        std::string line = "\"" + imp->path + "\"";
        if (!imp->alias.empty()) {
          line += " as " + imp->alias;
        }
        lines.push_back(line);
      }
    }
    return "import {\n" + indent_str(1) + join(lines, ",\n" + indent_str(1)) + "\n" + indent_str() +
           "}";
  }
  if (const auto *fn = dynamic_cast<const ast::FunctionDecl *>(&decl)) {
    std::ostringstream out;
    if (fn->is_public) {
      out << "pub ";
    }
    out << emit_type(fn->return_type) << " " << fn->name << emit_type_params(fn->type_params)
        << "(" << emit_parameters(fn->params) << ") ";
    if (const auto *block = dynamic_cast<const ast::BlockStmt *>(fn->body.get());
        block != nullptr && is_single_line_block(*block, fn->location.line)) {
      std::vector<std::string> parts;
      for (const ast::StmtPtr &stmt : block->statements) {
        parts.push_back(emit_stmt(*stmt, true));
      }
      out << "{ " << join(parts, " ") << " }";
    } else {
      out << emit_stmt(*fn->body);
    }
    return out.str();
  }
  if (const auto *st = dynamic_cast<const ast::StructDecl *>(&decl)) {
    std::ostringstream out;
    if (st->is_public) {
      out << "pub ";
    }
    out << "struct " << st->name << emit_type_params(st->type_params) << " {\n";
    const int saved = indent_level_;
    indent_level_++;
    std::size_t max_type = 0;
    std::size_t max_name = 0;
    if (config_.extension_enabled("align-struct-fields")) {
      for (const ast::FieldDef &field : st->fields) {
        max_type = std::max(max_type, emit_type(field.type).size());
        max_name = std::max(max_name, field.name.size());
      }
    }
    for (const ast::FieldDef &field : st->fields) {
      out << indent_str();
      const std::string type_text = emit_type(field.type);
      if (max_type > 0) {
        out << type_text << std::string(max_type - type_text.size() + 1, ' ') << field.name
            << ";\n";
      } else {
        out << type_text << " " << field.name << ";\n";
      }
    }
    indent_level_ = saved;
    out << indent_str() << "}";
    return out.str();
  }
  if (const auto *en = dynamic_cast<const ast::EnumDecl *>(&decl)) {
    std::ostringstream out;
    if (en->is_public) {
      out << "pub ";
    }
    out << "enum " << en->name << " {\n";
    const int saved = indent_level_;
    indent_level_++;
    for (std::size_t i = 0; i < en->variants.size(); ++i) {
      out << indent_str() << en->variants[i].name;
      if (!en->variants[i].param_types.empty()) {
        std::vector<std::string> params;
        for (const ast::TypeExpr &param : en->variants[i].param_types) {
          params.push_back(emit_type(param));
        }
        out << "(" << join(params, ", ") << ")";
      }
      out << ",\n";
    }
    indent_level_ = saved;
    out << indent_str() << "}";
    return out.str();
  }
  if (const auto *concept_decl = dynamic_cast<const ast::ConceptDecl *>(&decl)) {
    std::ostringstream out;
    if (concept_decl->is_public) {
      out << "pub ";
    }
    out << "concept " << concept_decl->name << emit_type_params(concept_decl->type_params)
        << " {\n";
    const int saved = indent_level_;
    indent_level_++;
    for (const ast::ConceptMethodDecl &method : concept_decl->methods) {
      out << indent_str() << emit_type(method.return_type) << " " << method.name << "("
          << emit_parameters(method.params) << ");\n";
    }
    indent_level_ = saved;
    out << indent_str() << "}";
    return out.str();
  }
  if (const auto *top = dynamic_cast<const ast::TopLevelStmtDecl *>(&decl)) {
    return emit_stmt(*top->stmt);
  }
  (void)top_level;
  return "/* unsupported decl */";
}

} // namespace kinglet::preen
