/*
 * Lexer for the self-hosted C compiler
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

#ifndef _LEXER_H_
#define _LEXER_H_

#include <stdlib.h>

typedef enum {
  TOKEN_UNKNOW = -1,
  TOKEN_EOF = 0,
  TOKEN_RETURN,
  TOKEN_INT_LITERAL,
  TOKEN_SEMICOLON,
  TOKEN_PLUS,    // '+'
  TOKEN_MINUS,    // '-'
  TOKEN_STAR,   // '*'
  TOKEN_SLASH,  // '/'
  TOKEN_LPAREN, // '('
  TOKEN_RPAREN, // ')'
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  int len;
  int line;
  int col;
  int64_t int_literal_val;
} Token;

typedef struct {
  const char *filename;
  const char *source;
  const char *cursor;
  const char *line_start;
  int line;
} Lexer;

extern void lexer_init(Lexer *l, const char *filename,
                       const char *source);
extern Token lexer_next_token(Lexer *l);

#endif
