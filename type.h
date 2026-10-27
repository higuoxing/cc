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

#ifndef _TYPE_H_
#define _TYPE_H_

#include <stdbool.h>

typedef enum {
  TYPE_CHAR,
  TYPE_INT,
  TYPE_PTR,
  TYPE_VOID,
} TypeKind;

typedef struct Type {
  TypeKind kind;
  int size; /* In bytes. */
  bool is_unsigned; /* Meaningful for integer types only. */
  struct Type *base; /* Pointed-to type for TYPE_PTR */
} Type;

extern Type builtin_type_char;
extern Type builtin_type_unsigned_char;
extern Type builtin_type_int;
extern Type builtin_type_unsigned;
extern Type builtin_type_void;

extern bool is_integer(const Type *type);
extern bool same_type(const Type *a, const Type *b);
extern bool compatible_pointers(const Type *a, const Type *b);
extern int type_size(const Type *type);
extern Type *pointer_to(Type *base);

#endif
