/*
 * Codegen for the self-hosted C compiler
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

#include <inttypes.h>
#include <stdio.h>

#include "codegen.h"
#include "parser.h"
#include "type.h"

/*
 * Counts generated labels so that each control-flow construct gets unique
 * ones (.L.else.N, .L.end.N, ...).
 */
static int label_seq;

/*
 * Number of unbalanced pushq instructions in the current expression, so
 * that a call site can restore 16-byte stack alignment.
 */
static int push_depth;

/*
 * Assembly is emitted here; set by codegen() so that helpers need no FILE*
 * parameter (GCC's asm_out_file).
 */
static FILE *out;

/*
 * The first six integer argument registers, in order (System V AMD64),
 * in their 64-bit, 32-bit, 16-bit, and 8-bit spellings.
 */
static const char *argregs64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
static const char *argregs32[] = {"%edi", "%esi", "%edx",
                                  "%ecx", "%r8d", "%r9d"};
static const char *argregs16[] = {"%di", "%si", "%dx", "%cx", "%r8w", "%r9w"};
static const char *argregs8[] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"};

static void codegen_expr(AstNode *expr);

static void codegen_addr(AstNode *expr) {
  switch (expr->kind) {
  case AST_VAR:
    fprintf(out, "\tleaq -%d(%%rbp), %%rax\n", expr->d.variable.var->offset);
    break;
  case AST_MEMBER:
    /*
     * Both '.' and '->' use the struct object's address plus its field offset.
     */
    codegen_addr(expr->d.member.base);
    fprintf(out, "\taddq $%d, %%rax\n", expr->d.member.member->offset);
    break;
  case AST_STRING_LITERAL: {
    int seq = label_seq++;
    int i;

    /*
     * Emit decoded bytes, including embedded zeros and the final terminator.
     * Restore the text section before generating the address instruction.
     */
    fprintf(out, "\t.pushsection .rodata\n");
    fprintf(out, ".L.string.%d:\n", seq);
    for (i = 0; i < expr->type->array_len; ++i) {
      if (i % 16 == 0)
        fprintf(out, "\t.byte ");
      else
        fprintf(out, ", ");
      fprintf(out, "%u", (unsigned)expr->d.string_literal.val[i]);
      if (i % 16 == 15 || i == expr->type->array_len - 1)
        fprintf(out, "\n");
    }
    fprintf(out, "\t.popsection\n");
    fprintf(out, "\tleaq .L.string.%d(%%rip), %%rax\n", seq);
    break;
  }
  case AST_DEREF:
    /* The pointer value is the address. */
    codegen_expr(expr->d.unary.operand);
    break;
  default:
    fprintf(stderr, "invalid address expr in codegen\n");
    exit(1);
  }
}

/*
 * Load through the address in %rax into a 64-bit destination register.
 * Signed loads sign-extend, unsigned loads zero-extend. Clobbers %r11.
 */
static void codegen_load(Type *type, const char *dst) {
  if (type->size == 8)
    fprintf(out, "\tmovq (%%rax), %s\n", dst);
  else if (type->kind == TYPE_CHAR)
    fprintf(out,
            type->is_unsigned ? "\tmovzbq (%%rax), %s\n"
                              : "\tmovsbq (%%rax), %s\n",
            dst);
  else if (type->kind == TYPE_SHORT)
    fprintf(out,
            type->is_unsigned ? "\tmovzwq (%%rax), %s\n"
                              : "\tmovswq (%%rax), %s\n",
            dst);
  else if (type->is_unsigned) {
    fprintf(out, "\tmovl (%%rax), %%r11d\n");
    fprintf(out, "\tmovq %%r11, %s\n", dst);
  } else
    fprintf(out, "\tmovslq (%%rax), %s\n", dst);
}

/*
 * Store the value in %rcx through the address in %rax. Both registers
 * are preserved so the caller can produce the expression's result.
 */
static void codegen_store(Type *type) {
  if (type->size == 8)
    fprintf(out, "\tmovq %%rcx, (%%rax)\n");
  else if (type->kind == TYPE_CHAR)
    fprintf(out, "\tmovb %%cl, (%%rax)\n");
  else if (type->kind == TYPE_SHORT)
    fprintf(out, "\tmovw %%cx, (%%rax)\n");
  else
    fprintf(out, "\tmovl %%ecx, (%%rax)\n");
}

/*
 * Normalize the value in %rax to its integer type: truncate to the type's
 * width, then sign- or zero-extend. Long, pointer and void need no change.
 */
static void codegen_normalize(Type *type) {
  if (type->kind == TYPE_CHAR)
    fprintf(out, type->is_unsigned ? "\tmovzbq %%al, %%rax\n"
                                   : "\tmovsbq %%al, %%rax\n");
  else if (type->kind == TYPE_SHORT)
    fprintf(out, type->is_unsigned ? "\tmovzwq %%ax, %%rax\n"
                                   : "\tmovswq %%ax, %%rax\n");
  else if (type->kind == TYPE_INT)
    fprintf(out, type->is_unsigned ? "\tmovl %%eax, %%eax\n" : "\tcltq\n");
}

/*
 * Test the full value in %rax using the expression's width.
 */
static void codegen_test(Type *type) {
  fprintf(out, type->size == 8 ? "\tcmpq $0, %%rax\n" : "\tcmpl $0, %%eax\n");
}

/*
 * Integer operands are already in %rax and %rcx. Width and signedness
 * select instructions; shifts always take their count from %cl.
 */
static void codegen_integer_binary(AstKind kind, Type *type) {
  bool wide = type->size == 8;
  char suffix = wide ? 'q' : 'l';
  const char *lhs = wide ? "%rax" : "%eax";
  const char *rhs = wide ? "%rcx" : "%ecx";
  const char *op;

  switch (kind) {
  case AST_ADD:
    op = "add";
    break;
  case AST_SUB:
    op = "sub";
    break;
  case AST_MUL:
    op = "imul";
    break;
  case AST_BIT_AND:
    op = "and";
    break;
  case AST_BIT_OR:
    op = "or";
    break;
  case AST_BIT_XOR:
    op = "xor";
    break;
  case AST_LSHIFT:
    op = "sal";
    rhs = "%cl";
    break;
  case AST_RSHIFT:
    op = type->is_unsigned ? "shr" : "sar";
    rhs = "%cl";
    break;
  case AST_DIV:
  case AST_MOD:
    if (type->is_unsigned) {
      fprintf(out, "\txorl %%edx, %%edx\n");
      fprintf(out, "\tdiv%c %s\n", suffix, rhs);
    } else {
      fprintf(out, wide ? "\tcqto\n" : "\tcltd\n");
      fprintf(out, "\tidiv%c %s\n", suffix, rhs);
    }
    if (kind == AST_MOD)
      fprintf(out, "\tmov%c %s, %s\n", suffix, wide ? "%rdx" : "%edx", lhs);
    return;
  default:
    fprintf(stderr, "unrecognized integer binary operator\n");
    exit(1);
  }
  fprintf(out, "\t%s%c %s, %s\n", op, suffix, rhs, lhs);
}

/* Pointer operands are in %rax and %rcx; sema has checked the pointee type. */
static void codegen_pointer_offset(AstKind kind, Type *type) {
  fprintf(out, "\timulq $%d, %%rcx\n", type->base->size);
  fprintf(out, kind == AST_PTR_ADD ? "\taddq %%rcx, %%rax\n"
                                  : "\tsubq %%rcx, %%rax\n");
}

static void codegen_expr(AstNode *expr) {
  switch (expr->kind) {
  case AST_VAR:
  case AST_DEREF:
  case AST_STRING_LITERAL:
  case AST_MEMBER:
    codegen_addr(expr);
    if (expr->type->kind != TYPE_ARRAY && expr->type->kind != TYPE_STRUCT)
      codegen_load(expr->type, "%rax");
    break;
  case AST_ADDR:
    /* The address is the value. */
    codegen_addr(expr->d.unary.operand);
    break;
  case AST_ASSIGN:
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_addr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;
    codegen_store(expr->type);
    fprintf(out, "\tmovq %%rcx, %%rax\n");
    codegen_normalize(expr->type);
    break;
  case AST_COMPOUND_ASSIGN: {
    AstKind op = expr->d.compound_assign.op;
    Type *operation_type = expr->d.compound_assign.operation_type;

    /*
     * Evaluate rhs first, as for ordinary assignment, and preserve it while
     * computing the target address. Calls in either operand may clobber
     * registers. Once the address is saved, load and update the object once.
     */
    codegen_expr(expr->d.compound_assign.rhs);
    codegen_normalize(expr->d.compound_assign.rhs->type);
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_addr(expr->d.compound_assign.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_load(expr->type, "%rax");
    codegen_normalize(operation_type);
    if (op == AST_PTR_ADD || op == AST_PTR_SUB)
      codegen_pointer_offset(op, operation_type);
    else
      codegen_integer_binary(op, operation_type);
    fprintf(out, "\tmovq %%rax, %%rcx\n");
    fprintf(out, "\tpopq %%rax\n");
    push_depth--;
    codegen_store(expr->type);
    fprintf(out, "\tmovq %%rcx, %%rax\n");
    codegen_normalize(expr->type);
    break;
  }
  case AST_CONVERT:
    codegen_expr(expr->d.unary.operand);
    if (is_integer(expr->type) &&
        expr->type->size > expr->d.unary.operand->type->size)
      codegen_normalize(expr->d.unary.operand->type);
    codegen_normalize(expr->type);
    break;
  case AST_INT_LITERAL:
    if (expr->type->size == 8)
      fprintf(out, "\tmovabsq $%" PRIu64 ", %%rax\n", expr->d.int_literal.val);
    else
      fprintf(out, "\tmovl $%" PRIu32 ", %%eax\n",
              (uint32_t)expr->d.int_literal.val);
    break;
  case AST_ADD:
  case AST_SUB:
  case AST_MUL:
  case AST_DIV:
  case AST_MOD:
  case AST_LSHIFT:
  case AST_RSHIFT:
  case AST_BIT_AND:
  case AST_BIT_XOR:
  case AST_BIT_OR:
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;
    codegen_integer_binary(expr->kind, expr->type);
    break;
  case AST_PTR_ADD:
  case AST_PTR_SUB: {
    /* Pointer +- integer, scaled by the pointee's size. */
    codegen_expr(expr->d.binary.rhs);
    codegen_normalize(expr->d.binary.rhs->type);
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;
    codegen_pointer_offset(expr->kind, expr->type);
    break;
  }
  case AST_PTR_DIFF: {
    /* Pointer difference: byte distance / pointee size. */
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;
    fprintf(out, "\tsubq %%rcx, %%rax\n");
    fprintf(out, "\tmovq $%d, %%rcx\n", expr->d.binary.lhs->type->base->size);
    fprintf(out, "\tcqto\n");
    fprintf(out, "\tidivq %%rcx\n");
    break;
  }
  case AST_LT:
  case AST_LE:
  case AST_GT:
  case AST_GE:
  case AST_EQ:
  case AST_NE: {
    bool wide = expr->d.binary.lhs->type->size == 8 ||
                expr->d.binary.rhs->type->size == 8;
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;

    fprintf(out, wide ? "\tcmpq %%rcx, %%rax\n" : "\tcmpl %%ecx, %%eax\n");
    switch (expr->kind) {
    case AST_EQ:
      fprintf(out, "\tsete %%al\n");
      break;
    case AST_NE:
      fprintf(out, "\tsetne %%al\n");
      break;
    case AST_LT:
      fprintf(out, expr->d.binary.lhs->type->is_unsigned ? "\tsetb %%al\n"
                                                         : "\tsetl %%al\n");
      break;
    case AST_LE:
      fprintf(out, expr->d.binary.lhs->type->is_unsigned ? "\tsetbe %%al\n"
                                                         : "\tsetle %%al\n");
      break;
    case AST_GT:
      fprintf(out, expr->d.binary.lhs->type->is_unsigned ? "\tseta %%al\n"
                                                         : "\tsetg %%al\n");
      break;
    case AST_GE:
      fprintf(out, expr->d.binary.lhs->type->is_unsigned ? "\tsetae %%al\n"
                                                         : "\tsetge %%al\n");
      break;
    default:
      fprintf(stderr, "unrecognized comparison operator\n");
      exit(1);
    }
    /* Zero-extend %al into %eax. */
    fprintf(out, "\tmovzbl %%al, %%eax\n");
    break;
  }
  case AST_LOGICAL_AND: {
    int seq = label_seq++;

    /* Short-circuit: skip the right operand when the left one is zero. */
    codegen_expr(expr->d.binary.lhs);
    codegen_test(expr->d.binary.lhs->type);
    fprintf(out, "\tje .L.false.%d\n", seq);
    codegen_expr(expr->d.binary.rhs);
    codegen_test(expr->d.binary.rhs->type);
    fprintf(out, "\tje .L.false.%d\n", seq);
    fprintf(out, "\tmovl $1, %%eax\n");
    fprintf(out, "\tjmp .L.end.%d\n", seq);
    fprintf(out, ".L.false.%d:\n", seq);
    fprintf(out, "\tmovl $0, %%eax\n");
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_LOGICAL_OR: {
    int seq = label_seq++;

    /* Short-circuit: skip the right operand when the left one is nonzero. */
    codegen_expr(expr->d.binary.lhs);
    codegen_test(expr->d.binary.lhs->type);
    fprintf(out, "\tjne .L.true.%d\n", seq);
    codegen_expr(expr->d.binary.rhs);
    codegen_test(expr->d.binary.rhs->type);
    fprintf(out, "\tjne .L.true.%d\n", seq);
    fprintf(out, "\tmovl $0, %%eax\n");
    fprintf(out, "\tjmp .L.end.%d\n", seq);
    fprintf(out, ".L.true.%d:\n", seq);
    fprintf(out, "\tmovl $1, %%eax\n");
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_COMMA:
    /* The left operand's value is discarded, its side effects kept. */
    codegen_expr(expr->d.binary.lhs);
    codegen_expr(expr->d.binary.rhs);
    break;
  case AST_CONDITIONAL: {
    int seq = label_seq++;

    /* Only the selected branch is evaluated. */
    codegen_expr(expr->d.conditional.cond);
    codegen_test(expr->d.conditional.cond->type);
    fprintf(out, "\tje .L.else.%d\n", seq);
    codegen_expr(expr->d.conditional.then_expr);
    fprintf(out, "\tjmp .L.end.%d\n", seq);
    fprintf(out, ".L.else.%d:\n", seq);
    codegen_expr(expr->d.conditional.else_expr);
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_CALL: {
    AstNode *arg;
    int nargs = 0;
    int nregisters;
    int nstack;
    int slots;
    int i;

    for (arg = expr->d.call.args; arg; arg = arg->next)
      nargs++;
    nregisters = nargs < 6 ? nargs : 6;
    nstack = nargs - nregisters;

    /*
     * Reserve outgoing stack arguments first, followed by temporary slots
     * for register arguments and any alignment padding. Argument seven is
     * at 0(%rsp) at the call; padding must stay above the arguments.
     * Count the entire reservation so nested calls also align their stack.
     */
    slots = nargs + (push_depth + nargs) % 2;
    if (slots)
      fprintf(out, "\tsubq $%d, %%rsp\n", slots * 8);
    push_depth += slots;

    /*
     * Preserve left-to-right evaluation. Stage all arguments in memory
     * before loading registers, since an argument may itself contain calls.
     */
    for (arg = expr->d.call.args, i = 0; arg; arg = arg->next, ++i) {
      int slot = i < 6 ? nstack + i : i - 6;
      codegen_expr(arg);
      fprintf(out, "\tmovq %%rax, %d(%%rsp)\n", slot * 8);
    }
    for (i = 0; i < nregisters; ++i)
      fprintf(out, "\tmovq %d(%%rsp), %s\n", (nstack + i) * 8, argregs64[i]);

    fprintf(out, "\tcall %.*s\n", expr->d.call.function->name.len,
            expr->d.call.function->name.start);
    if (slots)
      fprintf(out, "\taddq $%d, %%rsp\n", slots * 8);
    push_depth -= slots;
    codegen_normalize(expr->type);
    break;
  }
  case AST_NEG:
    codegen_expr(expr->d.unary.operand);
    fprintf(out, expr->type->size == 8 ? "\tnegq %%rax\n" : "\tnegl %%eax\n");
    break;
  case AST_BITWISE_NOT:
    codegen_expr(expr->d.unary.operand);
    fprintf(out, expr->type->size == 8 ? "\tnotq %%rax\n" : "\tnotl %%eax\n");
    break;
  case AST_LOGICAL_NOT:
    codegen_expr(expr->d.unary.operand);
    codegen_test(expr->d.unary.operand->type);
    /* Set %al to 1 if %eax was 0 */
    fprintf(out, "\tsete %%al\n");
    /* Zero-extend %al into %eax */
    fprintf(out, "\tmovzbl %%al, %%eax\n");
    break;
  case AST_PRE_INCREMENT:
  case AST_PRE_DECREMENT: {
    int is_ptr = expr->type->kind == TYPE_PTR;
    bool wide = expr->type->size == 8;
    /* Pointers step by the pointee's size, integers by 1. */
    int step = is_ptr ? expr->type->base->size : 1;
    codegen_addr(expr->d.unary.operand);
    codegen_load(expr->type, "%rcx");
    fprintf(out, "\t%s%c $%d, %s\n",
            expr->kind == AST_PRE_INCREMENT ? "add" : "sub", wide ? 'q' : 'l',
            step, wide ? "%rcx" : "%ecx");
    codegen_store(expr->type);
    fprintf(out, "\tmovq %%rcx, %%rax\n");
    codegen_normalize(expr->type);
    break;
  }
  case AST_POST_INCREMENT:
  case AST_POST_DECREMENT: {
    int is_ptr = expr->type->kind == TYPE_PTR;
    bool wide = expr->type->size == 8;
    int step = is_ptr ? expr->type->base->size : 1;

    codegen_addr(expr->d.unary.operand);
    codegen_load(expr->type, "%rcx");
    /* Save the original value as the result. */
    fprintf(out, "\tpushq %%rcx\n");
    push_depth++;
    fprintf(out, "\t%s%c $%d, %s\n",
            expr->kind == AST_POST_INCREMENT ? "add" : "sub", wide ? 'q' : 'l',
            step, wide ? "%rcx" : "%ecx");
    codegen_store(expr->type);
    fprintf(out, "\tpopq %%rax\n");
    push_depth--;
    break;
  }
  default:
    fprintf(stderr, "unrecognized ast node\n");
    exit(1);
  }
  return;
}

/*
 * break_label and continue_label are the label_seq values of the enclosing
 * loop's .L.end.N and .L.continue.N labels, or -1 outside any loop (the
 * parser rejects break/continue there, so -1 is defensive). ret_label is
 * this function's .Lreturn.N label: with several functions in one
 * translation unit, a fixed '.Lreturn' would be defined once per function
 * and collide.
 */
static void codegen_stmt(AstNode *stmt, int break_label, int continue_label,
                         int ret_label) {
  switch (stmt->kind) {
  case AST_RETURN:
    if (stmt->d.ret.expr)
      codegen_expr(stmt->d.ret.expr);
    fprintf(out, "\tjmp .Lreturn.%d\n", ret_label);
    break;
  case AST_BREAK:
    if (break_label < 0) {
      fprintf(stderr, "break outside loop in codegen\n");
      exit(1);
    }
    fprintf(out, "\tjmp .L.end.%d\n", break_label);
    break;
  case AST_CONTINUE:
    if (continue_label < 0) {
      fprintf(stderr, "continue outside loop in codegen\n");
      exit(1);
    }
    fprintf(out, "\tjmp .L.continue.%d\n", continue_label);
    break;
  case AST_IF: {
    int seq = label_seq++;

    /* Jump to the else branch when the condition is zero. */
    codegen_expr(stmt->d.if_stmt.cond);
    codegen_test(stmt->d.if_stmt.cond->type);
    fprintf(out, "\tje .L.else.%d\n", seq);
    codegen_stmt(stmt->d.if_stmt.then_stmt, break_label, continue_label,
                 ret_label);
    fprintf(out, "\tjmp .L.end.%d\n", seq);
    fprintf(out, ".L.else.%d:\n", seq);
    if (stmt->d.if_stmt.else_stmt)
      codegen_stmt(stmt->d.if_stmt.else_stmt, break_label, continue_label,
                   ret_label);
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_WHILE: {
    int seq = label_seq++;

    /*
     * .L.continue.N coincides with the head: 'continue' re-tests the
     * condition.
     */
    fprintf(out, ".L.begin.%d:\n", seq);
    fprintf(out, ".L.continue.%d:\n", seq);
    codegen_expr(stmt->d.while_stmt.cond);
    codegen_test(stmt->d.while_stmt.cond->type);
    fprintf(out, "\tje .L.end.%d\n", seq);
    codegen_stmt(stmt->d.while_stmt.body, seq, seq, ret_label);
    fprintf(out, "\tjmp .L.begin.%d\n", seq);
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_FOR: {
    int seq = label_seq++;

    if (stmt->d.for_stmt.init)
      codegen_stmt(stmt->d.for_stmt.init, break_label, continue_label,
                   ret_label);
    fprintf(out, ".L.begin.%d:\n", seq);
    if (stmt->d.for_stmt.cond) {
      /* An omitted condition is a nonzero constant: skip the test. */
      codegen_expr(stmt->d.for_stmt.cond);
      codegen_test(stmt->d.for_stmt.cond->type);
      fprintf(out, "\tje .L.end.%d\n", seq);
    }
    codegen_stmt(stmt->d.for_stmt.body, seq, seq, ret_label);
    /* 'continue' must skip to the post expression, not the condition. */
    fprintf(out, ".L.continue.%d:\n", seq);
    if (stmt->d.for_stmt.post)
      codegen_expr(stmt->d.for_stmt.post);
    fprintf(out, "\tjmp .L.begin.%d\n", seq);
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_DO: {
    int seq = label_seq++;

    /*
     * The test sits after the body, so the loop always runs at least once;
     * 'continue' jumps to the test and 'break' to the end label.
     */
    fprintf(out, ".L.begin.%d:\n", seq);
    codegen_stmt(stmt->d.do_stmt.body, seq, seq, ret_label);
    fprintf(out, ".L.continue.%d:\n", seq);
    codegen_expr(stmt->d.do_stmt.cond);
    codegen_test(stmt->d.do_stmt.cond->type);
    fprintf(out, "\tjne .L.begin.%d\n", seq);
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_GOTO:
    /*
     * User labels have function scope, but the assembler's symbol
     * namespace is file-wide, so the same label name in two functions
     * would collide. Prefixing with the function's ret_label number
     * keeps them distinct; the dot then also keeps them clear of the
     * generated '.L.begin.N' family.
     */
    fprintf(out, "\tjmp .L%d.%.*s\n", ret_label, stmt->d.goto_stmt.name.len,
            stmt->d.goto_stmt.name.start);
    break;
  case AST_LABEL:
    fprintf(out, ".L%d.%.*s:\n", ret_label, stmt->d.label_stmt.name.len,
            stmt->d.label_stmt.name.start);
    codegen_stmt(stmt->d.label_stmt.stmt, break_label, continue_label,
                 ret_label);
    break;
  case AST_SWITCH: {
    int seq = label_seq++;
    CaseList *cl;
    AstNode *default_node = NULL;

    /*
     * Dispatch is a chain of compares and jumps over the cases collected
     * by the parser; a jump table can replace it once case densities make
     * that worthwhile.
     */
    codegen_expr(stmt->d.switch_stmt.cond);
    for (cl = stmt->d.switch_stmt.cases; cl; cl = cl->next) {
      cl->node->d.case_stmt.label = label_seq++;
      if (cl->node->d.case_stmt.is_default) {
        default_node = cl->node;
        continue;
      }
      if (stmt->d.switch_stmt.cond->type->size == 8) {
        fprintf(out, "\tmovabsq $%" PRId64 ", %%rcx\n",
                cl->node->d.case_stmt.val);
        fprintf(out, "\tcmpq %%rcx, %%rax\n");
      } else {
        fprintf(out, "\tcmpl $%" PRId64 ", %%eax\n", cl->node->d.case_stmt.val);
      }
      fprintf(out, "\tje .L.case.%d\n", cl->node->d.case_stmt.label);
    }
    if (default_node)
      fprintf(out, "\tjmp .L.default.%d\n", default_node->d.case_stmt.label);
    fprintf(out, "\tjmp .L.end.%d\n", seq);

    /* 'break' inside the body exits this switch, not any outer loop. */
    codegen_stmt(stmt->d.switch_stmt.body, seq, continue_label, ret_label);

    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_CASE:
    if (stmt->d.case_stmt.is_default)
      fprintf(out, ".L.default.%d:\n", stmt->d.case_stmt.label);
    else
      fprintf(out, ".L.case.%d:\n", stmt->d.case_stmt.label);
    codegen_stmt(stmt->d.case_stmt.stmt, break_label, continue_label,
                 ret_label);
    break;
  case AST_DECL:
    if (stmt->d.decl.init) {
      codegen_expr(stmt->d.decl.init);
      if (stmt->d.decl.var->type->size == 8)
        fprintf(out, "\tmovq %%rax, -%d(%%rbp)\n", stmt->d.decl.var->offset);
      else if (stmt->d.decl.var->type->kind == TYPE_CHAR)
        fprintf(out, "\tmovb %%al, -%d(%%rbp)\n", stmt->d.decl.var->offset);
      else if (stmt->d.decl.var->type->kind == TYPE_SHORT)
        fprintf(out, "\tmovw %%ax, -%d(%%rbp)\n", stmt->d.decl.var->offset);
      else
        fprintf(out, "\tmovl %%eax, -%d(%%rbp)\n", stmt->d.decl.var->offset);
    }
    break;
  case AST_EXPR_STMT:
    if (stmt->d.expr_stmt.expr)
      codegen_expr(stmt->d.expr_stmt.expr);
    break;
  case AST_BLOCK: {
    AstNode *cur;
    for (cur = stmt->d.block.first_stmt; cur; cur = cur->next)
      codegen_stmt(cur, break_label, continue_label, ret_label);
    break;
  }
  default:
    fprintf(stderr, "unknown AST node\n");
    exit(1);
  }
}

void codegen(const AstNode *ast, FILE *out_file) {
  const AstNode *fn;

  out = out_file;
  fprintf(out, "\t.text\n");

  /* The translation unit is a chain of function definitions. */
  for (fn = ast; fn; fn = fn->next) {
    Token name;
    int stack_size;
    int ret_label = label_seq++;

    if (fn->kind != AST_FUNCTION) {
      fprintf(stderr, "expected function AST\n");
      exit(1);
    }

    name = fn->d.function.name;
    stack_size = (fn->d.function.stack_size + 15) / 16 * 16;
    fprintf(out, "\t.globl %.*s\n", name.len, name.start);
    fprintf(out, "%.*s:\n", name.len, name.start);
    fprintf(out, "\tpushq %%rbp\n");
    fprintf(out, "\tmovq %%rsp, %%rbp\n");
    if (stack_size)
      fprintf(out, "\tsubq $%d, %%rsp\n", stack_size);

    /*
     * Copy incoming parameters into writable local slots. The first six
     * arrive in registers; later ones start at 16(%rbp), above saved rbp
     * and the return address. The locals chain is in reverse parameter order.
     */
    if (fn->d.function.params) {
      LocalVar *param;
      int nparams = 0;

      for (param = fn->d.function.params; param; param = param->next)
        nparams++;
      for (param = fn->d.function.params; param; param = param->next) {
        --nparams;
        if (nparams < 6) {
          if (param->type->size == 8)
            fprintf(out, "\tmovq %s, -%d(%%rbp)\n", argregs64[nparams],
                    param->offset);
          else if (param->type->kind == TYPE_CHAR)
            fprintf(out, "\tmovb %s, -%d(%%rbp)\n", argregs8[nparams],
                    param->offset);
          else if (param->type->kind == TYPE_SHORT)
            fprintf(out, "\tmovw %s, -%d(%%rbp)\n", argregs16[nparams],
                    param->offset);
          else
            fprintf(out, "\tmovl %s, -%d(%%rbp)\n", argregs32[nparams],
                    param->offset);
        } else {
          int offset = 16 + (nparams - 6) * 8;
          if (param->type->size == 8) {
            fprintf(out, "\tmovq %d(%%rbp), %%rax\n", offset);
            fprintf(out, "\tmovq %%rax, -%d(%%rbp)\n", param->offset);
          } else if (param->type->kind == TYPE_CHAR) {
            fprintf(out, "\tmovb %d(%%rbp), %%al\n", offset);
            fprintf(out, "\tmovb %%al, -%d(%%rbp)\n", param->offset);
          } else if (param->type->kind == TYPE_SHORT) {
            fprintf(out, "\tmovw %d(%%rbp), %%ax\n", offset);
            fprintf(out, "\tmovw %%ax, -%d(%%rbp)\n", param->offset);
          } else {
            fprintf(out, "\tmovl %d(%%rbp), %%eax\n", offset);
            fprintf(out, "\tmovl %%eax, -%d(%%rbp)\n", param->offset);
          }
        }
      }
    }

    codegen_stmt(fn->d.function.body, -1, -1, ret_label);

    /* Reaching the end of the function returns zero. */
    fprintf(out, "\tmovl $0, %%eax\n");
    fprintf(out, ".Lreturn.%d:\n", ret_label);
    fprintf(out, "\tmovq %%rbp, %%rsp\n");
    fprintf(out, "\tpopq %%rbp\n");
    fprintf(out, "\tret\n");
  }

  fprintf(out, "\t.section .note.GNU-stack, \"\",@progbits\n");
}
