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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"

static LocalVar *declare_local_var(Parser *p, Token name);
static AstNode *new_ast_node(Parser *p, AstType ty);
static void parser_advance(Parser *p);
static void parse_error(Parser *p, const char *fmt, ...)
    __attribute__((format(printf, 2, 3), noreturn));
static AstNode *parse_expr(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_postfix(Parser *p);
static AstNode *parse_primary(Parser *p);
static AstNode *parse_decl(Parser *p);
static AstNode *parse_return_stmt(Parser *p);
static AstNode *parse_if_stmt(Parser *p);
static AstNode *parse_while_stmt(Parser *p);
static AstNode *parse_stmt(Parser *p);
static AstNode *parse_block(Parser *p);
static AstNode *parse_function(Parser *p);
static bool is_lvalue(AstType ty);
static bool is_increment_op(AstType ty);

static bool same_identifier(Token a, Token b) {
  return a.len == b.len && strncmp(a.start, b.start, a.len) == 0;
}

static LocalVar *find_local_var(Parser *p, Token name) {
  LocalVar *var;
  for (var = p->locals; var; var = var->next) {
    if (same_identifier(var->name, name))
      return var;
  }
  return NULL;
}

static LocalVar *declare_local_var(Parser *p, Token name) {
  LocalVar *var;
  for (var = p->locals; var && var->scope_depth == p->scope_depth;
       var = var->next) {
    if (same_identifier(var->name, name))
      parse_error(p, "duplicate local variable");
  }

  var = (LocalVar *)calloc(1, sizeof(LocalVar));
  if (!var)
    parse_error(p, "out of memory");
  var->name = name;
  var->scope_depth = p->scope_depth;
  p->stack_size += 4; // All locals are 32-bit int for now.
  var->offset = p->stack_size;
  var->next = p->locals;
  p->locals = var;
  return var;
}

static AstNode *new_ast_node(Parser *p, AstType ty) {
  AstNode *node;
  node = calloc(1, sizeof(AstNode));
  if (!node)
    parse_error(p, "out of memory");
  node->type = ty;
  return node;
}

static void parser_advance(Parser *p) {
  p->current = p->lookahead;
  p->lookahead = lexer_next_token(&p->lexer);
}

static void parser_init(Parser *p, const char *filename, const char *source) {
  p->locals = NULL;
  p->scope_depth = 0;
  p->stack_size = 0;
  lexer_init(&p->lexer, filename, source);
  p->current = lexer_next_token(&p->lexer);
  p->lookahead = lexer_next_token(&p->lexer);
}

static void parse_error(Parser *p, const char *fmt, ...) {
  va_list ap;

  fprintf(stderr, "%s:%d:%d: error: ", p->lexer.filename, p->current.line,
          p->current.col);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static void parse_expect(Parser *p, TokenType t, const char *msg) {
  if (p->current.type != t) {
    parse_error(p, "%s", msg);
  }

  parser_advance(p);
}

/*
 * Only variables are lvalues. The results of assignment and prefix
 * increment/decrement are not lvalues in C11, so this matches GCC and Clang
 * in C mode.
 */
static bool is_lvalue(AstType ty) { return ty == AST_VAR; }

static bool is_increment_op(AstType ty) {
  return ty == AST_PRE_INCREMENT || ty == AST_POST_INCREMENT;
}

/*
 * unary-expression:
 *   '-' unary-expression | '~' unary-expression | '!' unary-expression
 *   '++' unary-expression | '--' unary-expression
 *   postfix-expression
 * (C11 6.5.3; '&', '*', sizeof, and _Alignof are not implemented)
 */
static AstNode *parse_unary(Parser *p) {
  if (p->current.type == TOKEN_MINUS || p->current.type == TOKEN_TILDE ||
      p->current.type == TOKEN_BANG || p->current.type == TOKEN_PLUS_PLUS ||
      p->current.type == TOKEN_MINUS_MINUS) {
    AstNode *node;
    AstType op;
    Token op_tok;

    switch (p->current.type) {
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
    default:
      fprintf(stderr, "unreachable %s:%d", __FILE__, __LINE__);
      exit(1);
    }

    // Consume '-' or '~' or '!' or '++' or '--'
    op_tok = p->current;
    parser_advance(p);
    node = new_ast_node(p, op);
    node->d.unary.operand = parse_unary(p);

    if ((op == AST_PRE_INCREMENT || op == AST_PRE_DECREMENT) &&
        !is_lvalue(node->d.unary.operand->type)) {
      // Report at the operator, not at the token after the operand.
      p->current = op_tok;
      parse_error(p, "invalid %s target",
                  is_increment_op(op) ? "increment" : "decrement");
    }

    return node;
  }

  return parse_postfix(p);
}

/*
 * postfix-expression:
 *   primary-expression
 *   postfix-expression '++' | postfix-expression '--'
 * (C11 6.5.2; the operand must be a modifiable lvalue, 6.5.2.4)
 */
static AstNode *parse_postfix(Parser *p) {
  AstNode *left;

  left = parse_primary(p);
  while (p->current.type == TOKEN_PLUS_PLUS ||
         p->current.type == TOKEN_MINUS_MINUS) {
    AstNode *postfixed;
    AstType ty;

    ty = p->current.type == TOKEN_PLUS_PLUS ? AST_POST_INCREMENT
                                            : AST_POST_DECREMENT;

    /*
     * The grammar recurses here, but the operand must still be an lvalue:
     * (a++)++ is invalid in C. Check before consuming so the error lands
     * on the operator.
     */
    if (!is_lvalue(left->type))
      parse_error(p, "invalid %s target",
                  is_increment_op(ty) ? "increment" : "decrement");

    // Consume '++' or '--'
    parser_advance(p);
    postfixed = new_ast_node(p, ty);
    postfixed->d.unary.operand = left;
    left = postfixed;
  }

  return left;
}

/*
 * primary-expression:
 *   integer-constant | '(' expression ')' | identifier
 * (C11 6.5.1; only int literals, no string or character constants)
 */
static AstNode *parse_primary(Parser *p) {
  if (p->current.type == TOKEN_INT_LITERAL) {
    AstNode *expr;
    expr = new_ast_node(p, AST_INT_LITERAL);
    expr->d.int_literal.val = p->current.int_literal_val;

    // Consume integer literal
    parser_advance(p);
    return expr;
  }

  if (p->current.type == TOKEN_LPAREN) {
    AstNode *expr;
    // Consume '('
    parser_advance(p);
    expr = parse_expr(p);
    parse_expect(p, TOKEN_RPAREN, "expect ')'");
    return expr;
  }

  if (p->current.type == TOKEN_IDENTIFIER) {
    AstNode *expr;
    LocalVar *var = find_local_var(p, p->current);
    if (!var)
      parse_error(p, "undeclared identifier");
    expr = new_ast_node(p, AST_VAR);
    expr->d.variable.var = var;
    // Consume identifier.
    parser_advance(p);
    return expr;
  }

  parse_error(p, "expect expr");
  return NULL;
}

/*
 * declaration:
 *   'int' identifier ['=' expression] ';'
 * (C11 6.7, restricted to int and a single declarator)
 */
static AstNode *parse_decl(Parser *p) {
  AstNode *decl;
  Token name;

  parse_expect(p, TOKEN_INT, "expect 'int'");
  if (p->current.type != TOKEN_IDENTIFIER)
    parse_error(p, "expect variable name");

  name = p->current;
  decl = new_ast_node(p, AST_DECL);
  decl->d.decl.var = declare_local_var(p, name);
  // Consume variable name.
  parser_advance(p);

  if (p->current.type == TOKEN_ASSIGN) {
    // Consume '='
    parser_advance(p);
    decl->d.decl.init = parse_expr(p);
  }

  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");

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

  while (p->current.type == TOKEN_STAR || p->current.type == TOKEN_SLASH ||
         p->current.type == TOKEN_PERCENT) {
    TokenType op;
    AstNode *right;
    AstNode *node;

    op = p->current.type;
    // Consume '*' or '/' or '%'
    parser_advance(p);

    right = parse_unary(p);
    node = new_ast_node(p, op == TOKEN_STAR
                               ? AST_MUL
                               : (op == TOKEN_SLASH ? AST_DIV : AST_MOD));
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

  while (p->current.type == TOKEN_PLUS || p->current.type == TOKEN_MINUS) {
    TokenType op;
    AstNode *right;
    AstNode *node;

    op = p->current.type;
    // Consume '+' or '-'
    parser_advance(p);
    right = parse_multiplicative(p);

    node = new_ast_node(p, op == TOKEN_PLUS ? AST_ADD : AST_SUB);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

  while (p->current.type == TOKEN_LSHIFT || p->current.type == TOKEN_RSHIFT) {
    TokenType op;
    AstNode *right;
    AstNode *node;

    op = p->current.type;
    // Consume '<<' or '>>'
    parser_advance(p);

    right = parse_additive(p);
    node = new_ast_node(p, op == TOKEN_LSHIFT ? AST_LSHIFT : AST_RSHIFT);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

  while (p->current.type == TOKEN_LESS || p->current.type == TOKEN_GREATER ||
         p->current.type == TOKEN_LESS_EQ ||
         p->current.type == TOKEN_GREATER_EQ) {
    AstType ty;
    AstNode *right;
    AstNode *node;

    switch (p->current.type) {
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

    // Consume '<' or '>' or '<=' or '>='
    parser_advance(p);

    right = parse_shift(p);
    node = new_ast_node(p, ty);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

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

  while (p->current.type == TOKEN_EQ_EQ || p->current.type == TOKEN_NOT_EQ) {
    TokenType op;
    AstNode *right;
    AstNode *node;

    op = p->current.type;
    // Consume '==' or '!='
    parser_advance(p);

    right = parse_relational(p);
    node = new_ast_node(p, op == TOKEN_EQ_EQ ? AST_EQ : AST_NE);
    node->d.binary.lhs = left;
    node->d.binary.rhs = right;

    left = node;
  }

  return left;
}

/*
 * assignment-expression:
 *   equality-expression
 *   equality-expression assignment-operator assignment-expression
 * (C11 6.5.16, without conditional or comma expressions; the left
 *  operand must be an lvalue. assignment-operator: = += -= *= /= %=
 *  <<= >>=)
 */
static AstNode *parse_expr(Parser *p) {
  AstNode *left;

  left = parse_equality(p);
  if (p->current.type == TOKEN_ASSIGN || p->current.type == TOKEN_PLUS_EQ ||
      p->current.type == TOKEN_MINUS_EQ || p->current.type == TOKEN_STAR_EQ ||
      p->current.type == TOKEN_SLASH_EQ ||
      p->current.type == TOKEN_PERCENT_EQ ||
      p->current.type == TOKEN_LSHIFT_EQ ||
      p->current.type == TOKEN_RSHIFT_EQ) {
    AstNode *node;
    AstNode *rhs;
    TokenType op;

    if (!is_lvalue(left->type))
      parse_error(p, "invalid assignment target");

    op = p->current.type;
    // Consume the assignment operator
    parser_advance(p);

    rhs = parse_expr(p);

    if (op != TOKEN_ASSIGN) {
      /*
       * Compound assignment desugars to 'E1 = E1 <op> E2', where E1
       * is the left operand and E2 the right one (C11 6.5.16.2p3).
       * The standard requires the lvalue E1 to be evaluated only
       * once, which holds here because E1 is a plain variable.
       * lhs_copy is a fresh node: codegen walks a tree and must not
       * see the same node twice.
       */
      AstNode *binop;
      AstNode *lhs_copy;
      AstType binop_ty;

      switch (op) {
      case TOKEN_PLUS_EQ:
        binop_ty = AST_ADD;
        break;
      case TOKEN_MINUS_EQ:
        binop_ty = AST_SUB;
        break;
      case TOKEN_STAR_EQ:
        binop_ty = AST_MUL;
        break;
      case TOKEN_SLASH_EQ:
        binop_ty = AST_DIV;
        break;
      case TOKEN_PERCENT_EQ:
        binop_ty = AST_MOD;
        break;
      case TOKEN_LSHIFT_EQ:
        binop_ty = AST_LSHIFT;
        break;
      case TOKEN_RSHIFT_EQ:
        binop_ty = AST_RSHIFT;
        break;
      default:
        fprintf(stderr, "unreachable %s:%d", __FILE__, __LINE__);
        exit(1);
      }

      binop = new_ast_node(p, binop_ty);
      lhs_copy = new_ast_node(p, AST_VAR);
      lhs_copy->d.variable.var = left->d.variable.var;

      binop->d.binary.lhs = lhs_copy;
      binop->d.binary.rhs = rhs;
      rhs = binop;
    }

    node = new_ast_node(p, AST_ASSIGN);
    node->d.binary.lhs = left;
    node->d.binary.rhs = rhs;
    return node;
  }
  return left;
}

/*
 * jump-statement:
 *   'return' expression ';'
 * (C11 6.8.6.4; bare 'return;' is rejected for now)
 */
static AstNode *parse_return_stmt(Parser *p) {
  AstNode *ast;

  parse_expect(p, TOKEN_RETURN, "expect 'return'");
  if (p->current.type == TOKEN_SEMICOLON)
    parse_error(p, "expect return expr");

  ast = new_ast_node(p, AST_RETURN);
  ast->d.ret.expr = parse_expr(p);
  parse_expect(p, TOKEN_SEMICOLON, "expect ';'");

  return ast;
}

/*
 * compound-statement:
 *   '{' statement* '}'
 * (C11 6.8.2)
 */
static AstNode *parse_block(Parser *p) {
  AstNode *block;
  AstNode **tail;
  LocalVar *outer_locals = p->locals;

  block = new_ast_node(p, AST_BLOCK);
  tail = &block->d.block.first_stmt;

  parse_expect(p, TOKEN_LBRACE, "expect '{'");
  ++p->scope_depth;
  while (p->current.type != TOKEN_RBRACE) {
    if (p->current.type == TOKEN_EOF)
      parse_error(p, "expect '}'");

    *tail = parse_stmt(p);
    tail = &(*tail)->next;
  }

  parse_expect(p, TOKEN_RBRACE, "expect '}'");
  p->locals = outer_locals;
  --p->scope_depth;
  return block;
}

/*
 * function-definition:
 *   'int' identifier '(' ')' compound-statement
 * (C11 6.9.1, restricted to int return type and no parameters)
 */
static AstNode *parse_function(Parser *p) {
  AstNode *fn;

  fn = new_ast_node(p, AST_FUNCTION);
  parse_expect(p, TOKEN_INT, "expect 'int'");
  if (p->current.type != TOKEN_IDENTIFIER)
    parse_error(p, "expect function name");

  fn->d.function.name = p->current;
  // Consume <function_name>
  parser_advance(p);

  parse_expect(p, TOKEN_LPAREN, "expect '('");
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  fn->d.function.body = parse_block(p);
  fn->d.function.stack_size = p->stack_size;
  return fn;
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
  if_node->d.if_stmt.cond = parse_expr(p);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  if_node->d.if_stmt.then_stmt = parse_stmt(p);
  if (p->current.type == TOKEN_ELSE) {
    // Consume 'else'
    parser_advance(p);
    if_node->d.if_stmt.else_stmt = parse_stmt(p);
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
  while_node->d.while_stmt.cond = parse_expr(p);
  parse_expect(p, TOKEN_RPAREN, "expect ')'");
  while_node->d.while_stmt.body = parse_stmt(p);
  return while_node;
}

/*
 * statement:
 *   jump-statement | selection-statement | iteration-statement
 *   compound-statement | declaration | expression-statement | ';'
 * (C11 6.8; as in GCC and Clang, anything not dispatched above is
 *  attempted as an expression-statement, whose parser diagnoses
 *  tokens that cannot start an expression)
 */
static AstNode *parse_stmt(Parser *p) {
  AstNode *stmt;

  switch (p->current.type) {
  case TOKEN_RETURN:
    return parse_return_stmt(p);
  case TOKEN_IF:
    return parse_if_stmt(p);
  case TOKEN_WHILE:
    return parse_while_stmt(p);
  case TOKEN_LBRACE:
    return parse_block(p);
  case TOKEN_INT:
    return parse_decl(p);
  case TOKEN_SEMICOLON:
    stmt = new_ast_node(p, AST_EXPR_STMT);
    parser_advance(p);
    return stmt;
  default:
    stmt = new_ast_node(p, AST_EXPR_STMT);
    stmt->d.expr_stmt.expr = parse_expr(p);
    parse_expect(p, TOKEN_SEMICOLON, "expect ';'");
    return stmt;
  }
}

/*
 * translation-unit:
 *   function-definition EOF
 * (C11 6.9; only a single function is supported)
 */
AstNode *parse_translation_unit(const char *filename, const char *source) {
  Parser p;
  AstNode *ast;

  parser_init(&p, filename, source);
  ast = parse_function(&p);
  parse_expect(&p, TOKEN_EOF, "unexpected token after function");

  return ast;
}
