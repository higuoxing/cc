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
 * Assembly is emitted here; set by codegen() so that helpers need no FILE*
 * parameter (GCC's asm_out_file).
 */
static FILE *out;

static void codegen_addr(AstNode *expr) {
  if (expr->type != AST_VAR) {
    fprintf(stderr, "invalid address expr in codegen\n");
    exit(1);
  }
  fprintf(out, "\tleaq -%d(%%rbp), %%rax\n", expr->d.variable.var->offset);
}

static void codegen_expr(AstNode *expr) {
  switch (expr->type) {
  case AST_VAR:
    codegen_addr(expr);
    fprintf(out, "\tmovl (%%rax), %%eax\n");
    break;
  case AST_ASSIGN:
    codegen_expr(expr->d.binary.rhs);
    fprintf(out, "\tpushq %%rax\n");
    codegen_addr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");
    fprintf(out, "\tmovl %%ecx, (%%rax)\n");
    fprintf(out, "\tmovl %%ecx, %%eax\n");
    break;
  case AST_INT_LITERAL:
    /* movl $VAL, %eax */
    fprintf(out, "\tmovl $%" PRId64 ", %%eax\n", expr->d.int_literal.val);
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
    fprintf(out, "\tcltq\n");
    fprintf(out, "\tpushq %%rax\n");
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");

    if (expr->type == AST_ADD) {
      /* %eax = %eax + %ecx */
      fprintf(out, "\taddl %%ecx, %%eax\n");
    } else if (expr->type == AST_SUB) {
      /* %eax = %eax - %ecx */
      fprintf(out, "\tsubl %%ecx, %%eax\n");
    } else if (expr->type == AST_MUL) {
      /* %eax = %eax * %ecx */
      fprintf(out, "\timull %%ecx, %%eax\n");
    } else if (expr->type == AST_DIV || expr->type == AST_MOD) {
      /* idivl divides 64-bit value in %edx:%eax by operand (%ecx) */
      fprintf(out, "\tcltd\n");        /* Sign-extend %eax into %edx:%eax */
      fprintf(out, "\tidivl %%ecx\n"); /* Quotient in %eax, remainder in %edx */
      if (expr->type == AST_MOD)
        fprintf(out, "\tmovl %%edx, %%eax\n"); /* Remainder is the result */
    } else if (expr->type == AST_LSHIFT) {
      /* The shift count comes from %cl, the low byte of %ecx. */
      fprintf(out, "\tsall %%cl, %%eax\n");
    } else if (expr->type == AST_RSHIFT) {
      /* Arithmetic shift: C requires sign-fill for signed '>>'. */
      fprintf(out, "\tsarl %%cl, %%eax\n");
    } else if (expr->type == AST_BIT_AND) {
      fprintf(out, "\tandl %%ecx, %%eax\n");
    } else if (expr->type == AST_BIT_XOR) {
      fprintf(out, "\txorl %%ecx, %%eax\n");
    } else if (expr->type == AST_BIT_OR) {
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
    codegen_expr(expr->d.binary.lhs);
    fprintf(out, "\tpopq %%rcx\n");

    /* cmpl computes %eax - %ecx, i.e. lhs - rhs, and sets the flags. */
    fprintf(out, "\tcmpl %%ecx, %%eax\n");
    switch (expr->type) {
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
    codegen_addr(expr->d.unary.operand);
    /* movl (%rax), %ecx, load */
    fprintf(out, "\tmovl (%%rax), %%ecx\n");
    /* addl $1, %ecx, increment */
    fprintf(out, "\taddl $1, %%ecx\n");
    /* movl %eax, (%rax), write back */
    fprintf(out, "\tmovl %%ecx, (%%rax)\n");
    /* movl %ecx, %eax, result */
    fprintf(out, "\tmovl %%ecx, %%eax\n");
    break;
  case AST_POST_INCREMENT:
    codegen_addr(expr->d.unary.operand);
    /* movl (%rax), %ecx, load */
    fprintf(out, "\tmovl (%%rax), %%ecx\n");
    /* pushq %rcx, save the original val */
    fprintf(out, "\tpushq %%rcx\n");
    /* addl $1, %ecx, increment */
    fprintf(out, "\taddl $1, %%ecx\n");
    /* movl %ecx, (%rax), write back */
    fprintf(out, "\tmovl %%ecx, (%%rax)\n");
    /* popq %rax, result */
    fprintf(out, "\tpopq %%rax\n");
    break;
  case AST_PRE_DECREMENT:
    codegen_addr(expr->d.unary.operand);
    /* movl (%rax), %ecx, load */
    fprintf(out, "\tmovl (%%rax), %%ecx\n");
    /* subl $1, %ecx, decrement */
    fprintf(out, "\tsubl $1, %%ecx\n");
    /* movl %eax, (%rax), write back */
    fprintf(out, "\tmovl %%ecx, (%%rax)\n");
    /* movl %ecx, %eax, result */
    fprintf(out, "\tmovl %%ecx, %%eax\n");
    break;
  case AST_POST_DECREMENT:
    codegen_addr(expr->d.unary.operand);
    /* movl (%rax), %ecx, load */
    fprintf(out, "\tmovl (%%rax), %%ecx\n");
    /* pushq %rcx, save the original val */
    fprintf(out, "\tpushq %%rcx\n");
    /* subl $1, %ecx, decrement */
    fprintf(out, "\tsubl $1, %%ecx\n");
    /* movl %ecx, (%rax), write back */
    fprintf(out, "\tmovl %%ecx, (%%rax)\n");
    /* popq %rax, result */
    fprintf(out, "\tpopq %%rax\n");
    break;
  default:
    fprintf(stderr, "unrecognized ast node\n");
    exit(1);
  }
  return;
}

/*
 * break_label and continue_label are the label_seq values of the enclosing
 * loop's .L.end.N and .L.continue.N labels, or -1 outside any loop (the
 * parser rejects break/continue there, so -1 is defensive).
 */
static void codegen_stmt(AstNode *stmt, int break_label, int continue_label) {
  switch (stmt->type) {
  case AST_RETURN:
    codegen_expr(stmt->d.ret.expr);
    fprintf(out, "\tjmp .Lreturn\n");
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
    codegen_stmt(stmt->d.if_stmt.then_stmt, break_label, continue_label);
    fprintf(out, "\tjmp .L.end.%d\n", seq);
    fprintf(out, ".L.else.%d:\n", seq);
    if (stmt->d.if_stmt.else_stmt)
      codegen_stmt(stmt->d.if_stmt.else_stmt, break_label, continue_label);
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
    codegen_stmt(stmt->d.while_stmt.body, seq, seq);
    fprintf(out, "\tjmp .L.begin.%d\n", seq);
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_FOR: {
    int seq = label_seq++;

    if (stmt->d.for_stmt.init)
      codegen_stmt(stmt->d.for_stmt.init, break_label, continue_label);
    fprintf(out, ".L.begin.%d:\n", seq);
    if (stmt->d.for_stmt.cond) {
      /* An omitted condition is a nonzero constant: skip the test. */
      codegen_expr(stmt->d.for_stmt.cond);
      fprintf(out, "\tcmpl $0, %%eax\n");
      fprintf(out, "\tje .L.end.%d\n", seq);
    }
    codegen_stmt(stmt->d.for_stmt.body, seq, seq);
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
    codegen_stmt(stmt->d.do_stmt.body, seq, seq);
    fprintf(out, ".L.continue.%d:\n", seq);
    codegen_expr(stmt->d.do_stmt.cond);
    fprintf(out, "\tcmpl $0, %%eax\n");
    fprintf(out, "\tjne .L.begin.%d\n", seq);
    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_GOTO:
    /*
     * User labels are C identifiers, so '.L<name>' cannot collide with
     * the generated '.L.begin.N' family (identifiers contain no dots).
     */
    fprintf(out, "\tjmp .L%.*s\n", stmt->d.goto_stmt.name.len,
            stmt->d.goto_stmt.name.start);
    break;
  case AST_LABEL:
    fprintf(out, ".L%.*s:\n", stmt->d.label_stmt.name.len,
            stmt->d.label_stmt.name.start);
    codegen_stmt(stmt->d.label_stmt.stmt, break_label, continue_label);
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
    codegen_stmt(stmt->d.switch_stmt.body, seq, continue_label);

    fprintf(out, ".L.end.%d:\n", seq);
    break;
  }
  case AST_CASE:
    if (stmt->d.case_stmt.is_default)
      fprintf(out, ".L.default.%d:\n", stmt->d.case_stmt.label);
    else
      fprintf(out, ".L.case.%d:\n", stmt->d.case_stmt.label);
    codegen_stmt(stmt->d.case_stmt.stmt, break_label, continue_label);
    break;
  case AST_DECL:
    if (stmt->d.decl.init) {
      codegen_expr(stmt->d.decl.init);
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
      codegen_stmt(cur, break_label, continue_label);
    break;
  }
  default:
    fprintf(stderr, "unknown AST node\n");
    exit(1);
  }
}

void codegen(AstNode *ast, FILE *out_file) {
  Token name;
  int stack_size;

  if (ast->type != AST_FUNCTION) {
    fprintf(stderr, "expected function AST\n");
    exit(1);
  }

  out = out_file;
  name = ast->d.function.name;
  stack_size = (ast->d.function.stack_size + 15) / 16 * 16;
  fprintf(out, "\t.text\n");
  fprintf(out, "\t.globl %.*s\n", name.len, name.start);
  fprintf(out, "%.*s:\n", name.len, name.start);
  fprintf(out, "\tpushq %%rbp\n");
  fprintf(out, "\tmovq %%rsp, %%rbp\n");
  if (stack_size)
    fprintf(out, "\tsubq $%d, %%rsp\n", stack_size);
  codegen_stmt(ast->d.function.body, -1, -1);

  /* Reaching the end of main returns zero. */
  fprintf(out, "\tmovl $0, %%eax\n");
  fprintf(out, ".Lreturn:\n");
  fprintf(out, "\tmovq %%rbp, %%rsp\n");
  fprintf(out, "\tpopq %%rbp\n");
  fprintf(out, "\tret\n");
  fprintf(out, "\t.section .note.GNU-stack, \"\",@progbits\n");
}
