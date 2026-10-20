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
#include <stdio.h>
#include <string.h>

#include "lexer.h"

typedef struct {
  const char *name;
  size_t len;
  TokenType type;
} Keyword;

/* C11 keywords */
#define KW(spelling, kind)                                                     \
  {.name = spelling, .len = sizeof(spelling) - 1, .type = kind}

static const Keyword keywords[] = {
    KW("_Alignas", TOKEN_RESERVED),
    KW("_Alignof", TOKEN_RESERVED),
    KW("_Atomic", TOKEN_RESERVED),
    KW("_Bool", TOKEN_RESERVED),
    KW("_Complex", TOKEN_RESERVED),
    KW("_Generic", TOKEN_RESERVED),
    KW("_Imaginary", TOKEN_RESERVED),
    KW("_Noreturn", TOKEN_RESERVED),
    KW("_Static_assert", TOKEN_RESERVED),
    KW("_Thread_local", TOKEN_RESERVED),
    KW("auto", TOKEN_RESERVED),
    KW("break", TOKEN_BREAK),
    KW("case", TOKEN_CASE),
    KW("char", TOKEN_RESERVED),
    KW("const", TOKEN_RESERVED),
    KW("continue", TOKEN_CONTINUE),
    KW("default", TOKEN_DEFAULT),
    KW("do", TOKEN_DO),
    KW("double", TOKEN_RESERVED),
    KW("else", TOKEN_ELSE),
    KW("enum", TOKEN_RESERVED),
    KW("extern", TOKEN_RESERVED),
    KW("float", TOKEN_RESERVED),
    KW("for", TOKEN_FOR),
    KW("goto", TOKEN_GOTO),
    KW("if", TOKEN_IF),
    KW("inline", TOKEN_RESERVED),
    KW("int", TOKEN_INT),
    KW("long", TOKEN_RESERVED),
    KW("register", TOKEN_RESERVED),
    KW("restrict", TOKEN_RESERVED),
    KW("return", TOKEN_RETURN),
    KW("short", TOKEN_RESERVED),
    KW("signed", TOKEN_RESERVED),
    KW("sizeof", TOKEN_RESERVED),
    KW("static", TOKEN_RESERVED),
    KW("struct", TOKEN_RESERVED),
    KW("switch", TOKEN_SWITCH),
    KW("typedef", TOKEN_RESERVED),
    KW("union", TOKEN_RESERVED),
    KW("unsigned", TOKEN_RESERVED),
    KW("void", TOKEN_RESERVED),
    KW("volatile", TOKEN_RESERVED),
    KW("while", TOKEN_WHILE),
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
    /* Handle \r\n */
    if (c == '\r' && *l->cursor == '\n') {
      l->cursor++;
    }

    l->line++;
    l->line_start = l->cursor;
  }
}

/*
 * Skip whitespace and comments (C11 6.4.9: each comment is replaced by one
 * space character).
 */
void skip_whitespace(Lexer *l) {
  for (;;) {
    if (*l->cursor == ' ' || *l->cursor == '\t' || *l->cursor == '\n' ||
        *l->cursor == '\r') {
      lexer_advance(l);
      continue;
    }

    if (*l->cursor == '/' && l->cursor[1] == '/') {
      /* Line comment: skip to the end of the line. */
      while (*l->cursor != '\0' && *l->cursor != '\n')
        lexer_advance(l);
      continue;
    }

    if (*l->cursor == '/' && l->cursor[1] == '*') {
      /* Block comment: skip to the closing delimiter. */
      lexer_advance(l);
      lexer_advance(l);
      for (;;) {
        if (*l->cursor == '\0') {
          fprintf(stderr, "%s:%d:%d: error: unterminated comment\n",
                  l->filename, l->line, lexer_column(l));
          exit(1);
        }
        if (*l->cursor == '*' && l->cursor[1] == '/') {
          lexer_advance(l);
          lexer_advance(l);
          break;
        }
        lexer_advance(l);
      }
      continue;
    }

    return;
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
  case ':':
    tok.type = TOKEN_COLON;
    tok.len = 1;
    lexer_advance(l);
    return tok;
  case '+':
    lexer_advance(l);
    if (*l->cursor == '+') {
      lexer_advance(l);
      tok.type = TOKEN_PLUS_PLUS;
      tok.len = 2;
    } else if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_PLUS_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_PLUS;
      tok.len = 1;
    }
    return tok;
  case '-':
    lexer_advance(l);
    if (*l->cursor == '-') {
      lexer_advance(l);
      tok.type = TOKEN_MINUS_MINUS;
      tok.len = 2;
    } else if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_MINUS_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_MINUS;
      tok.len = 1;
    }
    return tok;
  case '*':
    lexer_advance(l);
    if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_STAR_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_STAR;
      tok.len = 1;
    }
    return tok;
  case '/':
    lexer_advance(l);
    if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_SLASH_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_SLASH;
      tok.len = 1;
    }
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
    lexer_advance(l);
    if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_NOT_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_BANG;
      tok.len = 1;
    }
    return tok;
  case '&':
    lexer_advance(l);
    if (*l->cursor == '&') {
      lexer_advance(l);
      tok.type = TOKEN_AND_AND;
      tok.len = 2;
    } else if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_BIT_AND_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_BIT_AND;
      tok.len = 1;
    }
    return tok;
  case '|':
    lexer_advance(l);
    if (*l->cursor == '|') {
      lexer_advance(l);
      tok.type = TOKEN_OR_OR;
      tok.len = 2;
    } else if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_BIT_OR_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_BIT_OR;
      tok.len = 1;
    }
    return tok;
  case '^':
    lexer_advance(l);
    if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_BIT_XOR_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_BIT_XOR;
      tok.len = 1;
    }
    return tok;
  case '=':
    lexer_advance(l);
    if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_EQ_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_ASSIGN;
      tok.len = 1;
    }
    return tok;
  case '<':
    lexer_advance(l);
    if (*l->cursor == '<') {
      lexer_advance(l);
      if (*l->cursor == '=') {
        lexer_advance(l);
        tok.type = TOKEN_LSHIFT_EQ;
        tok.len = 3;
      } else {
        tok.type = TOKEN_LSHIFT;
        tok.len = 2;
      }
    } else if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_LESS_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_LESS;
      tok.len = 1;
    }
    return tok;
  case '>':
    lexer_advance(l);
    if (*l->cursor == '>') {
      lexer_advance(l);
      if (*l->cursor == '=') {
        lexer_advance(l);
        tok.type = TOKEN_RSHIFT_EQ;
        tok.len = 3;
      } else {
        tok.type = TOKEN_RSHIFT;
        tok.len = 2;
      }
    } else if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_GREATER_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_GREATER;
      tok.len = 1;
    }
    return tok;
  case '%':
    lexer_advance(l);
    if (*l->cursor == '=') {
      lexer_advance(l);
      tok.type = TOKEN_PERCENT_EQ;
      tok.len = 2;
    } else {
      tok.type = TOKEN_PERCENT;
      tok.len = 1;
    }
    return tok;
  }

  if (isdigit(*l->cursor)) {
    int64_t int_literal_val;

    int_literal_val = 0;

    while (isdigit(*l->cursor)) {
      int digit;
      digit = *l->cursor - '0';
      if (int_literal_val > (INT32_MAX - digit) / 10) {
        fprintf(
            stderr,
            "%s:%d:%d: error: integer literal out of range for 32-bit int\n",
            l->filename, tok.line, tok.col);
        exit(1);
      }
      int_literal_val = int_literal_val * 10 + digit;
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
