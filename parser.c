/*
 * Parser for the self-hosted C compiler
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
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "type.h"
#include "vector.h"

static AstNode *new_ast_node(Parser *p, AstKind kind);
static AstNode *new_expr(Parser *p, AstKind kind, Type *type);
static void reset_function_state(Parser *p);
static void parser_advance(Parser *p);
static AstNode *parse_assignment(Parser *p);
static AstNode *parse_expression(Parser *p);
static AstNode *parse_condition(Parser *p);
static AstNode *parse_conditional(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_call_expr(Parser *p, Function *func);
static AstNode *parse_postfix(Parser *p);
static AstNode *parse_primary(Parser *p);
static AstNode *parse_string_literal(Parser *p);
static AstNode *parse_decl(Parser *p, bool allow_typedef);
static AstNode *parse_return_stmt(Parser *p);
static AstNode *parse_if_stmt(Parser *p);
static AstNode *parse_while_stmt(Parser *p);
static AstNode *parse_for_stmt(Parser *p);
static AstNode *parse_do_stmt(Parser *p);
static AstNode *parse_break_stmt(Parser *p);
static AstNode *parse_continue_stmt(Parser *p);
static AstNode *parse_goto_stmt(Parser *p);
static AstNode *parse_label_stmt(Parser *p);
static AstNode *parse_switch_stmt(Parser *p);
static AstNode *parse_case_stmt(Parser *p);
static AstNode *parse_default_stmt(Parser *p);
static AstNode *parse_stmt(Parser *p, bool allow_decl);
static AstNode *parse_compound_stmt(Parser *p);
static AstNode *parse_block(Parser *p);
static AstNode *parse_external_declaration(Parser *p);
static Type *parse_declspecs(Parser *p, bool *is_typedef);
static AstNode *parse_typedef_declarators(Parser *p, Type *base);
static Type *parse_declarator(Parser *p, Type *type, Token *name);
static Type *parse_type_name(Parser *p);

static bool is_type_specifier(Parser *p, Token token) {
  TokenKind kind = token.kind;
  if (kind == TOKEN_IDENTIFIER) {
    Symbol *sym = find_symbol(p, token);
    return sym && sym->kind == SYMBOL_TYPEDEF;
  }
  return kind == TOKEN_CHAR || kind == TOKEN_SHORT || kind == TOKEN_INT ||
         kind == TOKEN_LONG || kind == TOKEN_UNSIGNED || kind == TOKEN_VOID ||
         kind == TOKEN_STRUCT || kind == TOKEN_ENUM;
}

/* assignment-operator: = += -= *= /= %= <<= >>= &= ^= |=  (C11 6.5.16) */
static bool is_assignment_op(TokenKind ty) {
  return ty == TOKEN_ASSIGN || ty == TOKEN_PLUS_EQ || ty == TOKEN_MINUS_EQ ||
         ty == TOKEN_STAR_EQ || ty == TOKEN_SLASH_EQ ||
         ty == TOKEN_PERCENT_EQ || ty == TOKEN_LSHIFT_EQ ||
         ty == TOKEN_RSHIFT_EQ || ty == TOKEN_BIT_AND_EQ ||
         ty == TOKEN_BIT_XOR_EQ || ty == TOKEN_BIT_OR_EQ;
}

static AstNode *new_ast_node(Parser *p, AstKind kind) {
  AstNode *node;
  node = calloc(1, sizeof(AstNode));
  if (!node)
    parse_error(p, "out of memory");
  node->kind = kind;
  node->loc = p->current;
  node->type = &builtin_type_void;
  return node;
}

static AstNode *new_expr(Parser *p, AstKind kind, Type *type) {
  AstNode *node = new_ast_node(p, kind);
  node->type = type;
  return node;
}

static void parser_advance(Parser *p) {
  p->previous = p->current;
  p->current = p->lookahead;
  p->lookahead = lexer_next_token(&p->lexer);
}

static void enter_scope(Parser *p) { ++p->scope_depth; }

/*
 * Scope exit restores the outer bindings. File-scope function symbols remain
 * visible even when they were added while parsing parameters at depth 1.
 */
static void leave_scope(Parser *p) {
  while (p->symbols && p->symbols->scope_depth == p->scope_depth)
    p->symbols = p->symbols->next;
  while (p->tags && p->tags->scope_depth == p->scope_depth)
    p->tags = p->tags->next;
  --p->scope_depth;
}

static void reset_function_state(Parser *p) {
  p->labels = NULL;
  p->goto_refs = NULL;
  p->current_switch = NULL;
  p->current_function = NULL;
  p->scope_depth = 0;
  p->loop_depth = 0;
}

static void parser_init(Parser *p, const char *filename, const char *source) {
  p->tags = NULL;
  p->symbols = NULL;
  p->labels = NULL;
  p->goto_refs = NULL;
  p->call_refs = NULL;
  p->current_switch = NULL;
  p->current_function = NULL;
  p->scope_depth = 0;
  p->loop_depth = 0;
  lexer_init(&p->lexer, filename, source);
  p->previous = (Token){0};
  p->current = lexer_next_token(&p->lexer);
  p->lookahead = lexer_next_token(&p->lexer);
}

static void report_error(Parser *p, Token loc, const char *fmt, va_list ap) {
  fprintf(stderr, "%s:%d:%d: error: ", p->lexer.filename, loc.line, loc.col);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void error_at(Parser *p, Token loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  report_error(p, loc, fmt, ap);
  va_end(ap);
  exit(1);
}

void parse_error(Parser *p, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  report_error(p, p->current, fmt, ap);
  va_end(ap);
  exit(1);
}

static void parse_expect(Parser *p, TokenKind t, const char *msg) {
  if (p->current.kind != t) {
    parse_error(p, "%s", msg);
  }

  parser_advance(p);
}

/*
 * A missing statement terminator belongs immediately after the last token,
 * even when the next token is on another line or follows a comment.
 * Semicolons separating for clauses still use parse_expect at the next token.
 */
static void parse_semicolon(Parser *p) {
  if (p->current.kind != TOKEN_SEMICOLON) {
    Token loc = p->previous;
    loc.start += loc.len;
    loc.col += loc.len;
    loc.len = 0;
    error_at(p, loc, "expect ';'");
  }
  parser_advance(p);
}

/*
 * unary-expression:
 *   '-' unary-expression | '~' unary-expression | '!' unary-expression
 *   '++' unary-expression | '--' unary-expression
 *   '&' unary-expression | '*' unary-expression
 *   'sizeof' unary-expression | 'sizeof' '(' type-name ')'
 *   postfix-expression
 */
static AstNode *parse_unary(Parser *p) {
  if (p->current.kind == TOKEN_SIZEOF) {
    Token loc = p->current;
    Type *type;
    parser_advance(p);
    if (p->current.kind == TOKEN_LPAREN &&
        (p->lookahead.kind == TOKEN_TYPEDEF ||
         is_type_specifier(p, p->lookahead))) {
      parser_advance(p);
      type = parse_type_name(p);
      parse_expect(p, TOKEN_RPAREN, "expect ')'");
    } else {
      type = parse_unary(p)->type;
    }
    return sema_sizeof(p, type, loc);
  }
  if (p->current.kind == TOKEN_MINUS || p->current.kind == TOKEN_TILDE ||
      p->current.kind == TOKEN_BANG || p->current.kind == TOKEN_PLUS_PLUS ||
      p->current.kind == TOKEN_MINUS_MINUS ||
      p->current.kind == TOKEN_BIT_AND || p->current.kind == TOKEN_STAR) {
    AstNode *operand;
    AstKind op;
    Token op_tok;

    switch (p->current.kind) {
    case TOKEN_MINUS:
      op = AST_NEG;
      break;
    case TOKEN_TILDE:
      op = AST_BITWISE_NOT;
      break;
    case TOKEN_BANG:
      op = AST_LOGICAL_NOT;
      break;
    case TOKEN_PLUS_PLUS:
      op = AST_PRE_INCREMENT;
      break;
    case TOKEN_MINUS_MINUS:
      op = AST_PRE_DECREMENT;
      break;
    case TOKEN_BIT_AND:
      op = AST_ADDR;
      break;
    case TOKEN_STAR:
      op = AST_DEREF;
      break;
    default:
      fprintf(stderr, "unreachable %s:%d", __FILE__, __LINE__);
      exit(1);
    }

    /* Consume '-' or '~' or '!' or '++' or '--' or '&' or '*' */
    op_tok = p->current;
    parser_advance(p);
    operand = parse_unary(p);

    return sema_unary(p, op, operand, op_tok);
  }

  return parse_postfix(p);
}

/*
 * postfix-expression:
 *   identifier '(' argument-expression-list_opt ')'
 */
static AstNode *parse_call_expr(Parser *p, Function *func) {
  AstNode *call;
  CallRef *ref;
  int nargs = 0;

  ref = (CallRef *)calloc(1, sizeof(CallRef));
  if (!ref)
    parse_error(p, "out of memory");
  ref->name = p->current;
  ref->loc = p->current;
  ref->next = p->call_refs;
  p->call_refs = ref;

  call = new_expr(p, AST_CALL, func->return_type);
  call->d.call.function = func;
  ref->call = call;
  /* Consume the function name. */
  parser_advance(p);
  parse_expect(p, TOKEN_LPAREN, "expect '('");

  if (p->current.kind != TOKEN_RPAREN) {
    AstNode **tail = &call->d.call.args;

    for (;;) {
      *tail = parse_assignment(p);
      tail = &(*tail)->next;
      nargs++;
      if (p->current.kind == TOKEN_COMMA) {
        parser_advance(p);
        continue;
      }
      break;
    }
  }
  parse_expect(p, TOKEN_RPAREN, "expect ')'");

  ref->nargs = nargs;
  sema_check_call(p, func, call, nargs);
  return call;
}

/*
 * postfix-expression:
 *   primary-expression
 *   identifier '(' ')'
 *   postfix-expression '[' expression ']'
 *   postfix-expression '.' identifier
 *   postfix-expression '->' identifier
 *   postfix-expression '++' | postfix-expression '--'
 */
static AstNode *parse_postfix(Parser *p) {
  AstNode *left;

  if (p->current.kind == TOKEN_IDENTIFIER &&
      p->lookahead.kind == TOKEN_LPAREN) {
    Symbol *sym = find_symbol(p, p->current);
    if (!sym)
      parse_error(p, "function '%.*s' not declared", p->current.len,
                  p->current.start);
    if (sym->kind == SYMBOL_TYPEDEF)
      parse_error(p, "typedef name is not an expression");
    if (sym->kind != SYMBOL_FUNCTION)
      parse_error(p, "called object is not a function");
    left = parse_call_expr(p, sym->d.function);
  } else {
    left = parse_primary(p);
  }

  while (p->current.kind == TOKEN_PLUS_PLUS ||
         p->current.kind == TOKEN_MINUS_MINUS ||
         p->current.kind == TOKEN_LBRACKET || p->current.kind == TOKEN_DOT ||
         p->current.kind == TOKEN_ARROW) {
    Token op = p->current;
    if (op.kind == TOKEN_DOT || op.kind == TOKEN_ARROW) {
      Token name;
      parser_advance(p);
      if (p->current.kind != TOKEN_IDENTIFIER)
        parse_error(p, "expect member name");
      name = p->current;
      parser_advance(p);
      left = sema_member(p, left, op, name);
      continue;
    }
    if (op.kind == TOKEN_LBRACKET) {
      AstNode *index;
      parser_advance(p);
      index = parse_expression(p);
      parse_expect(p, TOKEN_RBRACKET, "expect ']'");
      left = sema_subscript(p, left, index, op);
      continue;
    }
    AstKind kind =
        op.kind == TOKEN_PLUS_PLUS ? AST_POST_INCREMENT : AST_POST_DECREMENT;
    parser_advance(p);
    left = sema_unary(p, kind, left, op);
  }

  return left;
}

/*
 * Concatenate adjacent string tokens (C11 5.1.1.2, translation phase 6).
 * Keep embedded zeros, append one final terminator, and transfer the combined
 * buffer to AST while retaining the first token's source location.
 */
static AstNode *parse_string_literal(Parser *p) {
  Token literal = p->current;
  Vector bytes;
  unsigned char terminator = 0;
  vector_init(&bytes, sizeof(unsigned char));
  do {
    Token next = p->current;
    if (bytes.len > (size_t)(INT_MAX - next.string_literal_len))
      error_at(p, literal, "string literal is too long");
    if (!vector_append(&bytes, next.string_literal_val,
                       (size_t)next.string_literal_len - 1))
      error_at(p, literal, "out of memory");
    free(next.string_literal_val);
    parser_advance(p);
  } while (p->current.kind == TOKEN_STRING_LITERAL);
  if (!vector_push(&bytes, &terminator))
    error_at(p, literal, "out of memory");
  literal.string_literal_len = (int)bytes.len;
  literal.string_literal_val = vector_release(&bytes);
  return sema_string_literal(p, literal);
}

/*
 * primary-expression:
 *   integer-constant | character-constant | string-literal
 *   '(' expression ')' | identifier
 */
static AstNode *parse_primary(Parser *p) {
  if (p->current.kind == TOKEN_STRING_LITERAL)
    return parse_string_literal(p);

  if (p->current.kind == TOKEN_INT_LITERAL) {
    AstNode *expr;
    expr = sema_integer_literal(p, p->current);

    /* Consume the integer or character constant. */
    parser_advance(p);
    return expr;
  }

  if (p->current.kind == TOKEN_LPAREN) {
    AstNode *expr;
    /* Consume '(' */
    parser_advance(p);
    expr = parse_expression(p);
    parse_expect(p, TOKEN_RPAREN, "expect ')'");
    return expr;
  }

  if (p->current.kind == TOKEN_IDENTIFIER) {
    AstNode *expr;
    Symbol *sym = find_symbol(p, p->current);
    if (!sym)
      parse_error(p, "undeclared identifier");
    if (sym->kind == SYMBOL_TYPEDEF)
      parse_error(p, "typedef name is not an expression");
    if (sym->kind == SYMBOL_ENUMERATOR) {
      expr = sema_enumerator_expr(p, sym->d.enumerator, p->current);
      parser_advance(p);
      return expr;
    }
    if (sym->kind != SYMBOL_VARIABLE)
      parse_error(p, "undeclared identifier");
    expr = new_expr(p, AST_VAR, sym->d.variable->type);
    expr->d.variable.var = sym->d.variable;
    /* Consume identifier. */
    parser_advance(p);
    return expr;
  }

  parse_error(p, "expect expr");
  return NULL;
}

static Tag *find_tag(Parser *p, Token name, bool current_scope) {
  Tag *tag;
  for (tag = p->tags; tag; tag = tag->next) {
    if (same_identifier(tag->name, name)) {
      if (!current_scope || tag->scope_depth == p->scope_depth)
        return tag;
    }
  }
  return NULL;
}

static Tag *declare_tag(Parser *p, Token name, Type *type) {
  Tag *tag = calloc(1, sizeof(Tag));
  if (!tag)
    error_at(p, name, "out of memory");
  tag->name = name;
  tag->type = type;
  tag->scope_depth = p->scope_depth;
  tag->next = p->tags;
  p->tags = tag;
  return tag;
}

/*
 * enum-specifier:
 *   'enum' identifier
 *   'enum' identifier_opt '{' enumerator (',' enumerator)* ','_opt '}'
 * enumerator: identifier ('=' constant-expression)_opt
 * (C11 6.7.2.2; enum tags must be defined before use without a definition.)
 */
static Type *parse_enum_specifier(Parser *p) {
  Token loc = p->current;
  Token name = {0};
  Tag *tag = NULL;
  Type *type;
  int64_t next_value = 0;

  /* Consume 'enum' */
  parser_advance(p);
  if (p->current.kind == TOKEN_IDENTIFIER) {
    name = p->current;
    parser_advance(p);
    tag = find_tag(p, name, p->current.kind == TOKEN_LBRACE);
  } else if (p->current.kind != TOKEN_LBRACE) {
    parse_error(p, "expect enum tag or '{'");
  }
  if (tag && tag->type->kind != TYPE_ENUM)
    error_at(p, name, "tag '%.*s' declared as a different kind", name.len,
             name.start);
  if (p->current.kind != TOKEN_LBRACE) {
    if (!tag || tag->type->size < 0)
      error_at(p, name, "enum '%.*s' is not defined", name.len, name.start);
    return tag->type;
  }
  if (tag) {
    if (tag->type->size >= 0 || tag->is_defining)
      error_at(p, name, "redefinition of enum '%.*s'", name.len, name.start);
    type = tag->type;
  } else {
    type = enum_type();
    if (!type)
      error_at(p, loc, "out of memory");
    if (name.kind == TOKEN_IDENTIFIER)
      tag = declare_tag(p, name, type);
  }
  if (tag)
    tag->is_defining = true;
  parser_advance(p);
  for (;;) {
    Token enumerator_name = p->current;
    AstNode *initializer = NULL;
    Enum *enumerator;
    parse_expect(p, TOKEN_IDENTIFIER, "expect enumerator name");
    if (p->current.kind == TOKEN_ASSIGN) {
      parser_advance(p);
      initializer = parse_conditional(p);
    }
    enumerator =
        declare_enumerator(p, enumerator_name, initializer, next_value);
    next_value = (int64_t)enumerator->value + 1;
    if (p->current.kind != TOKEN_COMMA)
      break;
    parser_advance(p);
    if (p->current.kind == TOKEN_RBRACE)
      break;
  }
  parse_expect(p, TOKEN_RBRACE, "expect '}'");
  type->size = type->base->size;
  if (tag)
    tag->is_defining = false;
  return type;
}

/*
 * Parse one member declarator and update the struct's size and alignment.
 * The caller links the returned member before parsing the next declarator.
 */
static Member *parse_struct_member(Parser *p, Type *type, Type *base,
                                   int *size) {
  Token name;
  Type *member_type = parse_declarator(p, base, &name);
  Member *member;
  int alignment;
  int padding;

  if (name.kind != TOKEN_IDENTIFIER)
    error_at(p, name, "expect member name");
  if (member_type->kind == TYPE_VOID)
    error_at(p, name, "member has void type");
  if (member_type->size < 0)
    error_at(p, name, "member has incomplete type");
  for (member = type->members; member; member = member->next)
    if (same_identifier(member->name, name))
      error_at(p, name, "duplicate struct member");
  alignment = type_alignment(member_type);
  padding = (alignment - *size % alignment) % alignment;
  if (*size > INT_MAX - padding ||
      member_type->size > INT_MAX - *size - padding)
    error_at(p, name, "struct size exceeds supported limit");
  member = calloc(1, sizeof(Member));
  if (!member)
    error_at(p, name, "out of memory");
  member->name = name;
  member->type = member_type;
  member->offset = *size + padding;
  *size = member->offset + member_type->size;
  if (alignment > type->alignment)
    type->alignment = alignment;
  return member;
}

/*
 * struct-specifier:
 *   'struct' identifier
 *   'struct' identifier_opt '{' struct-declaration+ '}'
 * struct-declaration:
 *   declaration-specifiers declarator (',' declarator)* ';'
 * (C11 6.7.2.1, excluding bit-fields and anonymous members.)
 */
static Type *parse_struct_specifier(Parser *p) {
  Token loc = p->current;
  Token name = {0};
  Tag *tag = NULL;
  Type *type;
  Member **tail;
  int size = 0;

  parser_advance(p);
  if (p->current.kind == TOKEN_IDENTIFIER) {
    name = p->current;
    parser_advance(p);
    tag = find_tag(p, name,
                   p->current.kind == TOKEN_LBRACE ||
                       p->current.kind == TOKEN_SEMICOLON);
  } else if (p->current.kind != TOKEN_LBRACE) {
    parse_error(p, "expect struct tag or '{'");
  }

  if (tag) {
    if (tag->type->kind != TYPE_STRUCT)
      error_at(p, name, "tag '%.*s' declared as a different kind", name.len,
               name.start);
    type = tag->type;
  } else {
    type = struct_type();
    if (!type)
      error_at(p, loc, "out of memory");
    if (name.kind == TOKEN_IDENTIFIER) {
      tag = declare_tag(p, name, type);
    }
  }
  if (p->current.kind != TOKEN_LBRACE)
    return type;
  if (type->size >= 0 || (tag && tag->is_defining))
    error_at(p, name, "redefinition of struct '%.*s'", name.len, name.start);
  if (tag)
    tag->is_defining = true;
  parser_advance(p);
  tail = &type->members;
  while (p->current.kind != TOKEN_RBRACE) {
    Type *base;
    if (p->current.kind == TOKEN_EOF)
      parse_error(p, "expect '}'");
    base = parse_declspecs(p, NULL);
    for (;;) {
      *tail = parse_struct_member(p, type, base, &size);
      tail = &(*tail)->next;
      if (p->current.kind != TOKEN_COMMA)
        break;
      parser_advance(p);
    }
    parse_semicolon(p);
  }
  if (!type->members)
    parse_error(p, "empty structs are not supported");
  int padding = (type->alignment - size % type->alignment) % type->alignment;
  if (size > INT_MAX - padding)
    error_at(p, loc, "struct size exceeds supported limit");
  type->size = size + padding;
  if (tag)
    tag->is_defining = false;
  parser_advance(p);
  return type;
}

/*
 * declaration-specifiers: (storage-class-specifier | type-specifier)+
 * (C11 6.7, supported subset).
 *
 * Consume typedef and type specifiers in one loop. Passing NULL forbids typedef
 * in type names, members and parameters. Once a base type is consumed, a
 * typedef name can instead be the declarator's name.
 */
static Type *parse_declspecs(Parser *p, bool *is_typedef) {
  enum {
    DECLSPEC_NONE = 0,
    DECLSPEC_INT = 1 << 0,
    DECLSPEC_CHAR = 1 << 1,
    DECLSPEC_UNSIGNED = 1 << 2,
    DECLSPEC_LONG = 1 << 3,
    DECLSPEC_SHORT = 1 << 4,
    DECLSPEC_LONG_LONG = 1 << 5,
    DECLSPEC_VOID = 1 << 6,
    DECLSPEC_STRUCT = 1 << 7,
    DECLSPEC_ENUM = 1 << 8,
    DECLSPEC_TYPEDEF_NAME = 1 << 9,
    DECLSPEC_EXCLUSIVE = DECLSPEC_VOID | DECLSPEC_STRUCT | DECLSPEC_ENUM |
        DECLSPEC_TYPEDEF_NAME,
  };
  unsigned specs = DECLSPEC_NONE;
  Type *specified_type = NULL;

  if (is_typedef)
    *is_typedef = false;
  while (p->current.kind == TOKEN_TYPEDEF || is_type_specifier(p, p->current)) {
    TokenKind kind = p->current.kind;
    unsigned flag;
    if (kind == TOKEN_TYPEDEF) {
      if (!is_typedef)
        parse_error(p, "typedef is not allowed here");
      if (*is_typedef)
        parse_error(p, "duplicate typedef specifier");
      *is_typedef = true;
      parser_advance(p);
      continue;
    }
    /*
     * In T T, the second typedef name is the declarator's identifier.
     */
    if (kind == TOKEN_IDENTIFIER && specs)
      break;
    switch (kind) {
    case TOKEN_INT:
      flag = DECLSPEC_INT;
      break;
    case TOKEN_CHAR:
      flag = DECLSPEC_CHAR;
      break;
    case TOKEN_UNSIGNED:
      flag = DECLSPEC_UNSIGNED;
      break;
    case TOKEN_LONG:
      flag = DECLSPEC_LONG;
      break;
    case TOKEN_SHORT:
      flag = DECLSPEC_SHORT;
      break;
    case TOKEN_VOID:
      flag = DECLSPEC_VOID;
      break;
    case TOKEN_STRUCT:
      flag = DECLSPEC_STRUCT;
      break;
    case TOKEN_ENUM:
      flag = DECLSPEC_ENUM;
      break;
    case TOKEN_IDENTIFIER:
      flag = DECLSPEC_TYPEDEF_NAME;
      break;
    default:
      parse_error(p, "expect type specifier");
    }

    if (flag == DECLSPEC_LONG && (specs & DECLSPEC_LONG))
      flag = DECLSPEC_LONG_LONG;
    if (specs & flag) {
      parse_error(p, "duplicate %.*s type specifier", p->current.len,
                  p->current.start);
    }
    if ((specs & DECLSPEC_EXCLUSIVE) || ((flag & DECLSPEC_EXCLUSIVE) && specs))
      parse_error(p, "invalid combination of type specifiers");
    specs |= flag;
    if (((specs & DECLSPEC_CHAR) &&
         (specs & (DECLSPEC_INT | DECLSPEC_LONG | DECLSPEC_SHORT))) ||
        ((specs & DECLSPEC_SHORT) && (specs & DECLSPEC_LONG)))
      parse_error(p, "invalid combination of type specifiers");
    if (kind == TOKEN_STRUCT)
      specified_type = parse_struct_specifier(p);
    else if (kind == TOKEN_ENUM)
      specified_type = parse_enum_specifier(p);
    else {
      if (kind == TOKEN_IDENTIFIER)
        specified_type = find_symbol(p, p->current)->d.typedef_type;
      parser_advance(p);
    }
  }

  switch (specs) {
  case DECLSPEC_VOID:
    return &builtin_type_void;
  case DECLSPEC_STRUCT:
  case DECLSPEC_ENUM:
  case DECLSPEC_TYPEDEF_NAME:
    return specified_type;
  case DECLSPEC_INT:
    return &builtin_type_int;
  case DECLSPEC_CHAR:
    return &builtin_type_char;
  case DECLSPEC_UNSIGNED:
  case DECLSPEC_UNSIGNED | DECLSPEC_INT:
    return &builtin_type_unsigned;
  case DECLSPEC_UNSIGNED | DECLSPEC_CHAR:
    return &builtin_type_unsigned_char;
  case DECLSPEC_SHORT:
  case DECLSPEC_SHORT | DECLSPEC_INT:
    return &builtin_type_short;
  case DECLSPEC_UNSIGNED | DECLSPEC_SHORT:
  case DECLSPEC_UNSIGNED | DECLSPEC_SHORT | DECLSPEC_INT:
    return &builtin_type_unsigned_short;
  case DECLSPEC_LONG:
  case DECLSPEC_LONG | DECLSPEC_INT:
    return &builtin_type_long;
  case DECLSPEC_UNSIGNED | DECLSPEC_LONG:
  case DECLSPEC_UNSIGNED | DECLSPEC_LONG | DECLSPEC_INT:
    return &builtin_type_unsigned_long;
  case DECLSPEC_LONG | DECLSPEC_LONG_LONG:
  case DECLSPEC_LONG | DECLSPEC_LONG_LONG | DECLSPEC_INT:
    return &builtin_type_long_long;
  case DECLSPEC_UNSIGNED | DECLSPEC_LONG | DECLSPEC_LONG_LONG:
  case DECLSPEC_UNSIGNED | DECLSPEC_LONG | DECLSPEC_LONG_LONG | DECLSPEC_INT:
    return &builtin_type_unsigned_long_long;
  case DECLSPEC_NONE:
    parse_error(p, "expect type specifier");
  default:
    parse_error(p, "invalid combination of type specifiers");
  }
}

/*
 * declarator:
 *   pointer_opt direct-declarator
 * direct-declarator:
 *   identifier
 *   identifier '[' constant-expression_opt ']'
 * pointer:
 *   '*'
 *   '*' pointer
 * (C11 6.7.6, supported subset; _opt means optional.)
 *
 * The name may be omitted for a parameter declaration; callers enforce where
 * it is required. An omitted name retains the token at its expected position
 * for diagnostics, even if an array suffix is subsequently consumed.
 * Only one-dimensional arrays with constant or unknown bounds are supported.
 * Function and parenthesized declarators come later.
 */
static Type *parse_declarator(Parser *p, Type *type, Token *name) {
  while (p->current.kind == TOKEN_STAR) {
    type = pointer_to(type);
    if (!type)
      parse_error(p, "out of memory");
    parser_advance(p);
  }
  *name = p->current;
  if (p->current.kind == TOKEN_IDENTIFIER) {
    parser_advance(p);
  }
  if (p->current.kind == TOKEN_LBRACKET) {
    Token loc = p->current;
    AstNode *bound;
    int64_t length = -1;
    parser_advance(p);
    if (type->kind == TYPE_VOID)
      error_at(p, loc, "array element has void type");
    if (type->size < 0)
      error_at(p, loc, "array element has incomplete type");
    if (p->current.kind != TOKEN_RBRACKET) {
      bound = parse_conditional(p);
      if (!is_integer(bound->type))
        error_at(p, bound->loc, "array bound must have integer type");
      length = eval_const_expr(p, bound);
      if (length == 0 || (length < 0 && !bound->type->is_unsigned))
        error_at(p, bound->loc, "array bound must be positive");
      if ((uint64_t)length > (uint64_t)(INT_MAX / type_size(type)))
        error_at(p, bound->loc, "array size exceeds supported limit");
    }
    parse_expect(p, TOKEN_RBRACKET, "expect ']'");
    type = array_of(type, (int)length);
    if (!type)
      error_at(p, loc, "out of memory");
    if (p->current.kind == TOKEN_LBRACKET)
      parse_error(p, "multidimensional arrays are not supported yet");
  }
  return type;
}

/*
 * type-name: specifier-qualifier-list abstract-declarator_opt (C11 6.7.7).
 * Our supported specifiers share parse_declspecs. Reuse pointer and fixed-array
 * declarators, but forbid an identifier. Parenthesized abstract declarators
 * are not supported yet.
 */
static Type *parse_type_name(Parser *p) {
  Token name;
  Type *type = parse_declspecs(p, NULL);
  type = parse_declarator(p, type, &name);
  if (name.kind == TOKEN_IDENTIFIER)
    error_at(p, name, "type name cannot contain an identifier");
  return type;
}

/*
 * declaration: declaration-specifiers declarator (',' declarator)* ';'
 * The declaration-specifiers contain typedef (C11 6.7, 6.7.8).
 * Aliases allocate no storage and use an empty declaration node in blocks.
 */
static AstNode *parse_typedef_declarators(Parser *p, Type *base) {
  AstNode *decl = new_ast_node(p, AST_DECL);
  if ((base->kind == TYPE_STRUCT || base->kind == TYPE_ENUM) &&
      p->current.kind == TOKEN_SEMICOLON) {
    parser_advance(p);
    return decl;
  }
  for (;;) {
    Token name;
    Type *type = parse_declarator(p, base, &name);
    if (name.kind != TOKEN_IDENTIFIER)
      error_at(p, name, "expect typedef name");
    if (p->current.kind == TOKEN_ASSIGN)
      parse_error(p, "typedef cannot have an initializer");
    if (p->current.kind == TOKEN_LPAREN)
      parse_error(p, "function typedefs are not supported yet");
    declare_typedef(p, name, type);
    if (p->current.kind != TOKEN_COMMA)
      break;
    parser_advance(p);
  }
  parse_semicolon(p);
  return decl;
}

/*
 * initializer: assignment-expression | '{' string-literal ','_opt '}'
 * Brace lists other than a single string remain unsupported.
 * The caller is positioned at '='.
 */
static AstNode *parse_initializer(Parser *p, Type *type) {
  Token loc = p->current;
  AstNode *expr;
  if (type->kind == TYPE_STRUCT)
    parse_error(p, "struct initializers are not supported yet");
  parser_advance(p);
  if (type->kind == TYPE_ARRAY && p->current.kind == TOKEN_LBRACE) {
    if (p->lookahead.kind != TOKEN_STRING_LITERAL)
      error_at(p, loc, "array initializers are not supported yet");
    parser_advance(p);
    expr = parse_assignment(p);
    if (p->current.kind == TOKEN_COMMA)
      parser_advance(p);
    parse_expect(p, TOKEN_RBRACE, "expect '}'");
    return expr;
  }
  return parse_assignment(p);
}

/*
 * declaration:
 *   declaration-specifiers declarator ['=' initializer] ';'
 * Restricted to a single declarator.
 */
static AstNode *parse_decl(Parser *p, bool allow_typedef) {
  AstNode *decl;
  Token loc = p->current;
  Token name;
  Type *type;
  bool is_typedef;

  type = parse_declspecs(p, &is_typedef);
  if (is_typedef) {
    if (!allow_typedef)
      error_at(p, loc, "typedef is not allowed in a for initializer");
    return parse_typedef_declarators(p, type);
  }
  if ((type->kind == TYPE_STRUCT || type->kind == TYPE_ENUM) &&
      p->current.kind == TOKEN_SEMICOLON) {
    decl = new_ast_node(p, AST_DECL);
    parser_advance(p);
    return decl;
  }
  type = parse_declarator(p, type, &name);
  if (type->kind == TYPE_VOID)
    error_at(p, name.kind == TOKEN_IDENTIFIER ? name : p->current,
             "variable has void type");

  if (name.kind != TOKEN_IDENTIFIER)
    error_at(p, name, "expect variable name");

  decl = new_ast_node(p, AST_DECL);
  decl->loc = name;
  sema_check_variable_name(p, name, false);
  decl->d.decl.var = new_variable(p, name, type);

  if (p->current.kind == TOKEN_ASSIGN) {
    decl->d.decl.init = parse_initializer(p, type);
    if (type->kind == TYPE_ARRAY)
      sema_initialize_string_array(p, decl->d.decl.var, decl->d.decl.init);
    else
      decl->d.decl.init =
          sema_convert_expr(p, type, decl->d.decl.init, name, "initializer");
  }
  if (type_size(decl->d.decl.var->type) < 0)
    error_at(p, name, "variable has incomplete type");

  parse_semicolon(p);

  return decl;
}

/*
 * multiplicative-expression:
 *   unary-expression
 *   multiplicative-expression '*' unary-expression
 *   multiplicative-expression '/' unary-expression
 *   multiplicative-expression '%' unary-expression
 * (C11 6.5.5)
 */
static AstNode *parse_multiplicative(Parser *p) {
  AstNode *left;

  left = parse_unary(p);

  while (p->current.kind == TOKEN_STAR || p->current.kind == TOKEN_SLASH ||
         p->current.kind == TOKEN_PERCENT) {
    TokenKind op;
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    op = p->current.kind;
    /* Consume '*' or '/' or '%' */
    parser_advance(p);

    right = parse_unary(p);
    node = sema_binary(
        p, op == TOKEN_STAR ? AST_MUL : (op == TOKEN_SLASH ? AST_DIV : AST_MOD),
        left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * additive-expression:
 *   multiplicative-expression
 *   additive-expression '+' multiplicative-expression
 *   additive-expression '-' multiplicative-expression
 * (C11 6.5.6)
 */
static AstNode *parse_additive(Parser *p) {
  AstNode *left;

  left = parse_multiplicative(p);

  while (p->current.kind == TOKEN_PLUS || p->current.kind == TOKEN_MINUS) {
    TokenKind op;
    AstNode *right;
    AstNode *node;
    Token op_tok;

    op = p->current.kind;
    op_tok = p->current;
    /* Consume '+' or '-' */
    parser_advance(p);
    right = parse_multiplicative(p);

    node = sema_binary(p, op == TOKEN_PLUS ? AST_ADD : AST_SUB, left, right,
                       op_tok);

    left = node;
  }

  return left;
}

/*
 * shift-expression:
 *   additive-expression
 *   shift-expression '<<' additive-expression
 *   shift-expression '>>' additive-expression
 * (C11 6.5.7)
 */
static AstNode *parse_shift(Parser *p) {
  AstNode *left;

  left = parse_additive(p);

  while (p->current.kind == TOKEN_LSHIFT || p->current.kind == TOKEN_RSHIFT) {
    TokenKind op;
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    op = p->current.kind;
    /* Consume '<<' or '>>' */
    parser_advance(p);

    right = parse_additive(p);
    node = sema_binary(p, op == TOKEN_LSHIFT ? AST_LSHIFT : AST_RSHIFT, left,
                       right, op_tok);

    left = node;
  }

  return left;
}

/*
 * relational-expression:
 *   shift-expression
 *   relational-expression '<' shift-expression
 *   relational-expression '>' shift-expression
 *   relational-expression '<=' shift-expression
 *   relational-expression '>=' shift-expression
 * (C11 6.5.8)
 */
static AstNode *parse_relational(Parser *p) {
  AstNode *left;

  left = parse_shift(p);

  while (p->current.kind == TOKEN_LESS || p->current.kind == TOKEN_GREATER ||
         p->current.kind == TOKEN_LESS_EQ ||
         p->current.kind == TOKEN_GREATER_EQ) {
    AstKind ty;
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    switch (p->current.kind) {
    case TOKEN_LESS:
      ty = AST_LT;
      break;
    case TOKEN_GREATER:
      ty = AST_GT;
      break;
    case TOKEN_LESS_EQ:
      ty = AST_LE;
      break;
    case TOKEN_GREATER_EQ:
      ty = AST_GE;
      break;
    default:
      fprintf(stderr, "unreachable %s:%d", __FILE__, __LINE__);
      exit(1);
    }

    /* Consume '<' or '>' or '<=' or '>=' */
    parser_advance(p);

    right = parse_shift(p);
    node = sema_binary(p, ty, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * equality-expression:
 *   relational-expression
 *   equality-expression '==' relational-expression
 *   equality-expression '!=' relational-expression
 * (C11 6.5.9)
 */
static AstNode *parse_equality(Parser *p) {
  AstNode *left;

  left = parse_relational(p);

  while (p->current.kind == TOKEN_EQ_EQ || p->current.kind == TOKEN_NOT_EQ) {
    TokenKind op;
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    op = p->current.kind;
    /* Consume '==' or '!=' */
    parser_advance(p);

    right = parse_relational(p);
    node = sema_binary(p, op == TOKEN_EQ_EQ ? AST_EQ : AST_NE, left, right,
                       op_tok);

    left = node;
  }

  return left;
}

/*
 * AND-expression:
 *   equality-expression
 *   AND-expression '&' equality-expression
 * (C11 6.5.10)
 */
static AstNode *parse_bitwise_and(Parser *p) {
  AstNode *left;

  left = parse_equality(p);

  while (p->current.kind == TOKEN_BIT_AND) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '&' */
    parser_advance(p);

    right = parse_equality(p);
    node = sema_binary(p, AST_BIT_AND, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * exclusive-OR-expression:
 *   AND-expression
 *   exclusive-OR-expression '^' AND-expression
 * (C11 6.5.11)
 */
static AstNode *parse_bitwise_xor(Parser *p) {
  AstNode *left;

  left = parse_bitwise_and(p);

  while (p->current.kind == TOKEN_BIT_XOR) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '^' */
    parser_advance(p);

    right = parse_bitwise_and(p);
    node = sema_binary(p, AST_BIT_XOR, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * inclusive-OR-expression:
 *   exclusive-OR-expression
 *   inclusive-OR-expression '|' exclusive-OR-expression
 * (C11 6.5.12)
 */
static AstNode *parse_bitwise_or(Parser *p) {
  AstNode *left;

  left = parse_bitwise_xor(p);

  while (p->current.kind == TOKEN_BIT_OR) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '|' */
    parser_advance(p);

    right = parse_bitwise_xor(p);
    node = sema_binary(p, AST_BIT_OR, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * logical-AND-expression:
 *   inclusive-OR-expression
 *   logical-AND-expression '&&' inclusive-OR-expression
 * (C11 6.5.13)
 */
static AstNode *parse_logical_and(Parser *p) {
  AstNode *left;

  left = parse_bitwise_or(p);

  while (p->current.kind == TOKEN_AND_AND) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '&&' */
    parser_advance(p);

    right = parse_bitwise_or(p);
    node = sema_binary(p, AST_LOGICAL_AND, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * logical-OR-expression:
 *   logical-AND-expression
 *   logical-OR-expression '||' logical-AND-expression
 * (C11 6.5.14)
 */
static AstNode *parse_logical_or(Parser *p) {
  AstNode *left;

  left = parse_logical_and(p);

  while (p->current.kind == TOKEN_OR_OR) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume '||' */
    parser_advance(p);

    right = parse_logical_and(p);
    node = sema_binary(p, AST_LOGICAL_OR, left, right, op_tok);

    left = node;
  }

  return left;
}

/*
 * expression:
 *   assignment-expression
 *   expression ',' assignment-expression
 * (C11 6.5.17; the left operand is evaluated and its value discarded,
 *  its side effects kept, before the right operand is evaluated)
 */
static AstNode *parse_expression(Parser *p) {
  AstNode *left;

  left = parse_assignment(p);
  while (p->current.kind == TOKEN_COMMA) {
    AstNode *right;
    AstNode *node;
    Token op_tok = p->current;

    /* Consume ',' */
    parser_advance(p);
    right = parse_assignment(p);
    node = sema_binary(p, AST_COMMA, left, right, op_tok);
    left = node;
  }
  return left;
}

/*
 * Parse a controlling expression and apply scalar condition checks.
 * The caller handles its surrounding parentheses or semicolons.
 */
static AstNode *parse_condition(Parser *p) {
  return sema_check_condition(p, parse_expression(p));
}

/* Struct values need aggregate codegen, including in discarded expressions. */
static AstNode *parse_discarded_expression(Parser *p) {
  AstNode *expr = parse_expression(p);
  if (expr->type->kind == TYPE_STRUCT)
    error_at(p, expr->loc, "struct values are not supported yet");
  return expr;
}

/*
 * conditional-expression:
 *   logical-OR-expression
 *   logical-OR-expression '?' expression ':' conditional-expression
 * (C11 6.5.15; the middle operand is a full expression, the third a
 *  conditional-expression, which makes the operator right-associative)
 */
static AstNode *parse_conditional(Parser *p) {
  AstNode *node;

  node = parse_logical_or(p);
  if (p->current.kind == TOKEN_QUESTION) {
    Token op_tok = p->current;
    AstNode *cond = node;
    AstNode *then_expr;
    AstNode *else_expr;

    /* Consume '?' */
    parser_advance(p);
    then_expr = parse_expression(p);
    parse_expect(p, TOKEN_COLON, "expect ':'");
    else_expr = parse_conditional(p);

    node = sema_conditional(p, cond, then_expr, else_expr, op_tok);
  }
  return node;
}

/*
 * assignment-expression:
 *   conditional-expression
 *   conditional-expression assignment-operator assignment-expression
 * (C11 6.5.16; the left operand must be an lvalue. assignment-operator:
 *  = += -= *= /= %= <<= >>= &= ^= |=)
 */
static AstNode *parse_assignment(Parser *p) {
  AstNode *left = parse_conditional(p);
  if (is_assignment_op(p->current.kind)) {
    Token op = p->current;
    AstNode *rhs;
    parser_advance(p);
    rhs = parse_assignment(p);
    return sema_assignment(p, op, left, rhs);
  }
  return left;
}

/*
 * jump-statement:
 *   'return' expression_opt ';'
 * The function's return type determines whether a value is required.
 */
static AstNode *parse_return_stmt(Parser *p) {
  AstNode *ast;
  Token loc = p->current;
  parse_expect(p, TOKEN_RETURN, "expect 'return'");
  if (p->current.kind == TOKEN_SEMICOLON &&
      p->current_function->return_type->kind != TYPE_VOID)
    error_at(p, loc, "expect return expr");

  ast = new_ast_node(p, AST_RETURN);
  ast->loc = loc;
  if (p->current.kind != TOKEN_SEMICOLON)
    ast->d.ret.expr = parse_expression(p);
  sema_check_return(p, ast);
  parse_semicolon(p);

  return ast;
}

/*
 * compound-statement:
 *   '{' block-item* '}'
 * (C11 6.8.2)
 *
 * Parse in the current scope. A function body shares the scope already entered
 * for its parameters; nested blocks enter a new scope through parse_block.
 */
static AstNode *parse_compound_stmt(Parser *p) {
  AstNode *block;
  AstNode **tail;

  block = new_ast_node(p, AST_BLOCK);
  tail = &block->d.block.first_stmt;

  parse_expect(p, TOKEN_LBRACE, "expect '{'");
  while (p->current.kind != TOKEN_RBRACE) {
    if (p->current.kind == TOKEN_EOF)
      parse_error(p, "expect '}'");

    *tail = parse_stmt(p, /*allow_decl=*/true);
    tail = &(*tail)->next;
  }

  parse_expect(p, TOKEN_RBRACE, "expect '}'");
  return block;
}

static AstNode *parse_block(Parser *p) {
  AstNode *block;

  enter_scope(p);
  block = parse_compound_stmt(p);
  leave_scope(p);
  return block;
}

/*
 * File-scope objects support scalar constants and character array strings.
 * Each declarator uses the same base type; compatible redeclarations produce
 * no additional AST node.
 */
static AstNode *parse_global_declaration(Parser *p, Type *base, Type *type,
                                         Token name) {
  AstNode *head = NULL;
  AstNode **tail = &head;
  for (;;) {
    AstNode *decl;
    if (name.kind != TOKEN_IDENTIFIER)
      error_at(p, name, "expect variable name");
    if (p->current.kind == TOKEN_LPAREN)
      parse_error(p, "function declarations in a declaration list are not "
                     "supported yet");
    decl = declare_global_variable(p, name, type);
    if (p->current.kind == TOKEN_ASSIGN) {
      sema_initialize_global_variable(p, name, parse_initializer(p, type));
    }
    if (decl) {
      *tail = decl;
      tail = &decl->next;
    }
    if (p->current.kind != TOKEN_COMMA)
      break;
    parser_advance(p);
    type = parse_declarator(p, base, &name);
  }
  parse_semicolon(p);
  return head;
}

/*
 * Parse a function's parameter declarations and optional body after its return
 * type and name have been consumed.
 */
static AstNode *parse_function_declaration(Parser *p, Type *return_type,
                                           Token func_name) {
  AstNode *fn = new_ast_node(p, AST_FUNCTION);
  Function *func;
  Variable *params = NULL;
  Vector param_types;
  int nparams;
  int definition = 0;
  int explicit_params = 0;
  Token unnamed_param = {0};

  vector_init(&param_types, sizeof(Type *));
  fn->loc = func_name;
  fn->d.function.name = func_name;
  if (return_type->kind == TYPE_ARRAY)
    error_at(p, func_name, "function cannot return an array");
  if (return_type->kind == TYPE_STRUCT)
    error_at(p, func_name, "returning structs by value is not supported yet");

  parse_expect(p, TOKEN_LPAREN, "expect '('");
  enter_scope(p);

  if (p->current.kind == TOKEN_VOID && p->lookahead.kind == TOKEN_RPAREN) {
    explicit_params = 1;
    parser_advance(p);
  } else if (p->current.kind != TOKEN_RPAREN) {
    explicit_params = 1;
    for (;;) {
      Variable *param;
      Token param_name;
      Token param_loc = p->current;
      Type *param_type;

      param_type = parse_declspecs(p, NULL);
      param_type = parse_declarator(p, param_type, &param_name);
      if (param_type->kind == TYPE_ARRAY) {
        param_type = pointer_to(param_type->base);
        if (!param_type)
          parse_error(p, "out of memory");
      }
      if (param_type->kind == TYPE_VOID && param_types.len == 0 &&
          param_name.kind != TOKEN_IDENTIFIER &&
          p->current.kind == TOKEN_RPAREN)
        break;
      if (param_type->kind == TYPE_VOID)
        error_at(p,
                 param_name.kind == TOKEN_IDENTIFIER ? param_name : p->current,
                 "parameter has void type");
      if (param_type->kind == TYPE_STRUCT)
        error_at(p,
                 param_name.kind == TOKEN_IDENTIFIER ? param_name : p->current,
                 "passing structs by value is not supported yet");
      if (param_types.len >= INT_MAX)
        parse_error(p, "too many function parameters");
      if (!vector_push(&param_types, &param_type))
        parse_error(p, "out of memory");

      if (param_name.kind == TOKEN_IDENTIFIER) {
        sema_check_variable_name(p, param_name, true);

        param = new_variable(p, param_name, param_type);
        param->is_parameter = true;
        param->next = params;
        params = param;
      } else {
        if (p->current.kind != TOKEN_COMMA && p->current.kind != TOKEN_RPAREN)
          parse_error(p, "expect parameter name");
        if (!unnamed_param.start)
          unnamed_param = param_loc;
      }

      if (p->current.kind == TOKEN_COMMA) {
        parser_advance(p);
        continue;
      }
      break;
    }
  }
  parse_expect(p, TOKEN_RPAREN, "expect ')'");

  if (p->current.kind != TOKEN_SEMICOLON && p->current.kind != TOKEN_LBRACE)
    parse_error(p, "expect ';' or '{'");

  definition = p->current.kind == TOKEN_LBRACE;
  if (definition && unnamed_param.start)
    error_at(p, unnamed_param, "expect parameter name in function definition");

  nparams = (int)param_types.len;
  func = declare_function(p, func_name, return_type, param_types.data, nparams,
                          explicit_params || definition, definition);
  vector_free(&param_types);

  if (!definition) {
    /* Consume ';' */
    parser_advance(p);
    leave_scope(p);
    return NULL;
  }

  p->current_function = func;
  fn->d.function.params = params;
  fn->d.function.body = parse_compound_stmt(p);
  leave_scope(p);
  check_gotos(p);
  return fn;
}

/*
 * external-declaration: function-definition | declaration (C11 6.9).
 * A '(' after the declarator starts a function; otherwise parse objects.
 */
static AstNode *parse_external_declaration(Parser *p) {
  Type *base;
  Type *type;
  Token name;
  bool is_typedef;

  reset_function_state(p);
  base = parse_declspecs(p, &is_typedef);
  if (is_typedef) {
    parse_typedef_declarators(p, base);
    return NULL;
  }
  if ((base->kind == TYPE_STRUCT || base->kind == TYPE_ENUM) &&
      p->current.kind == TOKEN_SEMICOLON) {
    parser_advance(p);
    return NULL;
  }
  type = parse_declarator(p, base, &name);
  if (name.kind != TOKEN_IDENTIFIER)
    error_at(p, name, "expect declaration name");
  if (p->current.kind == TOKEN_LPAREN)
    return parse_function_declaration(p, type, name);
  return parse_global_declaration(p, base, type, name);
}

/*
 * selection-statement:
 *   'if' '(' expression ')' statement
 *   'if' '(' expression ')' statement 'else' statement
 * (C11 6.8.4.1; the recursive call to parse_stmt for the branches
 *  gives 'else' the C association with the nearest unmatched 'if')
 */
static AstNode *parse_if_stmt(Parser *p) {
  AstNode *if_node;

  parse_expect(p, TOKEN_IF, "expect 'if'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");
  if_node = new_ast_node(p, AST_IF);
  if_node->d.if_stmt.cond = parse_condition(p);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  if_node->d.if_stmt.then_stmt = parse_stmt(p, /*allow_decl=*/false);
  if (p->current.kind == TOKEN_ELSE) {
    /* Consume 'else' */
    parser_advance(p);
    if_node->d.if_stmt.else_stmt = parse_stmt(p, /*allow_decl=*/false);
  }
  return if_node;
}

/*
 * iteration-statement:
 *   'while' '(' expression ')' statement
 * (C11 6.8.5.1)
 */
static AstNode *parse_while_stmt(Parser *p) {
  AstNode *while_node;

  parse_expect(p, TOKEN_WHILE, "expect 'while'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");
  while_node = new_ast_node(p, AST_WHILE);
  while_node->d.while_stmt.cond = parse_condition(p);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  ++p->loop_depth;
  while_node->d.while_stmt.body = parse_stmt(p, /*allow_decl=*/false);
  --p->loop_depth;
  return while_node;
}

/*
 * iteration-statement:
 *   'for' '(' expression_opt ';' expression_opt ';' expression_opt ')'
 *       statement
 *   'for' '(' declaration expression_opt ';' expression_opt ')' statement
 * (C11 6.8.5.3; a declaration in the init clause is scoped to the loop,
 *  and an omitted condition means a nonzero constant)
 */
static AstNode *parse_for_stmt(Parser *p) {
  AstNode *for_node;

  parse_expect(p, TOKEN_FOR, "expect 'for'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");

  for_node = new_ast_node(p, AST_FOR);

  /* A declaration in the init clause is scoped to the whole loop. */
  enter_scope(p);

  if (p->current.kind == TOKEN_TYPEDEF || is_type_specifier(p, p->current)) {
    /* parse_decl consumes the declaration's own ';'. */
    for_node->d.for_stmt.init = parse_decl(p, false);
  } else if (p->current.kind == TOKEN_SEMICOLON) {
    /* Omitted init clause; consume the ';'. */
    parser_advance(p);
  } else {
    AstNode *init = new_ast_node(p, AST_EXPR_STMT);
    init->d.expr_stmt.expr = parse_discarded_expression(p);
    parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
    for_node->d.for_stmt.init = init;
  }

  if (p->current.kind != TOKEN_SEMICOLON) {
    for_node->d.for_stmt.cond = parse_condition(p);
  }
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");

  if (p->current.kind != TOKEN_RPAREN)
    for_node->d.for_stmt.post = parse_discarded_expression(p);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");

  ++p->loop_depth;
  for_node->d.for_stmt.body = parse_stmt(p, /*allow_decl=*/false);
  --p->loop_depth;
  leave_scope(p);

  return for_node;
}

/*
 * iteration-statement:
 *   'do' statement 'while' '(' expression ')' ';'
 * (C11 6.8.5.2)
 */
static AstNode *parse_do_stmt(Parser *p) {
  AstNode *do_node;

  parse_expect(p, TOKEN_DO, "expect 'do'");
  do_node = new_ast_node(p, AST_DO);
  ++p->loop_depth;
  do_node->d.do_stmt.body = parse_stmt(p, /*allow_decl=*/false);
  --p->loop_depth;
  parse_expect(p, TOKEN_WHILE, "expect 'while'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");
  do_node->d.do_stmt.cond = parse_condition(p);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  /* The trailing ';' is part of the do-statement itself. */
  parse_semicolon(p);
  return do_node;
}

/*
 * jump-statement:
 *   'break' ';'
 * (C11 6.8.6.3, valid only within a loop)
 */
static AstNode *parse_break_stmt(Parser *p) {
  /* Check before consuming so the error lands on 'break' itself. */
  if (p->loop_depth == 0 && !p->current_switch)
    parse_error(p, "break statement not within loop or switch");
  parse_expect(p, TOKEN_BREAK, "expect 'break'");
  parse_semicolon(p);
  return new_ast_node(p, AST_BREAK);
}

/*
 * jump-statement:
 *   'continue' ';'
 * (C11 6.8.6.2, valid only within a loop)
 */
static AstNode *parse_continue_stmt(Parser *p) {
  /* Check before consuming so the error lands on 'continue' itself. */
  if (p->loop_depth == 0)
    parse_error(p, "continue statement not within loop");
  parse_expect(p, TOKEN_CONTINUE, "expect 'continue'");
  parse_semicolon(p);
  return new_ast_node(p, AST_CONTINUE);
}

/*
 * jump-statement:
 *   'goto' identifier ';'
 * (C11 6.8.6.1; the target is validated after the function body is
 *  parsed, since a label may be declared after the goto that uses it)
 */
static AstNode *parse_goto_stmt(Parser *p) {
  AstNode *goto_node;
  GotoRef *ref;
  Token goto_tok;

  goto_tok = p->current;
  parse_expect(p, TOKEN_GOTO, "expect 'goto'");
  if (p->current.kind != TOKEN_IDENTIFIER)
    parse_error(p, "expect label name");

  ref = (GotoRef *)calloc(1, sizeof(GotoRef));
  if (!ref)
    parse_error(p, "out of memory");
  ref->name = p->current;
  ref->loc = goto_tok;
  ref->next = p->goto_refs;
  p->goto_refs = ref;

  goto_node = new_ast_node(p, AST_GOTO);
  goto_node->d.goto_stmt.name = p->current;
  /* Consume the label name. */
  parser_advance(p);
  parse_semicolon(p);
  return goto_node;
}

/*
 * labeled-statement:
 *   identifier ':' statement
 * (C11 6.8.1; labels have function scope, so the label table survives
 *  block exit, unlike ordinary identifier bindings)
 */
static AstNode *parse_label_stmt(Parser *p) {
  AstNode *label_node;
  Label *label;
  Token name = p->current;

  /*
   * The duplicate check runs while 'current' is still the label itself,
   * so the error lands on it.
   */
  for (label = p->labels; label; label = label->next)
    if (same_identifier(label->name, name))
      parse_error(p, "duplicate label");

  /* Consume the identifier and ':'. */
  parser_advance(p);
  parse_expect(p, TOKEN_COLON, "expect ':'");

  label = (Label *)calloc(1, sizeof(Label));
  if (!label)
    parse_error(p, "out of memory");
  label->name = name;
  label->next = p->labels;
  p->labels = label;

  label_node = new_ast_node(p, AST_LABEL);
  label_node->d.label_stmt.name = name;
  label_node->d.label_stmt.stmt = parse_stmt(p, /*allow_decl=*/false);
  return label_node;
}

/*
 * selection-statement:
 *   'switch' '(' expression ')' statement
 * (C11 6.8.4.2; 'case' and 'default' are valid only within the body,
 *  and 'break' exits the switch)
 */
static AstNode *parse_switch_stmt(Parser *p) {
  AstNode *switch_node;
  AstNode *outer_switch;

  parse_expect(p, TOKEN_SWITCH, "expect 'switch'");
  parse_expect(p, TOKEN_LPAREN, "expect '('");
  switch_node = new_ast_node(p, AST_SWITCH);
  switch_node->d.switch_stmt.cond =
      sema_switch_condition(p, parse_expression(p));
  parse_expect(p, TOKEN_RPAREN, "expect ')'");

  /*
   * Case labels in the body belong to this switch; nested switches save
   * and restore the pointer to get their own.
   */
  outer_switch = p->current_switch;
  p->current_switch = switch_node;

  switch_node->d.switch_stmt.body = parse_stmt(p, /*allow_decl=*/false);

  p->current_switch = outer_switch;

  return switch_node;
}

/*
 * labeled-statement:
 *   'case' constant-expression ':' statement
 * (C11 6.8.4.2, valid only within a switch)
 */
static AstNode *parse_case_stmt(Parser *p) {
  AstNode *case_node;
  AstNode *expr;
  CaseList *entry;
  int64_t val;
  Token case_tok;

  /* Check before consuming so the error lands on 'case' itself. */
  if (!p->current_switch)
    parse_error(p, "case label not within a switch statement");

  case_tok = p->current;
  parse_expect(p, TOKEN_CASE, "expect 'case'");
  expr = parse_assignment(p);
  if (!is_integer(expr->type))
    error_at(p, expr->loc, "case label must have integer type");
  expr = sema_convert_expr(p, p->current_switch->d.switch_stmt.cond->type, expr,
                           case_tok, "case label");
  val = eval_const_expr(p, expr);

  for (entry = p->current_switch->d.switch_stmt.cases; entry;
       entry = entry->next)
    if (!entry->node->d.case_stmt.is_default &&
        entry->node->d.case_stmt.val == val) {
      p->current = case_tok;
      parse_error(p, "duplicate case value");
    }

  parse_expect(p, TOKEN_COLON, "expect ':'");

  case_node = new_ast_node(p, AST_CASE);
  case_node->d.case_stmt.val = val;

  entry = (CaseList *)calloc(1, sizeof(CaseList));
  if (!entry)
    parse_error(p, "out of memory");
  entry->node = case_node;
  entry->next = p->current_switch->d.switch_stmt.cases;
  p->current_switch->d.switch_stmt.cases = entry;

  case_node->d.case_stmt.stmt = parse_stmt(p, /*allow_decl=*/false);
  return case_node;
}

/*
 * labeled-statement:
 *   'default' ':' statement
 * (C11 6.8.4.2, valid only within a switch; represented as an AST_CASE
 *  with is_default set, like GCC's CASE_LABEL_EXPR)
 */
static AstNode *parse_default_stmt(Parser *p) {
  AstNode *default_node;
  CaseList *entry;

  /* Check before consuming so the error lands on 'default' itself. */
  if (!p->current_switch)
    parse_error(p, "default label not within a switch statement");

  for (entry = p->current_switch->d.switch_stmt.cases; entry;
       entry = entry->next)
    if (entry->node->d.case_stmt.is_default)
      parse_error(p, "multiple default labels in one switch");

  parse_expect(p, TOKEN_DEFAULT, "expect 'default'");
  parse_expect(p, TOKEN_COLON, "expect ':'");

  default_node = new_ast_node(p, AST_CASE);
  default_node->d.case_stmt.is_default = 1;

  entry = (CaseList *)calloc(1, sizeof(CaseList));
  if (!entry)
    parse_error(p, "out of memory");
  entry->node = default_node;
  entry->next = p->current_switch->d.switch_stmt.cases;
  p->current_switch->d.switch_stmt.cases = entry;

  default_node->d.case_stmt.stmt = parse_stmt(p, /*allow_decl=*/false);
  return default_node;
}

/*
 * statement:
 *   jump-statement | selection-statement | iteration-statement
 *   labeled-statement | compound-statement | expression-statement | ';'
 * (C11 6.8; as in GCC and Clang, anything not dispatched above is
 *  attempted as an expression-statement, whose parser diagnoses
 *  tokens that cannot start an expression. Declarations are block items,
 *  not statements, so they are accepted only when allow_decl is set —
 *  i.e. directly inside a compound-statement, like Clang's
 *  ParsedStmtContext::AllowDeclarationsInC)
 */
static AstNode *parse_stmt(Parser *p, bool allow_decl) {
  AstNode *stmt;

  if (p->current.kind == TOKEN_IDENTIFIER && p->lookahead.kind == TOKEN_COLON)
    return parse_label_stmt(p);

  if (p->current.kind == TOKEN_TYPEDEF || is_type_specifier(p, p->current)) {
    if (allow_decl)
      return parse_decl(p, true);
    parse_error(p, "expect statement");
  }

  switch (p->current.kind) {
  case TOKEN_RETURN:
    return parse_return_stmt(p);
  case TOKEN_IF:
    return parse_if_stmt(p);
  case TOKEN_WHILE:
    return parse_while_stmt(p);
  case TOKEN_FOR:
    return parse_for_stmt(p);
  case TOKEN_DO:
    return parse_do_stmt(p);
  case TOKEN_BREAK:
    return parse_break_stmt(p);
  case TOKEN_CONTINUE:
    return parse_continue_stmt(p);
  case TOKEN_GOTO:
    return parse_goto_stmt(p);
  case TOKEN_SWITCH:
    return parse_switch_stmt(p);
  case TOKEN_CASE:
    return parse_case_stmt(p);
  case TOKEN_DEFAULT:
    return parse_default_stmt(p);
  case TOKEN_LBRACE:
    return parse_block(p);
  case TOKEN_SEMICOLON:
    stmt = new_ast_node(p, AST_EXPR_STMT);
    parser_advance(p);
    return stmt;
  default:
    stmt = new_ast_node(p, AST_EXPR_STMT);
    stmt->d.expr_stmt.expr = parse_discarded_expression(p);
    parse_semicolon(p);
    return stmt;
  }
}

/*
 * translation-unit:
 *   external-definition*
 * (C11 6.9)
 */
AstNode *parse_translation_unit(const char *filename, const char *source) {
  Parser p;
  AstNode *head = NULL;
  AstNode **tail = &head;

  parser_init(&p, filename, source);

  /* Keep the empty-input diagnostics: the loop would just not run. */
  if (p.current.kind == TOKEN_EOF)
    parse_error(&p, "expect type specifier");

  while (p.current.kind != TOKEN_EOF) {
    *tail = parse_external_declaration(&p);
    while (*tail)
      tail = &(*tail)->next;
  }

  sema_check_global_variables(&p);
  check_calls(&p);
  return head;
}
