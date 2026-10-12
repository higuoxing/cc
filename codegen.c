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

#include "codegen.h"

static void codegen_stmt(AstNode *ast) {
  switch (ast->type) {
  case AST_RETURN:
    if (ast->d.ret.expr)
      printf("	movl $%d, %%eax\n", (int)ast->d.ret.expr->d.int_literal.val);
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
