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

static void codegen_addr(AstNode *expr) {
  if (expr->type != AST_VAR) {
    fprintf(stderr, "invalid address expr in codegen\n");
    exit(1);
  }
  printf("\tleaq -%d(%%rbp), %%rax\n", expr->d.variable.var->offset);
}

static void codegen_expr(AstNode *expr) {
  switch (expr->type) {
  case AST_VAR:
    codegen_addr(expr);
    printf("\tmovl (%%rax), %%eax\n");
    break;
  case AST_ASSIGN:
    codegen_expr(expr->d.binary.rhs);
    printf("\tpushq %%rax\n");
    codegen_addr(expr->d.binary.lhs);
    printf("\tpopq %%rcx\n");
    printf("\tmovl %%ecx, (%%rax)\n");
    printf("\tmovl %%ecx, %%eax\n");
    break;
  case AST_INT_LITERAL:
    // movl $VAL, %eax
    printf("\tmovl $%" PRId64 ", %%eax\n", expr->d.int_literal.val);
    break;
  case AST_ADD:
  case AST_SUB:
  case AST_MUL:
  case AST_DIV:
  case AST_MOD:
  case AST_LSHIFT:
  case AST_RSHIFT:
    codegen_expr(expr->d.binary.rhs);
    printf("\tcltq\n");
    printf("\tpushq %%rax\n");
    codegen_expr(expr->d.binary.lhs);
    printf("\tpopq %%rcx\n");

    if (expr->type == AST_ADD) {
      // %eax = %eax + %ecx
      printf("\taddl %%ecx, %%eax\n");
    } else if (expr->type == AST_SUB) {
      // %eax = %eax - %ecx
      printf("\tsubl %%ecx, %%eax\n");
    } else if (expr->type == AST_MUL) {
      // %eax = %eax * %ecx
      printf("\timull %%ecx, %%eax\n");
    } else if (expr->type == AST_DIV || expr->type == AST_MOD) {
      // idivl divides 64-bit value in %edx:%eax by operand (%ecx)
      printf("\tcltd\n");        // Sign-extend %eax into %edx:%eax
      printf("\tidivl %%ecx\n"); // Quotient in %eax, remainder in %edx
      if (expr->type == AST_MOD)
        printf("\tmovl %%edx, %%eax\n"); // Remainder is the result
    } else if (expr->type == AST_LSHIFT) {
      // The shift count comes from %cl, the low byte of %ecx.
      printf("\tsall %%cl, %%eax\n");
    } else if (expr->type == AST_RSHIFT) {
      // Arithmetic shift: C requires sign-fill for signed '>>'.
      printf("\tsarl %%cl, %%eax\n");
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
    printf("\tcltq\n");
    printf("\tpushq %%rax\n");
    codegen_expr(expr->d.binary.lhs);
    printf("\tpopq %%rcx\n");

    // cmpl computes %eax - %ecx, i.e. lhs - rhs, and sets the flags.
    printf("\tcmpl %%ecx, %%eax\n");
    switch (expr->type) {
    case AST_EQ:
      printf("\tsete %%al\n");
      break;
    case AST_NE:
      printf("\tsetne %%al\n");
      break;
    case AST_LT:
      printf("\tsetl %%al\n");
      break;
    case AST_LE:
      printf("\tsetle %%al\n");
      break;
    case AST_GT:
      printf("\tsetg %%al\n");
      break;
    case AST_GE:
      printf("\tsetge %%al\n");
      break;
    default:
      fprintf(stderr, "unrecognized comparison operator\n");
      exit(1);
    }
    // Zero-extend %al into %eax.
    printf("\tmovzbl %%al, %%eax\n");
    break;
  case AST_NEG:
    codegen_expr(expr->d.unary.operand);
    printf("\tnegl %%eax\n");
    break;
  case AST_BITWISE_NOT:
    codegen_expr(expr->d.unary.operand);
    printf("\tnotl %%eax\n");
    break;
  case AST_LOGICAL_NOT:
    codegen_expr(expr->d.unary.operand);
    // Compare %eax to 0
    printf("\tcmpl $0, %%eax\n");
    // Set %al to 1 if %eax was 0
    printf("\tsete %%al\n");
    // Zero-extend %al into %eax
    printf("\tmovzbl %%al, %%eax\n");
    break;
  case AST_PRE_INCREMENT:
    codegen_addr(expr->d.unary.operand);
    // movl (%rax), %ecx, load
    printf("\tmovl (%%rax), %%ecx\n");
    // addl $1, %ecx, increment
    printf("\taddl $1, %%ecx\n");
    // movl %eax, (%rax), write back
    printf("\tmovl %%ecx, (%%rax)\n");
    // movl %ecx, %eax, result
    printf("\tmovl %%ecx, %%eax\n");
    break;
  case AST_POST_INCREMENT:
    codegen_addr(expr->d.unary.operand);
    // movl (%rax), %ecx, load
    printf("\tmovl (%%rax), %%ecx\n");
    // pushq %rcx, save the original val
    printf("\tpushq %%rcx\n");
    // addl $1, %ecx, increment
    printf("\taddl $1, %%ecx\n");
    // movl %ecx, (%rax), write back
    printf("\tmovl %%ecx, (%%rax)\n");
    // popq %rax, result
    printf("\tpopq %%rax\n");
    break;
  case AST_PRE_DECREMENT:
    codegen_addr(expr->d.unary.operand);
    // movl (%rax), %ecx, load
    printf("\tmovl (%%rax), %%ecx\n");
    // subl $1, %ecx, decrement
    printf("\tsubl $1, %%ecx\n");
    // movl %eax, (%rax), write back
    printf("\tmovl %%ecx, (%%rax)\n");
    // movl %ecx, %eax, result
    printf("\tmovl %%ecx, %%eax\n");
    break;
  case AST_POST_DECREMENT:
    codegen_addr(expr->d.unary.operand);
    // movl (%rax), %ecx, load
    printf("\tmovl (%%rax), %%ecx\n");
    // pushq %rcx, save the original val
    printf("\tpushq %%rcx\n");
    // subl $1, %ecx, decrement
    printf("\tsubl $1, %%ecx\n");
    // movl %ecx, (%rax), write back
    printf("\tmovl %%ecx, (%%rax)\n");
    // popq %rax, result
    printf("\tpopq %%rax\n");
    break;
  default:
    fprintf(stderr, "unrecognized ast node\n");
    exit(1);
  }
  return;
}

static void codegen_stmt(AstNode *stmt) {
  switch (stmt->type) {
  case AST_RETURN:
    codegen_expr(stmt->d.ret.expr);
    printf("\tjmp .Lreturn\n");
    break;
  case AST_DECL:
    if (stmt->d.decl.init) {
      codegen_expr(stmt->d.decl.init);
      printf("\tmovl %%eax, -%d(%%rbp)\n", stmt->d.decl.var->offset);
    }
    break;
  case AST_EXPR_STMT:
    if (stmt->d.expr_stmt.expr)
      codegen_expr(stmt->d.expr_stmt.expr);
    break;
  case AST_BLOCK: {
    AstNode *cur;
    for (cur = stmt->d.block.first_stmt; cur; cur = cur->next)
      codegen_stmt(cur);
    break;
  }
  default:
    fprintf(stderr, "unknown AST node\n");
    exit(1);
  }
}

void codegen(AstNode *ast) {
  Token name;
  int stack_size;

  if (ast->type != AST_FUNCTION) {
    fprintf(stderr, "expected function AST\n");
    exit(1);
  }

  name = ast->d.function.name;
  stack_size = (ast->d.function.stack_size + 15) / 16 * 16;
  printf("\t.text\n");
  printf("\t.globl %.*s\n", name.len, name.start);
  printf("%.*s:\n", name.len, name.start);
  printf("\tpushq %%rbp\n");
  printf("\tmovq %%rsp, %%rbp\n");
  if (stack_size)
    printf("\tsubq $%d, %%rsp\n", stack_size);
  codegen_stmt(ast->d.function.body);

  // Reaching the end of main returns zero.
  printf("\tmovl $0, %%eax\n");
  printf(".Lreturn:\n");
  printf("\tmovq %%rbp, %%rsp\n");
  printf("\tpopq %%rbp\n");
  printf("\tret\n");
  printf("\t.section .note.GNU-stack, \"\",@progbits\n");
}
