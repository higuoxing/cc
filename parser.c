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

static LocalVar *declare_local_var(Parser *p, Token name);
static AstNode *new_ast_node(Parser *p, AstKind kind);
static void reset_function_state(Parser *p);
static void parser_advance(Parser *p);
static void parse_error(Parser *p, const char *fmt, ...)
    __attribute__((format(printf, 2, 3), noreturn));
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
static void check_gotos(Parser *p);
static void check_calls(Parser *p);
static int64_t eval_const_expr(Parser *p, AstNode *expr);
static AstNode *parse_block(Parser *p);
static AstNode *parse_external_declaration(Parser *p);
static bool is_lvalue(AstKind kind);
static bool is_increment_op(AstKind kind);
static bool is_assignment_op(TokenKind kind);

static bool same_identifier(Token a, Token b) {
  return a.len == b.len && strncmp(a.start, b.start, a.len) == 0;
}

static Function *find_function(Parser *p, Token name) {
  Function *fn;
  for (fn = p->functions; fn != NULL; fn = fn->next) {
    if (same_identifier(fn->name, name))
      return fn;
  }
  return NULL;
}

static LocalVar *find_local_var(Parser *p, Token name) {
  LocalVar *var;
  for (var = p->locals; var; var = var->next) {
    if (same_identifier(var->name, name))
      return var;
  }
  return NULL;
}

static LocalVar *declare_local_var(Parser *p, Token name) {
  LocalVar *var;
  for (var = p->locals; var && var->scope_depth == p->scope_depth;
       var = var->next) {
    if (same_identifier(var->name, name))
      parse_error(p, "duplicate local variable");
  }

  /* The function body's outermost block (depth 1) may not redeclare a
   * parameter (depth 0); deeper blocks may shadow freely (C11 6.2.1). */
  if (p->scope_depth == 1)
    for (var = p->locals; var && var->scope_depth == 0; var = var->next)
      if (same_identifier(var->name, name))
        parse_error(p, "declaration of '%.*s' shadows a parameter", name.len,
                    name.start);

  var = (LocalVar *)calloc(1, sizeof(LocalVar));
  if (!var)
    parse_error(p, "out of memory");
  var->name = name;
  var->scope_depth = p->scope_depth;
  p->stack_size += 4; /* All locals are 32-bit int for now. */
  var->offset = p->stack_size;
  var->next = p->locals;
  p->locals = var;
  return var;
}

static AstNode *new_ast_node(Parser *p, AstKind ty) {
  AstNode *node;
  node = calloc(1, sizeof(AstNode));
  if (!node)
    parse_error(p, "out of memory");
  node->kind = ty;
  return node;
}

static void parser_advance(Parser *p) {
  p->current = p->lookahead;
  p->lookahead = lexer_next_token(&p->lexer);
}

/*
 * Reset the per-function workspace. Function-scoped state (locals, labels,
 * pending gotos, the stack size, and the nesting counters) must not leak
 * between functions; only the function table survives the whole translation
 * unit. Blocks restore the locals list and scope depth by their own
 * discipline, so this is mostly defensive — except for stack_size, which
 * accumulates, and the label/goto tables, which are deliberately never
 * restored when a block ends.
 */
static void reset_function_state(Parser *p) {
  p->locals = NULL;
  p->labels = NULL;
  p->goto_refs = NULL;
  p->current_switch = NULL;
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
  p->scope_depth = 0;
  p->loop_depth = 0;
  p->stack_size = 0;
  lexer_init(&p->lexer, filename, source);
  p->current = lexer_next_token(&p->lexer);
  p->lookahead = lexer_next_token(&p->lexer);
}

static void parse_error(Parser *p, const char *fmt, ...) {
  va_list ap;

  fprintf(stderr, "%s:%d:%d: error: ", p->lexer.filename, p->current.line,
          p->current.col);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void parse_expect(Parser *p, TokenKind t, const char *msg) {
  if (p->current.kind != t) {
    parse_error(p, "%s", msg);
  }

  parser_advance(p);
}

/*
 * Only variables are lvalues. The results of assignment and prefix
 * increment/decrement are not lvalues in C11, so this matches GCC and Clang
 * in C mode.
 */
static bool is_lvalue(AstKind ty) { return ty == AST_VAR; }

static bool is_increment_op(AstKind ty) {
  return ty == AST_PRE_INCREMENT || ty == AST_POST_INCREMENT;
}

/* assignment-operator: = += -= *= /= %= <<= >>= &= ^= |=  (C11 6.5.16) */
static bool is_assignment_op(TokenKind ty) {
  return ty == TOKEN_ASSIGN || ty == TOKEN_PLUS_EQ || ty == TOKEN_MINUS_EQ ||
         ty == TOKEN_STAR_EQ || ty == TOKEN_SLASH_EQ ||
         ty == TOKEN_PERCENT_EQ || ty == TOKEN_LSHIFT_EQ ||
         ty == TOKEN_RSHIFT_EQ || ty == TOKEN_BIT_AND_EQ ||
         ty == TOKEN_BIT_XOR_EQ || ty == TOKEN_BIT_OR_EQ;
}

/*
 * unary-expression:
 *   '-' unary-expression | '~' unary-expression | '!' unary-expression
 *   '++' unary-expression | '--' unary-expression
 *   postfix-expression
 * (C11 6.5.3; '&', '*', sizeof, and _Alignof are not implemented)
 */
static AstNode *parse_unary(Parser *p) {
  if (p->current.kind == TOKEN_MINUS || p->current.kind == TOKEN_TILDE ||
      p->current.kind == TOKEN_BANG || p->current.kind == TOKEN_PLUS_PLUS ||
      p->current.kind == TOKEN_MINUS_MINUS) {
    AstNode *node;
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
    default:
      fprintf(stderr, "unreachable %s:%d", __FILE__, __LINE__);
      exit(1);
    }

    /* Consume '-' or '~' or '!' or '++' or '--' */
    op_tok = p->current;
    parser_advance(p);
    node = new_ast_node(p, op);
    node->d.unary.operand = parse_unary(p);

    if ((op == AST_PRE_INCREMENT || op == AST_PRE_DECREMENT) &&
        !is_lvalue(node->d.unary.operand->kind)) {
      /* Report at the operator, not at the token after the operand. */
      p->current = op_tok;
      parse_error(p, "invalid %s target",
                  is_increment_op(op) ? "increment" : "decrement");
    }

    return node;
  }

  return parse_postfix(p);
}

/*
 * postfix-expression:
 *   identifier '(' argument-expression-list_opt ')'
 * (C11 6.5.2.2; a function must be declared before its use.)
 */
static AstNode *parse_call_expr(Parser *p) {
  AstNode *call;
  CallRef *ref;
  int nargs = 0;

  ref = (CallRef *)calloc(1, sizeof(CallRef));
  if (!ref)
    parse_error(p, "out of memory");
  ref->name = p->current;
  ref->loc = p->current;
  ref->next = p->call_refs;
  p->call_refs = ref;

  if (find_function(p, ref->name) == NULL)
    parse_error(p, "function '%.*s' not declared", ref->name.len,
                ref->name.start);

  call = new_ast_node(p, AST_CALL);
  call->d.call.name = p->current;
  /* Consume the function name. */
  parser_advance(p);
  parse_expect(p, TOKEN_LPAREN, "expect '('");

  if (p->current.kind != TOKEN_RPAREN) {
    AstNode **tail = &call->d.call.args;

    for (;;) {
      if (nargs == 6)
        parse_error(p, "too many arguments");
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
  return call;
}

/*
 * postfix-expression:
 *   primary-expression
 *   identifier '(' ')'
 *   postfix-expression '++' | postfix-expression '--'
 * (C11 6.5.2; the increment operand must be a modifiable lvalue, 6.5.2.4)
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
    AstNode *postfixed;
    AstKind ty;

    ty = p->current.kind == TOKEN_PLUS_PLUS ? AST_POST_INCREMENT
                                            : AST_POST_DECREMENT;

    /*
     * The grammar recurses here, but the operand must still be an lvalue:
     * (a++)++ is invalid in C. Check before consuming so the error lands
     * on the operator.
     */
    if (!is_lvalue(left->kind))
      parse_error(p, "invalid %s target",
                  is_increment_op(ty) ? "increment" : "decrement");

    /* Consume '++' or '--' */
    parser_advance(p);
    postfixed = new_ast_node(p, ty);
    postfixed->d.unary.operand = left;
    left = postfixed;
  }

  return left;
}

/*
 * primary-expression:
 *   integer-constant | '(' expression ')' | identifier
 * (C11 6.5.1; only int literals, no string or character constants)
 */
static AstNode *parse_primary(Parser *p) {
  if (p->current.kind == TOKEN_INT_LITERAL) {
    AstNode *expr;
    expr = new_ast_node(p, AST_INT_LITERAL);
    expr->d.int_literal.val = p->current.int_literal_val;

    /* Consume integer literal */
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
    expr = new_ast_node(p, AST_VAR);
    expr->d.variable.var = var;
    /* Consume identifier. */
    parser_advance(p);
    return expr;
  }

  parse_error(p, "expect expr");
  return NULL;
}

/*
 * declaration:
 *   'int' identifier ['=' expression] ';'
 * (C11 6.7, restricted to int and a single declarator)
 */
static AstNode *parse_decl(Parser *p) {
  AstNode *decl;
  Token name;

  parse_expect(p, TOKEN_INT, "expect 'int'");
  if (p->current.kind != TOKEN_IDENTIFIER)
    parse_error(p, "expect variable name");

  name = p->current;
  decl = new_ast_node(p, AST_DECL);
  decl->d.decl.var = declare_local_var(p, name);
  /* Consume variable name. */
  parser_advance(p);

  if (p->current.kind == TOKEN_ASSIGN) {
    /* Consume '=' */
    parser_advance(p);
    decl->d.decl.init = parse_assignment(p);
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

    op = p->current.kind;
    /* Consume '*' or '/' or '%' */
    parser_advance(p);

    right = parse_unary(p);
    node = new_ast_node(p, op == TOKEN_STAR
                               ? AST_MUL
                               : (op == TOKEN_SLASH ? AST_DIV : AST_MOD));
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    op = p->current.kind;
    /* Consume '+' or '-' */
    parser_advance(p);
    right = parse_multiplicative(p);

    node = new_ast_node(p, op == TOKEN_PLUS ? AST_ADD : AST_SUB);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    op = p->current.kind;
    /* Consume '<<' or '>>' */
    parser_advance(p);

    right = parse_additive(p);
    node = new_ast_node(p, op == TOKEN_LSHIFT ? AST_LSHIFT : AST_RSHIFT);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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
    node = new_ast_node(p, ty);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    op = p->current.kind;
    /* Consume '==' or '!=' */
    parser_advance(p);

    right = parse_relational(p);
    node = new_ast_node(p, op == TOKEN_EQ_EQ ? AST_EQ : AST_NE);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    /* Consume '&' */
    parser_advance(p);

    right = parse_equality(p);
    node = new_ast_node(p, AST_BIT_AND);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    /* Consume '^' */
    parser_advance(p);

    right = parse_bitwise_and(p);
    node = new_ast_node(p, AST_BIT_XOR);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    /* Consume '|' */
    parser_advance(p);

    right = parse_bitwise_xor(p);
    node = new_ast_node(p, AST_BIT_OR);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    /* Consume '&&' */
    parser_advance(p);

    right = parse_bitwise_or(p);
    node = new_ast_node(p, AST_LOGICAL_AND);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    /* Consume '||' */
    parser_advance(p);

    right = parse_logical_and(p);
    node = new_ast_node(p, AST_LOGICAL_OR);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

    /* Consume ',' */
    parser_advance(p);
    right = parse_assignment(p);
    node = new_ast_node(p, AST_COMMA);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;
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
    AstNode *cond = node;

    /* Consume '?' */
    parser_advance(p);
    node = new_ast_node(p, AST_CONDITIONAL);
    node->d.conditional.cond = cond;
    node->d.conditional.then_expr = parse_expression(p);
    parse_expect(p, TOKEN_COLON, "expect ':'");
    node->d.conditional.else_expr = parse_conditional(p);
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
  AstNode *left;

  left = parse_conditional(p);
  if (is_assignment_op(p->current.kind)) {
    AstNode *node;
    AstNode *rhs;
    TokenKind op;

    if (!is_lvalue(left->kind))
      parse_error(p, "invalid assignment target");

    op = p->current.kind;
    /* Consume the assignment operator */
    parser_advance(p);

    rhs = parse_assignment(p);

    if (op != TOKEN_ASSIGN) {
      /*
       * Compound assignment desugars to 'E1 = E1 <op> E2', where E1
       * is the left operand and E2 the right one (C11 6.5.16.2p3).
       * The standard requires the lvalue E1 to be evaluated only
       * once, which holds here because E1 is a plain variable.
       * lhs_copy is a fresh node: codegen walks a tree and must not
       * see the same node twice.
       */
      AstNode *binop;
      AstNode *lhs_copy;
      AstKind binop_ty;

      switch (op) {
      case TOKEN_PLUS_EQ:
        binop_ty = AST_ADD;
        break;
      case TOKEN_MINUS_EQ:
        binop_ty = AST_SUB;
        break;
      case TOKEN_STAR_EQ:
        binop_ty = AST_MUL;
        break;
      case TOKEN_SLASH_EQ:
        binop_ty = AST_DIV;
        break;
      case TOKEN_PERCENT_EQ:
        binop_ty = AST_MOD;
        break;
      case TOKEN_LSHIFT_EQ:
        binop_ty = AST_LSHIFT;
        break;
      case TOKEN_RSHIFT_EQ:
        binop_ty = AST_RSHIFT;
        break;
      case TOKEN_BIT_AND_EQ:
        binop_ty = AST_BIT_AND;
        break;
      case TOKEN_BIT_XOR_EQ:
        binop_ty = AST_BIT_XOR;
        break;
      case TOKEN_BIT_OR_EQ:
        binop_ty = AST_BIT_OR;
        break;
      default:
        fprintf(stderr, "unreachable %s:%d", __FILE__, __LINE__);
        exit(1);
      }

      binop = new_ast_node(p, binop_ty);
      lhs_copy = new_ast_node(p, AST_VAR);
      lhs_copy->d.variable.var = left->d.variable.var;

      binop->d.binary.lhs = lhs_copy;
      binop->d.binary.rhs = rhs;
      rhs = binop;
    }

    node = new_ast_node(p, AST_ASSIGN);
    node->d.binary.lhs = left;
    node->d.binary.rhs = rhs;
    return node;
  }
  return left;
}

/*
 * jump-statement:
 *   'return' expression ';'
 * (C11 6.8.6.4; bare 'return;' is rejected for now)
 */
static AstNode *parse_return_stmt(Parser *p) {
  AstNode *ast;

  parse_expect(p, TOKEN_RETURN, "expect 'return'");
  if (p->current.kind == TOKEN_SEMICOLON)
    parse_error(p, "expect return expr");

  ast = new_ast_node(p, AST_RETURN);
  ast->d.ret.expr = parse_expression(p);
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
 * (C11 6.5.2.2) A function declaration is sufficient for code generation:
 * definitions may be in another translation unit.
 */
static void check_calls(Parser *p) {
  CallRef *ref;

  for (ref = p->call_refs; ref; ref = ref->next) {
    Function *func;

    func = find_function(p, ref->name);
    if (func && func->has_prototype && ref->nargs != func->nparams) {
      p->current = ref->loc;
      parse_error(p, "wrong number of arguments to function '%.*s'",
                  ref->name.len, ref->name.start);
    }
  }
}

/*
 * Every 'goto' must refer to a label declared somewhere in the function;
 * labels have function scope and may be declared after the goto.
 */
static void check_gotos(Parser *p) {
  GotoRef *ref;

  for (ref = p->goto_refs; ref; ref = ref->next) {
    Label *label;

    for (label = p->labels; label; label = label->next)
      if (same_identifier(label->name, ref->name))
        break;
    if (!label) {
      p->current = ref->loc;
      parse_error(p, "label '%.*s' not defined", ref->name.len,
                  ref->name.start);
    }
  }
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
  int nparams = 0;
  int definition = 0;
  int explicit_params = 0;
  int unamed_param = 0;

  reset_function_state(p);
  fn = new_ast_node(p, AST_FUNCTION);
  parse_expect(p, TOKEN_INT, "expect 'int'");
  if (p->current.kind != TOKEN_IDENTIFIER)
    parse_error(p, "expect function name");

  fn->d.function.name = p->current;
  func_name = p->current;
  /* Consume <function_name> */
  parser_advance(p);

  parse_expect(p, TOKEN_LPAREN, "expect '('");

  if (p->current.kind == TOKEN_VOID) {
    explicit_params = 1;
    parser_advance(p);
    if (p->current.kind != TOKEN_RPAREN)
      parse_error(p, "expect ')' after 'void'");
  } else if (p->current.kind != TOKEN_RPAREN) {
    explicit_params = 1;
    for (;;) {
      LocalVar *param;
      Token param_name;

      /* At most six parameters, matching the register ABI; the call
       * side enforces the same limit on arguments. */
      if (nparams == 6)
        parse_error(p, "too many parameters");
      parse_expect(p, TOKEN_INT, "expect 'int'");

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
        param->scope_depth = 0;
        param->offset = 8 * (nparams + 1);
        param->next = p->locals;
        p->locals = param;
      } else {
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

  func = find_function(p, func_name);
  if (func) {
    if (definition && func->is_defined) {
      p->current = func_name;
      parse_error(p, "redefinition of function '%.*s'", func_name.len,
                  func_name.start);
    }

    if (func->has_prototype && (explicit_params || definition) &&
        func->nparams != nparams) {
      p->current = func_name;
      parse_error(p, "conflicting declaration of function '%.*s'",
                  func_name.len, func_name.start);
    }

    if (explicit_params || definition) {
      func->nparams = nparams;
      func->has_prototype = 1;
    }
  } else {
    func = (Function *)calloc(1, sizeof(Function));
    if (!func)
      parse_error(p, "out of memory");
    func->name = func_name;
    func->nparams = nparams;
    func->has_prototype = explicit_params || definition;
    func->next = p->functions;
    p->functions = func;
  }

  if (!definition) {
    /* Consume ';' */
    parser_advance(p);
    return NULL;
  }

  func->is_defined = 1;
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

  if (p->current.kind == TOKEN_INT) {
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

  if (p->current.kind != TOKEN_SEMICOLON)
    for_node->d.for_stmt.cond = parse_expression(p);
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

/* Evaluate an integer constant expression (C11 6.6), used for case labels. */
static int64_t checked_int(Parser *p, int64_t val) {
  if (val < INT32_MIN || val > INT32_MAX)
    parse_error(p, "integer overflow in constant expression");
  return val;
}
static int64_t eval_const_expr(Parser *p, AstNode *expr) {
  int64_t lhs, rhs;
  switch (expr->kind) {
  case AST_INT_LITERAL:
    return expr->d.int_literal.val;
  case AST_NEG:
    return checked_int(p, -eval_const_expr(p, expr->d.unary.operand));
  case AST_BITWISE_NOT:
    return checked_int(p, ~eval_const_expr(p, expr->d.unary.operand));
  case AST_LOGICAL_NOT:
    return !eval_const_expr(p, expr->d.unary.operand);
  case AST_LOGICAL_AND: {
    /* Preserve short-circuit semantics, so 0 && (1 / 0) is valid. */
    int64_t lhs = eval_const_expr(p, expr->d.binary.lhs);
    return lhs ? eval_const_expr(p, expr->d.binary.rhs) != 0 : 0;
  }
  case AST_LOGICAL_OR: {
    int64_t lhs = eval_const_expr(p, expr->d.binary.lhs);
    return lhs ? 1 : eval_const_expr(p, expr->d.binary.rhs) != 0;
  }
  case AST_ADD:
  case AST_SUB:
  case AST_MUL:
  case AST_DIV:
  case AST_MOD:
  case AST_LSHIFT:
  case AST_RSHIFT:
  case AST_LT:
  case AST_LE:
  case AST_GT:
  case AST_GE:
  case AST_EQ:
  case AST_NE:
  case AST_BIT_AND:
  case AST_BIT_XOR:
  case AST_BIT_OR:
    break;
  default:
    /* AST_VAR, AST_ASSIGN, increments, and so on are not constant. */
    parse_error(p, "case label is not an integer constant");
  }

  lhs = eval_const_expr(p, expr->d.binary.lhs);
  rhs = eval_const_expr(p, expr->d.binary.rhs);

  switch (expr->kind) {
  case AST_ADD:
    return checked_int(p, lhs + rhs);
  case AST_SUB:
    return checked_int(p, lhs - rhs);
  case AST_MUL:
    return checked_int(p, lhs * rhs);
  case AST_DIV:
  case AST_MOD:
    if (rhs == 0)
      parse_error(p, "division by zero in constant expression");
    return checked_int(p, expr->kind == AST_DIV ? lhs / rhs : lhs % rhs);
  case AST_LSHIFT:
    if (rhs < 0 || rhs >= 32)
      parse_error(p, "invalid shift count in constant expression");
    if (lhs < 0)
      parse_error(p, "left shift of negative value in constant expression");
    checked_int(p, lhs);
    return checked_int(p, lhs << rhs);
  case AST_RSHIFT:
    if (rhs < 0 || rhs >= 32)
      parse_error(p, "invalid shift count in constant expression");
    return lhs >> rhs;
  case AST_LT:
    return lhs < rhs;
  case AST_LE:
    return lhs <= rhs;
  case AST_GT:
    return lhs > rhs;
  case AST_GE:
    return lhs >= rhs;
  case AST_EQ:
    return lhs == rhs;
  case AST_NE:
    return lhs != rhs;
  case AST_BIT_AND:
    return checked_int(p, lhs & rhs);
  case AST_BIT_XOR:
    return checked_int(p, lhs ^ rhs);
  case AST_BIT_OR:
    return checked_int(p, lhs | rhs);
  default:
    parse_error(p, "invalid constant expression");
  }
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
  switch_node->d.switch_stmt.cond = parse_expression(p);
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
  CaseList *entry;
  int64_t val;
  Token case_tok;

  /* Check before consuming so the error lands on 'case' itself. */
  if (!p->current_switch)
    parse_error(p, "case label not within a switch statement");

  case_tok = p->current;
  parse_expect(p, TOKEN_CASE, "expect 'case'");
  val = eval_const_expr(p, parse_assignment(p));

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
  case TOKEN_INT:
    if (allow_decl)
      return parse_decl(p);
    /* C11 6.8: a declaration is not a statement, so it cannot be the
       body of if/while/for/do or a label. */
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
