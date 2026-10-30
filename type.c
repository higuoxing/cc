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
Type builtin_type_short = {.kind = TYPE_SHORT, .size = 2, .base = NULL};
Type builtin_type_unsigned_short = {
    .kind = TYPE_SHORT, .size = 2, .is_unsigned = true, .base = NULL};
Type builtin_type_int = {.kind = TYPE_INT, .size = 4, .base = NULL};
Type builtin_type_unsigned = {
    .kind = TYPE_INT, .size = 4, .is_unsigned = true, .base = NULL};
Type builtin_type_long = {.kind = TYPE_LONG, .size = 8, .base = NULL};
Type builtin_type_unsigned_long = {
    .kind = TYPE_LONG, .size = 8, .is_unsigned = true, .base = NULL};
Type builtin_type_long_long = {.kind = TYPE_LONG_LONG, .size = 8, .base = NULL};
Type builtin_type_unsigned_long_long = {
    .kind = TYPE_LONG_LONG, .size = 8, .is_unsigned = true, .base = NULL};
Type builtin_type_void = {.kind = TYPE_VOID, .size = 0, .base = NULL};

bool is_integer(const Type *type) {
  return type->kind == TYPE_CHAR || type->kind == TYPE_SHORT ||
         type->kind == TYPE_INT || type->kind == TYPE_LONG ||
         type->kind == TYPE_LONG_LONG || type->kind == TYPE_ENUM;
}

/*
 * On our target, int represents every char and short value, including
 * unsigned char and unsigned short. Enums use int as their compatible type
 * and also promote to int. Other types remain unchanged.
 */
Type *integer_promotion_type(Type *type) {
  if (type->kind == TYPE_CHAR || type->kind == TYPE_SHORT ||
      type->kind == TYPE_ENUM)
    return &builtin_type_int;
  return type;
}

/*
 * Common type after integer promotion on our LP64 target. Signed long
 * represents every unsigned int value. Long long has higher rank than long,
 * but the same width, so mixing it with unsigned long yields unsigned long
 * long.
 */
Type *common_integer_type(const Type *a, const Type *b) {
  if (a->kind == TYPE_LONG_LONG || b->kind == TYPE_LONG_LONG) {
    if ((a->kind == TYPE_LONG_LONG && a->is_unsigned) ||
        (b->kind == TYPE_LONG_LONG && b->is_unsigned) ||
        (a->kind == TYPE_LONG && a->is_unsigned) ||
        (b->kind == TYPE_LONG && b->is_unsigned))
      return &builtin_type_unsigned_long_long;
    return &builtin_type_long_long;
  }
  if (a->kind == TYPE_LONG || b->kind == TYPE_LONG) {
    if ((a->kind == TYPE_LONG && a->is_unsigned) ||
        (b->kind == TYPE_LONG && b->is_unsigned))
      return &builtin_type_unsigned_long;
    return &builtin_type_long;
  }
  return a->is_unsigned || b->is_unsigned ? &builtin_type_unsigned
                                          : &builtin_type_int;
}

/*
 * Structs and enums have distinct identities. An enum is also compatible with
 * its chosen integer type. Other supported types compare structurally.
 * A void pointer is convertible, not identical, to another object pointer.
 */
bool same_type(const Type *a, const Type *b) {
  if (a->kind == TYPE_ENUM && b->kind != TYPE_ENUM)
    return same_type(a->base, b);
  if (b->kind == TYPE_ENUM && a->kind != TYPE_ENUM)
    return same_type(a, b->base);
  if (a->kind != b->kind)
    return false;
  if (a->kind == TYPE_STRUCT || a->kind == TYPE_ENUM)
    return a == b;
  if (is_integer(a) && a->is_unsigned != b->is_unsigned)
    return false;
  if (a->kind == TYPE_ARRAY)
    return a->array_len == b->array_len && same_type(a->base, b->base);
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

int type_alignment(const Type *type) {
  if (type->kind == TYPE_STRUCT)
    return type->alignment;
  return type->kind == TYPE_ARRAY ? type_alignment(type->base) : type->size;
}

Type *struct_type(void) {
  Type *type = calloc(1, sizeof(Type));
  if (!type)
    return NULL;
  type->kind = TYPE_STRUCT;
  type->size = -1; /* Incomplete until the definition is finished. */
  type->alignment = 1;
  return type;
}

/* Each enum has its own identity and uses int as its compatible integer type.
 */
Type *enum_type(void) {
  Type *type = calloc(1, sizeof(Type));
  if (!type)
    return NULL;
  type->kind = TYPE_ENUM;
  type->size = -1;
  type->base = &builtin_type_int;
  return type;
}

/*
 * A length of -1 denotes an unknown bound. Otherwise the caller validates a
 * positive length and a total size representable by int.
 */
Type *array_of(Type *base, int length) {
  Type *type = calloc(1, sizeof(Type));
  if (!type)
    return NULL;
  type->kind = TYPE_ARRAY;
  type->base = base;
  type->array_len = length;
  type->size = length < 0 ? -1 : base->size * length;
  return type;
}

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
