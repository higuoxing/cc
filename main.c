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
#include <sys/stat.h>

#include "codegen.h"
#include "layout.h"
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

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    fprintf(stderr, "failed to seek file '%s'\n", path);
    exit(1);
  }
  len = ftell(file);
  if (len < 0) {
    fclose(file);
    fprintf(stderr, "failed to measure file size '%s'\n", path);
    exit(1);
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    fprintf(stderr, "failed to seek file '%s'\n", path);
    exit(1);
  }

  buf = malloc(len + 1);
  if (!buf) {
    fclose(file);
    fprintf(stderr, "failed to allocate memory for reading file '%s'\n", path);
    exit(1);
  }

  bytes_read = fread(buf, 1, (size_t)len, file);
  if (bytes_read != (size_t)len) {
    fprintf(stderr, "failed to read complete file '%s'\n", path);
    free(buf);
    fclose(file);
    exit(1);
  }
  buf[bytes_read] = '\0';

  fclose(file);
  return buf;
}

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s <filename> [-o <output>]\n", prog);
  exit(1);
}

/*
 * Derive the assembly file name from the input one, mirroring 'gcc -S':
 * 'foo.c' becomes 'foo.s', and a name without a dot gets '.s' appended.
 */
static char *derive_output_path(const char *input) {
  const char *slash;
  const char *name;
  const char *dot;
  size_t base_len;
  char *path;

  slash = strrchr(input, '/');
  name = slash ? slash + 1 : input;
  dot = strrchr(name, '.');
  /* A leading dot is part of a hidden file name, not an extension. */
  base_len = dot && dot != name ? (size_t)(dot - input) : strlen(input);

  path = malloc(base_len + 3);
  if (!path) {
    fprintf(stderr, "failed to allocate output file name\n");
    exit(1);
  }
  memcpy(path, input, base_len);
  memcpy(path + base_len, ".s", 3); /* Includes the terminating NUL. */
  return path;
}

int main(int argc, char **argv) {
  const char *filename = NULL;
  const char *output = NULL;
  char *derived = NULL;
  FILE *out_file;
  char *source;
  AstNode *ast;
  int i;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-o") == 0) {
      if (i + 1 >= argc)
        usage(argv[0]);
      output = argv[++i];
    } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
      usage(argv[0]);
    } else if (!filename) {
      filename = argv[i];
    } else {
      usage(argv[0]);
    }
  }

  if (!filename)
    usage(argv[0]);

  source = read_source(filename);
  ast = parse_translation_unit(filename, source);
  assign_stack_offsets(filename, ast);

  if (output && strcmp(output, "-") == 0) {
    /* '-o -' writes assembly to stdout, like gcc and clang. */
    out_file = stdout;
  } else {
    struct stat input_st, output_st;

    if (!output)
      output = derived = derive_output_path(filename);

    if (stat(filename, &input_st) != 0) {
      fprintf(stderr, "failed to stat source file '%s'\n", filename);
      exit(1);
    }

    if (stat(output, &output_st) == 0 && input_st.st_dev == output_st.st_dev &&
        input_st.st_ino == output_st.st_ino) {
      fprintf(stderr, "refusing to overwrite input file '%s'\n", filename);
      exit(1);
    }

    out_file = fopen(output, "wb");
    if (!out_file) {
      fprintf(stderr, "failed to open output file '%s'\n", output);
      exit(1);
    }
  }

  codegen(ast, out_file);

  if (out_file != stdout && fclose(out_file) != 0) {
    fprintf(stderr, "failed to write output file '%s'\n", output);
    exit(1);
  }

  free(source);
  free(derived);

  return 0;
}
