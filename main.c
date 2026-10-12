/*
 * Main driver for the self-hosted C compiler
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
#include <stdlib.h>
#include <string.h>

#include "codegen.h"
#include "parser.h"

char *read_source(const char *path) {
  FILE *file;
  long len;
  char *buf;
  size_t bytes_read;

  file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "failed to open file '%s'\n", path);
    exit(1);
  }

  fseek(file, 0, SEEK_END);
  len = ftell(file);
  if (len < 0) {
    fclose(file);
    fprintf(stderr, "failed to measure file size '%s'\n", path);
    exit(1);
  }
  fseek(file, 0, SEEK_SET);

  buf = malloc(len + 1);
  if (!buf) {
    fclose(file);
    fprintf(stderr, "failed to allocate memory for reading file '%s'\n", path);
    exit(1);
  }

  bytes_read = fread(buf, 1, (size_t)len, file);
  buf[bytes_read] = '\0';

  fclose(file);
  return buf;
}

int main(int argc, char **argv) {
  const char *source;
  const char *filename;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    exit(1);
  }

  filename = argv[1];
  source = read_source(filename);

  codegen(parse_program(filename, source));

  return 0;
}
