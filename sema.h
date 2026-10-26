/*
 * Sema for the self-hosted C compiler
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

#ifndef _SEMA_H_
#define _SEMA_H_

#include <stdbool.h>

#include "parser.h"

/*
 * Called during parsing to check constructs and build typed expressions.
 * The parser owns grammar and precedence; sema owns expression meaning.
 * Parser remains the shared context during this incremental extraction.
 */

extern Type builtin_type_int;
extern Type builtin_type_void;

extern bool same_identifier(Token a, Token b);
extern Function *find_function(Parser *p, Token name);
extern int type_size(Type *type);
extern int64_t eval_const_expr(Parser *p, AstNode *expr);
extern void check_calls(Parser *p);
extern void check_gotos(Parser *p);

extern Type *sema_ptr_type(Parser *p, Type *base);
extern AstNode *sema_unary(Parser *p, AstKind op, AstNode *operand, Token loc);
extern AstNode *sema_binary(Parser *p, AstKind kind, AstNode *lhs, AstNode *rhs,
                           Token loc);
extern AstNode *sema_assignment(Parser *p, Token op, AstNode *lhs,
                               AstNode *rhs);
extern AstNode *sema_conditional(Parser *p, AstNode *cond, AstNode *then_expr,
                                AstNode *else_expr, Token loc);

#endif
