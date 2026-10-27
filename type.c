/*
 * Types for the self-hosted C compiler
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

#include <stdlib.h>

#include "type.h"

Type builtin_type_char = {.kind = TYPE_CHAR, .size = 1, .base = NULL};
Type builtin_type_unsigned_char = {
    .kind = TYPE_CHAR, .size = 1, .is_unsigned = true, .base = NULL};
Type builtin_type_int = {.kind = TYPE_INT, .size = 4, .base = NULL};
Type builtin_type_unsigned = {
    .kind = TYPE_INT, .size = 4, .is_unsigned = true, .base = NULL};
Type builtin_type_void = {.kind = TYPE_VOID, .size = 0, .base = NULL};

bool is_integer(const Type *type) {
  return type->kind == TYPE_CHAR || type->kind == TYPE_INT;
}

/*
 * Declaration compatibility is structural: separately parsed pointer types
 * need not share an address. A void pointer is convertible, not identical,
 * to a pointer to another object type.
 */
bool same_type(const Type *a, const Type *b) {
  if (a->kind != b->kind)
    return false;
  if (is_integer(a) && a->is_unsigned != b->is_unsigned)
    return false;
  return a->kind != TYPE_PTR || same_type(a->base, b->base);
}

bool compatible_pointers(const Type *a, const Type *b) {
  return a->kind == TYPE_PTR && b->kind == TYPE_PTR &&
         (same_type(a, b) || a->base->kind == TYPE_VOID ||
          b->base->kind == TYPE_VOID);
}

/*
 * Sizes are stored in Type: an array's or struct's size is data, not
 * derivable from its kind. Void must never reach this.
 */
int type_size(const Type *type) { return type->size; }

Type *pointer_to(Type *base) {
  Type *type;
  type = (Type *)calloc(1, sizeof(Type));
  if (!type)
    return NULL;
  type->kind = TYPE_PTR;
  type->size = 8;
  type->base = base;
  return type;
}
