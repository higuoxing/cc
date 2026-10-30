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

extern bool same_identifier(Token a, Token b);
/*
 * Expression and type-name lookup finds the nearest visible binding.
 * Redeclaration checks look only in the scope receiving the declaration.
 */
extern Symbol *find_symbol(Parser *p, Token name);
extern Symbol *find_symbol_in_scope(Parser *p, Token name, int scope_depth);
extern Symbol *add_symbol(Parser *p, Token name, SymbolKind kind,
                          void *declaration, int scope_depth);
extern void sema_check_variable_name(Parser *p, Token name, bool is_parameter);
extern void declare_typedef(Parser *p, Token name, Type *type);
extern Enum *declare_enumerator(Parser *p, Token name, AstNode *initializer,
                                     int64_t next_value);
extern AstNode *sema_enumerator_expr(Parser *p, Enum *enumerator, Token loc);
extern int64_t eval_const_expr(Parser *p, AstNode *expr);
extern void check_calls(Parser *p);
extern void check_gotos(Parser *p);

extern Function *declare_function(Parser *p, Token name, Type *return_type,
                                       Type **param_types, int nparams,
                                       bool has_prototype, bool definition);
extern AstNode *sema_switch_condition(Parser *p, AstNode *expr);
extern AstNode *sema_check_condition(Parser *p, AstNode *expr);
extern AstNode *sema_subscript(Parser *p, AstNode *base, AstNode *index,
                               Token loc);
extern AstNode *sema_member(Parser *p, AstNode *base, Token op, Token name);
extern void sema_check_nonvoid_expr(Parser *p, AstNode *expr);
extern AstNode *sema_convert_expr(Parser *p, Type *target, AstNode *expr,
                                  Token loc, const char *context);
extern void sema_check_call(Parser *p, Function *func, AstNode *call, int nargs);
extern void sema_check_return(Parser *p, AstNode *stmt);

extern AstNode *sema_integer_literal(Parser *p, Token loc);
extern AstNode *sema_string_literal(Parser *p, Token loc);
extern AstNode *sema_sizeof(Parser *p, Type *type, Token loc);
extern AstNode *sema_unary(Parser *p, AstKind op, AstNode *operand, Token loc);
extern AstNode *sema_binary(Parser *p, AstKind kind, AstNode *lhs, AstNode *rhs,
                           Token loc);
extern AstNode *sema_assignment(Parser *p, Token op, AstNode *lhs,
                               AstNode *rhs);
extern AstNode *sema_conditional(Parser *p, AstNode *cond, AstNode *then_expr,
                                AstNode *else_expr, Token loc);

#endif
