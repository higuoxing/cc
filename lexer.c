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

#include <ctype.h>
#include <string.h>

#include "lexer.h"

void lexer_init(Lexer *l, const char *filename, const char *source) {
  l->filename = filename;
  l->source = source;
  l->cursor = source;
  l->line_start = source;
  l->line = 1;
}

int lexer_column(const Lexer *l) {
  return (int)(l->cursor - l->line_start) + 1;
}

void lexer_advance(Lexer *l) {
  char c = *l->cursor;
  if (c == '\0') {
    return;
  }

  l->cursor++;

  if (c == '\r' || c == '\n') {
    // Handle \r\n
    if (c == '\r' && *l->cursor == '\n') {
      l->cursor++;
    }

    l->line++;
    l->line_start = l->cursor;
  }
}

void skip_whitespace(Lexer *l) {
  while (*l->cursor == ' ' || *l->cursor == '\t' || *l->cursor == '\n' ||
         *l->cursor == '\r') {
    lexer_advance(l);
  }
}

Token lexer_next_token(Lexer *l) {
  Token tok;

  skip_whitespace(l);

  tok.type = TOKEN_UNKNOW;
  tok.start = l->cursor;
  tok.line = l->line;
  tok.col = lexer_column(l);
  tok.len = 1;

  if (*l->cursor == '\0') {
    tok.type = TOKEN_EOF;
    tok.len = 0;
    return tok;
  }

  if (*l->cursor == ';') {
    tok.type = TOKEN_SEMICOLON;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  }

  if (isdigit(*l->cursor)) {
    int64_t int_literal_val;

    int_literal_val = 0;

    while (isdigit(*l->cursor)) {
      int_literal_val = int_literal_val * 10 + (*l->cursor - '0');
      lexer_advance(l);
    }

    tok.type = TOKEN_INT_LITERAL;
    tok.len = (int)(l->cursor - tok.start);
    tok.int_literal_val = int_literal_val;
    return tok;
  }

  if (isalpha(*l->cursor)) {
    while (isalnum(*l->cursor)) {
      lexer_advance(l);
    }
    tok.len = (int)(l->cursor - tok.start);

    if (tok.len == 6 && strncmp(tok.start, "return", 6) == 0) {
      tok.type = TOKEN_RETURN;
    }
    return tok;
  }

  lexer_advance(l);

  return tok;
}
