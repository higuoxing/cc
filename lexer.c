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

typedef struct {
  const char *name;
  size_t len;
  TokenType type;
} Keyword;

// C11 keywords
#define KW(spelling, kind)                                                     \
  {.name = spelling, .len = sizeof(spelling) - 1, .type = kind}

static const Keyword keywords[] = {
    KW("_Alignas", TOKEN_RESERVED),       KW("_Alignof", TOKEN_RESERVED),
    KW("_Atomic", TOKEN_RESERVED),        KW("_Bool", TOKEN_RESERVED),
    KW("_Complex", TOKEN_RESERVED),       KW("_Generic", TOKEN_RESERVED),
    KW("_Imaginary", TOKEN_RESERVED),     KW("_Noreturn", TOKEN_RESERVED),
    KW("_Static_assert", TOKEN_RESERVED), KW("_Thread_local", TOKEN_RESERVED),
    KW("auto", TOKEN_RESERVED),           KW("break", TOKEN_RESERVED),
    KW("case", TOKEN_RESERVED),           KW("char", TOKEN_RESERVED),
    KW("const", TOKEN_RESERVED),          KW("continue", TOKEN_RESERVED),
    KW("default", TOKEN_RESERVED),        KW("do", TOKEN_RESERVED),
    KW("double", TOKEN_RESERVED),         KW("else", TOKEN_RESERVED),
    KW("enum", TOKEN_RESERVED),           KW("extern", TOKEN_RESERVED),
    KW("float", TOKEN_RESERVED),          KW("for", TOKEN_RESERVED),
    KW("goto", TOKEN_RESERVED),           KW("if", TOKEN_RESERVED),
    KW("inline", TOKEN_RESERVED),         KW("int", TOKEN_INT),
    KW("long", TOKEN_RESERVED),           KW("register", TOKEN_RESERVED),
    KW("restrict", TOKEN_RESERVED),       KW("return", TOKEN_RETURN),
    KW("short", TOKEN_RESERVED),          KW("signed", TOKEN_RESERVED),
    KW("sizeof", TOKEN_RESERVED),         KW("static", TOKEN_RESERVED),
    KW("struct", TOKEN_RESERVED),         KW("switch", TOKEN_RESERVED),
    KW("typedef", TOKEN_RESERVED),        KW("union", TOKEN_RESERVED),
    KW("unsigned", TOKEN_RESERVED),       KW("void", TOKEN_RESERVED),
    KW("volatile", TOKEN_RESERVED),       KW("while", TOKEN_RESERVED),
};

#undef KW

static TokenType kw_token_type(const Token *tok) {
  size_t i;
  for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
    const Keyword *kw = &keywords[i];
    if ((size_t)tok->len == kw->len &&
        memcmp(tok->start, kw->name, kw->len) == 0)
      return kw->type;
  }

  return TOKEN_IDENTIFIER;
}

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

  switch (*l->cursor) {
  case ';':
    tok.type = TOKEN_SEMICOLON;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '+':
    tok.type = TOKEN_PLUS;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '-':
    tok.type = TOKEN_MINUS;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '*':
    tok.type = TOKEN_STAR;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '/':
    tok.type = TOKEN_SLASH;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '(':
    tok.type = TOKEN_LPAREN;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case ')':
    tok.type = TOKEN_RPAREN;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '{':
    tok.type = TOKEN_LBRACE;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '}':
    tok.type = TOKEN_RBRACE;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '~':
    tok.type = TOKEN_TILDE;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '!':
    tok.type = TOKEN_BANG;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '=':
    tok.type = TOKEN_ASSIGN;
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

  if (isalpha(*l->cursor) || *l->cursor == '_') {
    while (isalnum(*l->cursor) || *l->cursor == '_') {
      lexer_advance(l);
    }
    tok.len = (int)(l->cursor - tok.start);
    tok.type = kw_token_type(&tok);

    return tok;
  }

  lexer_advance(l);

  return tok;
}
