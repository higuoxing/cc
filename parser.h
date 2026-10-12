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

#ifndef _PARSER_H_
#define _PARSER_H_

#include "lexer.h"

typedef struct {
  Lexer lexer;
  Token current;
} Parser;

typedef enum {
  AST_RETURN,
  AST_INT_LITERAL,
  AST_ADD, // '+'
} AstType;

typedef struct AstNode {
  AstType type;
  union {
    struct {
      struct AstNode *expr;
    } ret;

    struct {
      int64_t val;
    } int_literal;

    struct {
      struct AstNode *lhs;
      struct AstNode *rhs;
    } add;
  } d;
} AstNode;

extern AstNode *parse_program(const char *filename, const char *source);

#endif
