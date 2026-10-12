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

#include <stdio.h>
#include <inttypes.h>

#include "codegen.h"
#include "parser.h"

static void codegen_expr(AstNode *expr) {
  if (expr->type == AST_INT_LITERAL) {
    // movl $VAL, %eax
    printf("	movl $%" PRId64 ", %%eax\n", expr->d.int_literal.val);
    return;
  }

  if (expr->type == AST_ADD) {
    // Evaluate RHS. Result is stored in %eax
    codegen_expr(expr->d.add.rhs);

    // Push RHS onto stack (covert 32-bit %eax to 64-bit %rax as we don't pushl instruction)
    printf("	cltq\n");
    printf("	pushq %%rax\n");

    // Evaluate LHS. Result is stored in %eax
    codegen_expr(expr->d.add.lhs);

    // Pop saved RHS into %rcx
    printf("	popq %%rcx\n");

    // Add RHS (%ecx) to LHS (%eax)
    printf("	addl %%ecx, %%eax\n");
    return;
  }

  fprintf(stderr, "unknown ast node type in codegen\n");
  exit(1);
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
