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
#define PUNCTUATORS(X)                                                         \
  X(LBRACKET, "[")                                                             \
  X(RBRACKET, "]")                                                             \
  X(LPAREN, "(")                                                               \
  X(RPAREN, ")")                                                               \
  X(LBRACE, "{")                                                               \
  X(RBRACE, "}")                                                               \
  X(DOT, ".")                                                                  \
  X(ARROW, "->")                                                               \
  X(INCR, "++")                                                                \
  X(DECR, "--")                                                                \
  X(AMPERSAND, "&")                                                            \
  X(ASTERISK, "*")                                                             \
  X(PLUS, "+")                                                                 \
  X(MINUS, "-")                                                                \
  X(BITNOT, "~")                                                               \
  X(NOT, "!")                                                                  \
  X(DIVIDE, "/")                                                               \
  X(MOD, "%")                                                                  \
  X(LSHIFT, "<<")                                                              \
  X(RSHIFT, ">>")                                                              \
  X(LT, "<")                                                                   \
  X(GT, ">")                                                                   \
  X(LE, "<=")                                                                  \
  X(GE, ">=")                                                                  \
  X(EQ, "==")                                                                  \
  X(NE, "!=")                                                                  \
  X(XOR, "^")                                                                  \
  X(BITOR, "|")                                                                \
  X(AND, "&&")                                                                 \
  X(OR, "||")                                                                  \
  X(QUESTION, "?")                                                             \
  X(COLON, ":")                                                                \
  X(SEMICOLON, ";")                                                            \
  X(ELIPSIS, "...")                                                            \
  X(ASSIGN, "=")                                                               \
  X(TIMESEQ, "*=")                                                             \
  X(DIVIDEEQ, "/=")                                                            \
  X(MODEQ, "%=")                                                               \
  X(PLUSEQ, "+=")                                                              \
  X(MINUSEQ, "-=")                                                             \
  X(LSHIFTEQ, "<<=")                                                           \
  X(RSHIFTEQ, ">>=")                                                           \
  X(ANDEQ, "&=")                                                               \
  X(XOREQ, "^=")                                                               \
  X(OREQ, "|=")                                                                \
  X(COMMA, ",")                                                                \
  X(HASH, "#")                                                                 \
  X(HASH2, "##")                                                               \
  X(LBRACKET_ALIAS, "<:")                                                      \
  X(RBRACKET_ALIAS, ":>")                                                      \
  X(LBRACE_ALIAS, "<%")                                                        \
  X(RBRACE_ALIAS, "%>")                                                        \
  X(HASH_ALIAS, "%:")                                                          \
  X(HASH2_ALIAS, "%:%:")

/* 6.4.1 Keywords */
#define KEYWORDS(X)                                                            \
  X(AUTO, "auto")                                                              \
  X(IF, "if")                                                                  \
  X(UNSIGNED, "unsigned")                                                      \
  X(BREAK, "break")                                                            \
  X(INLINE, "inline")                                                          \
  X(VOID, "void")                                                              \
  X(CASE, "case")                                                              \
  X(INT, "int")                                                                \
  X(VOLATILE, "volatile")                                                      \
  X(CHAR, "char")                                                              \
  X(LONG, "long")                                                              \
  X(WHILE, "while")                                                            \
  X(CONST, "const")                                                            \
  X(REGISTER, "register")                                                      \
  X(ALIGNAS, "_Alignas")                                                       \
  X(CONTINUE, "continue")                                                      \
  X(RESTRICT, "restrict")                                                      \
  X(ALIGNOF, "_Alignof")                                                       \
  X(DEFAULT, "default")                                                        \
  X(RETURN, "return")                                                          \
  X(ATOMIC, "_Atomic")                                                         \
  X(DO, "do")                                                                  \
  X(SHORT, "short")                                                            \
  X(BOOL, "_Bool")                                                             \
  X(DOUBLE, "double")                                                          \
  X(SIGNED, "signed")                                                          \
  X(COMPLEX, "_Complex")                                                       \
  X(ELSE, "else")                                                              \
  X(SIZEOF, "sizeof")                                                          \
  X(GENERIC, "_Generic")                                                       \
  X(ENUM, "enum")                                                              \
  X(STATIC, "static")                                                          \
  X(IMAGINARY, "_Imaginary")                                                   \
  X(EXTERN, "extern")                                                          \
  X(STRUCT, "struct")                                                          \
  X(NORETURN, "_Noreturn")                                                     \
  X(FLOAT, "float")                                                            \
  X(SWITCH, "switch")                                                          \
  X(STATIC_ASSERT, "_Static_assert")                                           \
  X(FOR, "for")                                                                \
  X(TYPEDEF, "typedef")                                                        \
  X(THREAD_LOCAL, "_Thread_local")                                             \
  X(GOTO, "goto")                                                              \
  X(UNION, "union")

#define TK_NAME(NAME, LITERAL) TK_##NAME,

  PUNCTUATORS(TK_NAME) KEYWORDS(TK_NAME) TK_EOF
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
  enum {
    SK_INVALID = 0,
    SK_RET,
  } kind;
  /* return statement */
  union {
    Expr *ret;
  } inner;
} Stmt;

typedef struct IRJmp {
  enum {
    JMP_INV = 0,
    JMP_RET,
  } kind;
} IRJmp;

typedef struct BasicBlockData {
  /* Id of the current basic block */
  u32 id;
  /* Name of the current basic block */
  char name[64];

  /* Terminator instruction of the current block */
  IRJmp jmp;

  /* Used in the cfg. */
  struct BasicBlockData *cfg_next_bb;
  /* Used in the hash table. */
  struct BasicBlockData *hash_next_bb;
} BasicBlockData;

typedef BasicBlockData *BasicBlock;

static u32 hash(const char *s) {
  u32 h;

  for (h = 0; *s; ++s)
    h = *s + 17 * h;
  return h;
}

BasicBlock make_bb(u32 id, const char *name) {
  BasicBlock bb = malloc(sizeof(BasicBlockData));
  memset(bb, 0, sizeof(BasicBlockData));
  bb->id = id;
  strcpy(bb->name, name);
  return bb;
}

static BasicBlock find_or_make_bb(const char *name) {
  static u32 bbid = 0;
  static BasicBlock basicblocks[8192];
  static u32 nbb = 0;
  u32 h = hash(name) & 8191;
  BasicBlock bb = NULL;
  for (bb = basicblocks[h]; bb; bb = bb->hash_next_bb) {
    if (strcmp(bb->name, name) == 0)
      return bb;
  }

  bb = make_bb(nbb++, name);
  bb->hash_next_bb = basicblocks[h];
  basicblocks[h] = bb;
  return bb;
}

void gen_ir_for_stmt(BasicBlock bb, Stmt *stmt) {
  switch (stmt->kind) {
  case SK_RET: {
    IRJmp jmp = {
        .kind = JMP_RET,
    };
    bb->jmp = jmp;
    break;
  }
  default:
    fatalf("unsupported stmt (%d)\n", stmt->kind);
  }
}

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
  typedef struct KwPair {
    const char *literal;
    TokenKind kind;
  } KwPair;

  const KwPair kw_list[] = {
#define KW_PAIR(NAME, LITERAL) {.literal = LITERAL, .kind = TK_##NAME},
      KEYWORDS(KW_PAIR)};

  u64 nkws = sizeof(kw_list) / sizeof(KwPair);
  Token tok = NULL;

  for (int i = 0; i < nkws; ++i) {
    if (startswith(keyword, kw_list[i].literal)) {
      kind = kw_list[i].kind;
      tok = make_token(kind, (startp - prog), (*p - startp), *line, start_col);
      break;
    }
  }

  if (!tok) {
    /* Restore pointers. */
    *p = startp;
    *col = start_col;
  }

  return tok;
}

static Token read_punctuator(const char *prog, const char **p, i64 *line,
                             i64 *col) {
  TokenKind kind;

  static const u64 toks_len[] = {
#define TOK_LEN(NAME, LITERAL) [TK_##NAME] = strlen(LITERAL),
      PUNCTUATORS(TOK_LEN)};

  switch ((*p)[0]) {
  case '[':
    kind = TK_LBRACKET;
    break;
  case ']':
    kind = TK_RBRACKET;
    break;
  case '(':
    kind = TK_LPAREN;
    break;
  case ')':
    kind = TK_RPAREN;
    break;
  case '{':
    kind = TK_LBRACE;
    break;
  case '}':
    kind = TK_RBRACE;
    break;
  case '~':
    kind = TK_BITNOT;
    break;
  case '?':
    kind = TK_QUESTION;
    break;
  case ';':
    kind = TK_SEMICOLON;
    break;
  case ',':
    kind = TK_COMMA;
    break;
  case '.':
    if (startswith(*p, "...")) {
      kind = TK_ELIPSIS;
    } else {
      kind = TK_DOT;
    }
    break;
  case '&':
    if (startswith(*p, "&=")) {
      kind = TK_ANDEQ;
    } else if (startswith(*p, "&&")) {
      kind = TK_AND;
    } else {
      kind = TK_AMPERSAND;
    }
    break;
  case '*':
    if (startswith(*p, "*=")) {
      kind = TK_TIMESEQ;
    } else {
      kind = TK_ASTERISK;
    }
    break;
  case '+':
    if (startswith(*p, "++")) {
      kind = TK_INCR;
    } else if (startswith(*p, "+=")) {
      kind = TK_PLUSEQ;
    } else {
      kind = TK_PLUS;
    }
    break;
  case '-':
    if (startswith(*p, "->")) {
      kind = TK_ARROW;
    } else if (startswith(*p, "--")) {
      kind = TK_DECR;
    } else if (startswith(*p, "-=")) {
      kind = TK_MINUSEQ;
    } else {
      kind = TK_MINUS;
    }
    break;
  case '!':
    if (startswith(*p, "!=")) {
      kind = TK_NE;
    } else {
      kind = TK_NOT;
    }
    break;
  case '/':
    if (startswith(*p, "/=")) {
      kind = TK_DIVIDEEQ;
    } else {
      kind = TK_DIVIDE;
    }
    break;
  case '%':
    if (startswith(*p, "%=")) {
      kind = TK_MODEQ;
    } else if (startswith(*p, "%>")) {
      kind = TK_RBRACE_ALIAS;
    } else if (startswith(*p, "%:%:")) {
      kind = TK_HASH2_ALIAS;
    } else if (startswith(*p, "%:")) {
      kind = TK_HASH_ALIAS;
    } else {
      kind = TK_MOD;
    }
    break;
  case '<':
    if (startswith(*p, "<<=")) {
      kind = TK_LSHIFTEQ;
    } else if (startswith(*p, "<<")) {
      kind = TK_LSHIFT;
    } else if (startswith(*p, "<=")) {
      kind = TK_LE;
    } else if (startswith(*p, "<:")) {
      kind = TK_LBRACKET_ALIAS;
    } else if (startswith(*p, "<%")) {
      kind = TK_LBRACE_ALIAS;
    } else {
      kind = TK_LT;
    }
    break;
  case '>':
    if (startswith(*p, ">>=")) {
      kind = TK_RSHIFTEQ;
    } else if (startswith(*p, ">>")) {
      kind = TK_RSHIFT;
    } else if (startswith(*p, ">=")) {
      kind = TK_GE;
    } else {
      kind = TK_GT;
    }
    break;
  case '=':
    if (startswith(*p, "==")) {
      kind = TK_EQ;
    } else {
      kind = TK_ASSIGN;
    }
    break;
  case '^':
    if (startswith(*p, "^=")) {
      kind = TK_XOREQ;
    } else {
      kind = TK_XOR;
    }
    break;
  case '|':
    if (startswith(*p, "|=")) {
      kind = TK_OREQ;
    } else if (startswith(*p, "||")) {
      kind = TK_OR;
    } else {
      kind = TK_BITOR;
    }
    break;
  case ':':
    if (startswith(*p, ":>")) {
      kind = TK_RBRACKET_ALIAS;
    } else {
      kind = TK_COLON;
    }
    break;
  case '#':
    if (startswith(*p, "##")) {
      kind = TK_HASH2;
    } else {
      kind = TK_HASH;
    }
    break;
  default:
    return NULL;
  }

  u64 tok_len = toks_len[kind];
  Token tok = make_token(kind, (*p - prog), tok_len, *line, *col);
  (*p) += tok_len;
  (*col) += tok_len;
  return tok;
}

static Expr *parse_expr(const char *prog, Token **tok_ref) {
  Token tok = **tok_ref;
  if (tok->kind != TK_CONSTANT)
    return NULL;

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
  ret_stmt->kind = SK_RET;
  ++(*tok_ref);
  Expr *expr = parse_expr(prog, tok_ref);
  if (expr)
    ret_stmt->inner.ret = expr;
  return ret_stmt;
}

static int flag_debug_dump_tokens = 0;
static int flag_debug_only_tokenize = 0;
static int flag_debug_dump_ast = 0;
static int flag_debug_only_parse = 0;
static int flag_debug_dump_ir = 0;
static int flag_debug_only_dump_ir = 0;

static void parse_args(int argc, char **argv) {
  int c;
  int option_index = 0;
  static struct option long_options[] = {
      {"debug-only-tokenize", no_argument, &flag_debug_only_tokenize, 1},
      {"debug-dump-tokens", no_argument, &flag_debug_dump_tokens, 1},
      {"debug-only-parse", no_argument, &flag_debug_only_parse, 1},
      {"debug-dump-ast", no_argument, &flag_debug_dump_ast, 1},
      {"debug-only-dump-ir", no_argument, &flag_debug_only_dump_ir, 1},
      {"debug-dump-ir", no_argument, &flag_debug_dump_ir, 1},
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

void debug_dump_tokens(const char *prog, Vector *tokens) {
  for (int i = 0; i < vector_len(tokens); ++i) {
    Token tok = vector_get(tokens, i);
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
  Vector *tokens = make_vector();
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
      vector_append(tokens, tok);
      continue;
    }

    /* Try to read constant (number literal) */
    if ((tok = read_constant(prog, &p, &line, &column)) != NULL) {
      vector_append(tokens, tok);
      continue;
    }

    /* Try to read punctuator */
    if ((tok = read_punctuator(prog, &p, &line, &column)) != NULL) {
      vector_append(tokens, tok);
      continue;
    }

    if (p[0])
      fatalf("failed to parse the rest of the program: '%s'\n", p);
  }

  vector_append(tokens, make_token(TK_EOF, 0, 0, -1, -1));

  if (flag_debug_dump_tokens)
    debug_dump_tokens(prog, tokens);

  if (flag_debug_only_tokenize)
    exit(0);

  /* Parser... */
  Token *tok = (Token *)vector_data(tokens);
  Stmt *stmt;
  while ((*tok)->kind != TK_EOF) {
    stmt = parse_stmt(prog, &tok);
  }

  /* Generate IR ... */
  BasicBlock bb = find_or_make_bb("start");
  gen_ir_for_stmt(bb, stmt);
  if (flag_debug_dump_ir) {
    BasicBlock curbb = NULL;
    for (curbb = bb; curbb; curbb = bb->cfg_next_bb) {
      printf("%s:\n", curbb->name);

      switch (curbb->jmp.kind) {
      case JMP_RET:
        printf("  ret\n");
        break;
      default:
        printf("  unknown jmp kind (%d)\n", curbb->jmp.kind);
        break;
      }
    }
  }

  if (flag_debug_only_dump_ir)
    exit(0);

  /* CodeGen ... */
  BasicBlock curbb = NULL;
  for (curbb = bb; curbb; curbb = bb->cfg_next_bb) {
    /* TODO: Add support for emitting instructions. */
    switch (curbb->jmp.kind) {
    case JMP_RET:
      printf("\tret\n");
      break;
    default:
      fatalf("unrecognized jmp type: %d\n", curbb->jmp.kind);
    }
  }

  return 0;
}
