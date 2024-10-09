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

  /* 6.4.1 Keywords */
  TK_AUTO,
  TK_IF,
  TK_UNSIGNED,
  TK_BREAK,
  TK_INLINE,
  TK_VOID,
  TK_CASE,
  TK_INT,
  TK_VOLATILE,
  TK_CHAR,
  TK_LONG,
  TK_WHILE,
  TK_CONST,
  TK_REGISTER,
  TK_ALIGNAS,
  TK_CONTINUE,
  TK_RESTRICT,
  TK_ALIGNOF,
  TK_DEFAULT,
  TK_RETURN,
  TK_ATOMIC,
  TK_DO,
  TK_SHORT,
  TK_BOOL,
  TK_DOUBLE,
  TK_SIGNED,
  TK_COMPLEX,
  TK_ELSE,
  TK_SIZEOF,
  TK_GENERIC,
  TK_ENUM,
  TK_STATIC,
  TK_IMAGINARY,
  TK_EXTERN,
  TK_STRUCT,
  TK_NORETURN,
  TK_FLOAT,
  TK_SWITCH,
  TK_STATIC_ASSERT,
  TK_FOR,
  TK_TYPEDEF,
  TK_THREAD_LOCAL,
  TK_GOTO,
  TK_UNION,

  TK_EOF,
} TokenKind;

typedef struct TokenData {
  TokenKind kind;
  u64 off; /* offset into the buffer of the buffer of the program source */
  u64 len; /* length of the token */
  i64 line;
  i64 column;
} TokenData;

typedef TokenData *Token;

typedef struct Expr {
  i64 integer;
} Expr;

typedef struct Stmt {
  /* return statement */
  Expr *ret;
} Stmt;

typedef struct ValueData {
} ValueData;

typedef ValueData *Value;

typedef enum InstructionKind {
  IK_RET,
} InstructionKind;

typedef struct InstructionData {
  InstructionKind kind;
} InstructionData;

typedef struct InstructionData *Instruction;

VECTOR_GENERATE_TYPE_NAME(Instruction, Instructions, instrs);
VECTOR_GENERATE_TYPE_NAME_IMPL(Instruction, Instructions, instrs);

typedef struct RetInst {
  InstructionData base;
  Value val;
} RetInst;

typedef struct BasicBlockData {
  Instructions *instrs;
} BasicBlockData;

typedef BasicBlockData *BasicBlock;

Token make_token(TokenKind kind, u64 off, u64 len, i64 line, i64 column) {
  Token tok = malloc(sizeof(TokenData));
  if (!tok)
    fatalf("failed to allocate memory\n");

  tok->kind = kind;
  tok->off = off;
  tok->len = len;
  tok->line = line;
  tok->column = column;
  return tok;
}

VECTOR_GENERATE_TYPE_NAME(Token, Tokens, tokens);
VECTOR_GENERATE_TYPE_NAME_IMPL(Token, Tokens, tokens);

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

static Token read_constant(const char *prog, const char **p, i64 *line,
                           i64 *col) {
  if (!isdigit((*p)[0]))
    return NULL;

  const char *startp = *p;
  const i64 start_col = *col;
  while (isdigit((*p)[0])) {
    ++(*col);
    ++(*p);
  }

  Token tok =
      make_token(TK_CONSTANT, (startp - prog), (*p - startp), *line, start_col);

  return tok;
}

#define MIN(a, b) (a < b ? (a) : (b))

static Token read_keywords(const char *prog, const char **p, i64 *line,
                           i64 *col) {
  const char *startp = *p;
  const i64 start_col = *col;
  char keyword[64] = {'\0'};

  while (isalpha((*p)[0]) || (*p)[0] == '_') {
    ++(*col);
    ++(*p);
  }

  memcpy(keyword, startp, MIN(63, (*p - startp)));
  TokenKind kind;

  if (startswith(keyword, "auto")) {
    kind = TK_AUTO;
  } else if (startswith(keyword, "if")) {
    kind = TK_IF;
  } else if (startswith(keyword, "unsigned")) {
    kind = TK_UNSIGNED;
  } else if (startswith(keyword, "break")) {
    kind = TK_BREAK;
  } else if (startswith(keyword, "inline")) {
    kind = TK_INLINE;
  } else if (startswith(keyword, "void")) {
    kind = TK_VOID;
  } else if (startswith(keyword, "case")) {
    kind = TK_CASE;
  } else if (startswith(keyword, "int")) {
    kind = TK_INT;
  } else if (startswith(keyword, "volatile")) {
    kind = TK_VOLATILE;
  } else if (startswith(keyword, "char")) {
    kind = TK_CHAR;
  } else if (startswith(keyword, "long")) {
    kind = TK_LONG;
  } else if (startswith(keyword, "while")) {
    kind = TK_WHILE;
  } else if (startswith(keyword, "const")) {
    kind = TK_CONST;
  } else if (startswith(keyword, "register")) {
    kind = TK_REGISTER;
  } else if (startswith(keyword, "_Alignas")) {
    kind = TK_ALIGNAS;
  } else if (startswith(keyword, "continue")) {
    kind = TK_CONTINUE;
  } else if (startswith(keyword, "restrict")) {
    kind = TK_RESTRICT;
  } else if (startswith(keyword, "_Alignof")) {
    kind = TK_ALIGNOF;
  } else if (startswith(keyword, "default")) {
    kind = TK_DEFAULT;
  } else if (startswith(keyword, "return")) {
    kind = TK_RETURN;
  } else if (startswith(keyword, "_Atomic")) {
    kind = TK_ATOMIC;
  } else if (startswith(keyword, "do")) {
    kind = TK_DO;
  } else if (startswith(keyword, "short")) {
    kind = TK_SHORT;
  } else if (startswith(keyword, "_Bool")) {
    kind = TK_BOOL;
  } else if (startswith(keyword, "double")) {
    kind = TK_DOUBLE;
  } else if (startswith(keyword, "signed")) {
    kind = TK_SIGNED;
  } else if (startswith(keyword, "_Complex")) {
    kind = TK_COMPLEX;
  } else if (startswith(keyword, "else")) {
    kind = TK_ELSE;
  } else if (startswith(keyword, "sizeof")) {
    kind = TK_SIZEOF;
  } else if (startswith(keyword, "_Generic")) {
    kind = TK_GENERIC;
  } else if (startswith(keyword, "enum")) {
    kind = TK_ENUM;
  } else if (startswith(keyword, "static")) {
    kind = TK_STATIC;
  } else if (startswith(keyword, "_Imaginary")) {
    kind = TK_IMAGINARY;
  } else if (startswith(keyword, "extern")) {
    kind = TK_EXTERN;
  } else if (startswith(keyword, "struct")) {
    kind = TK_STRUCT;
  } else if (startswith(keyword, "_Noreturn")) {
    kind = TK_NORETURN;
  } else if (startswith(keyword, "float")) {
    kind = TK_FLOAT;
  } else if (startswith(keyword, "switch")) {
    kind = TK_SWITCH;
  } else if (startswith(keyword, "_Static_assert")) {
    kind = TK_STATIC_ASSERT;
  } else if (startswith(keyword, "for")) {
    kind = TK_FOR;
  } else if (startswith(keyword, "typedef")) {
    kind = TK_TYPEDEF;
  } else if (startswith(keyword, "_Thread_local")) {
    kind = TK_THREAD_LOCAL;
  } else if (startswith(keyword, "goto")) {
    kind = TK_GOTO;
  } else if (startswith(keyword, "union")) {
    kind = TK_UNION;
  } else {
    /* Restore pointers. */
    *p = startp;
    *col = start_col;
    return NULL;
  }

  Token tok =
      make_token(kind, (startp - prog), (*p - startp), *line, start_col);

  return tok;
}

static Token read_punctuator(const char *prog, const char **p, i64 *line,
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

  Token tok = make_token(kind, (*p - prog), tok_len, *line, *col);
  (*p) += tok_len;
  (*col) += tok_len;
  return tok;
}

static Expr *parse_expr(const char *prog, Token **tok_ref) {
  Token tok = **tok_ref;
  if (tok->kind != TK_CONSTANT)
    fatalf("unexpected token (%d), TK_CONSTANT expected\n", tok->kind);

  Expr *const_expr = malloc(sizeof(Expr));
  const_expr->integer = atoi(&prog[tok->off]);
  ++(*tok_ref);
  return const_expr;
}

static Stmt *parse_stmt(const char *prog, Token **tok_ref) {
  Token tok = **tok_ref;
  if (tok->kind != TK_RETURN)
    fatalf("unexpected token (%d), TK_RETURN expected\n", tok->kind);

  Stmt *ret_stmt = malloc(sizeof(Stmt));
  ++(*tok_ref);
  ret_stmt->ret = parse_expr(prog, tok_ref);
  return ret_stmt;
}

static int flag_debug_dump_tokens = 0;
static int flag_debug_only_tokenize = 0;
static int flag_debug_dump_ast = 0;
static int flag_debug_only_parse = 0;

static void parse_args(int argc, char **argv) {
  int c;
  int option_index = 0;
  static struct option long_options[] = {
      {"debug-only-tokenize", no_argument, &flag_debug_only_tokenize, 1},
      {"debug-dump-tokens", no_argument, &flag_debug_dump_tokens, 1},
      {"debug-only-parse", no_argument, &flag_debug_only_parse, 1},
      {"debug-dump-ast", no_argument, &flag_debug_dump_ast, 1},
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

void debug_dump_tokens(const char *prog, Tokens *tokens) {
  for (int i = 0; i < tokens_len(tokens); ++i) {
    Token tok = tokens_get(tokens, i);
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

int main(int argc, char *argv[]) {
  parse_args(argc, argv);

  if (optind >= argc)
    fatalf("incorrect number of arguments\n");

  if (flag_debug_only_parse && flag_debug_only_tokenize)
    fatalf("only one of `--debug-only-tokenize` and `--debug-only-parse` can "
           "be specified\n");

  /* Tokenizer ... */
  /* p is a null-terminated string. */
  const char *prog = argv[optind];
  const char *p = argv[optind];
  i64 line = 1, column = 0;
  Tokens *tokens = make_tokens();
  while (p[0]) {
    Token tok;
    /*
     * token:
     *   keyword
     *   identifier
     *   constant
     *   string-literal
     *   punctuator
     */
    consume_whitespace(&p, &line, &column);

    /* Try to read keyword */
    if ((tok = read_keywords(prog, &p, &line, &column)) != NULL) {
      tokens_append(tokens, tok);
      continue;
    }

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

  if (flag_debug_dump_tokens)
    debug_dump_tokens(prog, tokens);

  if (flag_debug_only_tokenize)
    exit(0);

  /* Parser... */
  Token *tok = tokens_data(tokens);
  Stmt *stmt;
  while ((*tok)->kind != TK_EOF) {
    stmt = parse_stmt(prog, &tok);
  }

  /*  */
  BasicBlock bb = malloc(sizeof(BasicBlockData));
  bb->instrs = make_instrs();

  return 0;
}
