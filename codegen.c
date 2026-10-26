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
 * in their 64-bit and 32-bit spellings.
 */
static const char *argregs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
static const char *argregs32[] = {"%edi", "%esi", "%edx",
                                  "%ecx", "%r8d", "%r9d"};

static void codegen_expr(AstNode *expr);

static void codegen_addr(AstNode *expr) {
  switch (expr->kind) {
  case AST_VAR:
    fprintf(out, "\tleaq -%d(%%rbp), %%rax\n", expr->d.variable.var->offset);
    break;
  case AST_DEREF:
    /* The pointer value is the address. */
    codegen_expr(expr->d.unary.operand);
    break;
  default:
    fprintf(stderr, "invalid address expr in codegen\n");
    exit(1);
  }
}

static void codegen_expr(AstNode *expr) {
  switch (expr->kind) {
  case AST_VAR:
    codegen_addr(expr);
    if (expr->type->kind == TYPE_PTR)
      fprintf(out, "\tmovq (%%rax), %%rax\n");
    else
      fprintf(out, "\tmovl (%%rax), %%eax\n");
    break;
  case AST_ADDR:
    /* The address is the value. */
    codegen_addr(expr->d.unary.operand);
    break;
  case AST_DEREF:
    codegen_expr(expr->d.unary.operand);
    if (expr->type->kind == TYPE_PTR)
      fprintf(out, "\tmovq (%%rax), %%rax\n");
    else
      fprintf(out, "\tmovl (%%rax), %%eax\n");
    break;
  case AST_ASSIGN:
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_addr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;
    if (expr->type->kind == TYPE_PTR) {
      fprintf(out, "\tmovq %%rcx, (%%rax)\n");
      fprintf(out, "\tmovq %%rcx, %%rax\n");
    } else {
      fprintf(out, "\tmovl %%ecx, (%%rax)\n");
      fprintf(out, "\tmovl %%ecx, %%eax\n");
    }
    break;
  case AST_INT_LITERAL:
    /* movl $VAL, %eax */
    fprintf(out, "\tmovl $%" PRId64 ", %%eax\n", expr->d.int_literal.val);
    break;
  case AST_ADD:
  case AST_SUB:
    /*
     * int +- int: no type checks needed, the parser classified the
     * operation already.
     */
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tcltq\n");
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;
    fprintf(out, expr->kind == AST_ADD ? "\taddl %%ecx, %%eax\n"
                                       : "\tsubl %%ecx, %%eax\n");
    break;
  case AST_PTR_ADD:
  case AST_PTR_SUB: {
    /* Pointer +- integer, scaled by the pointee's size. */
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tcltq\n");
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;
    fprintf(out, "\timulq $%d, %%rcx\n", expr->type->base->size);
    fprintf(out, expr->kind == AST_PTR_ADD ? "\taddq %%rcx, %%rax\n"
                                           : "\tsubq %%rcx, %%rax\n");
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
  case AST_MUL:
  case AST_DIV:
  case AST_MOD:
  case AST_LSHIFT:
  case AST_RSHIFT:
  case AST_BIT_AND:
  case AST_BIT_XOR:
  case AST_BIT_OR:
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tcltq\n");
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;

    if (expr->kind == AST_MUL) {
      /* %eax = %eax * %ecx */
      fprintf(out, "\timull %%ecx, %%eax\n");
    } else if (expr->kind == AST_DIV || expr->kind == AST_MOD) {
      /* idivl divides 64-bit value in %edx:%eax by operand (%ecx) */
      fprintf(out, "\tcltd\n");        /* Sign-extend %eax into %edx:%eax */
      fprintf(out, "\tidivl %%ecx\n"); /* Quotient in %eax, remainder in %edx */
      if (expr->kind == AST_MOD)
        fprintf(out, "\tmovl %%edx, %%eax\n"); /* Remainder is the result */
    } else if (expr->kind == AST_LSHIFT) {
      /* The shift count comes from %cl, the low byte of %ecx. */
      fprintf(out, "\tsall %%cl, %%eax\n");
    } else if (expr->kind == AST_RSHIFT) {
      /* Arithmetic shift: C requires sign-fill for signed '>>'. */
      fprintf(out, "\tsarl %%cl, %%eax\n");
    } else if (expr->kind == AST_BIT_AND) {
      fprintf(out, "\tandl %%ecx, %%eax\n");
    } else if (expr->kind == AST_BIT_XOR) {
      fprintf(out, "\txorl %%ecx, %%eax\n");
    } else if (expr->kind == AST_BIT_OR) {
      fprintf(out, "\torl %%ecx, %%eax\n");
    } else {
      fprintf(stderr, "unrecognized binary operator\n");
      exit(1);
    }
    break;
  case AST_LT:
  case AST_LE:
  case AST_GT:
  case AST_GE:
  case AST_EQ:
  case AST_NE:
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tcltq\n");
    fprintf(out, "\tpushq %%rax\n");
    push_depth++;
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    push_depth--;

    /* cmpl computes %eax - %ecx, i.e. lhs - rhs, and sets the flags. */
    fprintf(out, "\tcmpl %%ecx, %%eax\n");
    switch (expr->kind) {
    case AST_EQ:
      fprintf(out, "\tsete %%al\n");
      break;
    case AST_NE:
      fprintf(out, "\tsetne %%al\n");
      break;
    case AST_LT:
      fprintf(out, "\tsetl %%al\n");
      break;
    case AST_LE:
      fprintf(out, "\tsetle %%al\n");
      break;
    case AST_GT:
      fprintf(out, "\tsetg %%al\n");
      break;
    case AST_GE:
      fprintf(out, "\tsetge %%al\n");
      break;
    default:
      fprintf(stderr, "unrecognized comparison operator\n");
      exit(1);
    }
    /* Zero-extend %al into %eax. */
    fprintf(out, "\tmovzbl %%al, %%eax\n");
    break;
  case AST_LOGICAL_AND: {
    int seq = label_seq++;

    /* Short-circuit: skip the right operand when the left one is zero. */
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tcmpl $0, %%eax\n");
    fprintf(out, "\tje .L.false.%d\n", seq);
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tcmpl $0, %%eax\n");
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
    fprintf(out, "\tcmpl $0, %%eax\n");
    fprintf(out, "\tjne .L.true.%d\n", seq);
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tcmpl $0, %%eax\n");
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
    fprintf(out, "\tcmpl $0, %%eax\n");
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
      fprintf(out, "\tmovq %d(%%rsp), %s\n", (nstack + i) * 8, argregs[i]);

    fprintf(out, "\tcall %.*s\n", expr->d.call.function->name.len,
            expr->d.call.function->name.start);
    if (slots)
      fprintf(out, "\taddq $%d, %%rsp\n", slots * 8);
    push_depth -= slots;
    break;
  }
  case AST_NEG:
    codegen_expr(expr->d.unary.operand);
    fprintf(out, "\tnegl %%eax\n");
    break;
  case AST_BITWISE_NOT:
    codegen_expr(expr->d.unary.operand);
    fprintf(out, "\tnotl %%eax\n");
    break;
  case AST_LOGICAL_NOT:
    codegen_expr(expr->d.unary.operand);
    /* Compare %eax to 0 */
    fprintf(out, "\tcmpl $0, %%eax\n");
    /* Set %al to 1 if %eax was 0 */
    fprintf(out, "\tsete %%al\n");
    /* Zero-extend %al into %eax */
    fprintf(out, "\tmovzbl %%al, %%eax\n");
    break;
  case AST_PRE_INCREMENT:
  case AST_PRE_DECREMENT: {
    int is_ptr = expr->type->kind == TYPE_PTR;
    /* Pointers step by the pointee's size, ints by 1. */
    int step = is_ptr ? expr->type->base->size : 1;
    const char *load =
        is_ptr ? "\tmovq (%rax), %rcx\n" : "\tmovl (%rax), %ecx\n";
    const char *store =
        is_ptr ? "\tmovq %rcx, (%rax)\n" : "\tmovl %ecx, (%rax)\n";
    const char *result = is_ptr ? "\tmovq %rcx, %rax\n" : "\tmovl %ecx, %eax\n";

    codegen_addr(expr->d.unary.operand);
    fprintf(out, "%s", load);
    fprintf(out, "\t%s $%d, %s\n",
            expr->kind == AST_PRE_INCREMENT ? (is_ptr ? "addq" : "addl")
                                            : (is_ptr ? "subq" : "subl"),
            step, is_ptr ? "%rcx" : "%ecx");
    fprintf(out, "%s", store);
    fprintf(out, "%s", result);
    break;
  }
  case AST_POST_INCREMENT:
  case AST_POST_DECREMENT: {
    int is_ptr = expr->type->kind == TYPE_PTR;
    int step = is_ptr ? expr->type->base->size : 1;
    const char *load =
        is_ptr ? "\tmovq (%rax), %rcx\n" : "\tmovl (%rax), %ecx\n";
    const char *store =
        is_ptr ? "\tmovq %rcx, (%rax)\n" : "\tmovl %ecx, (%rax)\n";

    codegen_addr(expr->d.unary.operand);
    fprintf(out, "%s", load);
    /* Save the original value as the result. */
    fprintf(out, "\tpushq %%rcx\n");
    push_depth++;
    fprintf(out, "\t%s $%d, %s\n",
            expr->kind == AST_POST_INCREMENT ? (is_ptr ? "addq" : "addl")
                                             : (is_ptr ? "subq" : "subl"),
            step, is_ptr ? "%rcx" : "%ecx");
    fprintf(out, "%s", store);
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
    fprintf(out, "\tcmpl $0, %%eax\n");
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
    fprintf(out, "\tcmpl $0, %%eax\n");
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
      fprintf(out, "\tcmpl $0, %%eax\n");
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
    fprintf(out, "\tcmpl $0, %%eax\n");
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
      fprintf(out, "\tcmpl $%" PRId64 ", %%eax\n", cl->node->d.case_stmt.val);
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
      if (stmt->d.decl.var->type->kind == TYPE_PTR)
        fprintf(out, "\tmovq %%rax, -%d(%%rbp)\n", stmt->d.decl.var->offset);
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
          if (param->type->kind == TYPE_PTR)
            fprintf(out, "\tmovq %s, -%d(%%rbp)\n", argregs[nparams],
                    param->offset);
          else
            fprintf(out, "\tmovl %s, -%d(%%rbp)\n", argregs32[nparams],
                    param->offset);
        } else {
          int offset = 16 + (nparams - 6) * 8;
          if (param->type->kind == TYPE_PTR) {
            fprintf(out, "\tmovq %d(%%rbp), %%rax\n", offset);
            fprintf(out, "\tmovq %%rax, -%d(%%rbp)\n", param->offset);
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
