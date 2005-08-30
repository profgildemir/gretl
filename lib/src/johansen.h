/* gretl - The Gnu Regression, Econometrics and Time-series Library
 * Copyright (C) 2005 Allin Cottrell
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

#ifndef JOHANSEN_H_
#define JOHANSEN_H_

#include "gretl_matrix.h"

typedef enum {
    J_NO_CONST = 0,
    J_REST_CONST,
    J_UNREST_CONST,
    J_REST_TREND,
    J_UNREST_TREND
} JohansenCode;

typedef struct JohansenInfo_ JohansenInfo;

struct JohansenInfo_ {
    int ID;               /* for identifying saved vars */
    JohansenCode code;    /* see above */
    int *list;            /* list of endogenous and exogenous vars */
    int *difflist;        /* list containing first diffs of endogenous vars */
    int *biglist;         /* list containing all regressors in each eqn */
    int rank;             /* if specified, chosen cointegration rank, else 0 */
    int seasonals;        /* number of seasonal dummies included */
    gretl_matrix *u;      /* resids, VAR in differences */
    gretl_matrix *v;      /* resids, second regressions */
    gretl_matrix *w;      /* resids, extra equation for restrictions */
    gretl_matrix *Suu;    /* matrix of cross-products of residuals */
    gretl_matrix *Svv;    /* matrix of cross-products of residuals */
    gretl_matrix *Suv;    /* matrix of cross-products of residuals */
    gretl_matrix *Beta;   /* matrix of eigenvectors */
    gretl_matrix *Alpha;  /* matrix of adjustments */
    gretl_matrix *Bse;    /* standard errors of EC terms */
};

struct GRETL_VAR_ {
    int ci;              /* command index */
    int err;             /* error code */
    int neqns;           /* number of equations in system */
    int order;           /* lag order */
    int t1;              /* starting observation */
    int t2;              /* ending observation */
    int T;               /* number of observations */
    int ifc;             /* equations include a constant (1) or not (0) */
    int ncoeff;          /* total coefficients per equation */
    gretl_matrix *A;     /* augmented coefficient matrix */
    gretl_matrix *E;     /* residuals matrix */
    gretl_matrix *C;     /* augmented Cholesky-decomposed error matrix */
    gretl_matrix *S;     /* cross-equation variance matrix */
    gretl_matrix *F;     /* optional forecast matrix */
    MODEL **models;      /* pointers to individual equation estimates */
    double *Fvals;       /* hold results of F-tests */
    double ldet;         /* log-determinant of S */
    double ll;           /* log-likelihood */
    double AIC;          /* Akaike criterion */
    double BIC;          /* Bayesian criterion */
    double LR;           /* for likelihood-ration testing */
    JohansenInfo *jinfo; /* extra information for VECMs */
    char *name;          /* for use in session management */
};
    
#define restricted(v) (v->jinfo->code == J_REST_CONST || \
                       v->jinfo->code == J_REST_TREND)

#define jcode(v) (v->jinfo->code)
#define jrank(v) (v->jinfo->rank)

GRETL_VAR *johansen_test (int order, const int *list, double ***pZ, DATAINFO *pdinfo,
			  gretlopt opt, PRN *prn);

int johansen_test_simple (int order, const int *list, double ***pZ, DATAINFO *pdinfo,
			  gretlopt opt, PRN *prn);

void print_Johansen_test_case (JohansenCode jcode, PRN *prn);

int gretl_VECM_id (GRETL_VAR *vecm);

int gretl_VAR_add_coeff_matrix (GRETL_VAR *var);

int gretl_VAR_add_C_matrix (GRETL_VAR *var);

int gretl_VAR_do_error_decomp (int g, const gretl_matrix *S,
			       gretl_matrix *C);
    
#endif /* JOHANSEN_H_ */

