/*
 * Parser for the self-hosted C compiler
 * Copyright (C) 2026  Xing Guo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"

static LocalVar *declare_local_var(Parser *p, Token name);

static AstNode *new_ast_node(Parser *p, AstType ty);
static void parser_advance(Parser *p);
static void parse_error(Parser *p, const char *errmsg);
static AstNode *parse_expr(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_primary(Parser *p);
static AstNode *parse_decl(Parser *p);
static AstNode *parse_return_stmt(Parser *p);
static AstNode *parse_stmt(Parser *p);
static AstNode *parse_block(Parser *p);
static AstNode *parse_function(Parser *p);

static bool same_identifier(Token a, Token b) {
  return a.len == b.len && strncmp(a.start, b.start, a.len) == 0;
}

static LocalVar *find_local_var(Parser *p, Token name) {
  LocalVar *var;
  for (var = p->locals; var; var = var->next) {
    if (same_identifier(var->name, name))
      return var;
  }
  return NULL;
}

static LocalVar *declare_local_var(Parser *p, Token name) {
  LocalVar *var;
  for (var = p->locals; var && var->scope_depth == p->scope_depth;
       var = var->next) {
    if (same_identifier(var->name, name))
      parse_error(p, "duplicate local variable");
  }

  var = (LocalVar *)calloc(1, sizeof(LocalVar));
  if (!var)
    parse_error(p, "out of memory");
  var->name = name;
  var->scope_depth = p->scope_depth;
  p->stack_size += 4; // All locals are 32-bit int for now.
  var->offset = p->stack_size;
  var->next = p->locals;
  p->locals = var;
  return var;
}

static AstNode *new_ast_node(Parser *p, AstType ty) {
  AstNode *node;
  node = calloc(1, sizeof(AstNode));
  if (!node)
    parse_error(p, "out of memory");
  node->type = ty;
  return node;
}

static void parser_advance(Parser *p) {
  p->current = p->lookahead;
  p->lookahead = lexer_next_token(&p->lexer);
}

static void parser_init(Parser *p, const char *filename, const char *source) {
  p->locals = NULL;
  p->scope_depth = 0;
  p->stack_size = 0;
  lexer_init(&p->lexer, filename, source);
  p->current = lexer_next_token(&p->lexer);
  p->lookahead = lexer_next_token(&p->lexer);
}

static void parse_error(Parser *p, const char *errmsg) {
  fprintf(stderr, "%s:%d:%d: error: %s\n", p->lexer.filename, p->current.line,
          p->current.col, errmsg);
  exit(1);
}

static void parse_expect(Parser *p, TokenType t, const char *msg) {
  if (p->current.type != t) {
    parse_error(p, msg);
  }

  parser_advance(p);
}

AstNode *parse_unary(Parser *p) {
  if (p->current.type == TOKEN_MINUS || p->current.type == TOKEN_TILDE ||
      p->current.type == TOKEN_BANG) {
    AstNode *node;
    AstType op;

    op = p->current.type == TOKEN_MINUS
             ? AST_NEG
             : (p->current.type == TOKEN_TILDE ? AST_BITWISE_NOT
                                               : AST_LOGICAL_NOT);
    // Consume '-' or '~' or '!'
    parser_advance(p);
    node = new_ast_node(p, op);
    node->d.unary.operand = parse_unary(p);

    return node;
  }

  return parse_primary(p);
}

// Parse integers and parentheses
AstNode *parse_primary(Parser *p) {
  if (p->current.type == TOKEN_INT_LITERAL) {
    AstNode *expr;
    expr = new_ast_node(p, AST_INT_LITERAL);
    expr->d.int_literal.val = p->current.int_literal_val;

    // Consume integer literal
    parser_advance(p);
    return expr;
  }

  if (p->current.type == TOKEN_LPAREN) {
    AstNode *expr;
    // Consume '('
    parser_advance(p);
    expr = parse_expr(p);
    parse_expect(p, TOKEN_RPAREN, "expect ')'");
    return expr;
  }

  if (p->current.type == TOKEN_IDENTIFIER) {
    AstNode *expr;
    LocalVar *var = find_local_var(p, p->current);
    if (!var)
      parse_error(p, "undeclared identifier");
    expr = new_ast_node(p, AST_VAR);
    expr->d.variable.var = var;
    // Consume identifier.
    parser_advance(p);
    return expr;
  }

  parse_error(p, "expect expr");
  return NULL;
}

static AstNode *parse_decl(Parser *p) {
  AstNode *decl;
  Token name;

  parse_expect(p, TOKEN_INT, "expect 'int'");
  if (p->current.type != TOKEN_IDENTIFIER)
    parse_error(p, "expect variable name");

  name = p->current;
  decl = new_ast_node(p, AST_DECL);
  decl->d.decl.var = declare_local_var(p, name);
  // Consume variable name.
  parser_advance(p);

  if (p->current.type == TOKEN_ASSIGN) {
    // Consume '='
    parser_advance(p);
    decl->d.decl.init = parse_expr(p);
  }

  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");

  return decl;
}

AstNode *parse_multiplicative(Parser *p) {
  AstNode *left;

  left = parse_unary(p);

  while (p->current.type == TOKEN_STAR || p->current.type == TOKEN_SLASH) {
    TokenType op;
    AstNode *right;
    AstNode *node;

    op = p->current.type;
    // Consume '*' or '/'
    parser_advance(p);

    right = parse_unary(p);
    node = new_ast_node(p, op == TOKEN_STAR ? AST_MUL : AST_DIV);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

    left = node;
  }

  return left;
}

// Additive expression
static AstNode *parse_additive(Parser *p) {
  AstNode *left;

  left = parse_multiplicative(p);

  while (p->current.type == TOKEN_PLUS || p->current.type == TOKEN_MINUS) {
    TokenType op;
    AstNode *right;
    AstNode *node;

    op = p->current.type;
    // Consume '+' or '-'
    parser_advance(p);
    right = parse_multiplicative(p);

    node = new_ast_node(p, op == TOKEN_PLUS ? AST_ADD : AST_SUB);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

    left = node;
  }

  return left;
}

static AstNode *parse_expr(Parser *p) {
  AstNode *left;

  left = parse_additive(p);
  if (p->current.type == TOKEN_ASSIGN) {
    AstNode *node;
    if (left->type != AST_VAR)
      parse_error(p, "invalid assignment target");
    // Consume '='
    parser_advance(p);
    node = new_ast_node(p, AST_ASSIGN);
    node->d.binary.lhs = left;
    node->d.binary.rhs = parse_expr(p);
    return node;
  }
  return left;
}

static AstNode *parse_return_stmt(Parser *p) {
  AstNode *ast;

  parse_expect(p, TOKEN_RETURN, "expect 'return'");
  if (p->current.type == TOKEN_SEMICOLON)
    parse_error(p, "expect return expr");

  ast = new_ast_node(p, AST_RETURN);
  ast->d.ret.expr = parse_expr(p);
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");

  return ast;
}

static AstNode *parse_block(Parser *p) {
  AstNode *block;
  AstNode **tail;
  LocalVar *outer_locals = p->locals;

  block = new_ast_node(p, AST_BLOCK);
  tail = &block->d.block.first_stmt;

  parse_expect(p, TOKEN_LBRACE, "expect '{'");
  ++p->scope_depth;
  while (p->current.type != TOKEN_RBRACE) {
    if (p->current.type == TOKEN_EOF)
      parse_error(p, "expect '}'");

    *tail = parse_stmt(p);
    tail = &(*tail)->next;
  }

  parse_expect(p, TOKEN_RBRACE, "expect '}'");
  p->locals = outer_locals;
  --p->scope_depth;
  return block;
}

static AstNode *parse_function(Parser *p) {
  AstNode *fn;

  fn = new_ast_node(p, AST_FUNCTION);
  parse_expect(p, TOKEN_INT, "expect 'int'");
  if (p->current.type != TOKEN_IDENTIFIER)
    parse_error(p, "expect function name");

  fn->d.function.name = p->current;
  // Consume <function_name>
  parser_advance(p);

  parse_expect(p, TOKEN_LPAREN, "expect '('");
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  fn->d.function.body = parse_block(p);
  fn->d.function.stack_size = p->stack_size;
  return fn;
}

static AstNode *parse_stmt(Parser *p) {
  if (p->current.type == TOKEN_RETURN) {
    return parse_return_stmt(p);
  }

  if (p->current.type == TOKEN_LBRACE)
    return parse_block(p);

  if (p->current.type == TOKEN_INT)
    return parse_decl(p);

  if (p->current.type == TOKEN_SEMICOLON) {
    AstNode *stmt = new_ast_node(p, AST_EXPR_STMT);
    parser_advance(p);
    return stmt;
  }

  if (p->current.type == TOKEN_INT_LITERAL || p->current.type == TOKEN_LPAREN ||
      p->current.type == TOKEN_MINUS || p->current.type == TOKEN_BANG ||
      p->current.type == TOKEN_TILDE ||
      (p->current.type == TOKEN_IDENTIFIER &&
       (find_local_var(p, p->current) || p->lookahead.type == TOKEN_ASSIGN))) {
    AstNode *stmt = new_ast_node(p, AST_EXPR_STMT);
    stmt->d.expr_stmt.expr = parse_expr(p);
    parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
    return stmt;
  }

  parse_error(p, "unexpected token after statement");
  return NULL;
}

AstNode *parse_program(const char *filename, const char *source) {
  Parser p;
  AstNode *ast;

  parser_init(&p, filename, source);
  ast = parse_function(&p);
  parse_expect(&p, TOKEN_EOF, "unexpected token after function");

  return ast;
}
