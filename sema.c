/*
 * Sema for the self-hosted C compiler
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

#include <string.h>

#include "sema.h"

Type builtin_type_int = {.kind = TYPE_INT, .size = 4, .base = NULL};
Type builtin_type_void = {.kind = TYPE_VOID, .size = 0, .base = NULL};

bool same_identifier(Token a, Token b) {
  return a.len == b.len && strncmp(a.start, b.start, a.len) == 0;
}

Function *find_function(Parser *p, Token name) {
  Function *fn;
  for (fn = p->functions; fn != NULL; fn = fn->next) {
    if (same_identifier(fn->name, name))
      return fn;
  }
  return NULL;
}

/*
 * Sizes are stored in Type: an array's or struct's size is data, not
 * derivable from its kind. Void must never reach this.
 */
int type_size(Type *type) { return type->size; }

static bool is_lvalue(AstKind ty) { return ty == AST_VAR || ty == AST_DEREF; }

static bool is_increment_op(AstKind ty) {
  return ty == AST_PRE_INCREMENT || ty == AST_POST_INCREMENT;
}

static AstNode *new_expr(Parser *p, AstKind kind, Type *type, Token loc) {
  AstNode *node = calloc(1, sizeof(AstNode));
  if (!node)
    error_at(p, loc, "out of memory");
  node->kind = kind;
  node->type = type;
  node->loc = loc;
  return node;
}

Type *sema_ptr_type(Parser *p, Type *base) {
  Type *type;
  type = (Type *)calloc(1, sizeof(Type));
  if (!type)
    parse_error(p, "out of memory");
  type->kind = TYPE_PTR;
  type->size = 8;
  type->base = base;
  return type;
}

AstNode *sema_unary(Parser *p, AstKind op, AstNode *operand, Token loc) {
  AstNode *node;
  if (((op == AST_PRE_INCREMENT || op == AST_PRE_DECREMENT ||
        op == AST_POST_INCREMENT || op == AST_POST_DECREMENT) &&
       !is_lvalue(operand->kind)) ||
      (op == AST_ADDR && !is_lvalue(operand->kind))) {
    /* Report at the operator, not at the token after the operand. */
    error_at(p, loc, "invalid %s target",
             op == AST_ADDR        ? "address-of"
             : is_increment_op(op) ? "increment"
                                   : "decrement");
  }

  if (op == AST_DEREF && operand->type->kind != TYPE_PTR)
    error_at(p, loc, "invalid deref target");

  node =
      new_expr(p, op,
               op == AST_DEREF  ? operand->type->base
               : op == AST_ADDR ? sema_ptr_type(p, operand->type)
               : op == AST_NEG || op == AST_BITWISE_NOT || op == AST_LOGICAL_NOT
                   ? &builtin_type_int
                   : operand->type /* pre/post increment */,
               loc);
  node->d.unary.operand = operand;
  return node;
}

AstNode *sema_binary(Parser *p, AstKind kind, AstNode *left, AstNode *right,
                     Token loc) {
  AstNode *node;
  if (kind == AST_ADD || kind == AST_SUB) {
    /* 'int + ptr' is 'ptr + int' (C11 6.5.6); normalize the operands. */
    if (kind == AST_ADD && left->type->kind != TYPE_PTR &&
        right->type->kind == TYPE_PTR) {
      AstNode *tmp = left;
      left = right;
      right = tmp;
    }

    /*
     * The operand types decide the operation once and for all; codegen
     * maps each kind to instructions without re-checking types.
     */
    if (left->type->kind == TYPE_PTR && right->type->kind == TYPE_PTR) {
      if (kind != AST_SUB) {
        error_at(p, loc, "invalid operands to binary +");
      }
      /*
       * Pointer difference: the element count (C11 6.5.6p9: ptrdiff_t,
       * approximated by int until 'long' exists).
       */
      node = new_expr(p, AST_PTR_DIFF, &builtin_type_int, loc);
    } else if (left->type->kind == TYPE_PTR) {
      node = new_expr(p, kind == AST_ADD ? AST_PTR_ADD : AST_PTR_SUB,
                      left->type, loc);
    } else if (right->type->kind == TYPE_PTR) {
      error_at(p, loc, "invalid operands to binary -");
    } else {
      node = new_expr(p, kind, &builtin_type_int, loc);
    }
  } else {
    node = new_expr(p, kind,
                    kind == AST_COMMA ? right->type : &builtin_type_int, loc);
  }
  node->d.binary.lhs = left;
  node->d.binary.rhs = right;
  return node;
}

AstNode *sema_assignment(Parser *p, Token operator, AstNode *left,
                         AstNode *rhs) {
  TokenKind op = operator.kind;
  Token loc = operator;
  AstNode *node;
  if (!is_lvalue(left->kind))
    error_at(p, loc, "invalid assignment target");
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
      /* p += 1 on a pointer steps by the pointee's size. */
      binop_ty = left->type->kind == TYPE_PTR ? AST_PTR_ADD : AST_ADD;
      break;
    case TOKEN_MINUS_EQ:
      binop_ty = left->type->kind == TYPE_PTR ? AST_PTR_SUB : AST_SUB;
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
      error_at(p, loc, "invalid assignment operator");
    }

    if (left->kind != AST_VAR)
      error_at(p, loc, "compound assignment through pointer not supported");

    binop = new_expr(p, binop_ty, left->type, loc);
    lhs_copy = new_expr(p, AST_VAR, left->d.variable.var->type, loc);
    lhs_copy->d.variable.var = left->d.variable.var;

    binop->d.binary.lhs = lhs_copy;
    binop->d.binary.rhs = rhs;
    rhs = binop;
  }

  node = new_expr(p, AST_ASSIGN, left->type, loc);
  node->d.binary.lhs = left;
  node->d.binary.rhs = rhs;
  return node;
}

AstNode *sema_conditional(Parser *p, AstNode *cond, AstNode *then_expr,
                          AstNode *else_expr, Token loc) {
  AstNode *node = new_expr(p, AST_CONDITIONAL, then_expr->type, loc);
  node->d.conditional.cond = cond;
  node->d.conditional.then_expr = then_expr;
  node->d.conditional.else_expr = else_expr;
  return node;
}

/* Every value in a constant expression must fit an int. */
static int64_t checked_int(Parser *p, int64_t val) {
  if (val < INT32_MIN || val > INT32_MAX)
    parse_error(p, "integer overflow in constant expression");
  return val;
}

/* Evaluate an integer constant expression (C11 6.6), used for case labels. */
int64_t eval_const_expr(Parser *p, AstNode *expr) {
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
 * (C11 6.5.2.2) A function declaration is sufficient for code generation:
 * definitions may be in another translation unit.
 */
void check_calls(Parser *p) {
  CallRef *ref;

  for (ref = p->call_refs; ref; ref = ref->next) {
    Function *func;

    func = find_function(p, ref->name);
    if (func && func->has_prototype && ref->nargs != func->nparams) {
      error_at(p, ref->loc, "wrong number of arguments to function '%.*s'",
               ref->name.len, ref->name.start);
    }
  }
}

/*
 * Every 'goto' must refer to a label declared somewhere in the function;
 * labels have function scope and may be declared after the goto.
 */
void check_gotos(Parser *p) {
  GotoRef *ref;

  for (ref = p->goto_refs; ref; ref = ref->next) {
    Label *label;

    for (label = p->labels; label; label = label->next)
      if (same_identifier(label->name, ref->name))
        break;
    if (!label) {
      error_at(p, ref->loc, "label '%.*s' not defined", ref->name.len,
               ref->name.start);
    }
  }
}
