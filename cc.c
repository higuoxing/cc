#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void fatalf(const char *format, ...) {
  va_list args;

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  exit(1);
}

bool read_number(const char **p, int *n) {
  if (!isdigit((*p)[0]))
    return false;

  const char *num = *p;
  while (isdigit((*p)[0])) {
    ++(*p);
  }

  *n = atoi(num);
  return true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fatalf("invalid number of arguments\n");
  }

  printf("    .global main\n");
  printf("main:\n");

  const char *p = argv[1];
  while (p && p[0]) {
    /* Skip whitespaces. */
    if (isspace(p[0])) {
      ++p;
      continue;
    }

    int n;
    if (read_number(&p, &n)) {
      printf("    movq $%d, %%rax\n", n);
    }

    switch (p[0]) {
    case '+':
      ++p;
      while (p && p[0] && isspace(p[0]))
        ++p;
      if (read_number(&p, &n))
        printf("    addq $%d, %%rax\n", n);
      else {
        fatalf("expect a number\n");
      }
      break;
    case '\0':
    case ' ':
      break;
    default:
      fatalf("unable to parse: %s", p);
    }
  }

  printf("    retq\n");
  return 0;
}
