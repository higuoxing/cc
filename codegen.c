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

static void codegen_expr(AstNode *expr) {
  switch (expr->type) {
  case AST_INT_LITERAL:
    // movl $VAL, %eax
    printf("	movl $%" PRId64 ", %%eax\n", expr->d.int_literal.val);
    break;
  case AST_ADD:
  case AST_SUB:
  case AST_MUL:
  case AST_DIV:
    codegen_expr(expr->d.binary.rhs);
    printf("	cltq\n");
    printf("	pushq %%rax\n");
    codegen_expr(expr->d.binary.lhs);
    printf("	popq %%rcx\n");

    if (expr->type == AST_ADD) {
      // %eax = %eax + %ecx
      printf("	addl %%ecx, %%eax\n");
    } else if (expr->type == AST_SUB) {
      // %eax = %eax - %ecx
      printf("	subl %%ecx, %%eax\n");
    } else if (expr->type == AST_MUL) {
      // %eax = %eax * %ecx
      printf("	imull %%ecx, %%eax\n");
    } else if (expr->type == AST_DIV) {
      // idivl divides 64-bit value in %edx:%eax by operand (%ecx)
      printf("	cltd\n");        // Sign-extend %eax into %edx:%eax
      printf("	idivl %%ecx\n"); // Quotient in %eax, remainder in %edx
    } else {
      fprintf(stderr, "unrecognized binary operator\n");
      exit(1);
    }
    break;
  case AST_NEG:
    codegen_expr(expr->d.unary.operand);
    printf("	negl %%eax\n");
    break;
  case AST_BITWISE_NOT:
    codegen_expr(expr->d.unary.operand);
    printf("	notl %%eax\n");
    break;
  case AST_LOGICAL_NOT:
    codegen_expr(expr->d.unary.operand);
    // Compare %eax to 0
    printf("	cmpl $0, %%eax\n");
    // Set %al to 1 if %eax was 0
    printf("	sete %%al\n");
    // Zero-extend %al into %eax
    printf("	movzbl %%al, %%eax\n");
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
    if (stmt->d.ret.expr)
      codegen_expr(stmt->d.ret.expr);
    else
      printf("	movl $0, %%eax\n");
    printf("	ret\n");
    break;
  default:
    fprintf(stderr, "unknown AST node\n");
    exit(1);
  }
}

void codegen(AstNode *ast) {
  printf("	.globl main\n");
  printf("main:\n");

  codegen_stmt(ast);
}
