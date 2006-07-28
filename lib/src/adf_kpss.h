/*
 *  Copyright (c) by Allin Cottrell
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA.
 *
 */

#ifndef ADF_KPSS_H
#define ADF_KPSS_H

typedef enum {
    UR_NO_CONST = 1,
    UR_CONST,
    UR_TREND,
    UR_QUAD_TREND,
    UR_MAX
} AdfCode;

int adf_test (int order, int varno, double ***pZ,
	      DATAINFO *pdinfo, gretlopt opt, PRN *prn);

int kpss_test (int order, int varno, double ***pZ,
	       DATAINFO *pdinfo, gretlopt opt, PRN *prn);

int variance_ratio_test (int q, int varno, double ***pZ,
			 DATAINFO *pdinfo, gretlopt opt, 
			 PRN *prn);

int coint (int order, const int *list, double ***pZ, 
	   DATAINFO *pdinfo, gretlopt opt, PRN *prn);

#endif /* ADF_KPSS_H */
