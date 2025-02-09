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
 */

#include "libgretl.h"
#include "version.h"
#include "libset.h"
#include "kalman.h"
#include "matrix_extra.h"
#include "gretl_bfgs.h"
#include "arma_priv.h"

#include "../cephes/libprob.h"

#define ARMA_DEBUG 0
#define ARMA_MDEBUG 0
#define SHOW_INIT 0

#include "arma_common.c"

#define KALMAN_ALL 999

static const double *as197_llt_callback (const double *b,
					 int i, void *data);

static const double *as154_llt_callback (const double *b,
					 int i, void *data);

int maybe_correct_MA (arma_info *ainfo,
		      double *theta,
		      double *Theta)
{
    int err = 0;

    if (ainfo->q > 0) {
	err = flip_poly(theta, ainfo, 0, 0);
    }
    if (!err && ainfo->Q > 0) {
	err = flip_poly(Theta, ainfo, 0, 1);
    }

    return err;
}

/*
  Given an ARMA process $A(L)B(L) y_t = C(L)D(L) \epsilon_t$, finds the
  roots of the four polynomials -- or just two polynomials if seasonal
  AR and MA effects, B(L) and D(L) are not present -- and attaches
  this information to the ARMA model.

  pmod: MODEL pointer to which the roots info should be attached.

  ainfo: gives various pieces of information on the ARMA model,
  including seasonal and non-seasonal AR and MA orders.

  coeff: ifc + p + q + P + Q vector of coefficients (if an intercept
  is present it is element 0 and is ignored)

  returns: zero on success, non-zero on failure
*/

int arma_model_add_roots (MODEL *pmod, arma_info *ainfo,
			  const double *coeff)
{
    const double *phi =   coeff + ainfo->ifc;
    const double *Phi =     phi + ainfo->np;
    const double *theta =   Phi + ainfo->P;
    const double *Theta = theta + ainfo->nq;
    int nr = ainfo->p + ainfo->P + ainfo->q + ainfo->Q;
    int pmax, qmax, lmax;
    double *temp = NULL, *tmp2 = NULL;
    cmplx *rptr, *roots = NULL;
    int i, k, cerr = 0;

    pmax = (ainfo->p > ainfo->P)? ainfo->p : ainfo->P;
    qmax = (ainfo->q > ainfo->Q)? ainfo->q : ainfo->Q;
    lmax = (pmax > qmax)? pmax : qmax;

    if (pmax == 0 && qmax == 0) {
	return 0;
    }

    temp = malloc((lmax + 1) * sizeof *temp);
    tmp2 = malloc((lmax + 1) * sizeof *tmp2);
    roots = malloc(nr * sizeof *roots);

    if (temp == NULL || tmp2 == NULL || roots == NULL) {
	free(temp);
	free(tmp2);
	free(roots);
	return E_ALLOC;
    }

    temp[0] = 1.0;
    rptr = roots;

    if (ainfo->p > 0) {
	/* A(L), non-seasonal */
	k = 0;
	for (i=0; i<ainfo->p; i++) {
	    if (AR_included(ainfo, i)) {
		temp[i+1] = -phi[k++];
	    } else {
		temp[i+1] = 0;
	    }
	}
	cerr = polrt(temp, tmp2, ainfo->p, rptr);
	rptr += ainfo->p;
    }

    if (!cerr && ainfo->P > 0) {
	/* B(L), seasonal */
	for (i=0; i<ainfo->P; i++) {
	    temp[i+1] = -Phi[i];
	}
	cerr = polrt(temp, tmp2, ainfo->P, rptr);
	rptr += ainfo->P;
    }

    if (!cerr && ainfo->q > 0) {
	/* C(L), non-seasonal */
	k = 0;
	for (i=0; i<ainfo->q; i++) {
	    if (MA_included(ainfo, i)) {
		temp[i+1] = theta[k++];
	    } else {
		temp[i+1] = 0;
	    }
	}
	cerr = polrt(temp, tmp2, ainfo->q, rptr);
	rptr += ainfo->q;
    }

    if (!cerr && ainfo->Q > 0) {
	/* D(L), seasonal */
	for (i=0; i<ainfo->Q; i++) {
	    temp[i+1] = Theta[i];
	}
	cerr = polrt(temp, tmp2, ainfo->Q, rptr);
    }

    free(temp);
    free(tmp2);

    if (cerr) {
	free(roots);
    } else {
	gretl_model_set_data(pmod, "roots", roots, GRETL_TYPE_CMPLX_ARRAY,
			     nr * sizeof *roots);
    }

    return 0;
}

/* below: exact ML using Kalman filter apparatus */

typedef struct kalman_helper_ khelper;

struct kalman_helper_ {
    gretl_matrix_block *B;
    gretl_matrix *S;
    gretl_matrix *P;
    gretl_matrix *F;
    gretl_matrix *A;
    gretl_matrix *H;
    gretl_matrix *Q;
    gretl_matrix *E;
    gretl_matrix *Svar;

    gretl_matrix *Svar2;
    gretl_matrix *vQ;

    gretl_matrix *F_; /* used only for ARIMA via levels */
    gretl_matrix *Q_; /* ditto */
    gretl_matrix *P_; /* ditto */

    arma_info *kainfo;
};

static void kalman_helper_free (khelper *kh)
{
    if (kh != NULL) {
	gretl_matrix_block_destroy(kh->B);
	gretl_matrix_free(kh->Svar2);
	gretl_matrix_free(kh->vQ);
	gretl_matrix_free(kh->F_);
	gretl_matrix_free(kh->Q_);
	gretl_matrix_free(kh->P_);
	free(kh);
    }
}

static khelper *kalman_helper_new (arma_info *ainfo,
				   int r, int k)
{
    khelper *kh;
    int r0, r2;
    int err = 0;

    kh = malloc(sizeof *kh);
    if (kh == NULL) {
	return NULL;
    }

    r0 = ainfo->r0;
    r2 = r0 * r0;

    kh->Svar2 = kh->vQ = NULL;
    kh->F_ = kh->Q_ = kh->P_ = NULL;

    kh->B = gretl_matrix_block_new(&kh->S, r, 1,
				   &kh->P, r, r,
				   &kh->F, r, r,
				   &kh->A, k, 1,
				   &kh->H, r, 1,
				   &kh->Q, r, r,
				   &kh->E, ainfo->fullT, 1,
				   &kh->Svar, r2, r2,
				   NULL);

    if (kh->B == NULL) {
	err = E_ALLOC;
    } else if (arma_using_vech(ainfo)) {
	int m = r0 * (r0 + 1) / 2;

	kh->Svar2 = gretl_matrix_alloc(m, m);
	kh->vQ = gretl_column_vector_alloc(m);
	if (kh->Svar2 == NULL || kh->vQ == NULL) {
	    err = E_ALLOC;
	}
    } else {
	kh->vQ = gretl_column_vector_alloc(r2);
	if (kh->vQ == NULL) {
	    err = E_ALLOC;
	}
    }

    if (!err && arima_levels(ainfo)) {
	kh->F_ = gretl_matrix_alloc(r0, r0);
	kh->Q_ = gretl_matrix_alloc(r0, r0);
	kh->P_ = gretl_matrix_alloc(r0, r0);
	if (kh->F_ == NULL || kh->Q_ == NULL || kh->P_ == NULL) {
	    err = E_ALLOC;
	}
    }

    if (err) {
	kalman_helper_free(kh);
	kh = NULL;
    } else {
	kh->kainfo = ainfo;
    }

    return kh;
}

/* Get the dimension of the state-space representation: note
   that this is augmented if we're estimating an ARIMA
   model using the levels formulation in order to handle
   missing values -- see Harvey and Pierse, "Estimating
   Missing Observations in Economic Time Series", JASA 1984.
*/

static int ainfo_get_state_size (arma_info *ainfo)
{
    int plen = ainfo->p + ainfo->pd * ainfo->P;
    int qlen = ainfo->q + ainfo->pd * ainfo->Q;
    int r = (plen > qlen + 1)? plen : qlen + 1;

    ainfo->r0 = r;

    if (arima_levels(ainfo)) {
	r += ainfo->d + ainfo->pd * ainfo->D;
    }

    return r;
}

static int allocate_ac_mc (arma_info *ainfo)
{
    int m = (ainfo->P > 0) + (ainfo->Q > 0);
    int err = 0;

    if (m > 0) {
	double *ac = NULL, *mc = NULL;
	int n, i = 0;

	ainfo->aux = doubles_array_new(m, 0);
	if (ainfo->aux == NULL) {
	    return E_ALLOC;
	}

	if (ainfo->P > 0) {
	    n = 1 + ainfo->p + ainfo->pd * ainfo->P;
	    ac = malloc(n * sizeof *ac);
	    if (ac == NULL) {
		err = E_ALLOC;
	    } else {
		ainfo->aux[i++] = ac;
	    }
	}

	if (!err && ainfo->Q > 0) {
	    n = 1 + ainfo->q + ainfo->pd * ainfo->Q;
	    mc = malloc(n * sizeof *mc);
	    if (mc == NULL) {
		err = E_ALLOC;
	    } else {
		ainfo->aux[i++] = mc;
	    }
	}

	if (err) {
	    doubles_array_free(ainfo->aux, m);
	} else {
	    ainfo->n_aux = m;
	}
    }

    return err;
}

static void write_big_phi (const double *phi,
			   const double *Phi,
			   arma_info *ainfo,
			   gretl_matrix *F)
{
    int pmax = ainfo->p + ainfo->pd * ainfo->P;
    double *ac = ainfo->aux[0];
    double x, y;
    int i, j, k, ii;

    for (i=0; i<=pmax; i++) {
	ac[i] = 0.0;
    }

    for (j=-1; j<ainfo->P; j++) {
	x = (j < 0)? -1 : Phi[j];
	k = 0.0;
	for (i=-1; i<ainfo->p; i++) {
	    if (i < 0) {
		y = -1;
	    } else if (AR_included(ainfo, i)) {
		y = phi[k++];
	    } else {
		y = 0.0;
	    }
	    ii = (j+1) * ainfo->pd + (i+1);
	    ac[ii] -= x * y;
	}
    }

    for (i=0; i<pmax; i++) {
	gretl_matrix_set(F, 0, i, ac[i+1]);
    }
}

static void write_big_theta (const double *theta,
			     const double *Theta,
			     arma_info *ainfo,
			     gretl_matrix *H,
			     gretl_matrix *F)
{
    int qmax = ainfo->q + ainfo->pd * ainfo->Q;
    int i = (ainfo->P > 0)? 1 : 0;
    double *mc = ainfo->aux[i];
    double x, y;
    int j, k, ii;

    for (i=0; i<=qmax; i++) {
	mc[i] = 0.0;
    }

    for (j=-1; j<ainfo->Q; j++) {
	x = (j < 0)? 1 : Theta[j];
	k = 0;
        for (i=-1; i<ainfo->q; i++) {
	    if (i < 0) {
		y = 1;
	    } else if (MA_included(ainfo, i)) {
		y = theta[k++];
	    } else {
		y = 0.0;
	    }
            ii = (j+1) * ainfo->pd + (i+1);
	    mc[ii] += x * y;
        }
    }

    for (i=1; i<=qmax; i++) {
	if (H != NULL) {
	    H->val[i] = mc[i];
	} else {
	    gretl_matrix_set(F, ainfo->r0, i, mc[i]);
	}
    }
}

static void condense_row (gretl_matrix *targ,
			  const gretl_matrix *src,
			  int targrow, int srcrow,
			  int n)
{
    double x;
    int i, j, k, g;
    int targcol = 0;

    for (j=0; j<n; j++) {
	for (i=j; i<n; i++) {
	    k = j * n + i;
	    g = (k % n) * n + k / n;
	    x = gretl_matrix_get(src, srcrow, k);
	    if (g != k) {
		x += gretl_matrix_get(src, srcrow, g);
	    }
	    gretl_matrix_set(targ, targrow, targcol++, x);
	}
    }
}

static void condense_state_vcv (gretl_matrix *targ,
				const gretl_matrix *src,
				int n)
{
    int posr = 0, posc = 0;
    int i, j;

    for (i=0; i<n; i++) {
	for (j=0; j<n; j++) {
	    if (j >= i) {
		condense_row(targ, src, posr++, posc, n);
	    }
	    posc++;
	}
    }
}

static int kalman_matrices_init (arma_info *ainfo,
				 khelper *kh,
				 const double *y)
{
    int r0 = ainfo->r0;
    int r = kh->F->rows;

    gretl_matrix_zero(kh->A);
    gretl_matrix_zero(kh->S);
    gretl_matrix_zero(kh->P);
    gretl_matrix_zero(kh->F);
    gretl_matrix_inscribe_I(kh->F, 1, 0, r0 - 1);

    gretl_matrix_zero(kh->Q);
    gretl_matrix_set(kh->Q, 0, 0, 1.0);

    gretl_matrix_zero(kh->H);
    gretl_vector_set(kh->H, 0, 1.0);

    if (arima_levels(ainfo)) {
	/* write additional constant elements of F, H and S */
	int d = ainfo->d, D = ainfo->D;
	int s = ainfo->pd;
	int i, k = d + s * D;
	int *c = arima_delta_coeffs(d, D, s);
	double y0;

	if (c == NULL) {
	    return E_ALLOC;
	}
	for (i=0; i<k; i++) {
	    gretl_matrix_set(kh->F, r0, r0 + i, c[i]);
	}
	gretl_matrix_set(kh->F, r0, 0, 1.0);
	if (r - r0 > 1) {
	    gretl_matrix_inscribe_I(kh->F, r0 + 1, r0, k - 1);
	}
	for (i=0; i<k; i++) {
	    gretl_vector_set(kh->H, r0 + i, c[i]);
	    /* lagged data */
	    y0 = y[ainfo->t1 - 1 - i];
	    if (ainfo->yscale != 1.0 && !na(y0)) {
		y0 -= ainfo->yshift;
		y0 *= ainfo->yscale;
	    }
	    gretl_vector_set(kh->S, r0 + i, y0);
	}
	free(c);

#if ARMA_DEBUG
	gretl_matrix_print(kh->S, "S0 (arima via levels)");
#endif
	/* initialize the plain-arma "shadow" matrices */
	gretl_matrix_zero(kh->F_);
	gretl_matrix_inscribe_I(kh->F_, 1, 0, r0 - 1);
	gretl_matrix_zero(kh->Q_);
	gretl_matrix_set(kh->Q_, 0, 0, 1.0);
	gretl_matrix_zero(kh->P_);
    } else if (ainfo->np == 0 && ainfo->P == 0) {
	/* initialize P to identity matrix */
	gretl_matrix_inscribe_I(kh->P, 0, 0, kh->P->rows);
    }

    return 0;
}

#define PRINT_P_INFO 0

#if PRINT_P_INFO

static void print_P_info (const gretl_matrix *m,
			  arma_info *ainfo)
{
    double x, x0 = m->val[0];
    int i, j, id = (x0 == 1.0)? 2 : 1;

    for (j=0; j<m->cols && id > 0; j++) {
	for (i=0; i<m->rows; i++) {
	    x = gretl_matrix_get(m, i, j);
	    if (i == j && x != x0) {
		id = 0;
	    } else if (i != j && x != 0.0) {
		id = 0;
	    }
	}
    }

    fprintf(stderr, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
	    id, ainfo->np, ainfo->d, ainfo->nq,
	    ainfo->P, ainfo->D, ainfo->Q,
	    ainfo->ifc, ainfo->nexo);
}

#endif

static int write_kalman_matrices (khelper *kh,
				  const double *b,
				  int idx)
{
    arma_info *ainfo = kh->kainfo;
    const double *phi =       b + ainfo->ifc;
    const double *Phi =     phi + ainfo->np;
    const double *theta =   Phi + ainfo->P;
    const double *Theta = theta + ainfo->nq;
    const double *beta =  Theta + ainfo->Q;
    double mu = (ainfo->ifc)? b[0] : 0.0;
    int rewrite_A = 0;
    int rewrite_F = 0;
    int rewrite_H = 0;
    int i, k, err = 0;

    if (idx == KALMAN_ALL) {
	rewrite_A = rewrite_F = rewrite_H = 1;
    } else {
	/* called in context of calculating score, for OPG matrix */
	int pmax = ainfo->ifc + ainfo->np + ainfo->P;
	int tmax = pmax + ainfo->nq + ainfo->Q;

	if (ainfo->ifc && idx == 0) {
	    rewrite_A = 1;
	} else if (idx >= ainfo->ifc && idx < pmax) {
	    rewrite_F = 1;
	} else if (idx >= ainfo->ifc && idx < tmax) {
	    rewrite_H = 1;
	} else {
	    rewrite_A = 1;
	}
    }

    /* revise for pure MA model */
    if (ainfo->np == 0 && ainfo->P == 0 && !arima_levels(ainfo)) {
	rewrite_F = 0;
    }
    /* and for case of no constant or other regressors */
    if (ainfo->ifc == 0 && ainfo->nexo == 0) {
	rewrite_A = 0;
    }

#if ARMA_MDEBUG
    fprintf(stderr, "write_kalman_matrices: rewrites: A=%d, F=%d, H=%d\n",
	    rewrite_A, rewrite_F, rewrite_H);
# if ARMA_MDEBUG > 1
    fprintf(stderr, "\n*** write_kalman_matrices: before\n");
    gretl_matrix_print(kh->A, "A");
    gretl_matrix_print(kh->F, "F");
    gretl_matrix_print(kh->H, "H");
    gretl_matrix_print(kh->P, "P");
# endif
#endif

    /* See Hamilton, Time Series Analysis, ch 13, p. 375 */

    if (rewrite_A) {
	/* const and coeffs on exogenous vars */
	gretl_vector_set(kh->A, 0, mu);
	for (i=0; i<ainfo->nexo; i++) {
	    gretl_vector_set(kh->A, i + 1, beta[i]);
	}
    }

    if (rewrite_H) {
	/* form the H vector using theta and/or Theta */
	if (ainfo->Q > 0) {
	    write_big_theta(theta, Theta, ainfo, kh->H, NULL);
	} else {
	    k = 0;
	    for (i=0; i<ainfo->q; i++) {
		if (MA_included(ainfo, i)) {
		    gretl_vector_set(kh->H, i+1, theta[k++]);
		} else {
		    gretl_vector_set(kh->H, i+1, 0.0);
		}
	    }
	}
    }

    if (rewrite_F) {
	/* form the F matrix using phi and/or Phi */
	gretl_matrix *F = (kh->F_ != NULL)? kh->F_ : kh->F;
	gretl_matrix *Q = (kh->Q_ != NULL)? kh->Q_ : kh->Q;
	gretl_matrix *P = (kh->P_ != NULL)? kh->P_ : kh->P;

	if (ainfo->P > 0) {
	    write_big_phi(phi, Phi, ainfo, F);
	} else {
	    k = 0;
	    for (i=0; i<ainfo->p; i++) {
		if (AR_included(ainfo, i)) {
		    gretl_matrix_set(F, 0, i, phi[k++]);
		} else {
		    gretl_matrix_set(F, 0, i, 0.0);
		}
	    }
	}

	if (arima_levels(ainfo)) {
	    /* the full F matrix incorporates \theta */
	    if (ainfo->Q > 0) {
		write_big_theta(theta, Theta, ainfo, NULL, kh->F);
	    } else {
		k = 0;
		for (i=0; i<ainfo->q; i++) {
		    if (MA_included(ainfo, i)) {
			gretl_matrix_set(kh->F, ainfo->r0, i+1, theta[k++]);
		    } else {
			gretl_matrix_set(kh->F, ainfo->r0, i+1, 0.0);
		    }
		}
	    }
	}

	/* form $P_{1|0}$ (MSE) matrix, as per Hamilton, ch 13, p. 378. */

	gretl_matrix_kronecker_product(F, F, kh->Svar);
	gretl_matrix_I_minus(kh->Svar);
	if (arma_using_vech(ainfo)) {
	    condense_state_vcv(kh->Svar2, kh->Svar, gretl_matrix_rows(F));
	    gretl_matrix_vectorize_h(kh->vQ, Q);
	    err = gretl_LU_solve(kh->Svar2, kh->vQ);
	    if (!err) {
		gretl_matrix_unvectorize_h(P, kh->vQ);
	    }
	} else {
	    gretl_matrix_vectorize(kh->vQ, Q);
	    err = gretl_LU_solve(kh->Svar, kh->vQ);
	    if (!err) {
		gretl_matrix_unvectorize(P, kh->vQ);
	    }
	}
# if PRINT_P_INFO
	print_P_info(P, ainfo);
# endif
    }

    if (arima_levels(ainfo)) {
	/* complete the job on F, Q, P */
	gretl_matrix_inscribe_matrix(kh->F, kh->F_, 0, 0, GRETL_MOD_NONE);
	gretl_matrix_inscribe_matrix(kh->Q, kh->Q_, 0, 0, GRETL_MOD_NONE);
	gretl_matrix_inscribe_matrix(kh->P, kh->P_, 0, 0, GRETL_MOD_NONE);
    }

#if ARMA_MDEBUG
    fprintf(stderr, "\n*** after\n");
    gretl_matrix_print(kh->A, "A");
    gretl_matrix_print(kh->F, "F");
    gretl_matrix_print(kh->H, "H");
    gretl_matrix_print(kh->P, "P");
#endif

    return err;
}

static int rewrite_kalman_matrices (kalman *K, const double *b, int i)
{
    khelper *kh = (khelper *) kalman_get_data(K);
    int err = write_kalman_matrices(kh, b, i);

    if (!err) {
	kalman_set_initial_state_vector(K, kh->S);
	kalman_set_initial_MSE_matrix(K, kh->P);
    }

    return err;
}

static const double *kalman_arma_llt_callback (const double *b, int i,
					       void *data)
{
    kalman *K = (kalman *) data;
    khelper *kh = kalman_get_data(K);
    int err;

    rewrite_kalman_matrices(K, b, i);
    err = kalman_forecast(K, NULL);

#if ARMA_DEBUG
    fprintf(stderr, "kalman_arma_llt: kalman f'cast gave "
	    "err = %d, ll = %#.12g\n", err, kalman_get_loglik(K));
#endif

    return (err)? NULL : kh->E->val;
}

/* add covariance matrix and standard errors based on Outer Product of
   Gradient
*/

static int arma_OPG_vcv (MODEL *pmod, void *data, int algo,
			 double *b, double s2,
			 int k, int T,
			 PRN *prn)
{
    gretl_matrix *G = NULL;
    gretl_matrix *V = NULL;
    int err = 0;

    if (algo == 154) {
	G = numerical_score_matrix(b, T, k, as154_llt_callback,
				   data, &err);
    } else if (algo == 197) {
	G = numerical_score_matrix(b, T, k, as197_llt_callback,
				   data, &err);
    } else {
	G = numerical_score_matrix(b, T, k, kalman_arma_llt_callback,
				   data, &err);
    }

    if (!err) {
	V = gretl_matrix_XTX_new(G);
	if (V == NULL) {
	    err = E_ALLOC;
	}
    }

    if (!err) {
	double rcond = gretl_symmetric_matrix_rcond(V, &err);

	if (!err && rcond < 1.0E-10) {
	    pprintf(prn, "OPG: rcond = %g; will try Hessian\n", rcond);
	    err = 1;
	}
    }

    if (!err) {
	err = gretl_invert_symmetric_matrix(V);
    }

    if (!err) {
	gretl_matrix_multiply_by_scalar(V, s2);
	err = gretl_model_write_vcv(pmod, V);
    }

    gretl_matrix_free(G);
    gretl_matrix_free(V);

    return err;
}

static int arma_QML_vcv (MODEL *pmod, gretl_matrix *H,
			 void *data, int algo,
			 double *b, double s2, int k, int T,
			 PRN *prn)
{
    gretl_matrix *G;
    int err = 0;

    if (algo == 154) {
	G = numerical_score_matrix(b, T, k, as154_llt_callback,
				   data, &err);
    } else if (algo == 197) {
	G = numerical_score_matrix(b, T, k, as197_llt_callback,
				   data, &err);
    } else {
	G = numerical_score_matrix(b, T, k, kalman_arma_llt_callback,
				   data, &err);
    }

    if (!err) {
	gretl_matrix_divide_by_scalar(G, sqrt(s2));
	err = gretl_model_add_QML_vcv(pmod, ARMA, H, G,
				      NULL, OPT_NONE, NULL);
    }

    gretl_matrix_free(G);

    return err;
}

#if ARMA_DEBUG

static void debug_print_theta (const double *theta,
			       const double *Theta,
			       arma_info *ainfo)
{
    int i, k = 0;

    fprintf(stderr, "kalman_arma_ll():\n");

    for (i=0; i<ainfo->q; i++) {
	if (MA_included(ainfo, i)) {
	    fprintf(stderr, "theta[%d] = %#.12g\n", i+1, theta[k++]);
	}
    }

    for (i=0; i<ainfo->Q; i++) {
	fprintf(stderr, "Theta[%d] = %#.12g\n", i, Theta[i]);
    }
}

#endif

static int kalman_do_ma_check = 1;

static double kalman_arma_ll (const double *b, void *data)
{
    kalman *K = (kalman *) data;
    khelper *kh = kalman_get_data(K);
    arma_info *ainfo = kh->kainfo;
    int offset = ainfo->ifc + ainfo->np + ainfo->P;
    double *theta = (double *) b + offset;
    double *Theta = theta + ainfo->nq;
    double ll = NADBL;
    int err = 0;

#if ARMA_DEBUG
    if (ainfo->q > 0 || ainfo->Q > 0) {
	debug_print_theta(theta, Theta, ainfo);
    }
#endif

    if (kalman_do_ma_check && maybe_correct_MA(ainfo, theta, Theta)) {
	pputs(kalman_get_printer(K), _("MA estimate(s) out of bounds\n"));
	return NADBL;
    }

    err = rewrite_kalman_matrices(K, b, KALMAN_ALL);

    if (!err) {
	err = kalman_forecast(K, NULL);
	ll = kalman_get_loglik(K);
#if ARMA_DEBUG > 1
	fprintf(stderr, "kalman_arma_ll: kalman_forecast gave %d, "
		"loglik = %#.12g\n", err, ll);
#endif
    }

    return ll;
}

static int arima_ydiff_only (arma_info *ainfo)
{
    if ((ainfo->d > 0 || ainfo->D > 0) &&
	ainfo->nexo > 0 && !arma_xdiff(ainfo)) {
	return 1;
    } else {
	return 0;
    }
}

static int arma_use_opg (gretlopt opt)
{
    int ret = 0; /* use of the Hessian is the default */

    if (opt & OPT_G) {
	ret = 1;
    } else if (libset_get_int(ARMA_VCV) == ML_OP) {
	ret = 1;
    }

    return ret;
}

/* The following is now basically functionless, other
   than for backward compatibility: it duplicates the
   model's $uhat array as the $ehat vector, just in
   case any scripts want it under that name.
*/

static void arma_add_ehat (MODEL *pmod, arma_info *ainfo,
			   kalman *K, double *b)
{
    khelper *kh = kalman_get_data(K);
    gretl_matrix *ehat = gretl_matrix_copy(kh->E);

    if (ehat != NULL) {
	gretl_matrix_set_t1(ehat, pmod->t1);
	gretl_matrix_set_t2(ehat, pmod->t2);
	gretl_model_set_matrix_as_data(pmod, "ehat", ehat);
    }
}

static int kalman_arma_finish (MODEL *pmod,
			       arma_info *ainfo,
			       const DATASET *dset,
			       kalman *K, double *b,
			       gretlopt opt, PRN *prn)
{
    khelper *kh = kalman_get_data(K);
    int do_opg = arma_use_opg(opt);
    int kopt, i, t, k = ainfo->nc;
    int QML = (opt & OPT_R);
    double s2;
    int err;

    pmod->t1 = ainfo->t1;
    pmod->t2 = ainfo->t2;
    pmod->nobs = ainfo->T;
    pmod->ncoeff = ainfo->nc;
    pmod->full_n = dset->n;

    /* in the Kalman case the basic model struct is empty, so we
       have to allocate for coefficients, residuals and so on
    */
    err = gretl_model_allocate_storage(pmod);
    if (err) {
	return err;
    }

    for (i=0; i<k; i++) {
	pmod->coeff[i] = b[i];
    }

    i = 0;
    for (t=pmod->t1; t<=pmod->t2; t++) {
	pmod->uhat[t] = gretl_vector_get(kh->E, i++);
    }

    s2 = kalman_get_arma_variance(K);
    pmod->sigma = sqrt(s2);

    pmod->lnL = kalman_get_loglik(K);
    kopt = kalman_get_options(K);

    /* rescale if we're using average loglikelihood */
    if (kopt & KALMAN_AVG_LL) {
	pmod->lnL *= ainfo->T;
    }

    if (opt & OPT_E) {
	arma_add_ehat(pmod, ainfo, K, b);
    }

#if ARMA_DEBUG
    fprintf(stderr, "kalman_arma_finish: doing VCV, method %s\n",
	    (opt & OPT_R)? "QML" : (do_opg)? "OPG" : "Hessian");
#endif

    if (!do_opg) {
	/* base covariance matrix on Hessian (perhaps QML) */
	gretl_matrix *Hinv;
	double d = 0.0; /* adjust? */

	kalman_do_ma_check = 0;
	Hinv = numerical_hessian_inverse(b, ainfo->nc, kalman_arma_ll,
					 K, d, &err);
	kalman_do_ma_check = 1;
	if (!err) {
	    if (kopt & KALMAN_AVG_LL) {
		gretl_matrix_divide_by_scalar(Hinv, ainfo->T);
	    }
	    if (QML) {
		err = arma_QML_vcv(pmod, Hinv, K, 0, b, s2, k, ainfo->T, prn);
	    } else {
		err = gretl_model_write_vcv(pmod, Hinv);
		if (!err) {
		    gretl_model_set_vcv_info(pmod, VCV_ML, ML_HESSIAN);
		}
	    }
	} else if (!(opt & OPT_H)) {
	    /* fallback when Hessian not explicitly requested */
	    err = 0;
	    do_opg = 1;
	    gretl_model_set_int(pmod, "hess-error", 1);
	}
	gretl_matrix_free(Hinv);
    }

    if (do_opg) {
	err = arma_OPG_vcv(pmod, K, 0, b, s2, k, ainfo->T, prn);
	if (!err) {
	    gretl_model_set_vcv_info(pmod, VCV_ML, ML_OP);
	    pmod->opt |= OPT_G;
	}
    }

    if (!err) {
	write_arma_model_stats(pmod, ainfo, dset);
	arma_model_add_roots(pmod, ainfo, b);
	gretl_model_set_int(pmod, "arma_flags", ARMA_EXACT);
	if (arma_lbfgs(ainfo)) {
	    pmod->opt |= OPT_L;
	}
	if (arima_ydiff_only(ainfo)) {
	    pmod->opt |= OPT_Y;
	}
    }

#if 0 /* not ready yet: after estimation of ARMA with missing
         values, use the Kalman smoother to estimate the
	 missing data
      */
    if (!err && arma_missvals(ainfo)) {
	gretl_matrix *ys = kalman_arma_smooth(K, &err);
	int t;

	i = 0;
	for (t=ainfo->t1; t<=ainfo->t2; t++) {
	    if (na(pmod->yhat[t])) {
		pmod->yhat[t] = gretl_vector_get(ys, i);
	    } else {
		fprintf(stderr, "%g vs %g\n", pmod->yhat[t], ys->val[i]);
	    }
	    i++;
	}
	gretl_matrix_free(ys);
    }
#endif

    return err;
}

static void kalman_rescale_y (gretl_vector *y, arma_info *ainfo)
{
    int i;

#if ARMA_DEBUG
    fprintf(stderr, "kalman_rescale_y: multiplying by %g\n",
	    ainfo->yscale);
#endif

    for (i=0; i<y->rows; i++) {
	if (!isnan(y->val[i])) {
	    y->val[i] -= ainfo->yshift;
	    y->val[i] *= ainfo->yscale;
	}
    }
}

/* for Kalman: convert from full-length y series to
   y vector of length ainfo->T */

static gretl_matrix *form_arma_y_vector (arma_info *ainfo,
					 int *err)
{
    gretl_matrix *yvec;

    yvec = gretl_vector_from_series(ainfo->y, ainfo->t1, ainfo->t2);

    if (yvec == NULL) {
	*err = E_ALLOC;
    } else {
	if (ainfo->yscale != 1.0) {
	    kalman_rescale_y(yvec, ainfo);
	}
#if ARMA_DEBUG
	gretl_matrix_print(yvec, "arma y vector");
#endif
    }

    return yvec;
}

static gretl_matrix *form_arma_X_matrix (arma_info *ainfo,
					 const DATASET *dset,
					 int *err)
{
    gretl_matrix *X;
    int missop;

#if ARMA_DEBUG
    printlist(ainfo->xlist, "ainfo->xlist (exog vars)");
#endif

    if (arma_na_ok(ainfo)) {
	missop = M_MISSING_OK;
    } else {
	missop = M_MISSING_ERROR;
    }

    X = gretl_matrix_data_subset(ainfo->xlist, dset,
				 ainfo->t1, ainfo->t2,
				 missop, err);

#if ARMA_DEBUG
    gretl_matrix_print(X, "X");
#endif

    return X;
}

static int kalman_undo_y_scaling (arma_info *ainfo,
				  gretl_matrix *y, double *b,
				  kalman *K)
{
    double *beta = b + ainfo->ifc + ainfo->np + ainfo->P +
	ainfo->nq + ainfo->Q;
    int i, t, T = ainfo->t2 - ainfo->t1 + 1;
    int err = 0;

    if (ainfo->ifc) {
	b[0] /= ainfo->yscale;
	b[0] += ainfo->yshift;
    }

    for (i=0; i<ainfo->nexo; i++) {
	beta[i] /= ainfo->yscale;
    }

    i = ainfo->t1;
    for (t=0; t<T; t++) {
	y->val[t] /= ainfo->yscale;
	y->val[t] += ainfo->yshift;
    }

    if (na(kalman_arma_ll(b, K))) {
	err = 1;
    }

    return err;
}

static void free_arma_X_matrix (arma_info *ainfo, gretl_matrix *X)
{
    if (X == ainfo->dX) {
	gretl_matrix_free(ainfo->dX);
	ainfo->dX = NULL;
    } else {
	gretl_matrix_free(X);
    }
}

static int kalman_arma (const double *coeff,
			const DATASET *dset,
			arma_info *ainfo,
			MODEL *pmod,
			gretlopt opt)
{
    kalman *K = NULL;
    khelper *kh = NULL;
    gretl_matrix *y = NULL;
    gretl_matrix *X = NULL;
    int r, k = 1 + ainfo->nexo; /* number of exog vars plus space for const */
    int use_newton = 0;
    double *b;
    int err = 0;

    b = copyvec(coeff, ainfo->nc);
    if (b == NULL) {
	return E_ALLOC;
    }

#if ARMA_DEBUG
    fputs("# kalman_arma: initial coefficients:\n", stderr);
    fprintf(stderr, "%d 1\n", ainfo->nc);
    int i;
    for (i=0; i<ainfo->nc; i++) {
	fprintf(stderr, "%.15g\n", b[i]);
    }
#endif

    y = form_arma_y_vector(ainfo, &err);

    if (!err && ainfo->nexo > 0) {
	if (ainfo->dX != NULL) {
	    X = ainfo->dX;
	} else {
	    X = form_arma_X_matrix(ainfo, dset, &err);
	}
    }

    if (!err) {
	err = allocate_ac_mc(ainfo);
    }

    if (err) {
	goto bailout;
    }

    r = ainfo_get_state_size(ainfo);

    /* when should we use vech apparatus? */
    if (r > 4) {
	set_arma_use_vech(ainfo);
    }

    kh = kalman_helper_new(ainfo, r, k);
    if (kh == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    kalman_matrices_init(ainfo, kh, dset->Z[ainfo->yno]);

#if ARMA_DEBUG
    fprintf(stderr, "ready to estimate: ainfo specs:\n"
	    "p=%d, P=%d, q=%d, Q=%d, ifc=%d, nexo=%d, t1=%d, t2=%d\n",
	    ainfo->p, ainfo->P, ainfo->q, ainfo->Q, ainfo->ifc,
	    ainfo->nexo, ainfo->t1, ainfo->t2);
    fprintf(stderr, "Kalman dims: r = %d, k = %d, T = %d, ncoeff=%d\n",
	    r, k, ainfo->T, ainfo->nc);
#endif

    K = kalman_new(kh->S, kh->P, kh->F, kh->A, kh->H, kh->Q,
		   NULL, y, X, NULL, kh->E, &err);

    if (err) {
	fprintf(stderr, "kalman_new(): err = %d\n", err);
    } else {
	double toler;
	int maxit;
	int avg_ll;

	kalman_attach_printer(K, ainfo->prn);
	kalman_attach_data(K, kh);

	if (r > 3 && !arima_levels(ainfo)) {
	    kalman_set_nonshift(K, 1);
	} else {
	    kalman_set_nonshift(K, r);
	}

	avg_ll = arma_avg_ll(ainfo);
	use_newton = libset_get_int(GRETL_OPTIM) == OPTIM_NEWTON;

	if (avg_ll) {
	    kalman_set_options(K, KALMAN_ARMA_LL | KALMAN_AVG_LL);
	} else {
	    kalman_set_options(K, KALMAN_ARMA_LL);
	}

	BFGS_defaults(&maxit, &toler, ARMA);

	if (use_newton) {
	    double crittol = 1.0e-7;
	    double gradtol = 1.0e-7;

	    err = newton_raphson_max(b, ainfo->nc, maxit,
				     crittol, gradtol, &ainfo->fncount,
				     C_LOGLIK, kalman_arma_ll,
				     NULL, NULL, K, opt,
				     ainfo->prn);
	} else {
	    int save_lbfgs = libset_get_bool(USE_LBFGS);

	    if (save_lbfgs) {
		ainfo->pflags |= ARMA_LBFGS;
	    } else if (opt & OPT_L) {
		libset_set_bool(USE_LBFGS, 1);
		ainfo->pflags |= ARMA_LBFGS;
	    }

	    err = BFGS_max(b, ainfo->nc, maxit, toler,
			   &ainfo->fncount, &ainfo->grcount,
			   kalman_arma_ll, C_LOGLIK,
			   NULL, K, NULL, opt | OPT_A,
			   ainfo->prn);

	    if (save_lbfgs == 0 && (opt & OPT_L)) {
		libset_set_bool(USE_LBFGS, 0);
	    }
	}

	if (err) {
	    fprintf(stderr, "kalman_arma: optimizer returned %d\n", err);
	}
    }

#if ARMA_DEBUG
    fprintf(stderr, "undo_scaling? yscale = %g\n", ainfo->yscale);
#endif

    if (!err && ainfo->yscale != 1.0) {
	kalman_undo_y_scaling(ainfo, y, b, K);
    }

    if (!err) {
	if (use_newton) {
	    gretl_model_set_int(pmod, "iters", ainfo->fncount);
	} else {
	    gretl_model_set_int(pmod, "fncount", ainfo->fncount);
	    gretl_model_set_int(pmod, "grcount", ainfo->grcount);
	}
	err = kalman_arma_finish(pmod, ainfo, dset, K, b,
				 opt, ainfo->prn);
    }

 bailout:

    if (err) {
	pmod->errcode = err;
    }

    kalman_free(K);
    kalman_helper_free(kh);

    gretl_matrix_free(y);
    free_arma_X_matrix(ainfo, X);
    free(b);

    return err;
}

/* end of Kalman-specific material */

#define y_missing(y) (na(y) || isnan(y))

/* support for AS 154 and AS 197 */

# include "as197.c"
# include "as154.c"
# include "as_driver.c"

static void arma_init_message (arma_info *ainfo)
{
    pprintf(ainfo->prn, "\n%s: ", _("ARMA initialization"));

    if (ainfo->init == INI_USER) {
	pprintf(ainfo->prn, "%s\n\n", _("user-specified values"));
    } else if (ainfo->init == INI_HR) {
	pprintf(ainfo->prn, "%s\n\n", _("Hannan-Rissanen method"));
    } else if (ainfo->init == INI_SMALL) {
	pprintf(ainfo->prn, "%s\n\n", _("small MA values"));
    } else if (ainfo->init == INI_NLS) {
	pprintf(ainfo->prn, "%s\n\n", _("using nonlinear AR model"));
    } else if (ainfo->init == INI_OLS) {
	pprintf(ainfo->prn, "%s\n\n", _("using linear AR model"));
    }
}

static int user_arma_init (double *coeff, arma_info *ainfo)
{
    int i, nc = n_initvals();

    if (nc == 0) {
	return 0;
    } else if (nc < ainfo->nc) {
	pprintf(ainfo->prn, "ARMA initialization: need %d coeffs but got %d\n",
		ainfo->nc, nc);
	return E_DATA;
    }

    if (arma_exact_ml(ainfo)) {
	/* user-specified initializer is handled within BFGSmax */
	for (i=0; i<ainfo->nc; i++) {
	    coeff[i] = 0.0;
	}
    } else {
	gretl_matrix *m = get_initvals();

	for (i=0; i<ainfo->nc; i++) {
	    coeff[i] = gretl_vector_get(m, i);
	}
	gretl_matrix_free(m);
    }

    ainfo->init = INI_USER;

    return 0;
}

/* Should we try Hannan-Rissanen initialization of ARMA
   coefficients? */

static int prefer_hr_init (arma_info *ainfo)
{
    int ret = 0;

    if (ainfo->q > 1 || ainfo->Q > 0) {
	ret = 1;
	if (arma_xdiff(ainfo)) {
	    /* don't use for ARIMAX (yet?) */
	    ret = 0;
	} else if (ainfo->T < 100) {
	    /* unlikely to work well with small sample */
	    ret = 0;
	} else if (ainfo->p > 0 && ainfo->P > 0) {
	    /* not sure about this: HR catches the MA terms, but NLS
	       handles the seasonal/non-seasonal AR interactions
	       better?
	    */
	    ret = 0;
	} else if ((ainfo->P > 0 && ainfo->p >= ainfo->pd) ||
		   (ainfo->Q > 0 && ainfo->q >= ainfo->pd)) {
	    /* overlapping seasonal/non-seasonal orders screw things up */
	    ret = 0;
	} else if (ret && arma_exact_ml(ainfo)) {
	    /* screen for cases where we'll use NLS */
	    if (ainfo->P > 0) {
		ret = 0;
	    } else if (ainfo->p + ainfo->P > 0 && ainfo->nexo > 0) {
		ret = 0;
	    } else if (ainfo->Q > 0 && arma_missvals(ainfo)) {
		ret = 0;
	    }
	}
    }

#if ARMA_DEBUG
    fprintf(stderr, "prefer_hr_init? %s\n", ret? "yes" : "no");
#endif

    return ret;
}

/* estimate an ARIMA (0,d,0) x (0,D,0) model via OLS */

static int arima_by_ls (const DATASET *dset, arma_info *ainfo,
			MODEL *pmod)
{
    gretl_matrix *X;
    gretl_matrix *b, *u, *V;
    double x, s2;
    int i, t, k = ainfo->dX->cols;
    int err = 0;

    if (ainfo->ifc) {
	/* the constant will not have been included in ainfo->dX */
	X = gretl_matrix_alloc(ainfo->T, k + 1);
	if (X == NULL) {
	    return E_ALLOC;
	}
	for (i=0; i<=k; i++) {
	    for (t=0; t<ainfo->T; t++) {
		if (i == 0) {
		    gretl_matrix_set(X, t, i, 1.0);
		} else {
		    x = gretl_matrix_get(ainfo->dX, t, i-1);
		    gretl_matrix_set(X, t, i, x);
		}
	    }
	}
	k++;
    } else {
	X = ainfo->dX;
    }

    b = gretl_column_vector_alloc(k);
    u = gretl_column_vector_alloc(ainfo->T);
    V = gretl_matrix_alloc(k, k);

    if (b == NULL || u == NULL || V == NULL) {
	err = E_ALLOC;
    } else {
	gretl_vector y;

	gretl_matrix_init(&y);
	y.rows = ainfo->T;
	y.cols = 1;
	y.val = ainfo->y + ainfo->t1;
	gretl_matrix_set_t1(&y, ainfo->t1);
	gretl_matrix_set_t2(&y, ainfo->t2);

	err = gretl_matrix_ols(&y, X, b, V, u, &s2);
    }

    if (!err) {
	pmod->ncoeff = k;
	pmod->full_n = dset->n;
	err = gretl_model_allocate_storage(pmod);
    }

    if (!err) {
	for (i=0; i<k; i++) {
	    pmod->coeff[i] = b->val[i];
	}
	for (t=0; t<ainfo->T; t++) {
	    pmod->uhat[t + ainfo->t1] = u->val[t];
	}
	err = gretl_model_write_vcv(pmod, V);
    }

    if (!err) {
    	pmod->ybar = gretl_mean(ainfo->t1, ainfo->t2, ainfo->y);
	pmod->sdy = gretl_stddev(ainfo->t1, ainfo->t2, ainfo->y);
	pmod->nobs = ainfo->T;
    }

    gretl_matrix_free(b);
    gretl_matrix_free(u);
    gretl_matrix_free(V);

    if (X != ainfo->dX) {
	gretl_matrix_free(X);
    }

    return err;
}

/* calculate info criteria for compatibility with ML? */
#define ML_COMPAT 1 /* 2017-03-23 */

static int arma_via_OLS (arma_info *ainfo, const double *coeff,
			 const DATASET *dset, MODEL *pmod)
{
    int err = 0;

    ainfo->flags |= ARMA_LS;

    if (arma_xdiff(ainfo)) {
	err = arima_by_ls(dset, ainfo, pmod);
    } else {
	err = arma_by_ls(coeff, dset, ainfo, pmod);
    }

    if (!err) {
	ArmaFlags f = arma_exact_ml(ainfo) ? ARMA_OLS : ARMA_LS;

	pmod->t1 = ainfo->t1;
	pmod->t2 = ainfo->t2;
	pmod->full_n = dset->n;
	write_arma_model_stats(pmod, ainfo, dset);
	if (arma_exact_ml(ainfo)) {
#if ML_COMPAT
	    /* In the case of ainfo->nc == 0 (no coefficients
	       actually estimated), pmod->ncoeff will be 1, since
	       we add a dummy constant with value 0. That "1"
	       will account for the variance estimate so that
	       addk should be zero. Otherwise we add 1 for the
	       variance estimate.
	    */
	    int addk = ainfo->nc == 0 ? 0 : 1;

	    mle_criteria(pmod, addk);
#else
	    ls_criteria(pmod);
#endif
	} else {
	    arma_model_add_roots(pmod, ainfo, pmod->coeff);
	}
	gretl_model_set_int(pmod, "arma_flags", f);
    }

    if (!err && pmod->errcode) {
	err = pmod->errcode;
    }

    return err;
}

/* Set flag to indicate differencing of exogenous regressors, in the
   case of an ARIMAX model using native exact ML -- unless this is
   forbidden by OPT_Y (--y-diff-only).  Note that we don't do this
   when we're using conditional ML (BHHH).
*/

static void maybe_set_xdiff_flag (arma_info *ainfo, gretlopt opt)
{
    if (arma_exact_ml(ainfo) &&
	(ainfo->d > 0 || ainfo->D > 0) &&
	ainfo->nexo > 0 && !(opt & OPT_Y)) {
	ainfo->pflags |= ARMA_XDIFF;
    }
}

/* Respond to OPT_S (--stdx): standardize exogenous regressors */

static int arma_standardize_x (arma_info *ainfo,
			       DATASET *dset)
{
    int orig_v = dset->v;
    int err = 0;

    ainfo->xstats = gretl_matrix_alloc(ainfo->nexo, 2);
    if (ainfo->xstats == NULL) {
	return E_ALLOC;
    }

    err = dataset_add_series(dset, ainfo->nexo);

    if (!err) {
	double xbar, sdx;
	int i, vi, vj, t;

	for (i=0; i<ainfo->nexo && !err; i++) {
	    vi = ainfo->xlist[i+1];
	    err = gretl_moments(ainfo->t1, ainfo->t2, dset->Z[vi],
				NULL, &xbar, &sdx, NULL, NULL, 1);
	    if (!err) {
		vj = orig_v + i;
		for (t=0; t<dset->n; t++) {
		    dset->Z[vj][t] = (dset->Z[vi][t] - xbar) / sdx;
		}
		/* replace x-ref with standardized version */
		ainfo->xlist[i+1] = vj;
		/* and record the stats used */
		gretl_matrix_set(ainfo->xstats, i, 0, xbar);
		gretl_matrix_set(ainfo->xstats, i, 1, sdx);
	    }
	}
    }

    if (!err) {
	set_arma_stdx(ainfo);
    }

    return err;
}

/* Set flag to allow NAs within the sample range for an
   ARMA model using native exact ML.
*/

static void maybe_allow_missvals (arma_info *ainfo)
{
    if (arma_exact_ml(ainfo)) {
	ainfo->pflags |= ARMA_NAOK;
    }
}

static int check_arma_options (gretlopt opt)
{
    int err;

    /* can't specify LBFGS or --robust with conditional ML */
    err = options_incompatible_with(opt, OPT_C, OPT_L | OPT_R);

    if (!err) {
	/* nor more than one of AS, CML or Kalman */
	err = incompatible_options(opt, OPT_A | OPT_C | OPT_K);
    }

    if (!err) {
	/* nor --stdx with --kalman */
	err = incompatible_options(opt, OPT_S | OPT_K);
    }

    return err;
}

MODEL arma_model (const int *list, const int *pqspec,
		  DATASET *dset, gretlopt opt, PRN *prn)
{
    double *coeff = NULL;
    MODEL armod;
    arma_info ainfo_s, *ainfo;
    int missv = 0, misst = 0;
    int orig_v = dset->v;
    int err = 0;

    ainfo = &ainfo_s;
    arma_info_init(ainfo, opt, pqspec, dset);

    if (opt & OPT_V) {
	ainfo->prn = prn;
    }

    gretl_model_init(&armod, dset);

    err = check_arma_options(opt);

    if (!err) {
	ainfo->alist = gretl_list_copy(list);
	if (ainfo->alist == NULL) {
	    err = E_ALLOC;
	}
    }

    if (!err) {
	err = arma_check_list(ainfo, dset, opt);
    }

    if (!err) {
	/* calculate maximum lag */
	maybe_set_xdiff_flag(ainfo, opt);
	calc_max_lag(ainfo);
    }

    if (!err) {
	/* adjust sample range if need be */
	maybe_allow_missvals(ainfo);
	err = arma_adjust_sample(ainfo, dset, &missv, &misst);
	if (err) {
	    if (missv > 0 && misst > 0) {
		gretl_errmsg_sprintf(_("Missing value encountered for "
				       "variable %d, obs %d"), missv, misst);
	    }
	} else if (missv > 0) {
	    set_arma_missvals(ainfo);
	}
    }

    if (!err && ainfo->nexo > 0 && arma_exact_ml(ainfo)) {
	/* FIXME check conditionality more rigorously */
	if ((opt & OPT_S) && !arma_xdiff(ainfo)) {
	    /* --stdx */
	    err = arma_standardize_x(ainfo, dset);
	}
    }

    if (!err) {
	/* allocate initial coefficient vector */
	coeff = malloc(ainfo->nc * sizeof *coeff);
	if (coeff == NULL) {
	    err = E_ALLOC;
	}
    }

    if (!err) {
	/* organize the dependent variable */
	ainfo->y = (double *) dset->Z[ainfo->yno];
	if (ainfo->d > 0 || ainfo->D > 0) {
	    if (arma_missvals(ainfo)) {
		/* for now: insist on native Kalman, since only it
		   handles the levels formulation of ARIMA */
		opt &= ~OPT_A;
		opt |= OPT_K;
		set_arima_levels(ainfo);
	    } else {
		/* note: this replaces ainfo->y */
		err = arima_difference(ainfo, dset, 0);
	    }
	}
    }

    if (err) {
	goto bailout;
    }

    if (ainfo->p == 0 && ainfo->P == 0 &&
	ainfo->q == 0 && ainfo->Q == 0 &&
	arma_exact_ml(ainfo) && !arma_missvals(ainfo)) {
	/* pure "I" model, no NAs: OLS provides the MLE */
	err = arma_via_OLS(ainfo, NULL, dset, &armod);
	goto bailout; /* estimation handled */
    }

    /* start initialization of the coefficients */

    /* first see if the user specified some values */
    err = user_arma_init(coeff, ainfo);
    if (err) {
	goto bailout;
    }

    if (!arma_exact_ml(ainfo) && ainfo->q == 0 && ainfo->Q == 0) {
	/* for a pure AR model, the conditional MLE is least
	   squares (OLS or NLS); in the NLS case a user-specified
	   initializer may be useful, if present
	*/
	const double *b = ainfo->init ? coeff : NULL;

	err = arma_via_OLS(ainfo, b, dset, &armod);
	goto bailout; /* estimation handled */
    }

    /* see if it may be helpful to scale the dependent
       variable, if we're doing exact ML */
    if (arma_exact_ml(ainfo) && ainfo->ifc && ainfo->init != INI_USER) {
	maybe_set_yscale(ainfo);
#if SHOW_INIT
	fprintf(stderr, "yscale = %g\n", ainfo->yscale);
#endif
    }

    /* try Hannan-Rissanen init, if suitable */
    if (!ainfo->init && prefer_hr_init(ainfo)) {
	hr_arma_init(coeff, dset, ainfo);
#if SHOW_INIT
	fprintf(stderr, "HR init (%d %d): %s\n", ainfo->p, ainfo->q,
		ainfo->init ? "success" : "fail");
#endif
    }

    /* initialize via AR model by OLS or NLS, adding minimal
       MA coefficients if needed: this is the fallback if
       Hannan-Rissanen fails, but also the default if the
       conditions of applicability of H-R are not met
    */
    if (!err && !ainfo->init) {
	err = ar_arma_init(coeff, dset, ainfo, &armod, opt);
#if SHOW_INIT
	fprintf(stderr, "AR init: err = %d\n", err);
#endif
    }

    if (ainfo->prn != NULL && ainfo->init) {
	arma_init_message(ainfo);
    }

    if (!err) {
	clear_model_xpx(&armod);
	if (arma_exact_ml(ainfo)) {
	    if (opt & OPT_K) {
		err = kalman_arma(coeff, dset, ainfo, &armod, opt);
	    } else {
		err = as_arma(coeff, dset, ainfo, &armod, opt);
	    }
	} else {
	    err = bhhh_arma(coeff, dset, ainfo, &armod, opt);
	}
    }

 bailout:

    if (err && !armod.errcode) {
	armod.errcode = err;
    }

    if (!err) {
	transcribe_extra_info(ainfo, &armod);
    }

    if (!armod.errcode) {
	gretl_model_smpl_init(&armod, dset);
    }

    free(coeff);
    arma_info_cleanup(ainfo);

    if (dset->v > orig_v) {
	dataset_drop_last_variables(dset, dset->v - orig_v);
    }

    return armod;
}
