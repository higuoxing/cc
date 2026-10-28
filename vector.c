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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vector.h"

void vector_init(Vector *vector, size_t element_size) {
  vector->data = NULL;
  vector->len = 0;
  vector->capacity = 0;
  vector->element_size = element_size;
}

bool vector_reserve(Vector *vector, size_t capacity) {
  size_t limit;
  size_t grown;
  void *data;
  if (!vector->element_size)
    return false;
  limit = SIZE_MAX / vector->element_size;
  if (capacity > limit)
    return false;
  if (capacity <= vector->capacity)
    return true;

  grown = vector->capacity ? vector->capacity : 8;
  if (grown > limit)
    grown = limit;
  while (grown < capacity) {
    if (grown > limit / 2) {
      grown = capacity;
      break;
    }
    grown *= 2;
  }
  data = realloc(vector->data, grown * vector->element_size);
  if (!data)
    return false;
  vector->data = data;
  vector->capacity = grown;
  return true;
}

bool vector_append(Vector *vector, const void *elements, size_t count) {
  if (!count)
    return true;
  if (!elements || count > SIZE_MAX - vector->len)
    return false;
  if (!vector_reserve(vector, vector->len + count))
    return false;
  memcpy((unsigned char *)vector->data + vector->len * vector->element_size,
         elements, count * vector->element_size);
  vector->len += count;
  return true;
}

bool vector_push(Vector *vector, const void *element) {
  return vector_append(vector, element, 1);
}

void vector_free(Vector *vector) {
  free(vector->data);
  vector_init(vector, vector->element_size);
}

void *vector_release(Vector *vector) {
  void *data = vector->data;
  vector_init(vector, vector->element_size);
  return data;
}
