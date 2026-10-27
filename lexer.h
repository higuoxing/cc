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

#include <stdint.h>
#include <stdlib.h>

typedef enum {
  TOKEN_UNKNOW = -1,
  TOKEN_EOF = 0,
  TOKEN_AND_AND,    /* '&&' */
  TOKEN_ASSIGN,     /* '=' */
  TOKEN_BANG,       /* '!' */
  TOKEN_BIT_AND,    /* '&' */
  TOKEN_BIT_AND_EQ, /* '&=' */
  TOKEN_BIT_OR,     /* '|' */
  TOKEN_BIT_OR_EQ,  /* '|=' */
  TOKEN_BIT_XOR,    /* '^' */
  TOKEN_BIT_XOR_EQ, /* '^=' */
  TOKEN_BREAK,      /* 'break' */
  TOKEN_CASE,       /* 'case' */
  TOKEN_CHAR,       /* 'char' */
  TOKEN_COLON,      /* ':' */
  TOKEN_COMMA,      /* ',' */
  TOKEN_CONTINUE,   /* 'continue' */
  TOKEN_DEFAULT,    /* 'default' */
  TOKEN_DO,         /* 'do' */
  TOKEN_ELSE,       /* 'else' */
  TOKEN_EQ_EQ,      /* '==' */
  TOKEN_FOR,        /* 'for' */
  TOKEN_GOTO,       /* 'goto' */
  TOKEN_GREATER,    /* '>' */
  TOKEN_GREATER_EQ, /* '>=' */
  TOKEN_IDENTIFIER,
  TOKEN_IF,  /* 'if' */
  TOKEN_INT, /* 'int' */
  TOKEN_INT_LITERAL,
  TOKEN_LBRACE,      /* '{' */
  TOKEN_LESS,        /* '<' */
  TOKEN_LESS_EQ,     /* '<=' */
  TOKEN_LONG,        /* 'long' */
  TOKEN_LPAREN,      /* '(' */
  TOKEN_LSHIFT,      /* '<<' */
  TOKEN_LSHIFT_EQ,   /* '<<=' */
  TOKEN_MINUS,       /* '-' */
  TOKEN_MINUS_EQ,    /* '-=' */
  TOKEN_MINUS_MINUS, /* '--' */
  TOKEN_NOT_EQ,      /* '!=' */
  TOKEN_OR_OR,       /* '||' */
  TOKEN_PERCENT,     /* '%' */
  TOKEN_PERCENT_EQ,  /* '%=' */
  TOKEN_PLUS,        /* '+' */
  TOKEN_PLUS_EQ,     /* '+=' */
  TOKEN_PLUS_PLUS,   /* '++' */
  TOKEN_QUESTION,    /* '?' */
  TOKEN_RBRACE,      /* '}' */
  TOKEN_RESERVED,
  TOKEN_RETURN,
  TOKEN_RPAREN,    /* ')' */
  TOKEN_RSHIFT,    /* '>>' */
  TOKEN_RSHIFT_EQ, /* '>>=' */
  TOKEN_SEMICOLON,
  TOKEN_SHORT,    /* 'short' */
  TOKEN_SLASH,    /* '/' */
  TOKEN_SLASH_EQ, /* '/=' */
  TOKEN_STAR,     /* '*' */
  TOKEN_STAR_EQ,  /* '*=' */
  TOKEN_SWITCH,   /* 'switch' */
  TOKEN_TILDE,    /* '~' */
  TOKEN_UNSIGNED, /* 'unsigned' */
  TOKEN_VOID, /* 'void' */
  TOKEN_WHILE, /* 'while' */
} TokenKind;

enum {
  INT_SUFFIX_UNSIGNED = 1 << 0,
  INT_SUFFIX_LONG = 1 << 1,
};

typedef struct {
  TokenKind kind;
  const char *start;
  int len;
  int line;
  int col;
  uint64_t int_literal_val;
  unsigned int_literal_suffix;
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
