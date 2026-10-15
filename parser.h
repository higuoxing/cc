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

typedef struct LocalVar {
  Token name;
  int offset; // Positive distance below %rbp
  int scope_depth;
  struct LocalVar *next;
} LocalVar;

typedef struct {
  Lexer lexer;
  Token current;
  Token lookahead;

  LocalVar *locals;
  int scope_depth;
  int stack_size;
} Parser;

typedef enum {
  AST_FUNCTION,
  AST_BLOCK,
  AST_RETURN,
  AST_DECL,
  AST_EXPR_STMT,
  AST_VAR,
  AST_ASSIGN,
  AST_INT_LITERAL,
  AST_ADD,
  AST_SUB,
  AST_MUL,
  AST_DIV,
  AST_NEG,
  AST_BITWISE_NOT,
  AST_LOGICAL_NOT,
} AstType;

typedef struct AstNode {
  AstType type;
  struct AstNode *next; // Linked list of statements inside a block.
  union {
    struct {
      Token name;
      struct AstNode *body;
      int stack_size;
    } function;

    struct {
      struct AstNode *first_stmt;
    } block;

    struct {
      struct AstNode *expr;
    } ret;

    struct {
      LocalVar *var;
      struct AstNode *init;
    } decl;

    struct {
      struct AstNode *expr;
    } expr_stmt;

    struct {
      LocalVar *var;
    } variable;

    struct {
      int64_t val;
    } int_literal;

    struct {
      struct AstNode *operand;
    } unary;

    struct {
      struct AstNode *lhs;
      struct AstNode *rhs;
    } binary;
  } d;
} AstNode;

extern AstNode *parse_program(const char *filename, const char *source);

#endif
