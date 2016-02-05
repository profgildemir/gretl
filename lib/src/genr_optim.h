/* 
 *  gretl -- Gnu Regression, Econometrics and Time-series Library
 *  Copyright (C) 2001 Allin Cottrell and Riccardo "Jack" Lucchetti
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifndef GENR_OPTIM_H_
#define GENR_OPTIM_H_

/* private header, setting defines for various aspects
   of "genr" optimization; referenced in monte_carlo.c,
   gretl_func.c and geneval.c
*/

#define LOOPSAVE 1           /* keep an eye on this! */
#define GEN_STORE_UVARS 1    /* this too! */

#if LOOPSAVE && GEN_STORE_UVARS
# define LOOPSAVE_PLUS 1
#else
# define LOOPSAVE_PLUS 0
#endif

#endif /* GENR_OPTIM_H_ */
