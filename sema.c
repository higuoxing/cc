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
#include "type.h"

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

static AstNode *new_expr(Parser *p, AstKind kind, Type *type, Token loc);

/*
 * Convert an operand used as a value without promoting its integer type.
 * Array decay is the only explicit value conversion needed by our types so far.
 */
static AstNode *convert_to_value(Parser *p, AstNode *expr) {
  AstNode *node;
  Type *type;
  if (expr->type->kind != TYPE_ARRAY)
    return expr;
  type = pointer_to(expr->type->base);
  if (!type)
    error_at(p, expr->loc, "out of memory");
  node = new_expr(p, AST_CONVERT, type, expr->loc);
  node->d.unary.operand = expr;
  return node;
}

/*
 * Controlling expressions must have scalar type. Array operands first
 * convert to pointers; preserve integer widths for codegen's zero test.
 */
AstNode *sema_check_condition(Parser *p, AstNode *expr) {
  expr = convert_to_value(p, expr);
  sema_check_nonvoid_expr(p, expr);
  if (!is_integer(expr->type) && expr->type->kind != TYPE_PTR)
    error_at(p, expr->loc, "condition must have scalar type");
  return expr;
}

static AstNode *integer_promotion(Parser *p, AstNode *expr) {
  AstNode *node;
  Type *type = integer_promotion_type(expr->type);
  if (type == expr->type)
    return expr;
  node = new_expr(p, AST_CONVERT, type, expr->loc);
  node->d.unary.operand = expr;
  return node;
}

/*
 * Arithmetic operands and default arguments also undergo integer promotion.
 */
static AstNode *promote_operand(Parser *p, AstNode *expr) {
  return integer_promotion(p, convert_to_value(p, expr));
}

AstNode *sema_switch_condition(Parser *p, AstNode *expr) {
  sema_check_nonvoid_expr(p, expr);
  if (!is_integer(expr->type))
    error_at(p, expr->loc, "switch condition must have integer type");
  return integer_promotion(p, expr);
}

Function *declare_function(Parser *p, Token name, Type *return_type,
                           Type **param_types, int nparams, bool has_prototype,
                           bool definition) {
  Function *func = find_function(p, name);
  int i;
  if (func) {
    if (definition && func->is_defined)
      error_at(p, name, "redefinition of function '%.*s'", name.len,
               name.start);
    if (!same_type(func->return_type, return_type))
      error_at(p, name, "conflicting declaration of function '%.*s'", name.len,
               name.start);
    if (func->has_prototype != has_prototype) {
      Type **types = func->has_prototype ? func->param_types : param_types;
      int count = func->has_prototype ? func->nparams : nparams;
      for (i = 0; i < count; ++i)
        if (!same_type(types[i], integer_promotion_type(types[i])))
          error_at(p, name, "conflicting declaration of function '%.*s'",
                   name.len, name.start);
    }
    if (func->has_prototype && has_prototype) {
      if (func->nparams != nparams)
        error_at(p, name, "conflicting declaration of function '%.*s'",
                 name.len, name.start);
      for (i = 0; i < nparams; ++i)
        if (!same_type(func->param_types[i], param_types[i]))
          error_at(p, name, "conflicting declaration of function '%.*s'",
                   name.len, name.start);
    }
  } else {
    func = calloc(1, sizeof(Function));
    if (!func)
      error_at(p, name, "out of memory");
    func->name = name;
    func->return_type = return_type;
    func->next = p->functions;
    p->functions = func;
  }
  if (has_prototype && !func->has_prototype) {
    if (nparams) {
      func->param_types = calloc(nparams, sizeof(Type *));
      if (!func->param_types)
        error_at(p, name, "out of memory");
    }
    func->has_prototype = 1;
    func->nparams = nparams;
    for (i = 0; i < nparams; ++i)
      func->param_types[i] = param_types[i];
  }
  if (definition)
    func->is_defined = 1;
  return func;
}

void sema_check_nonvoid_expr(Parser *p, AstNode *expr) {
  if (expr->type->kind == TYPE_VOID)
    error_at(p, expr->loc, "void expression used as a value");
}

/*
 * Only evaluate constant integer trees when testing for a null pointer
 * constant. An ordinary integer variable whose value is zero is insufficient.
 */
static bool constant_integer_tree(AstNode *expr) {
  if (!is_integer(expr->type))
    return false;
  switch (expr->kind) {
  case AST_INT_LITERAL:
    return true;
  case AST_CONVERT:
    return constant_integer_tree(expr->d.unary.operand);
  case AST_CONDITIONAL:
    return constant_integer_tree(expr->d.conditional.cond) &&
           constant_integer_tree(expr->d.conditional.then_expr) &&
           constant_integer_tree(expr->d.conditional.else_expr);
  case AST_NEG:
  case AST_BITWISE_NOT:
  case AST_LOGICAL_NOT:
    return constant_integer_tree(expr->d.unary.operand);
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
  case AST_LOGICAL_AND:
  case AST_LOGICAL_OR:
    return constant_integer_tree(expr->d.binary.lhs) &&
           constant_integer_tree(expr->d.binary.rhs);
  default:
    return false;
  }
}

static bool null_pointer_constant(Parser *p, AstNode *expr) {
  return constant_integer_tree(expr) && eval_const_expr(p, expr) == 0;
}

AstNode *sema_convert_expr(Parser *p, Type *target, AstNode *expr, Token loc,
                           const char *context) {
  AstNode *node;
  expr = convert_to_value(p, expr);
  sema_check_nonvoid_expr(p, expr);
  if (same_type(target, expr->type))
    return expr;
  if (!(is_integer(target) && is_integer(expr->type)) &&
      !compatible_pointers(target, expr->type) &&
      !(target->kind == TYPE_PTR && null_pointer_constant(p, expr)))
    error_at(p, loc, "incompatible %s type", context);
  node = new_expr(p, AST_CONVERT, target, expr->loc);
  node->d.unary.operand = expr;
  return node;
}

void sema_check_call(Parser *p, Function *func, AstNode *call, int nargs) {
  AstNode **link;
  int i = 0;
  if (func->has_prototype && nargs != func->nparams)
    error_at(p, call->loc, "wrong number of arguments to function '%.*s'",
             func->name.len, func->name.start);
  for (link = &call->d.call.args; *link; link = &(*link)->next, ++i) {
    AstNode *arg = *link;
    AstNode *next = arg->next;
    arg->next = NULL;
    sema_check_nonvoid_expr(p, arg);
    if (func->has_prototype)
      *link =
          sema_convert_expr(p, func->param_types[i], arg, arg->loc, "argument");
    else
      *link = promote_operand(p, arg);
    (*link)->next = next;
  }
}

void sema_check_return(Parser *p, AstNode *stmt) {
  Type *return_type = p->current_function->return_type;
  AstNode *expr = stmt->d.ret.expr;
  if (return_type->kind == TYPE_VOID) {
    if (expr)
      error_at(p, stmt->loc, "return with a value in void function");
    return;
  }
  stmt->d.ret.expr =
      sema_convert_expr(p, return_type, expr, stmt->loc, "return");
}

static bool is_lvalue(AstKind ty) { return ty == AST_VAR || ty == AST_DEREF; }

static bool is_modifiable_lvalue(const AstNode *expr) {
  return is_lvalue(expr->kind) && expr->type->kind != TYPE_ARRAY;
}

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

/*
 * Select the first supported type that holds the literal (C11 6.4.4.1).
 * Decimal constants without U use signed types; octal may use unsigned.
 * Character constants always have type int.
 */
AstNode *sema_integer_literal(Parser *p, Token loc) {
  uint64_t value = loc.int_literal_val;
  bool is_unsigned = (loc.int_literal_suffix & INT_SUFFIX_UNSIGNED) != 0;
  bool is_long = (loc.int_literal_suffix & INT_SUFFIX_LONG) != 0;
  bool is_long_long = (loc.int_literal_suffix & INT_SUFFIX_LONG_LONG) != 0;
  bool decimal = loc.start[0] != '0';
  Type *type;
  AstNode *node;

  if (loc.start[0] == '\'')
    type = &builtin_type_int;
  else if (is_long_long) {
    if (is_unsigned || (!decimal && value > INT64_MAX))
      type = &builtin_type_unsigned_long_long;
    else if (value <= INT64_MAX)
      type = &builtin_type_long_long;
    else
      error_at(p, loc,
               "integer literal out of range for supported signed types");
  } else if (is_unsigned)
    type = !is_long && value <= UINT32_MAX ? &builtin_type_unsigned
                                           : &builtin_type_unsigned_long;
  else if (!is_long && value <= INT32_MAX)
    type = &builtin_type_int;
  else if (!is_long && !decimal && value <= UINT32_MAX)
    type = &builtin_type_unsigned;
  else if (value <= INT64_MAX)
    type = &builtin_type_long;
  else if (!decimal)
    type = &builtin_type_unsigned_long;
  else
    error_at(p, loc, "integer literal out of range for supported signed types");

  node = new_expr(p, AST_INT_LITERAL, type, loc);
  node->d.int_literal.val = value;
  return node;
}

AstNode *sema_unary(Parser *p, AstKind op, AstNode *operand, Token loc) {
  AstNode *node;
  Type *type;
  sema_check_nonvoid_expr(p, operand);

  switch (op) {
  case AST_NEG:
  case AST_BITWISE_NOT:
    operand = promote_operand(p, operand);
    if (!is_integer(operand->type))
      error_at(p, loc, "invalid operand to unary %s",
               op == AST_NEG ? "-" : "~");
    type = operand->type;
    break;
  case AST_LOGICAL_NOT:
    operand = sema_check_condition(p, operand);
    type = &builtin_type_int;
    break;
  case AST_DEREF:
    operand = convert_to_value(p, operand);
    if (operand->type->kind != TYPE_PTR ||
        operand->type->base->kind == TYPE_VOID)
      error_at(p, loc, "invalid deref target");
    type = operand->type->base;
    break;
  case AST_ADDR:
    if (!is_lvalue(operand->kind))
      error_at(p, loc, "invalid address-of target");
    type = pointer_to(operand->type);
    if (!type)
      error_at(p, loc, "out of memory");
    break;
  case AST_PRE_INCREMENT:
  case AST_PRE_DECREMENT:
  case AST_POST_INCREMENT:
  case AST_POST_DECREMENT:
    if (!is_modifiable_lvalue(operand))
      error_at(p, loc, "invalid %s target",
               is_increment_op(op) ? "increment" : "decrement");
    if (operand->type->kind == TYPE_PTR &&
        operand->type->base->kind == TYPE_VOID)
      error_at(p, loc, "pointer arithmetic on void pointer");
    type = operand->type;
    break;
  default:
    error_at(p, loc, "invalid unary operator");
  }

  node = new_expr(p, op, type, loc);
  node->d.unary.operand = operand;
  return node;
}

static AstNode *new_binary_expr(Parser *p, AstKind kind, AstNode *left,
                                AstNode *right, Type *type, Token loc) {
  AstNode *node = new_expr(p, kind, type, loc);
  node->d.binary.lhs = left;
  node->d.binary.rhs = right;
  return node;
}

/*
 * Promote integer operands and convert both to their common arithmetic type.
 * Shifts use separate promotions and must not use this common-type conversion.
 */
static Type *convert_integer_operands(Parser *p, AstNode **left,
                                      AstNode **right, Token loc) {
  Type *type;
  *left = promote_operand(p, *left);
  *right = promote_operand(p, *right);
  if (!is_integer((*left)->type) || !is_integer((*right)->type))
    error_at(p, loc, "invalid operands to integer operator");
  type = common_integer_type((*left)->type, (*right)->type);
  *left = sema_convert_expr(p, type, *left, loc, "operand");
  *right = sema_convert_expr(p, type, *right, loc, "operand");
  return type;
}

/*
 * Addition and subtraction select integer arithmetic, scaled pointer offsets,
 * or pointer difference. Normalize integer + pointer to pointer + integer.
 */
static AstNode *sema_additive(Parser *p, AstKind kind, AstNode *left,
                              AstNode *right, Token loc) {
  Type *type;
  if (is_integer(left->type) && is_integer(right->type)) {
    type = convert_integer_operands(p, &left, &right, loc);
    return new_binary_expr(p, kind, left, right, type, loc);
  }

  left = promote_operand(p, left);
  right = promote_operand(p, right);
  if ((left->type->kind == TYPE_PTR && left->type->base->kind == TYPE_VOID) ||
      (right->type->kind == TYPE_PTR && right->type->base->kind == TYPE_VOID))
    error_at(p, loc, "pointer arithmetic on void pointer");
  if (kind == AST_ADD && left->type->kind != TYPE_PTR &&
      right->type->kind == TYPE_PTR) {
    AstNode *tmp = left;
    left = right;
    right = tmp;
  }

  if (left->type->kind == TYPE_PTR && right->type->kind == TYPE_PTR) {
    if (kind != AST_SUB)
      error_at(p, loc, "invalid operands to binary +");
    if (!same_type(left->type->base, right->type->base))
      error_at(p, loc, "pointer subtraction requires compatible types");
    /* Pointer difference has type ptrdiff_t, signed long on our target. */
    kind = AST_PTR_DIFF;
    type = &builtin_type_long;
  } else if (left->type->kind == TYPE_PTR) {
    if (!is_integer(right->type))
      error_at(p, loc, "pointer offset must have integer type");
    kind = kind == AST_ADD ? AST_PTR_ADD : AST_PTR_SUB;
    type = left->type;
  } else {
    error_at(p, loc, "invalid operands to binary -");
  }
  return new_binary_expr(p, kind, left, right, type, loc);
}

AstNode *sema_binary(Parser *p, AstKind kind, AstNode *left, AstNode *right,
                     Token loc) {
  Type *type;
  if (kind != AST_COMMA) {
    sema_check_nonvoid_expr(p, left);
    sema_check_nonvoid_expr(p, right);
  }

  switch (kind) {
  case AST_COMMA:
    left = convert_to_value(p, left);
    right = convert_to_value(p, right);
    type = right->type;
    break;
  case AST_LOGICAL_AND:
  case AST_LOGICAL_OR:
    left = sema_check_condition(p, left);
    right = sema_check_condition(p, right);
    type = &builtin_type_int;
    break;
  case AST_MUL:
  case AST_DIV:
  case AST_MOD:
  case AST_BIT_AND:
  case AST_BIT_OR:
  case AST_BIT_XOR:
    type = convert_integer_operands(p, &left, &right, loc);
    break;
  case AST_LSHIFT:
  case AST_RSHIFT:
    left = promote_operand(p, left);
    right = promote_operand(p, right);
    if (!is_integer(left->type) || !is_integer(right->type))
      error_at(p, loc, "invalid operands to integer operator");
    type = left->type;
    break;
  case AST_ADD:
  case AST_SUB:
    return sema_additive(p, kind, left, right, loc);
  case AST_LT:
  case AST_LE:
  case AST_GT:
  case AST_GE:
  case AST_EQ:
  case AST_NE:
    if (is_integer(left->type) && is_integer(right->type)) {
      convert_integer_operands(p, &left, &right, loc);
    } else {
      left = promote_operand(p, left);
      right = promote_operand(p, right);
    }
    type = &builtin_type_int;
    break;
  default:
    error_at(p, loc, "invalid binary operator");
  }
  return new_binary_expr(p, kind, left, right, type, loc);
}

/* C11 6.5.2.1: E1[E2] is identical to *((E1) + (E2)). */
AstNode *sema_subscript(Parser *p, AstNode *base, AstNode *index, Token loc) {
  base = convert_to_value(p, base);
  index = convert_to_value(p, index);
  if (!((base->type->kind == TYPE_PTR && is_integer(index->type)) ||
        (is_integer(base->type) && index->type->kind == TYPE_PTR)))
    error_at(p, loc, "subscript requires a pointer and an integer");
  return sema_unary(p, AST_DEREF, sema_binary(p, AST_ADD, base, index, loc),
                    loc);
}

AstNode *sema_assignment(Parser *p, Token operator, AstNode *left,
                         AstNode *rhs) {
  TokenKind op = operator.kind;
  Token loc = operator;
  AstNode *node;
  if (!is_modifiable_lvalue(left))
    error_at(p, loc, "invalid assignment target");
  sema_check_nonvoid_expr(p, rhs);
  if (op != TOKEN_ASSIGN) {
    if (left->type->kind == TYPE_PTR && left->type->base->kind == TYPE_VOID)
      error_at(p, loc, "pointer arithmetic on void pointer");
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
      error_at(p, loc, "invalid assignment operator");
    }

    if (left->kind != AST_VAR)
      error_at(p, loc, "compound assignment through pointer not supported");

    lhs_copy = new_expr(p, AST_VAR, left->d.variable.var->type, loc);
    lhs_copy->d.variable.var = left->d.variable.var;

    binop = sema_binary(p, binop_ty, lhs_copy, rhs, loc);
    rhs = binop;
  }

  rhs = sema_convert_expr(p, left->type, rhs, loc, "assignment");
  node = new_expr(p, AST_ASSIGN, left->type, loc);
  node->d.binary.lhs = left;
  node->d.binary.rhs = rhs;
  return node;
}

AstNode *sema_conditional(Parser *p, AstNode *cond, AstNode *then_expr,
                          AstNode *else_expr, Token loc) {
  Type *type;
  AstNode *node;
  cond = sema_check_condition(p, cond);
  if (is_integer(then_expr->type) && is_integer(else_expr->type)) {
    then_expr = promote_operand(p, then_expr);
    else_expr = promote_operand(p, else_expr);
  } else {
    then_expr = convert_to_value(p, then_expr);
    else_expr = convert_to_value(p, else_expr);
  }
  type = then_expr->type;
  if (is_integer(type) && is_integer(else_expr->type)) {
    type = common_integer_type(type, else_expr->type);
  } else if (!same_type(type, else_expr->type)) {
    if (compatible_pointers(type, else_expr->type)) {
      if (else_expr->type->base->kind == TYPE_VOID)
        type = else_expr->type;
    } else if (type->kind == TYPE_PTR && null_pointer_constant(p, else_expr)) {
      /* The pointer branch determines the result type. */
    } else if (else_expr->type->kind == TYPE_PTR &&
               null_pointer_constant(p, then_expr)) {
      type = else_expr->type;
    } else {
      error_at(p, loc, "incompatible conditional operand types");
    }
  }
  if (type->kind != TYPE_VOID) {
    then_expr =
        sema_convert_expr(p, type, then_expr, loc, "conditional operand");
    else_expr =
        sema_convert_expr(p, type, else_expr, loc, "conditional operand");
  }
  node = new_expr(p, AST_CONDITIONAL, type, loc);
  node->d.conditional.cond = cond;
  node->d.conditional.then_expr = then_expr;
  node->d.conditional.else_expr = else_expr;
  return node;
}

/*
 * Constant values use int64_t to carry signed values or unsigned bit patterns.
 * Convert high unsigned values without relying on an out-of-range signed cast.
 */
static int64_t integer_from_bits(uint64_t bits) {
  return bits <= INT64_MAX ? (int64_t)bits : -1 - (int64_t)(UINT64_MAX - bits);
}

/*
 * Arithmetic is promoted to int or long. Unsigned results wrap to the type's
 * width; signed 64-bit overflow is checked before performing the operation.
 */
static int64_t checked_integer(Parser *p, Type *type, int64_t val) {
  if (type->size == 8)
    return val;
  if (type->is_unsigned)
    return (uint32_t)val;
  if (val < INT32_MIN || val > INT32_MAX)
    parse_error(p, "integer overflow in constant expression");
  return val;
}

/* Evaluate an integer constant expression (C11 6.6). */
int64_t eval_const_expr(Parser *p, AstNode *expr) {
  int64_t lhs, rhs;
  bool unsigned_comparison;
  int width = expr->type->size * 8;
  switch (expr->kind) {
  case AST_INT_LITERAL:
    return integer_from_bits(expr->d.int_literal.val);
  case AST_CONVERT: {
    int64_t val = eval_const_expr(p, expr->d.unary.operand);
    if (is_integer(expr->type) && expr->type->size < 8) {
      int64_t modulus = INT64_C(1) << width;
      val = (uint64_t)val & (uint64_t)(modulus - 1);
      return expr->type->is_unsigned || val < modulus / 2 ? val : val - modulus;
    }
    return val;
  }
  case AST_CONDITIONAL:
    return eval_const_expr(p, expr->d.conditional.cond)
               ? eval_const_expr(p, expr->d.conditional.then_expr)
               : eval_const_expr(p, expr->d.conditional.else_expr);
  case AST_NEG: {
    int64_t val = eval_const_expr(p, expr->d.unary.operand);
    if (!expr->type->is_unsigned && val == INT64_MIN)
      parse_error(p, "integer overflow in constant expression");
    return checked_integer(p, expr->type, integer_from_bits(0 - (uint64_t)val));
  }
  case AST_BITWISE_NOT:
    return checked_integer(p, expr->type,
                           ~eval_const_expr(p, expr->d.unary.operand));
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
    parse_error(p, "not an integer constant expression");
  }

  lhs = eval_const_expr(p, expr->d.binary.lhs);
  rhs = eval_const_expr(p, expr->d.binary.rhs);
  unsigned_comparison = expr->d.binary.lhs->type->is_unsigned;

  switch (expr->kind) {
  case AST_ADD:
    if (expr->type->is_unsigned)
      return checked_integer(p, expr->type,
                             integer_from_bits((uint64_t)lhs + (uint64_t)rhs));
    if ((rhs > 0 && lhs > INT64_MAX - rhs) ||
        (rhs < 0 && lhs < INT64_MIN - rhs))
      parse_error(p, "integer overflow in constant expression");
    return checked_integer(p, expr->type, lhs + rhs);
  case AST_SUB:
    if (expr->type->is_unsigned)
      return checked_integer(p, expr->type,
                             integer_from_bits((uint64_t)lhs - (uint64_t)rhs));
    if ((rhs > 0 && lhs < INT64_MIN + rhs) ||
        (rhs < 0 && lhs > INT64_MAX + rhs))
      parse_error(p, "integer overflow in constant expression");
    return checked_integer(p, expr->type, lhs - rhs);
  case AST_MUL: {
    if (expr->type->is_unsigned)
      return checked_integer(p, expr->type,
                             integer_from_bits((uint64_t)lhs * (uint64_t)rhs));
    bool negative = (lhs < 0) != (rhs < 0);
    uint64_t a = lhs < 0 ? 0 - (uint64_t)lhs : (uint64_t)lhs;
    uint64_t b = rhs < 0 ? 0 - (uint64_t)rhs : (uint64_t)rhs;
    uint64_t limit = (uint64_t)INT64_MAX + negative;
    if (b && a > limit / b)
      parse_error(p, "integer overflow in constant expression");
    uint64_t product = a * b;
    return checked_integer(p, expr->type,
                           integer_from_bits(negative ? 0 - product : product));
  }
  case AST_DIV:
  case AST_MOD:
    if (rhs == 0)
      parse_error(p, "division by zero in constant expression");
    if (expr->type->is_unsigned)
      return checked_integer(
          p, expr->type,
          integer_from_bits(expr->kind == AST_DIV
                                ? (uint64_t)lhs / (uint64_t)rhs
                                : (uint64_t)lhs % (uint64_t)rhs));
    if (lhs == (width == 64 ? INT64_MIN : INT32_MIN) && rhs == -1)
      parse_error(p, "integer overflow in constant expression");
    return checked_integer(p, expr->type,
                           expr->kind == AST_DIV ? lhs / rhs : lhs % rhs);
  case AST_LSHIFT:
    if (rhs < 0 || rhs >= width)
      parse_error(p, "invalid shift count in constant expression");
    if (!expr->type->is_unsigned) {
      if (lhs < 0)
        parse_error(p, "left shift of negative value in constant expression");
      uint64_t limit = width == 64 ? INT64_MAX : INT32_MAX;
      if ((uint64_t)lhs > (limit >> rhs))
        parse_error(p, "integer overflow in constant expression");
    }
    return checked_integer(p, expr->type,
                           integer_from_bits((uint64_t)lhs << rhs));
  case AST_RSHIFT:
    if (rhs < 0 || rhs >= width)
      parse_error(p, "invalid shift count in constant expression");
    if (expr->type->is_unsigned || lhs >= 0)
      return checked_integer(p, expr->type,
                             integer_from_bits((uint64_t)lhs >> rhs));
    return checked_integer(p, expr->type,
                           integer_from_bits(~(~(uint64_t)lhs >> rhs)));
  case AST_LT:
    return unsigned_comparison ? (uint64_t)lhs < (uint64_t)rhs : lhs < rhs;
  case AST_LE:
    return unsigned_comparison ? (uint64_t)lhs <= (uint64_t)rhs : lhs <= rhs;
  case AST_GT:
    return unsigned_comparison ? (uint64_t)lhs > (uint64_t)rhs : lhs > rhs;
  case AST_GE:
    return unsigned_comparison ? (uint64_t)lhs >= (uint64_t)rhs : lhs >= rhs;
  case AST_EQ:
    return lhs == rhs;
  case AST_NE:
    return lhs != rhs;
  case AST_BIT_AND:
    return checked_integer(p, expr->type, lhs & rhs);
  case AST_BIT_XOR:
    return checked_integer(p, expr->type, lhs ^ rhs);
  case AST_BIT_OR:
    return checked_integer(p, expr->type, lhs | rhs);
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

  for (ref = p->call_refs; ref; ref = ref->next)
    sema_check_call(p, ref->call->d.call.function, ref->call, ref->nargs);
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
