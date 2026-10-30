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
#include "type.h"

typedef struct AstNode AstNode;

/* Struct and enum tags share a namespace and follow block scope. */
typedef struct Tag {
  Token name;
  Type *type;
  int scope_depth;
  bool is_defining;
  struct Tag *next;
} Tag;

typedef struct Variable {
  Token name;
  Type *type;
  int offset; /* Positive distance below %rbp for stack variables. */
  bool is_global;
  bool is_parameter;
  bool has_initializer;
  uint64_t init_value; /* Global integer initializer bits, at the object's width. */
  struct Variable *next; /* Parameter list, in reverse declaration order. */
} Variable;

typedef struct Enum {
  Token name;
  int value;
} Enum;

/*
 * Labels have function scope (C11 6.2.1), so this table is not restored
 * when a block ends, unlike ordinary identifier bindings.
 */
typedef struct Label {
  Token name;
  struct Label *next;
} Label;

/*
 * A 'goto' whose target is validated after the function body is parsed,
 * since labels may be declared after the goto that uses them.
 */
typedef struct GotoRef {
  Token name; /* Target label. */
  Token loc; /* The 'goto' keyword, for diagnostics. */
  struct GotoRef *next;
} GotoRef;

/*
 * Function declarations refer to a shared descriptor across redeclarations.
 * In C11, f() doesn't supply a prototype (6.7.6.3).
 */
typedef struct Function {
  Token name;
  Type *return_type;
  int nparams;
  Type **param_types;
  int has_prototype; /* 0 for a declaration with an empty argument list. */
  int is_defined; /* 1 once a definition has been seen. */
} Function;

typedef enum {
  SYMBOL_VARIABLE,
  SYMBOL_FUNCTION,
  SYMBOL_TYPEDEF,
  SYMBOL_ENUMERATOR,
} SymbolKind;

/*
 * A symbol can represent a variable, function, typedef name, or enumerator.
 */
typedef struct Symbol {
  Token name;
  SymbolKind kind;
  int scope_depth;
  union {
    Variable *variable;
    Function *function;
    Type *typedef_type;
    Enum *enumerator;
  } d;
  struct Symbol *next;
} Symbol;

/*
 * A function call whose argument count is checked against a visible
 * prototype or later compatible definition.
 */
typedef struct CallRef {
  Token name; /* Called function. */
  Token loc; /* The callee identifier, for diagnostics. */
  int nargs;
  AstNode *call;
  struct CallRef *next;
} CallRef;

/*
 * The case labels of one switch, attached to its AST_SWITCH node for
 * codegen's dispatch. Order in the list is not significant.
 */
typedef struct CaseList {
  struct AstNode *node; /* AST_CASE */
  struct CaseList *next;
} CaseList;

typedef struct {
  Lexer lexer;
  Token previous; /* Last consumed token, for missing-token diagnostics. */
  Token current;
  Token lookahead;

  Symbol *symbols;
  Tag *tags;
  Label *labels;
  GotoRef *goto_refs;
  Function *current_function;
  CallRef *call_refs;
  /*
   * The innermost switch being parsed, or NULL; 'case' and 'default' are
   * valid only inside its body, and 'break' may target it.
   */
  AstNode *current_switch;
  int scope_depth;
  int loop_depth;
  int stack_size;
} Parser;

typedef enum {
  AST_ADD,
  AST_ADDR,
  AST_ASSIGN,
  AST_BIT_AND,
  AST_BIT_OR,
  AST_BIT_XOR,
  AST_BITWISE_NOT,
  AST_BLOCK,
  AST_BREAK,
  AST_CALL,
  AST_CASE,
  AST_COMMA,
  AST_COMPOUND_ASSIGN,
  AST_CONDITIONAL,
  AST_CONTINUE,
  AST_CONVERT,
  AST_DECL,
  AST_DEREF,
  AST_DIV,
  AST_DO,
  AST_EQ,
  AST_EXPR_STMT,
  AST_FOR,
  AST_FUNCTION,
  AST_GE,
  AST_GOTO,
  AST_GT,
  AST_IF,
  AST_INT_LITERAL,
  AST_LABEL,
  AST_LE,
  AST_LOGICAL_AND,
  AST_LOGICAL_NOT,
  AST_LOGICAL_OR,
  AST_LSHIFT,
  AST_LT,
  AST_MEMBER,
  AST_MOD,
  AST_MUL,
  AST_NE,
  AST_NEG,
  AST_POST_DECREMENT,
  AST_PTR_ADD,
  AST_PTR_DIFF,
  AST_PTR_SUB,
  AST_POST_INCREMENT,
  AST_PRE_DECREMENT,
  AST_PRE_INCREMENT,
  AST_RETURN,
  AST_RSHIFT,
  AST_STRING_LITERAL,
  AST_SUB,
  AST_SWITCH,
  AST_VAR,
  AST_WHILE,
} AstKind;

typedef struct AstNode {
  AstKind kind;
  Type *type; /* Inferred expression type. */
  Token loc;
  struct AstNode *next; /* Statements or external declarations. */
  union {
    struct {
      Token name;
      struct AstNode *body;
      /*
       * Parameters in reverse declaration order. This list is retained for
       * codegen independently of their scope-bound symbol entries.
       */
      struct Variable *params;
      int stack_size;
    } function;

    struct {
      struct AstNode *first_stmt;
    } block;

    struct {
      struct AstNode *cond;
      struct AstNode *then_stmt;
      struct AstNode *else_stmt; /* NULL if there is no 'else' clause. */
    } if_stmt;

    struct {
      struct AstNode *cond;
      struct AstNode *body;
    } while_stmt;

    struct {
      struct AstNode *init; /* Decl or expression-statement, NULL if omitted. */
      struct AstNode *cond; /* NULL if omitted (a nonzero constant, 6.8.5.3p2). */
      struct AstNode *post; /* Expression, NULL if omitted. */
      struct AstNode *body;
    } for_stmt;

    struct {
      struct AstNode *body;
      struct AstNode *cond;
    } do_stmt;

    struct {
      Token name;
    } goto_stmt;

    struct {
      Token name;
      struct AstNode *stmt;
    } label_stmt;

    struct {
      struct AstNode *cond;
      struct AstNode *body;
      /*
       * The case and default nodes still sit in the body's statement
       * chain (their position matters for fallthrough); this list is for
       * codegen's dispatch. The default case, if any, is the entry whose
       * node has is_default set.
       */
      struct CaseList *cases;
    } switch_stmt;

    struct {
      int64_t val; /* Meaningless when is_default is set. */
      struct AstNode *stmt;
      int label; /* Dispatch label number, assigned by codegen. */
      int is_default; /* 1 for 'default:', 0 for 'case'. */
    } case_stmt;

    struct {
      Function *function;
      struct AstNode *args; /* First argument expression, or NULL. */
    } call;

    struct {
      struct AstNode *cond;
      struct AstNode *then_expr;
      struct AstNode *else_expr;
    } conditional;

    struct {
      struct AstNode *expr;
    } ret;

    struct {
      Variable *var;
      struct AstNode *init;
    } decl;

    struct {
      struct AstNode *expr;
    } expr_stmt;

    struct {
      Variable *var;
    } variable;

    struct {
      uint64_t val;
    } int_literal;

    struct {
      unsigned char *val; /* Decoded bytes; type->array_len includes final zero. */
    } string_literal;

    struct {
      struct AstNode *operand;
    } unary;

    struct {
      struct AstNode *base; /* Struct expression; arrow uses AST_DEREF. */
      Member *member;
    } member;

    struct {
      struct AstNode *lhs;
      struct AstNode *rhs;
    } binary;

    struct {
      struct AstNode *lhs;
      struct AstNode *rhs; /* Converted right operand. */
      AstKind op; /* Typed integer operation or AST_PTR_ADD/AST_PTR_SUB. */
      Type *operation_type; /* Type before conversion back to lhs->type. */
    } compound_assign;
  } d;
} AstNode;

/* Shared diagnostics; error_at does not alter parser token state. */
extern void error_at(Parser *p, Token loc, const char *fmt, ...)
    __attribute__((format(printf, 3, 4), noreturn));
extern void parse_error(Parser *p, const char *fmt, ...)
    __attribute__((format(printf, 2, 3), noreturn));

extern AstNode *parse_translation_unit(const char *filename, const char *source);

#endif
