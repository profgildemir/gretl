/*
 *  Copyright (c) 2002-2004 by Allin Cottrell
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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "libgretl.h"
#include "gretl_matrix.h"
#include "gretl_matrix_private.h"
#include "system.h"

#define SDEBUG 0

/* fiml.c */
extern int fiml_driver (gretl_equation_system *sys, double ***pZ, 
			DATAINFO *pdinfo, gretlopt opt, PRN *prn);

/* liml.c */
extern int liml_driver (gretl_equation_system *sys, double ***pZ, 
			DATAINFO *pdinfo, gretlopt opt, PRN *prn);

static void 
print_system_vcv (const gretl_equation_system *sys, PRN *prn)
{
    const gretl_matrix *m = system_get_sigma(sys);
    gretl_matrix *mcopy;
    int jmax = 1;
    char numstr[16];
    double x, lm;
    int i, j;

    pprintf(prn, "%s\n(%s)\n\n",
	    _("Cross-equation VCV for residuals"),
	    _("correlations above the diagonal"));

    for (i=0; i<m->rows; i++) {
	for (j=0; j<jmax; j++) {
	    pprintf(prn, "%#10.5g ", gretl_matrix_get(m, i, j));
	}
	for (j=jmax; j<m->cols; j++) {
	    x = gretl_matrix_get(m, i, i) * gretl_matrix_get(m, j, j);
	    x = sqrt(x);
	    x = gretl_matrix_get(m, i, j) / x;
	    sprintf(numstr,"(%.3f)", x); 
	    pprintf(prn, "%11s", numstr);
	}
	pputc(prn, '\n');
	if (jmax < m->cols) {
	    jmax++;
	}
    }

    mcopy = gretl_matrix_copy(m);
    if (mcopy != NULL) {
	x = gretl_vcv_log_determinant(mcopy);
	if (!na(x)) {
	    pprintf(prn, "\n%s = %g\n", _("log determinant"), x);
	}
	gretl_matrix_free(mcopy);
    }

    lm = system_get_diag_stat(sys);
    if (lm > 0) {
	int df = m->rows * (m->rows - 1) / 2;
	
	pprintf(prn, "%s:\n", _("Breusch-Pagan test for diagonal covariance matrix"));
	pprintf(prn, "  %s(%d) = %g %s %g\n", _("Chi-square"),
		df, lm, _("with p-value"), chisq(lm, df));
    }

    pputc(prn, '\n');
}

/* insert the elements of sub-matrix M, multuplied by scale, in the
   appropriate position within the big matrix X */

static void 
kronecker_place (gretl_matrix *X, const gretl_matrix *M,
		 int startrow, int startcol, double scale)
{
    int i, j;
    int row, col;
    double x;
    
    for (i=0; i<M->rows; i++) {
	row = startrow + i;
	for (j=0; j<M->cols; j++) {
	    col = startcol + j;
	    x = gretl_matrix_get(M, i, j);
	    gretl_matrix_set(X, row, col, x * scale);
	}
    }
}

/* retrieve the special k-class transformed data wanted for LIML
   estimation */

static int make_liml_X_block (gretl_matrix *X, const MODEL *pmod,
			      double **Z, int t1)
{
    int i, t;
    const double *Xi;

    X->cols = pmod->ncoeff;

    for (i=0; i<X->cols; i++) {
	Xi = tsls_get_Xi(pmod, Z, i);
	if (Xi == NULL) {
	    return 1;
	}
	for (t=0; t<X->rows; t++) {
	    gretl_matrix_set(X, t, i, Xi[t+t1]);
	}
    }

    return 0;
}

/* construct the X data block pertaining to a specific equation, using
   either the original data or fitted values from regression on a set
   of instruments */

static int 
make_sys_X_block (gretl_matrix *X, const MODEL *pmod,
		  double **Z, int t1, int method)
{
    int i, t;
    const double *Xi;

    X->cols = pmod->ncoeff;

    for (i=0; i<X->cols; i++) {
	if (method == SYS_3SLS || method == SYS_FIML || method == SYS_TSLS) {
	    Xi = tsls_get_Xi(pmod, Z, i);
	} else {
	    Xi = Z[pmod->list[i+2]];
	}
	if (Xi == NULL) {
	    return 1;
	}
	for (t=0; t<X->rows; t++) {
	    gretl_matrix_set(X, t, i, Xi[t+t1]);
	}
    }

    return 0;
}

/* populate the cross-equation covariance matrix based on the
   per-equation residuals */

static int
gls_sigma_from_uhat (gretl_equation_system *sys,
		     gretl_matrix *sigma,
		     const gretl_matrix *e, 
		     int m, int T)
{
    int geomean = system_vcv_geomean(sys);
    double lm = system_get_diag_stat(sys);
    int i, j, t;
    double xx;

    for (i=0; i<m; i++) {
	for (j=i; j<m; j++) {
	    xx = 0.0;
	    for (t=0; t<T; t++) {
		xx += gretl_matrix_get(e, i, t) * gretl_matrix_get(e, j, t);
	    }
	    if (geomean) {
		xx /= system_vcv_denom(sys, i, j);
	    } else {
		xx /= T;
	    }
	    gretl_matrix_set(sigma, i, j, xx);
	    if (j != i) {
		gretl_matrix_set(sigma, j, i, xx);
	    }
	}
    }

    if (system_get_method(sys) == SYS_OLS && lm == 0.0) {
	double sii, sij, sjj;

	for (i=1; i<m; i++) {
	    sii = gretl_matrix_get(sigma, i, i);
	    for (j=0; j<i; j++) {
		sij = gretl_matrix_get(sigma, i, j);
		sjj = gretl_matrix_get(sigma, j, j);
		lm += (sij * sij) / (sii * sjj);
	    }
	}
	lm *= T;
	system_set_diag_stat(sys, lm);
    }

    return 0;
}

/* compute residuals, for all cases other than FIML */

static void 
sys_resids (const gretl_equation_system *sys, MODEL *pmod, 
	    const double **Z, gretl_matrix *uhat)
{
    double yh;
    int i, t;

    pmod->ess = 0.0;

    for (t=pmod->t1; t<=pmod->t2; t++) {
	yh = 0.0;
	for (i=0; i<pmod->ncoeff; i++) {
	    yh += pmod->coeff[i] * Z[pmod->list[i+2]][t];
	}
	pmod->yhat[t] = yh;
	pmod->uhat[t] = Z[pmod->list[1]][t] - yh;
	/* for cross-equation vcv */
	gretl_matrix_set(uhat, pmod->ID, t - pmod->t1, pmod->uhat[t]);
	pmod->ess += pmod->uhat[t] * pmod->uhat[t];
    }

    /* df correction? */
    if (system_want_df_corr(sys)) {
	pmod->sigma = sqrt(pmod->ess / pmod->dfd);
    } else {
	pmod->sigma = sqrt(pmod->ess / pmod->nobs);
    }
}

static void
liml_scale_vcv (gretl_equation_system *sys, MODEL **models, int m,
		gretl_matrix *vcv)
{
    double s2, vij;
    int vmin = 0;
    int vi, vj;
    int i, j, k;

    for (i=0; i<m; i++) {
	s2 = models[i]->sigma * models[i]->sigma;
	for (j=0; j<models[i]->ncoeff; j++) {
	    for (k=j; k<models[i]->ncoeff; k++) {
		vi = j + vmin;
		vj = k + vmin;
		vij = gretl_matrix_get(vcv, vi, vj);
		vij *= s2;
		gretl_matrix_set(vcv, vi, vj, vij);
		gretl_matrix_set(vcv, vj, vi, vij);
	    }
	}
	vmin += models[i]->ncoeff;
    }		
}

/* calculate the standard error of the residuals for the system as a
   whole */

static double 
calc_system_sigma (const gretl_equation_system *sys, MODEL **models, int m)
{
    double ess = 0.0;
    int nr = 0, dfc = 0;
    int den = 0;
    int i;

    if (system_want_df_corr(sys)) {
	nr = system_n_restrictions(sys);
	dfc = 1;
    }

    /* is this right? */

    for (i=0; i<m; i++) {
	ess += models[i]->ess;
	den += models[i]->nobs;
	if (dfc) {
	    den -= models[i]->ncoeff;
	}
    }

    den += nr;

    return sqrt(ess / den);
}

/* compute SUR, 3SLS or LIML parameter estimates (or restricted OLS,
   TSLS) */

static int 
calculate_sys_coefficients (gretl_equation_system *sys,
			    MODEL **models, const double **Z,
			    gretl_matrix *X, gretl_matrix *uhat,
			    gretl_matrix *y, int m, int mk,
			    int do_iteration)
{
    int method = system_get_method(sys);
    int do_bdiff = ((method == SYS_3SLS) && do_iteration);
    double bij, oldb, bnum = 0.0, bden = 0.0;
    gretl_matrix *vcv;
    int i, j, k, j0;
    int err = 0;

    vcv = gretl_matrix_copy(X);
    if (vcv == NULL) {
	return 1;
    }

    err = gretl_LU_solve(X, y);
    if (err) {
	return err;
    }

    err = gretl_invert_general_matrix(vcv);
    if (err) {
	return err;
    }

#if SDEBUG
    gretl_matrix_print(vcv, "in calc_coeffs, vcv", NULL);
#endif

    j0 = 0;
    for (i=0; i<m; i++) {
	for (j=0; j<models[i]->ncoeff; j++) {
	    k = j0 + j;
	    bij = gretl_vector_get(y, k);
	    if (do_bdiff) {
		oldb = models[i]->coeff[j];
		bnum += (bij - oldb) * (bij - oldb);
		bden += oldb * oldb;
	    }
	    models[i]->coeff[j] = bij;
	}
	sys_resids(sys, models[i], Z, uhat);
	j0 += models[i]->ncoeff;
    }

    if (do_bdiff) {
	system_set_bdiff(sys, sqrt(bnum / bden));
    }

    /* simple single-equation methods: need to multiply by an estimate
       of sigma.  Should this really be the system sigma, and not
       equation-specific?
    */

    if (method == SYS_OLS || method == SYS_TSLS) {
	double s = calc_system_sigma(sys, models, m);

	gretl_matrix_multiply_by_scalar(vcv, s * s);
    } else if (method == SYS_LIML) {
	liml_scale_vcv(sys, models, m, vcv);
    }

    /* now set the model standard errors */
    j0 = 0;
    for (i=0; i<m; i++) {
	for (j=0; j<models[i]->ncoeff; j++) {
	    k = j0 + j;
	    models[i]->sderr[j] = sqrt(gretl_matrix_get(vcv, k, k));
	}
	j0 += models[i]->ncoeff;
    }

    /* are we saving the coefficient vector and covariance matrix
       (e.g. as the basis for testing restrictions)? */

    if (system_save_vcv(sys)) {
	gretl_matrix *b = gretl_matrix_copy(y);

	system_attach_coeffs(sys, b);
	system_attach_vcv(sys, vcv);
    } else {
	gretl_matrix_free(vcv);
    }

    return err;
}

/* FIXME below: naming and labeling of added variables */

static void add_results_to_dataset (gretl_equation_system *sys, 
				    MODEL *pmod, int i, int *pj,
				    double **Z, DATAINFO *pdinfo,
				    int method)
{
    int t;

    if (system_save_uhat(sys)) {
	for (t=0; t<pdinfo->n; t++) {
	    if (t < pmod->t1 || t > pmod->t2) {
		Z[*pj][t] = NADBL;
	    } else {
		Z[*pj][t] = pmod->uhat[t];
	    }
	}
	sprintf(pdinfo->varname[*pj], "uhat_s%02d", i + 1);
	if (method == SYS_SUR) {
	    sprintf(VARLABEL(pdinfo, *pj), _("SUR residual, equation %d"), 
		    i + 1);
	} else if (method == SYS_3SLS) {
	    sprintf(VARLABEL(pdinfo, *pj), _("3SLS residual, equation %d"), 
		    i + 1);
	} else {
	    sprintf(VARLABEL(pdinfo, *pj), "system residual, equation %d", 
		    i + 1);
	}
	*pj += 1;
    }

    if (system_save_yhat(sys)) {
	for (t=0; t<pdinfo->n; t++) {
	    if (t < pmod->t1 || t > pmod->t2) {
		Z[*pj][t] = NADBL;
	    } else {
		Z[*pj][t] = pmod->yhat[t];
	    }
	}	
	sprintf(pdinfo->varname[*pj], "yhat_s%02d", i + 1);
	if (method == SYS_SUR) {
	    sprintf(VARLABEL(pdinfo, *pj), _("SUR fitted value, equation %d"), 
		    i + 1);
	} else if (method == SYS_3SLS) {
	    sprintf(VARLABEL(pdinfo, *pj), _("3SLS fitted value, equation %d"), 
		    i + 1);
	} else {
	    sprintf(VARLABEL(pdinfo, *pj), "system fitted value, equation %d", 
		    i + 1);
	}	    
	*pj += 1;
    }
}

static int in_list (const int *list, int k)
{
    int i;

    for (i=1; i<=list[0]; i++) {
	if (k == list[i]) return 1;
    }

    return 0;
}

static int *
system_model_list (gretl_equation_system *sys, int i, int *freeit)
{
    int method = system_get_method(sys);
    int *list = NULL;

    *freeit = 0;

    if (method == SYS_SUR || method == SYS_3SLS ||
	method == SYS_OLS || method == SYS_TSLS) {
	list = system_get_list(sys, i);
    }

    if (method == SYS_3SLS || method == SYS_TSLS) {
	/* is list already in tsls form? */
	if (list != NULL && !in_list(list, LISTSEP)) {
	    list = NULL;
	}
    }

    if (method == SYS_FIML || method == SYS_LIML ||
	((method == SYS_3SLS || method == SYS_TSLS) 
	 && list == NULL)) {
	list = compose_tsls_list(sys, i);
	*freeit = 1;
    }

    return list;
}

static void
print_system_overidentification_test (const gretl_equation_system *sys,
				      int method, PRN *prn)
{
    int df = system_get_overid_df(sys);

    if (method == SYS_FIML && df > 0) {
	double ll = system_get_ll(sys);
	double llu = system_get_llu(sys);
	double X2;

	if (na(ll) || na(llu) || ll == 0.0 || llu == 0.0) {
	    return;
	}

	X2 = 2.0 * (llu - ll);

	pprintf(prn, "%s:\n", _("LR over-identification test"));
	pprintf(prn, "  %s = %g\n", _("Restricted log-likelihood"), ll);
	pprintf(prn, "  %s = %g\n", _("Unrestricted log-likelihood"), llu);
	pprintf(prn, "  %s(%d) = %g %s %g\n", _("Chi-square"),
		df, X2, _("with p-value"), chisq(X2, df));
	pputc(prn, '\n');
    } else if ((method == SYS_3SLS || method == SYS_SUR) && df > 0) {
	double X2 = system_get_X2(sys);

	if (na(X2) || X2 == 0.0) {
	    return;
	}

	pprintf(prn, "%s:\n", _("Hansen-Sargan over-identification test"));
	pprintf(prn, "  %s(%d) = %g %s %g\n", _("Chi-square"),
		df, X2, _("with p-value"), chisq(X2, df));
	pputc(prn, '\n');
    }
}

/* Hansen-Sargan overidentification test for the system as a whole, as
   in Davidson and MacKinnon, ETM: p. 511 and equation (12.25) for the
   case of SUR; p. 532 and equation (12.61) for the case of 3SLS.  See
   also D & M, Estimation and Inference in Econometrics, equation
   (18.60), for a more computation-friendly statement of the criterion
   function.
*/

static int hansen_sargan_test (gretl_equation_system *sys, 
			       const gretl_matrix *uhat,
			       const double **Z,
			       int t1)
{
    const int *exlist = system_get_instr_vars(sys);
    const gretl_matrix *sigma = system_get_sigma(sys);
    int nx = exlist[0];
    int m = system_n_equations(sys);
    int T = system_n_obs(sys);
    int df = system_get_overid_df(sys);

    const double *Wi, *Wj;

    gretl_matrix *WTW = NULL;
    gretl_matrix *eW = NULL;
    gretl_matrix *tmp = NULL;

    double x, X2;
    int i, j, t;
    int err = 0;

    if (df <= 0) return 1;

    WTW = gretl_matrix_alloc(nx, nx);
    eW = gretl_matrix_alloc(m, nx);
    tmp = gretl_matrix_alloc(m, nx);

    if (WTW == NULL || eW == NULL || tmp == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    /* construct W-transpose W */
    for (i=0; i<nx; i++) {
	Wi = Z[exlist[i+1]] + t1;
	for (j=i; j<nx; j++) {
	    Wj = Z[exlist[j+1]] + t1;
	    x = 0.0;
	    for (t=0; t<T; t++) {
		x += Wi[t] * Wj[t];
	    }
	    gretl_matrix_set(WTW, i, j, x);
	    if (i != j) {
		gretl_matrix_set(WTW, j, i, x);
	    }
	}
    }

    err = gretl_invert_symmetric_matrix(WTW);
    if (err) goto bailout;

    /* set up vectors of SUR or 3SLS residuals, transposed, times W:
       these are stacked in an m * nx matrix
    */
    for (i=0; i<m; i++) {
	for (j=0; j<nx; j++) {
	    Wj = Z[exlist[j+1]] + t1;
	    x = 0.0;
	    for (t=0; t<T; t++) {
		x += gretl_matrix_get(uhat, i, t) * Wj[t];
	    }
	    gretl_matrix_set(eW, i, j, x);
	}
    }

    /* multiply these vectors into (WTW)^{-1} */
    for (i=0; i<m; i++) {
	for (j=0; j<nx; j++) {
	    x = 0.0;
	    for (t=0; t<nx; t++) {
		x += gretl_matrix_get(eW, i, t) * 
		    gretl_matrix_get(WTW, t, j);
	    }
	    gretl_matrix_set(tmp, i, j, x);
	}
    }   

    /* cumulate the Chi-square value */
    X2 = 0.0;
    for (i=0; i<m; i++) {
	for (j=0; j<m; j++) {
	    x = 0.0;
	    for (t=0; t<nx; t++) {
		x += gretl_matrix_get(tmp, i, t) * 
		    gretl_matrix_get(eW, j, t); /* transposed */
	    }
	    X2 += gretl_matrix_get(sigma, i, j) * x;
	}
    }

#if SDEBUG
    fprintf(stderr, "Hansen-Sargan: Chi-square(%d) = %g (p-value %g)\n", 
	    df, X2, chisq(X2, df));
#endif
    system_set_X2(sys, X2);

 bailout:

    gretl_matrix_free(WTW);
    gretl_matrix_free(eW);
    gretl_matrix_free(tmp);

    return err;
}

static int basic_system_allocate (gretl_equation_system *sys,
				  int m, int T, int mk, 
				  int nr, int save_vcv,
				  MODEL ***models,
				  gretl_matrix **uhat, 
				  gretl_matrix **X,
				  gretl_matrix **y)
{
    int method = system_get_method(sys);
    gretl_matrix *sigma;
    MODEL **pmods;
    int ldx = mk + nr;
    int i, j;

    /* allocate a model for each stochastic equation */
    pmods = malloc(m * sizeof *pmods);
    if (pmods == NULL) {
	return E_ALLOC;
    }
    for (i=0; i<m; i++) {
	pmods[i] = gretl_model_new();
	if (pmods[i] == NULL) {
	    for (j=0; j<i; j++) {
		free_model(pmods[j]);
	    }
	    free(pmods);
	    return E_ALLOC;
	}
    }

    *models = pmods;

    *uhat = gretl_matrix_alloc(m, T);
    if (*uhat == NULL) {
	return E_ALLOC;
    }

    sigma = gretl_matrix_alloc(m, m);
    if (sigma == NULL) {
	return E_ALLOC;
    }

    system_attach_sigma(sys, sigma);

    /* simple single-equation estimators don't need the stacked X and
       y matrices, unless we're testing a set of restrictions or
       planning to save the whole system covariance matrix
    */

    if ((method == SYS_OLS || method == SYS_TSLS) 
	&& nr == 0 && !save_vcv) {
	return 0;
    }

    *X = gretl_matrix_alloc(ldx, ldx);
    if (*X == NULL) {
	return E_ALLOC;
    }

    *y = gretl_column_vector_alloc(ldx);
    if (*y == NULL) {
	return E_ALLOC;
    }

    return 0;
}

static int 
save_and_print_results (gretl_equation_system *sys, MODEL **models,
			double ***pZ, DATAINFO *pdinfo,
			gretlopt opt, PRN *prn)
{
    const char *sysname = gretl_system_get_name(sys);
    int method = system_get_method(sys);
    int iters = system_iters(sys);
    int m = system_n_equations(sys);
    int nr = system_n_restrictions(sys);
    int i, j = 0;
    int err = 0;

    if (opt & OPT_Q) {
	return 0;
    }    

    if (system_save_uhat(sys)) {
	j = pdinfo->v;
	err = dataset_add_vars(m, pZ, pdinfo);
    }
    if (system_save_yhat(sys)) {
	if (j == 0) {
	    j = pdinfo->v;
	}
	err = dataset_add_vars(m, pZ, pdinfo);
    }

    pputc(prn, '\n');

    if (sysname != NULL) {
	pprintf(prn, "%s, %s\n", _("Equation system"), sysname);
	pprintf(prn, "%s: %s\n", _("Estimator"), 
		system_get_full_string(sys, opt));
    } else {
	pprintf(prn, "%s, %s\n", _("Equation system"),
		system_get_full_string(sys, opt));
    }

    if (iters > 0) {
	pprintf(prn, _("Convergence achieved after %d iterations\n"), iters);
	if (method == SYS_SUR || method == SYS_FIML) {
	    pprintf(prn, "%s = %g\n", _("Log-likelihood"), system_get_ll(sys));
	}
    }

    pputc(prn, '\n');

    for (i=0; i<m; i++) {
	printmodel(models[i], pdinfo, OPT_NONE, prn);
	if (!err) {
	    add_results_to_dataset(sys, models[i], i, &j, *pZ, pdinfo, 
				   method);
	}
    }

    print_system_vcv(sys, prn);

    if (nr == 0 && (method == SYS_FIML || 
		    method == SYS_3SLS || 
		    method == SYS_SUR)) {
	print_system_overidentification_test(sys, method, prn);
    }

    return err;
}

/* compute log-likelihood for iterated SUR estimator */

double sur_ll (gretl_equation_system *sys, const gretl_matrix *uhat, 
	       int m, int T)
{
    gretl_matrix *sigtmp;
    double ldet, ll;

    sigtmp = gretl_matrix_alloc(m, m);
    if (sigtmp == NULL) return NADBL;

    gls_sigma_from_uhat(sys, sigtmp, uhat, m, T);
    ldet = gretl_vcv_log_determinant(sigtmp);

    if (na(ldet)) {
	ll = NADBL;
    } else {
	ll = -(m * T / 2.0) * (LN_2_PI + 1.0);
	ll -= (T / 2.0) * ldet;
    }

    system_set_ll(sys, ll);

    gretl_matrix_free(sigtmp);

    return ll;
}

/* if we're estimating with a specified set of linear restrictions,
   Rb = q, augment the X matrix with R and R-transpose */

static int 
augment_X_with_restrictions (gretl_matrix *X, int mk, 
			     gretl_equation_system *sys)
{
    const gretl_matrix *R;
    double rij;
    int i, j;

    R = system_get_R_matrix(sys);
    if (R == NULL) return 1;

    /* place the R matrix */

    kronecker_place(X, R, mk, 0, 1.0);

    /* place R-transpose */

    for (i=0; i<R->rows; i++) {
	for (j=0; j<R->cols; j++) {
	    rij = gretl_matrix_get(R, i, j);
	    gretl_matrix_set(X, j, i + mk, rij);
	}
    }

    /* zero the bottom right-hand block */

    for (i=mk; i<mk+R->rows; i++) {
	for (j=mk; j<mk+R->rows; j++) {
	    gretl_matrix_set(X, i, j, 0.0);
	}
    }

    return 0;
}

/* when estimating with a specified set of linear restrictions,
   Rb = q, augment the y matrix with q */

static int 
augment_y_with_restrictions (gretl_matrix *y, int mk, int nr,
			     gretl_equation_system *sys)
{
    const gretl_matrix *q;    
    int i;

    q = system_get_q_matrix(sys);
    if (q == NULL) return 1;

    for (i=0; i<nr; i++) {
	gretl_vector_set(y, mk + i, gretl_vector_get(q, i));
    }

    return 0;
}

#define SYS_MAX_ITER 100
#define SYS_LL_TOL 1.0e-12
#define SYS_BDIFF_TOL 1.0e-9

/* check for convergence of iteration: we use the change in the
   log-likelihood when iterating SUR to the ML solution, or
   a measure of the change in the coefficients when iterating
   three-stage least squares
*/

static int converged (gretl_equation_system *sys, int method,
		      gretl_matrix *uhat, int m, int T,
		      int iters, double *llbak, int *err,
		      PRN *prn)
{
    double crit, tol = 0.0;
    int met = 0;

    if (method == SYS_SUR) {
	double ll = sur_ll(sys, uhat, m, T);

	tol = SYS_LL_TOL;
	crit = ll - *llbak;

#if SDEBUG
	printf("SUR iteration %d, ll = %.8g\n", iters, ll);
#endif

	if (crit <= tol) {
	    met = 1;
	} else if (iters < SYS_MAX_ITER) {
	    *llbak = ll;
	} 
    } else if (method == SYS_3SLS) {
	tol = SYS_BDIFF_TOL;
	crit = system_get_bdiff(sys);

#if SDEBUG
	printf("3SLS iteration %d, crit = %.8g\n", iters, crit);
#endif

	if (crit <= tol) {
	    met = 1;
	} 
    }

    if (!met && iters >= SYS_MAX_ITER) {
	pprintf(prn, "reached %d iterations without meeting "
		"tolerance of %g\n", iters, tol);
	*err = E_NOCONV;
    }	

    return met;
}

static void clean_up_models (gretl_equation_system *sys, 
			     MODEL **models, int m)
{
    int method = system_get_method(sys);
    double ess = 0.0;
    int i;

    for (i=0; i<m; i++) {
	ess += models[i]->ess;
	if (method == SYS_3SLS || method == SYS_FIML || 
	    method == SYS_TSLS || method == SYS_LIML) {
	    tsls_free_data(models[i]);
	}
	free_model(models[i]);
    }

    free(models);

    system_set_ess(sys, ess);
}

/* general function that forms the basis for all system estimates */

int system_estimate (gretl_equation_system *sys, double ***pZ, DATAINFO *pdinfo, 
		     gretlopt opt, PRN *prn)
{
    int i, j, k, T, t, m = 0;
    int v, l, mk, krow, nr;
    int orig_t1 = pdinfo->t1;
    int orig_t2 = pdinfo->t2;

    gretl_matrix *X = NULL;
    gretl_matrix *y = NULL;
    gretl_matrix *uhat = NULL;
    gretl_matrix *sigma = NULL;
    gretl_matrix *Xi = NULL;
    gretl_matrix *Xj = NULL;
    gretl_matrix *M = NULL;
    MODEL **models = NULL;

    int method = system_get_method(sys);
    double llbak = -1.0e9;
    int single_equation = 0;
    int do_iteration = 0;
    int save_vcv = 0;
    int rtsls = 0;
    int iters = 0;
    int err = 0;

    if (opt & OPT_T) {
	do_iteration = 1;
    }
    
    nr = system_n_restrictions(sys);

    if (method == SYS_OLS || method == SYS_TSLS || method == SYS_LIML) {
	single_equation = 1;
    }

    if (nr > 0 && method == SYS_3SLS) {
	/* doing 3SLS with restrictions: we want to obtain
	   restricted TSLS estimates as a starting point */
	rtsls = 1;
    }

    if (system_save_vcv(sys)) {
	/* saving covariance matrix for testing restrictions */
	save_vcv = 1;
    }

    /* get uniform sample starting and ending points */
    if (system_adjust_t1t2(sys, &pdinfo->t1, &pdinfo->t2, 
			   (const double **) *pZ)) {
	err = E_DATA;
	goto cleanup;
    }   

    /* number of equations */
    m = system_n_equations(sys);

    /* max indep vars per equation */
    k = system_max_indep_vars(sys);

    /* total indep vars, all equations */
    mk = system_n_indep_vars(sys);

    /* number of observations per series */
    T = pdinfo->t2 - pdinfo->t1 + 1;
    system_set_n_obs(sys, T);

    /* allocate models etc */
    err = basic_system_allocate(sys, m, T, mk, nr, save_vcv,
				&models, &uhat, &X, &y);
    if (err) goto cleanup;

    sigma = system_get_sigma(sys);
	    
    if ((method == SYS_FIML || method == SYS_LIML) && !(opt & OPT_Q)) {
	print_equation_system_info(sys, pdinfo, prn);
    }

    /* First estimate the equations separately, and put the
       single-equation residuals into the uhat matrix.  Note that at
       this stage we are not in a position to impose any
       cross-equation restrictions, since we're doing straight
       equation-by-equation estimation.
    */

    for (i=0; i<m; i++) {
	int freeit = 0;
	int *list = system_model_list(sys, i, &freeit);
	MODEL *pmod = models[i];

	if (list == NULL) {
	    err = 1;
	    break;
	}

	if (method == SYS_SUR || method == SYS_OLS) {
	    *pmod = lsq(list, pZ, pdinfo, OLS, OPT_A, 0.0);
	} else if (method == SYS_3SLS || method == SYS_FIML || 
		   method == SYS_TSLS) {
	    *pmod = tsls_func(list, 0, pZ, pdinfo, OPT_S);
	} else if (method == SYS_LIML) {
	    *pmod = tsls_func(list, 0, pZ, pdinfo, OPT_N | OPT_S);
	}

	if (freeit) {
	    free(list);
	}

	if ((err = pmod->errcode)) {
	    fprintf(stderr, "model failed on lists[%d], code=%d\n",
		    i, err);
	    break;
	}

	pmod->ID = i;
	pmod->aux = AUX_SYS;
	gretl_model_set_int(pmod, "method", method);

	for (t=0; t<T; t++) {
	    gretl_matrix_set(uhat, i, t, pmod->uhat[t + pdinfo->t1]);
	}
    }

    if (err) goto cleanup;

    if (method == SYS_LIML) {
	/* compute the minimum eigenvalues and generated the
	   suitably transformed data matrices */
	system_attach_models(sys, models);
	err = liml_driver(sys, pZ, pdinfo, opt, prn);
	system_unattach_models(sys);
	if (err) goto cleanup;
    }

    /* marker for iterated versions of SUR or 3SLS; also for 
       loopback in case of restricted 3SLS, where we
       want to compute restricted TSLS estimates first
    */
 iteration_start:

    gls_sigma_from_uhat(sys, sigma, uhat, m, T);

    /* simple single-equation method, no restrictions to test and 
       system vcv not required: skip ahead */
    if ((method == SYS_OLS || method == SYS_TSLS) 
	&& nr == 0 && !system_save_vcv(sys)) {
	goto print_save;
    }

    if (single_equation || rtsls) {
	gretl_matrix_zero(X);
    } else {
	err = gretl_invert_symmetric_matrix(sigma);    
	if (err) {
	    goto cleanup;
	}
    }

    /* the tests against NULL here allow for the possibility that
       we're iterating */

    if (Xi == NULL) {
	Xi = gretl_matrix_alloc(T, k);
    }
    if (Xj == NULL) {
	Xj = gretl_matrix_alloc(T, k);
    } 
    if (M == NULL) {
	M = gretl_matrix_alloc(k, k);
    }
    if (Xi == NULL || Xj == NULL || M == NULL) {
	err = E_ALLOC;
	goto cleanup;
    }

    /* form the big stacked X matrix: Xi = data matrix for equation i, 
       specified in lists[i] 
    */

    krow = 0;
    for (i=0; i<m && !err; i++) {
	int kcol = 0;

	err = make_sys_X_block(Xi, models[i], *pZ, pdinfo->t1, method);

	for (j=0; j<m && !err; j++) { 
	    const gretl_matrix *Xk;
	    double sij;

	    if (i != j) {
		if (single_equation || rtsls) {
		    kcol += models[j]->ncoeff;
		    continue;
		}
		err = make_sys_X_block(Xj, models[j], *pZ, pdinfo->t1, method);
		Xk = Xj;
	    } else if (method == SYS_LIML) {
		err = make_liml_X_block(Xj, models[i], *pZ, pdinfo->t1);
		Xk = Xj;
	    } else {
		Xk = Xi;
	    }

	    M->rows = Xi->cols;
	    M->cols = Xk->cols;

	    err = gretl_matrix_multiply_mod(Xi, GRETL_MOD_TRANSPOSE,
					    Xk, GRETL_MOD_NONE, 
					    M);

	    if (single_equation || rtsls) {
		sij = 1.0;
	    } else {
		sij = gretl_matrix_get(sigma, i, j);
	    }

	    kronecker_place(X, M, krow, kcol, sij);
	    kcol += models[j]->ncoeff;
	}

	krow += models[i]->ncoeff;
    }

    if (err) goto cleanup;

    if (nr > 0) {
	/* there are cross-equation restrictions to be imposed */
	augment_X_with_restrictions(X, mk, sys);
    }

    if (!do_iteration && !rtsls) {
	/* we're not coming back this way, so free some storage */
	gretl_matrix_free(Xj);
	Xj = NULL;
	gretl_matrix_free(M);
	M = NULL;
    }

    /* form stacked Y column vector (m x k) */

    v = 0;
    for (i=0; i<m; i++) { 

	/* loop over the m vertically-arranged blocks */

	make_sys_X_block(Xi, models[i], *pZ, pdinfo->t1, method);

	for (j=0; j<models[i]->ncoeff; j++) { /* loop over the rows within each of 
						 the m blocks */
	    double yv = 0.0;
	    int lmin = 0, lmax = m;

	    if (single_equation || rtsls) {
		/* no cross terms wanted */
		lmin = i;
		lmax = i + 1;
	    }

	    for (l=lmin; l<lmax; l++) { /* loop over the components that
					   must be added to form each element */
		const double *yl = NULL;
		double sil, xx = 0.0;

		if (method == SYS_LIML) {
		    yl = gretl_model_get_data(models[l], "liml_y");
		} else {
		    yl = (*pZ)[system_get_depvar(sys, l)];
		}

		/* multiply X'[l] into y */
		for (t=0; t<T; t++) {
		    xx += gretl_matrix_get(Xi, t, j) * yl[t + pdinfo->t1];
		}

		if (single_equation || rtsls) {
		    sil = 1.0;
		} else {
		    sil = gretl_matrix_get(sigma, i, l);
		}

		yv += xx * sil;
	    }
	    gretl_vector_set(y, v++, yv);
	}
    }

    if (nr > 0) {
	/* there are cross-equation restrictions */
	augment_y_with_restrictions(y, mk, nr, sys);
    }

#if SDEBUG
    gretl_matrix_print(X, "sys X", NULL);
    gretl_matrix_print(y, "sys y", NULL);
#endif    

    /* The estimates calculated below will be SUR, 3SLS or LIML,
       depending on how the data matrices above were constructed --
       unless we're just doing restricted OLS or TSLS estimates
    */
    calculate_sys_coefficients(sys, models, (const double **) *pZ, X, 
			       uhat, y, m, mk, do_iteration);

    if (rtsls) {
	rtsls = 0;
	goto iteration_start;
    }

    if (do_iteration) {
	if (!converged(sys, method, uhat, m, T, iters, &llbak, 
		       &err, prn)) {
	    if (err) {
		goto cleanup;
	    } else {
		iters++;
		goto iteration_start;
	    }
	}
    }

    if (nr == 0 && (method == SYS_3SLS || method == SYS_SUR)) {
	/* compute this test while we have sigma-inverse available */
	hansen_sargan_test(sys, uhat, (const double **) *pZ, pdinfo->t1);
    }

    /* refresh sigma (non-inverted) */
    gls_sigma_from_uhat(sys, sigma, uhat, m, T);

    /* record the number of iterations taken */
    system_set_iters(sys, iters);

    if (method == SYS_FIML) {
	/* set up convenience pointers */
	system_attach_uhat(sys, uhat);
	system_attach_models(sys, models);

	/* compute FIML estimates */
	err = fiml_driver(sys, pZ, pdinfo, opt, prn);

	/* detach convenience pointers */
	system_unattach_uhat(sys);
	system_unattach_models(sys);
    }

 print_save: 

    if (!err) {
	err = save_and_print_results(sys, models, pZ, pdinfo, 
				     opt, prn);
    }

 cleanup:

    gretl_matrix_free(Xi);
    gretl_matrix_free(Xj);
    gretl_matrix_free(M);
    gretl_matrix_free(X);
    gretl_matrix_free(y);
    gretl_matrix_free(uhat);

    if (models != NULL) {
	clean_up_models(sys, models, m);
    }

    pdinfo->t1 = orig_t1;
    pdinfo->t2 = orig_t2;

    return err;
}
