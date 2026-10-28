/*
 * Dynamic arrays for the self-hosted C compiler
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

#ifndef _VECTOR_H_
#define _VECTOR_H_

#include <stdbool.h>
#include <stddef.h>

/*
 * Contiguous, copied elements of a fixed size. The vector owns its storage,
 * but not objects referenced by pointer elements. Growth invalidates pointers
 * into data. Initialize before use; element_size must be positive.
 */
typedef struct {
  void *data;
  size_t len;
  size_t capacity;
  size_t element_size;
} Vector;

extern void vector_init(Vector *vector, size_t element_size);

/* Allocation or size overflow returns false without changing the vector. */
extern bool vector_reserve(Vector *vector, size_t capacity);

/* Source elements must not overlap the vector's storage. */
extern bool vector_append(Vector *vector, const void *elements, size_t count);
extern bool vector_push(Vector *vector, const void *element);

/* Both operations leave an empty vector that retains its element size. */
extern void vector_free(Vector *vector);
extern void *vector_release(Vector *vector); /* Caller owns the returned buffer. */

#endif
