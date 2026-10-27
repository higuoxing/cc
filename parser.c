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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "type.h"

static LocalVar *declare_local_var(Parser *p, Token name, Type *type);
static AstNode *new_ast_node(Parser *p, AstKind kind);
static AstNode *new_expr(Parser *p, AstKind kind, Type *type);
static void reset_function_state(Parser *p);
static void parser_advance(Parser *p);
static AstNode *parse_assignment(Parser *p);
static AstNode *parse_expression(Parser *p);
static AstNode *parse_conditional(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_call_expr(Parser *p);
static AstNode *parse_postfix(Parser *p);
static AstNode *parse_primary(Parser *p);
static AstNode *parse_decl(Parser *p);
static AstNode *parse_return_stmt(Parser *p);
static AstNode *parse_if_stmt(Parser *p);
static AstNode *parse_while_stmt(Parser *p);
static AstNode *parse_for_stmt(Parser *p);
static AstNode *parse_do_stmt(Parser *p);
static AstNode *parse_break_stmt(Parser *p);
static AstNode *parse_continue_stmt(Parser *p);
static AstNode *parse_goto_stmt(Parser *p);
static AstNode *parse_label_stmt(Parser *p);
static AstNode *parse_switch_stmt(Parser *p);
static AstNode *parse_case_stmt(Parser *p);
static AstNode *parse_default_stmt(Parser *p);
static AstNode *parse_stmt(Parser *p, bool allow_decl);
static AstNode *parse_block(Parser *p);
static AstNode *parse_external_declaration(Parser *p);

/* assignment-operator: = += -= *= /= %= <<= >>= &= ^= |=  (C11 6.5.16) */
static bool is_assignment_op(TokenKind ty) {
  return ty == TOKEN_ASSIGN || ty == TOKEN_PLUS_EQ || ty == TOKEN_MINUS_EQ ||
         ty == TOKEN_STAR_EQ || ty == TOKEN_SLASH_EQ ||
         ty == TOKEN_PERCENT_EQ || ty == TOKEN_LSHIFT_EQ ||
         ty == TOKEN_RSHIFT_EQ || ty == TOKEN_BIT_AND_EQ ||
         ty == TOKEN_BIT_XOR_EQ || ty == TOKEN_BIT_OR_EQ;
}

static LocalVar *find_local_var(Parser *p, Token name) {
  LocalVar *var;
  for (var = p->locals; var; var = var->next) {
    if (same_identifier(var->name, name))
      return var;
  }
  return NULL;
}

static LocalVar *declare_local_var(Parser *p, Token name, Type *type) {
  LocalVar *var;
  int size;

  size = type_size(type);
  for (var = p->locals; var && var->scope_depth == p->scope_depth;
       var = var->next) {
    if (same_identifier(var->name, name))
      parse_error(p, "duplicate local variable");
  }

  /*
   * The function body's outermost block (depth 1) may not redeclare a
   * parameter (depth 0); deeper blocks may shadow freely (C11 6.2.1).
   */
  if (p->scope_depth == 1)
    for (var = p->locals; var && var->scope_depth == 0; var = var->next)
      if (same_identifier(var->name, name))
        parse_error(p, "declaration of '%.*s' shadows a parameter", name.len,
                    name.start);

  var = (LocalVar *)calloc(1, sizeof(LocalVar));
  if (!var)
    parse_error(p, "out of memory");
  var->name = name;
  var->type = type;
  var->scope_depth = p->scope_depth;
  p->stack_size = (p->stack_size + size - 1) / size * size + size;
  var->offset = p->stack_size;
  var->next = p->locals;
  p->locals = var;
  return var;
}

static AstNode *new_ast_node(Parser *p, AstKind kind) {
  AstNode *node;
  node = calloc(1, sizeof(AstNode));
  if (!node)
    parse_error(p, "out of memory");
  node->kind = kind;
  node->loc = p->current;
  node->type = &builtin_type_void;
  return node;
}

static AstNode *new_expr(Parser *p, AstKind kind, Type *type) {
  AstNode *node = new_ast_node(p, kind);
  node->type = type;
  return node;
}

static void parser_advance(Parser *p) {
  p->current = p->lookahead;
  p->lookahead = lexer_next_token(&p->lexer);
}

static void reset_function_state(Parser *p) {
  p->locals = NULL;
  p->labels = NULL;
  p->goto_refs = NULL;
  p->current_switch = NULL;
  p->current_function = NULL;
  p->scope_depth = 0;
  p->loop_depth = 0;
  p->stack_size = 0;
}

static void parser_init(Parser *p, const char *filename, const char *source) {
  p->locals = NULL;
  p->labels = NULL;
  p->goto_refs = NULL;
  p->functions = NULL;
  p->call_refs = NULL;
  p->current_switch = NULL;
  p->current_function = NULL;
  p->scope_depth = 0;
  p->loop_depth = 0;
  p->stack_size = 0;
  lexer_init(&p->lexer, filename, source);
  p->current = lexer_next_token(&p->lexer);
  p->lookahead = lexer_next_token(&p->lexer);
}

static void report_error(Parser *p, Token loc, const char *fmt, va_list ap) {
  fprintf(stderr, "%s:%d:%d: error: ", p->lexer.filename, loc.line, loc.col);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void error_at(Parser *p, Token loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  report_error(p, loc, fmt, ap);
  va_end(ap);
  exit(1);
}

void parse_error(Parser *p, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  report_error(p, p->current, fmt, ap);
  va_end(ap);
  exit(1);
}

static void parse_expect(Parser *p, TokenKind t, const char *msg) {
  if (p->current.kind != t) {
    parse_error(p, "%s", msg);
  }

  parser_advance(p);
}

/*
 * unary-expression:
 *   '-' unary-expression | '~' unary-expression | '!' unary-expression
 *   '++' unary-expression | '--' unary-expression
 *   '&' unary-expression | '*' unary-expression
 *   postfix-expression
 */
static AstNode *parse_unary(Parser *p) {
  if (p->current.kind == TOKEN_MINUS || p->current.kind == TOKEN_TILDE ||
      p->current.kind == TOKEN_BANG || p->current.kind == TOKEN_PLUS_PLUS ||
      p->current.kind == TOKEN_MINUS_MINUS ||
      p->current.kind == TOKEN_BIT_AND || p->current.kind == TOKEN_STAR) {
    AstNode *operand;
    AstKind op;
    Token op_tok;

    switch (p->current.kind) {
    case TOKEN_MINUS:
      op = AST_NEG;
      break;
    case TOKEN_TILDE:
      op = AST_BITWISE_NOT;
      break;
    case TOKEN_BANG:
      op = AST_LOGICAL_NOT;
      break;
    case TOKEN_PLUS_PLUS:
      op = AST_PRE_INCREMENT;
      break;
    case TOKEN_MINUS_MINUS:
      op = AST_PRE_DECREMENT;
      break;
    case TOKEN_BIT_AND:
      op = AST_ADDR;
      break;
    case TOKEN_STAR:
      op = AST_DEREF;
      break;
    default:
      fprintf(stderr, "unreachable %s:%d", __FILE__, __LINE__);
      exit(1);
    }

    /* Consume '-' or '~' or '!' or '++' or '--' or '&' or '*' */
    op_tok = p->current;
    parser_advance(p);
    operand = parse_unary(p);

    return sema_unary(p, op, operand, op_tok);
  }

  return parse_postfix(p);
}

/*
 * postfix-expression:
 *   identifier '(' argument-expression-list_opt ')'
 */
static AstNode *parse_call_expr(Parser *p) {
  AstNode *call;
  CallRef *ref;
  Function *func;
  int nargs = 0;

  ref = (CallRef *)calloc(1, sizeof(CallRef));
  if (!ref)
    parse_error(p, "out of memory");
  ref->name = p->current;
  ref->loc = p->current;
  ref->next = p->call_refs;
  p->call_refs = ref;

  func = find_function(p, ref->name);
  if (func == NULL)
    parse_error(p, "function '%.*s' not declared", ref->name.len,
                ref->name.start);

  call = new_expr(p, AST_CALL, func->return_type);
  call->d.call.function = func;
  ref->call = call;
  /* Consume the function name. */
  parser_advance(p);
  parse_expect(p, TOKEN_LPAREN, "expect '('");

  if (p->current.kind != TOKEN_RPAREN) {
    AstNode **tail = &call->d.call.args;

    for (;;) {
      *tail = parse_assignment(p);
      tail = &(*tail)->next;
      nargs++;
      if (p->current.kind == TOKEN_COMMA) {
        parser_advance(p);
        continue;
      }
      break;
    }
  }
  parse_expect(p, TOKEN_RPAREN, "expect ')'");

  ref->nargs = nargs;
  sema_check_call(p, func, call, nargs);
  return call;
}

/*
 * postfix-expression:
 *   primary-expression
 *   identifier '(' ')'
 *   postfix-expression '++' | postfix-expression '--'
 */
static AstNode *parse_postfix(Parser *p) {
  AstNode *left;

  if (p->current.kind == TOKEN_IDENTIFIER &&
      p->lookahead.kind == TOKEN_LPAREN) {
    if (find_local_var(p, p->current))
      parse_error(p, "called object is not a function");
    left = parse_call_expr(p);
  } else {
    left = parse_primary(p);
  }

  while (p->current.kind == TOKEN_PLUS_PLUS ||
         p->current.kind == TOKEN_MINUS_MINUS) {
    Token op = p->current;
    AstKind kind =
        op.kind == TOKEN_PLUS_PLUS ? AST_POST_INCREMENT : AST_POST_DECREMENT;
    parser_advance(p);
    left = sema_unary(p, kind, left, op);
  }

  return left;
}

/*
 * primary-expression:
 *   integer-constant | character-constant | '(' expression ')' | identifier
 */
static AstNode *parse_primary(Parser *p) {
  if (p->current.kind == TOKEN_INT_LITERAL) {
    AstNode *expr;
    expr = new_expr(p, AST_INT_LITERAL, &builtin_type_int);
    expr->d.int_literal.val = p->current.int_literal_val;

    /* Consume the integer or character constant. */
    parser_advance(p);
    return expr;
  }

  if (p->current.kind == TOKEN_LPAREN) {
    AstNode *expr;
    /* Consume '(' */
    parser_advance(p);
    expr = parse_expression(p);
    parse_expect(p, TOKEN_RPAREN, "expect ')'");
    return expr;
  }

  if (p->current.kind == TOKEN_IDENTIFIER) {
    AstNode *expr;
    LocalVar *var = find_local_var(p, p->current);
    if (!var)
      parse_error(p, "undeclared identifier");
    expr = new_expr(p, AST_VAR, var->type);
    expr->d.variable.var = var;
    /* Consume identifier. */
    parser_advance(p);
    return expr;
  }

  parse_error(p, "expect expr");
  return NULL;
}

/*
 * Consume declaration specifiers and return the base type only.
 * Unsigned may precede or follow int or char.
 */
static Type *parse_declspecs(Parser *p) {
  Type *type = NULL;
  bool seen_unsigned = false;
  if (p->current.kind == TOKEN_VOID) {
    parser_advance(p);
    return &builtin_type_void;
  }
  if (p->current.kind != TOKEN_INT && p->current.kind != TOKEN_CHAR &&
      p->current.kind != TOKEN_UNSIGNED)
    parse_error(p, "expect 'int'");
  while (p->current.kind == TOKEN_INT || p->current.kind == TOKEN_CHAR ||
         p->current.kind == TOKEN_UNSIGNED) {
    if (p->current.kind == TOKEN_UNSIGNED) {
      if (seen_unsigned)
        parse_error(p, "duplicate unsigned type specifier");
      seen_unsigned = true;
    } else {
      Type *specifier =
          p->current.kind == TOKEN_INT ? &builtin_type_int : &builtin_type_char;
      if (type == specifier)
        parse_error(p, "duplicate %s type specifier",
                    type->kind == TYPE_INT ? "int" : "char");
      if (type)
        parse_error(p, "invalid combination of type specifiers");
      type = specifier;
    }
    parser_advance(p);
  }
  if (!type)
    type = &builtin_type_int;
  if (seen_unsigned)
    return type->kind == TYPE_CHAR ? &builtin_type_unsigned_char
                                   : &builtin_type_unsigned;
  return type;
}

/*
 * Consume pointer stars separately from declaration specifiers until a full
 * declarator parser handles pointers, arrays, and function declarators.
 */
static Type *parse_pointer_type(Parser *p, Type *type) {
  while (p->current.kind == TOKEN_STAR) {
    type = pointer_to(type);
    if (!type)
      parse_error(p, "out of memory");
    parser_advance(p);
  }
  return type;
}

/*
 * declaration:
 *   declaration-type identifier ['=' assignment-expression] ';'
 * Restricted to a single declarator.
 */
static AstNode *parse_decl(Parser *p) {
  AstNode *decl;
  Token name;
  Type *type;

  type = parse_declspecs(p);
  type = parse_pointer_type(p, type);
  if (type->kind == TYPE_VOID)
    parse_error(p, "variable has void type");

  if (p->current.kind != TOKEN_IDENTIFIER)
    parse_error(p, "expect variable name");

  name = p->current;
  decl = new_ast_node(p, AST_DECL);
  decl->d.decl.var = declare_local_var(p, name, type);
  /* Consume variable name. */
  parser_advance(p);

  if (p->current.kind == TOKEN_ASSIGN) {
    /* Consume '=' */
    parser_advance(p);
    decl->d.decl.init = parse_assignment(p);
    decl->d.decl.init =
        sema_convert_expr(p, type, decl->d.decl.init, name, "initializer");
  }

  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");

  return decl;
}

/*
 * multiplicative-expression:
 *   unary-expression
 *   multiplicative-expression '*' unary-expression
 *   multiplicative-expression '/' unary-expression
 *   multiplicative-expression '%' unary-expression
 * (C11 6.5.5)
 */
static AstNode *parse_multiplicative(Parser *p) {
  AstNode *left;

  left = parse_unary(p);

  while (p->current.kind == TOKEN_STAR || p->current.kind == TOKEN_SLASH ||
         p->current.kind == TOKEN_PERCENT) {
    TokenKind op;
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    op = p->current.kind;
    /* Consume '*' or '/' or '%' */
    parser_advance(p);

    right = parse_unary(p);
    node = sema_binary(
        p, op == TOKEN_STAR ? AST_MUL : (op == TOKEN_SLASH ? AST_DIV : AST_MOD),
        left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * additive-expression:
 *   multiplicative-expression
 *   additive-expression '+' multiplicative-expression
 *   additive-expression '-' multiplicative-expression
 * (C11 6.5.6)
 */
static AstNode *parse_additive(Parser *p) {
  AstNode *left;

  left = parse_multiplicative(p);

  while (p->current.kind == TOKEN_PLUS || p->current.kind == TOKEN_MINUS) {
    TokenKind op;
    AstNode *right;
    AstNode *node;
    Token op_tok;

    op = p->current.kind;
    op_tok = p->current;
    /* Consume '+' or '-' */
    parser_advance(p);
    right = parse_multiplicative(p);

    node = sema_binary(p, op == TOKEN_PLUS ? AST_ADD : AST_SUB, left, right,
                       op_tok);

    left = node;
  }

  return left;
}

/*
 * shift-expression:
 *   additive-expression
 *   shift-expression '<<' additive-expression
 *   shift-expression '>>' additive-expression
 * (C11 6.5.7)
 */
static AstNode *parse_shift(Parser *p) {
  AstNode *left;

  left = parse_additive(p);

  while (p->current.kind == TOKEN_LSHIFT || p->current.kind == TOKEN_RSHIFT) {
    TokenKind op;
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    op = p->current.kind;
    /* Consume '<<' or '>>' */
    parser_advance(p);

    right = parse_additive(p);
    node = sema_binary(p, op == TOKEN_LSHIFT ? AST_LSHIFT : AST_RSHIFT, left,
                       right, op_tok);

    left = node;
  }

  return left;
}

/*
 * relational-expression:
 *   shift-expression
 *   relational-expression '<' shift-expression
 *   relational-expression '>' shift-expression
 *   relational-expression '<=' shift-expression
 *   relational-expression '>=' shift-expression
 * (C11 6.5.8)
 */
static AstNode *parse_relational(Parser *p) {
  AstNode *left;

  left = parse_shift(p);

  while (p->current.kind == TOKEN_LESS || p->current.kind == TOKEN_GREATER ||
         p->current.kind == TOKEN_LESS_EQ ||
         p->current.kind == TOKEN_GREATER_EQ) {
    AstKind ty;
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    switch (p->current.kind) {
    case TOKEN_LESS:
      ty = AST_LT;
      break;
    case TOKEN_GREATER:
      ty = AST_GT;
      break;
    case TOKEN_LESS_EQ:
      ty = AST_LE;
      break;
    case TOKEN_GREATER_EQ:
      ty = AST_GE;
      break;
    default:
      fprintf(stderr, "unreachable %s:%d", __FILE__, __LINE__);
      exit(1);
    }

    /* Consume '<' or '>' or '<=' or '>=' */
    parser_advance(p);

    right = parse_shift(p);
    node = sema_binary(p, ty, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * equality-expression:
 *   relational-expression
 *   equality-expression '==' relational-expression
 *   equality-expression '!=' relational-expression
 * (C11 6.5.9)
 */
static AstNode *parse_equality(Parser *p) {
  AstNode *left;

  left = parse_relational(p);

  while (p->current.kind == TOKEN_EQ_EQ || p->current.kind == TOKEN_NOT_EQ) {
    TokenKind op;
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    op = p->current.kind;
    /* Consume '==' or '!=' */
    parser_advance(p);

    right = parse_relational(p);
    node = sema_binary(p, op == TOKEN_EQ_EQ ? AST_EQ : AST_NE, left, right,
                       op_tok);

    left = node;
  }

  return left;
}

/*
 * AND-expression:
 *   equality-expression
 *   AND-expression '&' equality-expression
 * (C11 6.5.10)
 */
static AstNode *parse_bitwise_and(Parser *p) {
  AstNode *left;

  left = parse_equality(p);

  while (p->current.kind == TOKEN_BIT_AND) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '&' */
    parser_advance(p);

    right = parse_equality(p);
    node = sema_binary(p, AST_BIT_AND, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * exclusive-OR-expression:
 *   AND-expression
 *   exclusive-OR-expression '^' AND-expression
 * (C11 6.5.11)
 */
static AstNode *parse_bitwise_xor(Parser *p) {
  AstNode *left;

  left = parse_bitwise_and(p);

  while (p->current.kind == TOKEN_BIT_XOR) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '^' */
    parser_advance(p);

    right = parse_bitwise_and(p);
    node = sema_binary(p, AST_BIT_XOR, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * inclusive-OR-expression:
 *   exclusive-OR-expression
 *   inclusive-OR-expression '|' exclusive-OR-expression
 * (C11 6.5.12)
 */
static AstNode *parse_bitwise_or(Parser *p) {
  AstNode *left;

  left = parse_bitwise_xor(p);

  while (p->current.kind == TOKEN_BIT_OR) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '|' */
    parser_advance(p);

    right = parse_bitwise_xor(p);
    node = sema_binary(p, AST_BIT_OR, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * logical-AND-expression:
 *   inclusive-OR-expression
 *   logical-AND-expression '&&' inclusive-OR-expression
 * (C11 6.5.13)
 */
static AstNode *parse_logical_and(Parser *p) {
  AstNode *left;

  left = parse_bitwise_or(p);

  while (p->current.kind == TOKEN_AND_AND) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '&&' */
    parser_advance(p);

    right = parse_bitwise_or(p);
    node = sema_binary(p, AST_LOGICAL_AND, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * logical-OR-expression:
 *   logical-AND-expression
 *   logical-OR-expression '||' logical-AND-expression
 * (C11 6.5.14)
 */
static AstNode *parse_logical_or(Parser *p) {
  AstNode *left;

  left = parse_logical_and(p);

  while (p->current.kind == TOKEN_OR_OR) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '||' */
    parser_advance(p);

    right = parse_logical_and(p);
    node = sema_binary(p, AST_LOGICAL_OR, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * expression:
 *   assignment-expression
 *   expression ',' assignment-expression
 * (C11 6.5.17; the left operand is evaluated and its value discarded,
 *  its side effects kept, before the right operand is evaluated)
 */
static AstNode *parse_expression(Parser *p) {
  AstNode *left;

  left = parse_assignment(p);
  while (p->current.kind == TOKEN_COMMA) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume ',' */
    parser_advance(p);
    right = parse_assignment(p);
    node = sema_binary(p, AST_COMMA, left, right, op_tok);
    left = node;
  }
  return left;
}

/*
 * conditional-expression:
 *   logical-OR-expression
 *   logical-OR-expression '?' expression ':' conditional-expression
 * (C11 6.5.15; the middle operand is a full expression, the third a
 *  conditional-expression, which makes the operator right-associative)
 */
static AstNode *parse_conditional(Parser *p) {
  AstNode *node;

  node = parse_logical_or(p);
  if (p->current.kind == TOKEN_QUESTION) {
    Token op_tok = p->current;
    AstNode *cond = node;
    AstNode *then_expr;
    AstNode *else_expr;

    /* Consume '?' */
    parser_advance(p);
    then_expr = parse_expression(p);
    parse_expect(p, TOKEN_COLON, "expect ':'");
    else_expr = parse_conditional(p);

    node = sema_conditional(p, cond, then_expr, else_expr, op_tok);
  }
  return node;
}

/*
 * assignment-expression:
 *   conditional-expression
 *   conditional-expression assignment-operator assignment-expression
 * (C11 6.5.16; the left operand must be an lvalue. assignment-operator:
 *  = += -= *= /= %= <<= >>= &= ^= |=)
 */
static AstNode *parse_assignment(Parser *p) {
  AstNode *left = parse_conditional(p);
  if (is_assignment_op(p->current.kind)) {
    Token op = p->current;
    AstNode *rhs;
    parser_advance(p);
    rhs = parse_assignment(p);
    return sema_assignment(p, op, left, rhs);
  }
  return left;
}

/*
 * jump-statement:
 *   'return' expression_opt ';'
 * The function's return type determines whether a value is required.
 */
static AstNode *parse_return_stmt(Parser *p) {
  AstNode *ast;
  Token loc = p->current;
  parse_expect(p, TOKEN_RETURN, "expect 'return'");
  if (p->current.kind == TOKEN_SEMICOLON &&
      p->current_function->return_type->kind != TYPE_VOID)
    parse_error(p, "expect return expr");

  ast = new_ast_node(p, AST_RETURN);
  ast->loc = loc;
  if (p->current.kind != TOKEN_SEMICOLON)
    ast->d.ret.expr = parse_expression(p);
  sema_check_return(p, ast);
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");

  return ast;
}

/*
 * compound-statement:
 *   '{' statement* '}'
 * (C11 6.8.2)
 */
static AstNode *parse_block(Parser *p) {
  AstNode *block;
  AstNode **tail;
  LocalVar *outer_locals = p->locals;

  block = new_ast_node(p, AST_BLOCK);
  tail = &block->d.block.first_stmt;

  parse_expect(p, TOKEN_LBRACE, "expect '{'");
  ++p->scope_depth;
  while (p->current.kind != TOKEN_RBRACE) {
    if (p->current.kind == TOKEN_EOF)
      parse_error(p, "expect '}'");

    *tail = parse_stmt(p, /*allow_decl=*/true);
    tail = &(*tail)->next;
  }

  parse_expect(p, TOKEN_RBRACE, "expect '}'");
  p->locals = outer_locals;
  --p->scope_depth;
  return block;
}

/*
 * external-declaration:
 *   function-definition
 *   declaration
 */
static AstNode *parse_external_declaration(Parser *p) {
  AstNode *fn;
  Function *func;
  LocalVar *var;
  Token func_name;
  Type *return_type;
  Type **param_types = NULL;
  int param_capacity = 0;
  int nparams = 0;
  int definition = 0;
  int explicit_params = 0;
  int unamed_param = 0;

  reset_function_state(p);
  fn = new_ast_node(p, AST_FUNCTION);
  return_type = parse_declspecs(p);
  return_type = parse_pointer_type(p, return_type);
  if (p->current.kind != TOKEN_IDENTIFIER)
    parse_error(p, "expect function name");

  fn->d.function.name = p->current;
  func_name = p->current;
  /* Consume <function_name> */
  parser_advance(p);

  parse_expect(p, TOKEN_LPAREN, "expect '('");

  if (p->current.kind == TOKEN_VOID && p->lookahead.kind == TOKEN_RPAREN) {
    explicit_params = 1;
    parser_advance(p);
  } else if (p->current.kind != TOKEN_RPAREN) {
    explicit_params = 1;
    for (;;) {
      LocalVar *param;
      Token param_name;
      Type *param_type;

      if (nparams == param_capacity) {
        Type **types;
        param_capacity = param_capacity ? param_capacity * 2 : 8;
        types = realloc(param_types, param_capacity * sizeof(Type *));
        if (!types)
          parse_error(p, "out of memory");
        param_types = types;
      }
      param_type = parse_declspecs(p);
      param_type = parse_pointer_type(p, param_type);
      if (param_type->kind == TYPE_VOID)
        parse_error(p, "parameter has void type");
      param_types[nparams] = param_type;

      if (p->current.kind == TOKEN_IDENTIFIER) {
        param_name = p->current;
        for (var = p->locals; var && var->scope_depth == 0; var = var->next) {
          if (same_identifier(var->name, param_name))
            parse_error(p, "duplicate local variable");
        }

        /* Consume param name */
        parser_advance(p);

        param = (LocalVar *)calloc(1, sizeof(LocalVar));
        if (!param)
          parse_error(p, "out of memory");
        param->name = param_name;
        param->type = param_type;
        param->scope_depth = 0;
        param->offset = 8 * (nparams + 1);
        param->next = p->locals;
        p->locals = param;
      } else {
        if (p->current.kind != TOKEN_COMMA && p->current.kind != TOKEN_RPAREN)
          parse_error(p, "expect parameter name");
        unamed_param = 1;
      }

      nparams++;

      if (p->current.kind == TOKEN_COMMA) {
        parser_advance(p);
        continue;
      }
      break;
    }
  }
  parse_expect(p, TOKEN_RPAREN, "expect ')'");

  if (p->current.kind != TOKEN_SEMICOLON && p->current.kind != TOKEN_LBRACE)
    parse_error(p, "expect ';' or '{'");

  definition = p->current.kind == TOKEN_LBRACE;
  if (definition && unamed_param)
    parse_error(p, "expect parameter name in function definition");

  func = declare_function(p, func_name, return_type, param_types, nparams,
                          explicit_params || definition, definition);
  free(param_types);

  if (!definition) {
    /* Consume ';' */
    parser_advance(p);
    return NULL;
  }

  p->current_function = func;
  fn->d.function.params = p->locals;
  p->stack_size = 8 * nparams;
  fn->d.function.body = parse_block(p);
  check_gotos(p);
  fn->d.function.stack_size = p->stack_size;
  return fn;
}

/*
 * selection-statement:
 *   'if' '(' expression ')' statement
 *   'if' '(' expression ')' statement 'else' statement
 * (C11 6.8.4.1; the recursive call to parse_stmt for the branches
 *  gives 'else' the C association with the nearest unmatched 'if')
 */
static AstNode *parse_if_stmt(Parser *p) {
  AstNode *if_node;

  parse_expect(p, TOKEN_IF, "expect 'if'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");
  if_node = new_ast_node(p, AST_IF);
  if_node->d.if_stmt.cond = parse_expression(p);
  sema_check_nonvoid_expr(p, if_node->d.if_stmt.cond);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  if_node->d.if_stmt.then_stmt = parse_stmt(p, /*allow_decl=*/false);
  if (p->current.kind == TOKEN_ELSE) {
    /* Consume 'else' */
    parser_advance(p);
    if_node->d.if_stmt.else_stmt = parse_stmt(p, /*allow_decl=*/false);
  }
  return if_node;
}

/*
 * iteration-statement:
 *   'while' '(' expression ')' statement
 * (C11 6.8.5.1)
 */
static AstNode *parse_while_stmt(Parser *p) {
  AstNode *while_node;

  parse_expect(p, TOKEN_WHILE, "expect 'while'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");
  while_node = new_ast_node(p, AST_WHILE);
  while_node->d.while_stmt.cond = parse_expression(p);
  sema_check_nonvoid_expr(p, while_node->d.while_stmt.cond);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  ++p->loop_depth;
  while_node->d.while_stmt.body = parse_stmt(p, /*allow_decl=*/false);
  --p->loop_depth;
  return while_node;
}

/*
 * iteration-statement:
 *   'for' '(' expression_opt ';' expression_opt ';' expression_opt ')'
 *       statement
 *   'for' '(' declaration expression_opt ';' expression_opt ')' statement
 * (C11 6.8.5.3; a declaration in the init clause is scoped to the loop,
 *  and an omitted condition means a nonzero constant)
 */
static AstNode *parse_for_stmt(Parser *p) {
  AstNode *for_node;
  LocalVar *outer_locals;

  parse_expect(p, TOKEN_FOR, "expect 'for'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");

  for_node = new_ast_node(p, AST_FOR);

  /* A declaration in the init clause is scoped to the whole loop. */
  outer_locals = p->locals;
  ++p->scope_depth;

  if (p->current.kind == TOKEN_INT || p->current.kind == TOKEN_CHAR ||
      p->current.kind == TOKEN_UNSIGNED || p->current.kind == TOKEN_VOID) {
    /* parse_decl consumes the declaration's own ';'. */
    for_node->d.for_stmt.init = parse_decl(p);
  } else if (p->current.kind == TOKEN_SEMICOLON) {
    /* Omitted init clause; consume the ';'. */
    parser_advance(p);
  } else {
    AstNode *init = new_ast_node(p, AST_EXPR_STMT);
    init->d.expr_stmt.expr = parse_expression(p);
    parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
    for_node->d.for_stmt.init = init;
  }

  if (p->current.kind != TOKEN_SEMICOLON) {
    for_node->d.for_stmt.cond = parse_expression(p);
    sema_check_nonvoid_expr(p, for_node->d.for_stmt.cond);
  }
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");

  if (p->current.kind != TOKEN_RPAREN)
    for_node->d.for_stmt.post = parse_expression(p);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");

  ++p->loop_depth;
  for_node->d.for_stmt.body = parse_stmt(p, /*allow_decl=*/false);
  --p->loop_depth;
  p->locals = outer_locals;
  --p->scope_depth;

  return for_node;
}

/*
 * iteration-statement:
 *   'do' statement 'while' '(' expression ')' ';'
 * (C11 6.8.5.2)
 */
static AstNode *parse_do_stmt(Parser *p) {
  AstNode *do_node;

  parse_expect(p, TOKEN_DO, "expect 'do'");
  do_node = new_ast_node(p, AST_DO);
  ++p->loop_depth;
  do_node->d.do_stmt.body = parse_stmt(p, /*allow_decl=*/false);
  --p->loop_depth;
  parse_expect(p, TOKEN_WHILE, "expect 'while'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");
  do_node->d.do_stmt.cond = parse_expression(p);
  sema_check_nonvoid_expr(p, do_node->d.do_stmt.cond);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  /* The trailing ';' is part of the do-statement itself. */
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
  return do_node;
}

/*
 * jump-statement:
 *   'break' ';'
 * (C11 6.8.6.3, valid only within a loop)
 */
static AstNode *parse_break_stmt(Parser *p) {
  /* Check before consuming so the error lands on 'break' itself. */
  if (p->loop_depth == 0 && !p->current_switch)
    parse_error(p, "break statement not within loop or switch");
  parse_expect(p, TOKEN_BREAK, "expect 'break'");
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
  return new_ast_node(p, AST_BREAK);
}

/*
 * jump-statement:
 *   'continue' ';'
 * (C11 6.8.6.2, valid only within a loop)
 */
static AstNode *parse_continue_stmt(Parser *p) {
  /* Check before consuming so the error lands on 'continue' itself. */
  if (p->loop_depth == 0)
    parse_error(p, "continue statement not within loop");
  parse_expect(p, TOKEN_CONTINUE, "expect 'continue'");
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
  return new_ast_node(p, AST_CONTINUE);
}

/*
 * jump-statement:
 *   'goto' identifier ';'
 * (C11 6.8.6.1; the target is validated after the function body is
 *  parsed, since a label may be declared after the goto that uses it)
 */
static AstNode *parse_goto_stmt(Parser *p) {
  AstNode *goto_node;
  GotoRef *ref;
  Token goto_tok;

  goto_tok = p->current;
  parse_expect(p, TOKEN_GOTO, "expect 'goto'");
  if (p->current.kind != TOKEN_IDENTIFIER)
    parse_error(p, "expect label name");

  ref = (GotoRef *)calloc(1, sizeof(GotoRef));
  if (!ref)
    parse_error(p, "out of memory");
  ref->name = p->current;
  ref->loc = goto_tok;
  ref->next = p->goto_refs;
  p->goto_refs = ref;

  goto_node = new_ast_node(p, AST_GOTO);
  goto_node->d.goto_stmt.name = p->current;
  /* Consume the label name. */
  parser_advance(p);
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
  return goto_node;
}

/*
 * labeled-statement:
 *   identifier ':' statement
 * (C11 6.8.1; labels have function scope, so the label table survives
 *  block exit, unlike the locals list)
 */
static AstNode *parse_label_stmt(Parser *p) {
  AstNode *label_node;
  Label *label;
  Token name = p->current;

  /*
   * The duplicate check runs while 'current' is still the label itself,
   * so the error lands on it.
   */
  for (label = p->labels; label; label = label->next)
    if (same_identifier(label->name, name))
      parse_error(p, "duplicate label");

  /* Consume the identifier and ':'. */
  parser_advance(p);
  parse_expect(p, TOKEN_COLON, "expect ':'");

  label = (Label *)calloc(1, sizeof(Label));
  if (!label)
    parse_error(p, "out of memory");
  label->name = name;
  label->next = p->labels;
  p->labels = label;

  label_node = new_ast_node(p, AST_LABEL);
  label_node->d.label_stmt.name = name;
  label_node->d.label_stmt.stmt = parse_stmt(p, /*allow_decl=*/false);
  return label_node;
}

/*
 * selection-statement:
 *   'switch' '(' expression ')' statement
 * (C11 6.8.4.2; 'case' and 'default' are valid only within the body,
 *  and 'break' exits the switch)
 */
static AstNode *parse_switch_stmt(Parser *p) {
  AstNode *switch_node;
  AstNode *outer_switch;

  parse_expect(p, TOKEN_SWITCH, "expect 'switch'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");
  switch_node = new_ast_node(p, AST_SWITCH);
  switch_node->d.switch_stmt.cond =
      sema_switch_condition(p, parse_expression(p));
  parse_expect(p, TOKEN_RPAREN, "expect ')'");

  /*
   * Case labels in the body belong to this switch; nested switches save
   * and restore the pointer to get their own.
   */
  outer_switch = p->current_switch;
  p->current_switch = switch_node;

  switch_node->d.switch_stmt.body = parse_stmt(p, /*allow_decl=*/false);

  p->current_switch = outer_switch;

  return switch_node;
}

/*
 * labeled-statement:
 *   'case' constant-expression ':' statement
 * (C11 6.8.4.2, valid only within a switch)
 */
static AstNode *parse_case_stmt(Parser *p) {
  AstNode *case_node;
  AstNode *expr;
  CaseList *entry;
  int64_t val;
  Token case_tok;

  /* Check before consuming so the error lands on 'case' itself. */
  if (!p->current_switch)
    parse_error(p, "case label not within a switch statement");

  case_tok = p->current;
  parse_expect(p, TOKEN_CASE, "expect 'case'");
  expr = parse_assignment(p);
  if (!is_integer(expr->type))
    error_at(p, expr->loc, "case label must have integer type");
  expr = sema_convert_expr(p, p->current_switch->d.switch_stmt.cond->type, expr,
                           case_tok, "case label");
  val = eval_const_expr(p, expr);

  for (entry = p->current_switch->d.switch_stmt.cases; entry;
       entry = entry->next)
    if (!entry->node->d.case_stmt.is_default &&
        entry->node->d.case_stmt.val == val) {
      p->current = case_tok;
      parse_error(p, "duplicate case value");
    }

  parse_expect(p, TOKEN_COLON, "expect ':'");

  case_node = new_ast_node(p, AST_CASE);
  case_node->d.case_stmt.val = val;

  entry = (CaseList *)calloc(1, sizeof(CaseList));
  if (!entry)
    parse_error(p, "out of memory");
  entry->node = case_node;
  entry->next = p->current_switch->d.switch_stmt.cases;
  p->current_switch->d.switch_stmt.cases = entry;

  case_node->d.case_stmt.stmt = parse_stmt(p, /*allow_decl=*/false);
  return case_node;
}

/*
 * labeled-statement:
 *   'default' ':' statement
 * (C11 6.8.4.2, valid only within a switch; represented as an AST_CASE
 *  with is_default set, like GCC's CASE_LABEL_EXPR)
 */
static AstNode *parse_default_stmt(Parser *p) {
  AstNode *default_node;
  CaseList *entry;

  /* Check before consuming so the error lands on 'default' itself. */
  if (!p->current_switch)
    parse_error(p, "default label not within a switch statement");

  for (entry = p->current_switch->d.switch_stmt.cases; entry;
       entry = entry->next)
    if (entry->node->d.case_stmt.is_default)
      parse_error(p, "multiple default labels in one switch");

  parse_expect(p, TOKEN_DEFAULT, "expect 'default'");
  parse_expect(p, TOKEN_COLON, "expect ':'");

  default_node = new_ast_node(p, AST_CASE);
  default_node->d.case_stmt.is_default = 1;

  entry = (CaseList *)calloc(1, sizeof(CaseList));
  if (!entry)
    parse_error(p, "out of memory");
  entry->node = default_node;
  entry->next = p->current_switch->d.switch_stmt.cases;
  p->current_switch->d.switch_stmt.cases = entry;

  default_node->d.case_stmt.stmt = parse_stmt(p, /*allow_decl=*/false);
  return default_node;
}

/*
 * statement:
 *   jump-statement | selection-statement | iteration-statement
 *   labeled-statement | compound-statement | expression-statement | ';'
 * (C11 6.8; as in GCC and Clang, anything not dispatched above is
 *  attempted as an expression-statement, whose parser diagnoses
 *  tokens that cannot start an expression. Declarations are block items,
 *  not statements, so they are accepted only when allow_decl is set —
 *  i.e. directly inside a compound-statement, like Clang's
 *  ParsedStmtContext::AllowDeclarationsInC)
 */
static AstNode *parse_stmt(Parser *p, bool allow_decl) {
  AstNode *stmt;

  if (p->current.kind == TOKEN_IDENTIFIER && p->lookahead.kind == TOKEN_COLON)
    return parse_label_stmt(p);

  switch (p->current.kind) {
  case TOKEN_RETURN:
    return parse_return_stmt(p);
  case TOKEN_IF:
    return parse_if_stmt(p);
  case TOKEN_WHILE:
    return parse_while_stmt(p);
  case TOKEN_FOR:
    return parse_for_stmt(p);
  case TOKEN_DO:
    return parse_do_stmt(p);
  case TOKEN_BREAK:
    return parse_break_stmt(p);
  case TOKEN_CONTINUE:
    return parse_continue_stmt(p);
  case TOKEN_GOTO:
    return parse_goto_stmt(p);
  case TOKEN_SWITCH:
    return parse_switch_stmt(p);
  case TOKEN_CASE:
    return parse_case_stmt(p);
  case TOKEN_DEFAULT:
    return parse_default_stmt(p);
  case TOKEN_LBRACE:
    return parse_block(p);
  case TOKEN_CHAR:
  case TOKEN_INT:
  case TOKEN_UNSIGNED:
  case TOKEN_VOID:
    if (allow_decl)
      return parse_decl(p);
    /*
     * C11 6.8: a declaration is not a statement, so it cannot be the
     * body of if/while/for/do or a label.
     */
    parse_error(p, "expect statement");
  case TOKEN_SEMICOLON:
    stmt = new_ast_node(p, AST_EXPR_STMT);
    parser_advance(p);
    return stmt;
  default:
    stmt = new_ast_node(p, AST_EXPR_STMT);
    stmt->d.expr_stmt.expr = parse_expression(p);
    parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
    return stmt;
  }
}

/*
 * translation-unit:
 *   external-definition*
 * (C11 6.9)
 */
AstNode *parse_translation_unit(const char *filename, const char *source) {
  Parser p;
  AstNode *head = NULL;
  AstNode **tail = &head;

  parser_init(&p, filename, source);

  /* Keep the empty-input diagnostics: the loop would just not run. */
  if (p.current.kind == TOKEN_EOF)
    parse_error(&p, "expect 'int'");

  while (p.current.kind != TOKEN_EOF) {
    /* Declarations return no AST node. */
    AstNode *fn = parse_external_declaration(&p);
    if (fn) {
      *tail = fn;
      tail = &(*tail)->next;
    }
  }

  check_calls(&p);
  return head;
}
