#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vector.h"

static inline void fatalf(const char *format, ...) {
  va_list args;

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  exit(1);
}

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint8_t u8;
typedef int8_t i8;

typedef enum TokenKind {
  TK_INVALID = 0,
  /* 6.4.4 Constants */
  TK_CONSTANT,
  /* 6.4.6 Punctuators */
  TK_LBRACKET,       /* '[' */
  TK_RBRACKET,       /* ']' */
  TK_LPAREN,         /* '(' */
  TK_RPAREN,         /* ')' */
  TK_LBRACE,         /* '{' */
  TK_RBRACE,         /* '}' */
  TK_DOT,            /* '.' */
  TK_ARROW,          /* '->' */
  TK_INCR,           /* '++' */
  TK_DECR,           /* '--' */
  TK_AMPERSAND,      /* '&' */
  TK_ASTERISK,       /* '*' */
  TK_PLUS,           /* '+' */
  TK_MINUS,          /* '-' */
  TK_BITNOT,         /* '~' */
  TK_NOT,            /* '!' */
  TK_DIVIDE,         /* '/' */
  TK_MOD,            /* '%' */
  TK_LSHIFT,         /* '<<' */
  TK_RSHIFT,         /* '>>' */
  TK_LT,             /* '<' */
  TK_GT,             /* '>' */
  TK_LE,             /* '<=' */
  TK_GE,             /* '>=' */
  TK_EQ,             /* '==' */
  TK_NE,             /* '!=' */
  TK_XOR,            /* '^' */
  TK_BITOR,          /* '|' */
  TK_AND,            /* '&&' */
  TK_OR,             /* '||' */
  TK_QUESTION,       /* '?' */
  TK_COLON,          /* ':' */
  TK_SEMICOLON,      /* ';' */
  TK_ELIPSIS,        /* '...' */
  TK_ASSIGN,         /* '=' */
  TK_TIMESEQ,        /* '*=' */
  TK_DIVIDEEQ,       /* '/=' */
  TK_MODEQ,          /* '%=' */
  TK_PLUSEQ,         /* '+=' */
  TK_MINUSEQ,        /* '-=' */
  TK_LSHIFTEQ,       /* '<<=' */
  TK_RSHIFTEQ,       /* '>>=' */
  TK_ANDEQ,          /* '&=' */
  TK_XOREQ,          /* '^=' */
  TK_OREQ,           /* '|=' */
  TK_COMMA,          /* ',' */
  TK_HASH,           /* '#' */
  TK_HASH2,          /* '##' */
  TK_LBRACKET_ALIAS, /* '<:' */
  TK_RBRACKET_ALIAS, /* ':>' */
  TK_LBRACE_ALIAS,   /* '<%' */
  TK_RBRACE_ALIAS,   /* '%>' */
  TK_HASH_ALIAS,     /* '%:' */
  TK_HASH2_ALIAS,    /* '%:%:' */
  TK_EOF,
} TokenKind;

typedef struct Token {
  TokenKind kind;
  u64 off; /* offset into the buffer of the buffer of the program source */
  u64 len; /* length of the token */
  i64 line;
  i64 column;
} Token;

Token *make_token(TokenKind kind, u64 off, u64 len, i64 line, i64 column) {
  Token *tok = malloc(sizeof(Token));
  if (!tok)
    fatalf("failed to allocate memory\n");

  tok->kind = kind;
  tok->off = off;
  tok->len = len;
  tok->line = line;
  tok->column = column;
  return tok;
}

VECTOR_GENERATE_TYPE_NAME(Token *, Tokens, tokens);
VECTOR_GENERATE_TYPE_NAME_IMPL(Token *, Tokens, tokens);

static inline bool startswith(const char *p1, const char *p2) {
  return strncmp(p1, p2, strlen(p2)) == 0;
}

static void consume_whitespace(const char **p, i64 *line, i64 *col) {
  while (isspace((*p)[0])) {
    if ((*p)[0] == ' ') {
      ++(*col);
    } else if ((*p)[0] == '\n') {
      ++(*line);
    } else {
      fatalf("unrecognized whitespace\n");
    }
    ++(*p);
  }
}

static Token *read_constant(const char *prog, const char **p, i64 *line,
                            i64 *col) {
  if (!isdigit((*p)[0]))
    return NULL;

  const char *startp = *p;
  const i64 start_col = *col;
  while (isdigit((*p)[0])) {
    ++(*col);
    ++(*p);
  }

  Token *tok =
      make_token(TK_CONSTANT, (startp - prog), (*p - startp), *line, start_col);

  return tok;
}

static Token *read_punctuator(const char *prog, const char **p, i64 *line,
                              i64 *col) {
  u64 tok_len;
  TokenKind kind;
  switch ((*p)[0]) {
  case '[':
    kind = TK_LBRACKET;
    tok_len = 1;
    break;
  case ']':
    kind = TK_RBRACKET;
    tok_len = 1;
    break;
  case '(':
    kind = TK_LPAREN;
    tok_len = 1;
    break;
  case ')':
    kind = TK_RPAREN;
    tok_len = 1;
    break;
  case '{':
    kind = TK_LBRACE;
    tok_len = 1;
    break;
  case '}':
    kind = TK_RBRACE;
    tok_len = 1;
    break;
  case '~':
    kind = TK_BITNOT;
    tok_len = 1;
    break;
  case '?':
    kind = TK_QUESTION;
    tok_len = 1;
    break;
  case ';':
    kind = TK_SEMICOLON;
    tok_len = 1;
    break;
  case ',':
    kind = TK_COMMA;
    tok_len = 1;
    break;
  case '.':
    if (startswith(*p, "...")) {
      kind = TK_ELIPSIS;
      tok_len = 3;
    } else {
      kind = TK_DOT;
      tok_len = 1;
    }
    break;
  case '&':
    if (startswith(*p, "&=")) {
      kind = TK_ANDEQ;
      tok_len = 2;
    } else if (startswith(*p, "&&")) {
      kind = TK_AND;
      tok_len = 2;
    } else {
      kind = TK_AMPERSAND;
      tok_len = 1;
    }
    break;
  case '*':
    if (startswith(*p, "*=")) {
      kind = TK_TIMESEQ;
      tok_len = 2;
    } else {
      kind = TK_ASTERISK;
      tok_len = 1;
    }
    break;
  case '+':
    if (startswith(*p, "++")) {
      kind = TK_INCR;
      tok_len = 2;
    } else if (startswith(*p, "+=")) {
      kind = TK_PLUSEQ;
      tok_len = 2;
    } else {
      kind = TK_PLUS;
      tok_len = 1;
    }
    break;
  case '-':
    if (startswith(*p, "->")) {
      kind = TK_ARROW;
      tok_len = 2;
    } else if (startswith(*p, "--")) {
      kind = TK_DECR;
      tok_len = 2;
    } else if (startswith(*p, "-=")) {
      kind = TK_MINUSEQ;
      tok_len = 2;
    } else {
      kind = TK_MINUS;
      tok_len = 1;
    }
    break;
  case '!':
    if (startswith(*p, "!=")) {
      kind = TK_NE;
      tok_len = 2;
    } else {
      kind = TK_NOT;
      tok_len = 1;
    }
    break;
  case '/':
    if (startswith(*p, "/=")) {
      kind = TK_DIVIDEEQ;
      tok_len = 2;
    } else {
      kind = TK_DIVIDE;
      tok_len = 1;
    }
    break;
  case '%':
    if (startswith(*p, "%=")) {
      kind = TK_MODEQ;
      tok_len = 2;
    } else if (startswith(*p, "%>")) {
      kind = TK_RBRACE_ALIAS;
      tok_len = 2;
    } else if (startswith(*p, "%:%:")) {
      kind = TK_HASH2_ALIAS;
      tok_len = 4;
    } else if (startswith(*p, "%:")) {
      kind = TK_HASH_ALIAS;
      tok_len = 2;
    } else {
      kind = TK_MOD;
      tok_len = 1;
    }
    break;
  case '<':
    if (startswith(*p, "<<=")) {
      kind = TK_LSHIFTEQ;
      tok_len = 3;
    } else if (startswith(*p, "<<")) {
      kind = TK_LSHIFT;
      tok_len = 2;
    } else if (startswith(*p, "<=")) {
      kind = TK_LE;
      tok_len = 2;
    } else if (startswith(*p, "<:")) {
      kind = TK_LBRACKET_ALIAS;
      tok_len = 2;
    } else if (startswith(*p, "<%")) {
      kind = TK_LBRACE_ALIAS;
      tok_len = 2;
    } else {
      kind = TK_LT;
      tok_len = 1;
    }
    break;
  case '>':
    if (startswith(*p, ">>=")) {
      kind = TK_RSHIFTEQ;
      tok_len = 3;
    } else if (startswith(*p, ">>")) {
      kind = TK_RSHIFT;
      tok_len = 2;
    } else if (startswith(*p, ">=")) {
      kind = TK_GE;
      tok_len = 2;
    } else {
      kind = TK_GT;
      tok_len = 1;
    }
    break;
  case '=':
    if (startswith(*p, "==")) {
      kind = TK_EQ;
      tok_len = 2;
    } else {
      kind = TK_ASSIGN;
      tok_len = 1;
    }
    break;
  case '^':
    if (startswith(*p, "^=")) {
      kind = TK_XOREQ;
      tok_len = 2;
    } else {
      kind = TK_XOR;
      tok_len = 1;
    }
    break;
  case '|':
    if (startswith(*p, "|=")) {
      kind = TK_OREQ;
      tok_len = 2;
    } else if (startswith(*p, "||")) {
      kind = TK_OR;
      tok_len = 2;
    } else {
      kind = TK_BITOR;
      tok_len = 1;
    }
    break;
  case ':':
    if (startswith(*p, ":>")) {
      kind = TK_RBRACKET_ALIAS;
      tok_len = 2;
    } else {
      kind = TK_COLON;
      tok_len = 1;
    }
    break;
  case '#':
    if (startswith(*p, "##")) {
      kind = TK_HASH2;
      tok_len = 2;
    } else {
      kind = TK_HASH;
      tok_len = 1;
    }
    break;
  default:
    return NULL;
  }

  Token *tok = make_token(kind, (*p - prog), tok_len, *line, *col);
  (*p) += tok_len;
  (*col) += tok_len;
  return tok;
}

static int flag_debug_dump_tokens = 0;

static void parse_args(int argc, char **argv) {
  int c;
  int option_index = 0;
  static struct option long_options[] = {
      {"debug-dump-tokens", no_argument, &flag_debug_dump_tokens, 1},
      {0, 0, 0, 0},
  };

  while (1) {
    c = getopt_long(argc, argv, "", long_options, &option_index);

    /* Detect the end of options. */
    if (c == -1) {
      break;
    }
  }
}

int main(int argc, char *argv[]) {
  parse_args(argc, argv);

  if (optind >= argc)
    fatalf("incorrect number of arguments\n");

  // printf("    .global main\n");
  // printf("main:\n");

  /* p is a null-terminated string. */
  const char *prog = argv[optind];
  const char *p = argv[optind];
  i64 line = 1, column = 0;
  Tokens *tokens = make_tokens();
  while (p[0]) {
    Token *tok;
    /*
     * token:
     *   keyword
     *   identifier
     *   constant
     *   string-literal
     *   punctuator
     */
    consume_whitespace(&p, &line, &column);

    /* Try to read constant (number literal) */
    if ((tok = read_constant(prog, &p, &line, &column)) != NULL) {
      tokens_append(tokens, tok);
      continue;
    }

    /* Try to read punctuator */
    if ((tok = read_punctuator(prog, &p, &line, &column)) != NULL) {
      tokens_append(tokens, tok);
      continue;
    }

    if (p[0])
      fatalf("failed to parse the rest of the program: '%s'\n", p);
  }

  tokens_append(tokens, make_token(TK_EOF, 0, 0, -1, -1));

  if (flag_debug_dump_tokens) {
    for (int i = 0; i < tokens_len(tokens); ++i) {
      Token *tok = tokens_get(tokens, i);
      char literal[100];
      if (tok->kind != TK_EOF) {
        memcpy(literal, &prog[tok->off], tok->len);
        literal[tok->len] = '\0';
      }
      switch (tok->kind) {
      case TK_CONSTANT:
        printf("TK_CONSTANT '%s' line: %ld column: %ld\n", literal, tok->line,
               tok->column);
        break;
      case TK_EOF:
        printf("TK_EOF line: %ld column: %ld\n", tok->line, tok->column);
        break;
      default:
        printf("TK_PUNCTUATOR '%s' line: %ld column: %ld\n", literal, tok->line,
               tok->column);
        break;
      }
    }
  }

  // printf("    retq\n");
  return 0;
}
