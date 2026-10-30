/*
 * Stack layout for the self-hosted C compiler
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "layout.h"
#include "type.h"

typedef struct {
  const char *filename;
  int size;
} StackLayout;

static void layout_error(StackLayout *layout, Token loc, const char *message)
    __attribute__((noreturn));

static void layout_error(StackLayout *layout, Token loc, const char *message) {
  fprintf(stderr, "%s:%d:%d: error: %s\n", layout->filename, loc.line, loc.col,
          message);
  exit(1);
}

static void layout_variable(StackLayout *layout, Variable *var) {
  int size = type_size(var->type);
  int alignment = type_alignment(var->type);
  int padding = (alignment - layout->size % alignment) % alignment;
  /*
   * Leave room for codegen to round the frame size to a multiple of 16.
   */
  if (size > INT_MAX - 15 - layout->size - padding)
    layout_error(layout, var->name,
                 "local variables exceed supported stack size");
  layout->size += padding + size;
  var->offset = layout->size;
}

/*
 * Only statement nodes can contain local declarations in our C subset.
 * Case-list entries refer to nodes already reached through the switch body.
 */
static void layout_stmt(StackLayout *layout, AstNode *stmt) {
  if (!stmt)
    return;
  switch (stmt->kind) {
  case AST_DECL:
    if (stmt->d.decl.var)
      layout_variable(layout, stmt->d.decl.var);
    break;
  case AST_BLOCK: {
    AstNode *cur;
    for (cur = stmt->d.block.first_stmt; cur; cur = cur->next)
      layout_stmt(layout, cur);
    break;
  }
  case AST_IF:
    layout_stmt(layout, stmt->d.if_stmt.then_stmt);
    layout_stmt(layout, stmt->d.if_stmt.else_stmt);
    break;
  case AST_WHILE:
    layout_stmt(layout, stmt->d.while_stmt.body);
    break;
  case AST_FOR:
    layout_stmt(layout, stmt->d.for_stmt.init);
    layout_stmt(layout, stmt->d.for_stmt.body);
    break;
  case AST_DO:
    layout_stmt(layout, stmt->d.do_stmt.body);
    break;
  case AST_SWITCH:
    layout_stmt(layout, stmt->d.switch_stmt.body);
    break;
  case AST_CASE:
    layout_stmt(layout, stmt->d.case_stmt.stmt);
    break;
  case AST_LABEL:
    layout_stmt(layout, stmt->d.label_stmt.stmt);
    break;
  default:
    break;
  }
}

void assign_stack_offsets(const char *filename, AstNode *ast) {
  AstNode *fn;
  for (fn = ast; fn; fn = fn->next) {
    Variable *param;
    int nparams = 0;
    StackLayout layout = {.filename = filename};
    if (fn->kind != AST_FUNCTION)
      continue;

    /*
     * Keep an eight-byte home for each parameter in declaration order.
     * The list is reversed; its first entry receives the highest offset.
     */
    for (param = fn->d.function.params; param; param = param->next) {
      if (nparams >= (INT_MAX - 15) / 8)
        layout_error(&layout, param->name, "too many function parameters");
      ++nparams;
    }
    layout.size = 8 * nparams;
    for (param = fn->d.function.params; param; param = param->next)
      param->offset = 8 * nparams--;

    layout_stmt(&layout, fn->d.function.body);
    fn->d.function.stack_size = layout.size;
  }
}
