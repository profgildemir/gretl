/* gretl - The Gnu Regression, Econometrics and Time-series Library
 * Copyright (C) 1999-2004 Ramu Ramanathan and Allin Cottrell
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License 
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this software; if not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef GRETL_FUNC_H
#define GRETL_FUNC_H

int gretl_compiling_function (void);

int gretl_executing_function (void);

int gretl_start_compiling_function (const char *line);

int gretl_function_append_line (const char *line);

int gretl_is_user_function (const char *s);

int gretl_function_start_exec (const char *line);

char *gretl_function_get_line (char *line, int len);

void gretl_function_error (void);

#endif /* GRETL_FUNC_H */
