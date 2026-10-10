/*
 * Main driver for the self-hosted C compiler
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
#include <stdlib.h>
#include <string.h>

char *read_source(const char *path) {
  FILE *file;
  long len;
  char *buf;
  size_t bytes_read;

  file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "failed to open file '%s'\n", path);
    exit(1);
  }

  fseek(file, 0, SEEK_END);
  len = ftell(file);
  if (len < 0) {
    fclose(file);
    fprintf(stderr, "failed to measure file size '%s'\n", path);
    exit(1);
  }
  fseek(file, 0, SEEK_SET);

  buf = malloc(len + 1);
  if (!buf) {
    fclose(file);
    fprintf(stderr, "failed to allocate memory for reading file '%s'\n", path);
    exit(1);
  }

  bytes_read = fread(buf, 1, (size_t)len, file);
  buf[bytes_read] = '\0';

  fclose(file);
  return buf;
}

typedef struct {
  const char *filename;
  const char *source;
  const char *cursor;
  const char *line_start;
  int line;
} Lexer;

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

typedef enum {
  TOKEN_UNKNOW = -1,
  TOKEN_EOF = 0,
  TOKEN_RETURN,
  TOKEN_SEMICOLON,
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  int len;
  int line;
  int col;
} Token;

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

typedef struct {
  Lexer lexer;
  Token current;
} Parser;

void parser_advance(Parser *p) {
  p->current = lexer_next_token(&p->lexer);
}

void parser_init(Parser *p, const char *filename, const char *source) {
  lexer_init(&p->lexer, filename, source);
  parser_advance(p);
}

typedef enum {
  AST_RETURN,
} AstType;

typedef struct {
	AstType type;
} AstNode;

void parse_error(Parser *p, const char *errmsg) {
  fprintf(stderr, "%s:%d:%d: error: %s\n", p->lexer.filename, p->current.line,
          p->current.col, errmsg);
  exit(1);
}

AstNode *parse_return_stmt(Parser *p) {
  AstNode *ast;

  if (p->current.type != TOKEN_RETURN)
    parse_error(p, "expect 'return'");

  // Consume 'return'
  parser_advance(p);

  if (p->current.type != TOKEN_SEMICOLON)
    parse_error(p, "expect ';' after return");

  // Consume ';'
  parser_advance(p);

  ast = (AstNode *)malloc(sizeof(AstNode));
  ast->type = AST_RETURN;
  return ast;
}

AstNode *parse_stmt(Parser *p) {
  if (p->current.type == TOKEN_RETURN) {
    return parse_return_stmt(p);
  }

  parse_error(p, "failed to parse statement");
  return NULL;
}

void codegen_stmt(AstNode *ast) {
  switch (ast->type) {
  case AST_RETURN:
    printf("	movl $0, %%eax\n");
    printf("	ret\n");
    break;
  default:
    fprintf(stderr, "unknown AST node\n");
    exit(1);
  }
}

void codegen(AstNode *ast) {
  printf("	.globl main\n");
  printf("main:\n");

  codegen_stmt(ast);
}

void dump_ast(AstNode *ast) {
  switch (ast->type) {
  case AST_RETURN:
    printf("RETURN\n");
    break;
  default:
    fprintf(stderr, "unknown ast node\n");
    exit(1);
  }
}

int main(int argc, char **argv) {
  const char *source;
  const char *filename;
  Parser p;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    exit(1);
  }

  filename = argv[1];
  source = read_source(filename);

  parser_init(&p, filename, source);

  codegen(parse_stmt(&p));

  return 0;
}
