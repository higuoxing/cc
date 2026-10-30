/*
 * Stack layout for the self-hosted C compiler
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

#ifndef _LAYOUT_H_
#define _LAYOUT_H_

#include "parser.h"

/*
 * Assign parameter and local offsets and function stack sizes in a completed
 * translation unit. Report target stack-size limits before assembly emission.
 */
extern void assign_stack_offsets(const char *filename, AstNode *ast);

#endif
