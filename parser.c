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

#include <stdio.h>

#include "parser.h"

void parser_advance(Parser *p) {
  p->current = lexer_next_token(&p->lexer);
}

void parser_init(Parser *p, const char *filename, const char *source) {
  lexer_init(&p->lexer, filename, source);
  parser_advance(p);
}

void parse_error(Parser *p, const char *errmsg) {
  fprintf(stderr, "%s:%d:%d: error: %s\n", p->lexer.filename, p->current.line,
          p->current.col, errmsg);
  exit(1);
}

AstNode *parse_expr(Parser *p) {
  AstNode *expr;

  if (p->current.type == TOKEN_INT_LITERAL) {
    expr = (AstNode *)malloc(sizeof(AstNode));
    expr->type = AST_INT_LITERAL;
    expr->d.int_literal.val = p->current.int_literal_val;

    // Consume integer literal
    parser_advance(p);
    return expr;
  }

  return NULL;
}

AstNode *parse_return_stmt(Parser *p) {
  AstNode *ast;
  AstNode *ret_expr;

  if (p->current.type != TOKEN_RETURN)
    parse_error(p, "expect 'return'");

  // Consume 'return'
  parser_advance(p);

  // Consume expr in return stmt.
  ret_expr = parse_expr(p);

  if (p->current.type != TOKEN_SEMICOLON) {
    parse_error(p, "expect ';'");
  }
  // Consume ';'
  parser_advance(p);

  ast = (AstNode *)malloc(sizeof(AstNode));
  ast->type = AST_RETURN;
  ast->d.ret.expr = ret_expr;
  return ast;
}

AstNode *parse_stmt(Parser *p) {
  if (p->current.type == TOKEN_RETURN) {
    return parse_return_stmt(p);
  }

  parse_error(p, "failed to parse statement");
  return NULL;
}

AstNode *parse_program(const char *filename, const char *source) {
  Parser p;
  AstNode *ast;

  parser_init(&p, filename, source);
  ast = parse_stmt(&p);

  return ast;
}
