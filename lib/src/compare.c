/*
 *  Copyright (c) by Ramu Ramanathan and Allin Cottrell
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

/*
    compare.c - gretl model comparison procedures
*/

#include "libgretl.h"
#include "internal.h"

#ifdef OS_WIN32
# include <windows.h>
#endif

static int _justreplaced (int i, const DATAINFO *pdinfo, 
			  const int *list);

/* ........................................................... */

int _addtolist (const int *oldlist, const int *addvars, int **plist,
		const DATAINFO *pdinfo, int model_count)
/* Adds specified independent variables to a specified
   list, forming newlist.  The first element of addvars
   is the number of variables to be added; the remaining
   elements are the ID numbers of the variables to be added.
*/
{
    int i, j, k, match;
    const int nadd = addvars[0];

    *plist = malloc((oldlist[0] + nadd + 1) * sizeof **plist);
    if (*plist == NULL) return E_ALLOC;

    for (i=0; i<=oldlist[0]; i++) (*plist)[i] = oldlist[i];
    k = oldlist[0];

    for (i=1; i<=addvars[0]; i++) {
	match = 0;
	for (j=1; j<=oldlist[0]; j++) {
	    if (addvars[i] == oldlist[j]) {
		/* a "new" var was already present */
		free(*plist);
		return E_ADDDUP;
	    }
	}
	if (!match) {
	    (*plist)[0] += 1;
	    k++;
	    (*plist)[k] = addvars[i];
	}
    }

    if ((*plist)[0] == oldlist[0]) {
	return E_NOADD;
    }

    if (_justreplaced(model_count, pdinfo, oldlist)) {
	return E_VARCHANGE;
    }

    return 0;
}

/* ........................................................... */

int _omitfromlist (int *list, const int *omitvars, int *newlist,
		   const DATAINFO *pdinfo, int model_count)
/* Drops specified independent variables from a specified
   list, forming newlist.  The first element of omitvars
   is the number of variables to be omitted; the remaining
   elements are the ID numbers of the variables to be dropped.
*/
{
    int i, j, k, nomit = omitvars[0], l0 = list[0], match; 

    /* attempting to omit all vars or more ? */
    if (nomit >= l0 - 1) return E_NOVARS;

    newlist[0] = 1;
    newlist[1] = list[1];
    k = 1;

    for (i=2; i<=l0; i++) {
        match = 0;
        for (j=1; j<=nomit; j++) {
            if (list[i] == omitvars[j]) {
                match = 1; /* matching var: omit it */
		break;
            }
        }
        if (!match) { /* var is not in omit list: keep it */
	    k++;
            newlist[k] = list[i];
        }
    }
    newlist[0] = k;

    if (newlist[0] == list[0]) {
	/* no vars were omitted */
	return E_NOOMIT; 
    }

    if (_justreplaced(model_count, pdinfo, newlist)) {
	/* values of one or more vars to omit have changed */
	return E_VARCHANGE; 
    }

    return 0;
}

/* ........................................................... */

static void _difflist (int *biglist, int *smalist, int *targ)
{
    int i, j, k, match;

    targ[0] = biglist[0] - smalist[0];
    k = 1;

    for (i=2; i<=biglist[0]; i++) {
	match = 0;
	for (j=2; j<=smalist[0]; j++) {
	    if (smalist[j] == biglist[i]) {
		match = 1;
		break;
	    }
	}
	if (!match) {
	    targ[k++] = biglist[i];
	}
    }
}

/* ........................................................... */

static int _justreplaced (int i, const DATAINFO *pdinfo, 
			  const int *list)
     /* check if any var in list has been replaced via genr since a
	previous model (model_count i) was estimated.  Expects
	the "label" in datainfo to be of the form "Replaced
	after model <count>" */
{
    int j, repl = 0;

    for (j=1; j<=list[0]; j++) {
	if (strncmp(VARLABEL(pdinfo, list[j]), _("Replaced"), 8) == 0 &&
	    sscanf(VARLABEL(pdinfo, list[j]), "%*s %*s %*s %d", &repl) == 1)
	if (repl >= i) return 1;
    }
    return 0; 
}

/* ........................................................... */

static COMPARE add_compare (const MODEL *pmodA, const MODEL *pmodB) 
/* Generate comparison statistics between an initial model, A,
   and a new model, B, arrived at by adding variables to A. */
{
    COMPARE add;
    int i;	

    add.m1 = pmodA->ID;
    add.m2 = pmodB->ID;
    add.F = 0.0;
    add.ci = pmodA->ci;

    add.score = 0;
    add.dfn = pmodB->ncoeff - pmodA->ncoeff;
    add.dfd = pmodB->dfd;

    if (add.ci == OLS && pmodB->aux == AUX_ADD) {
	add.F = ((pmodA->ess - pmodB->ess)/pmodB->ess)
	    * add.dfd / add.dfn;
    }
    else if (add.ci == LOGIT || add.ci == PROBIT) {
	add.chisq = 2.0 * (pmodB->lnL - pmodA->lnL);
	return add;
    }
    else if (add.ci == HCCM) {
	add.chisq = pmodB->chisq; 
    }

    for (i=0; i<8; i++) {
	if (pmodB->criterion[i] < pmodA->criterion[i]) add.score++;
    }
    return add;
}	    

/* ........................................................... */

static COMPARE omit_compare (const MODEL *pmodA, const MODEL *pmodB)
/* Generate comparison statistics between a general model, A,
   and a restricted model B, arrived at via one or more
   zero restrictions on the parameters of A.
*/
{
    COMPARE omit;
    int i;	

    omit.m1 = pmodA->ID;
    omit.m2 = pmodB->ID;
    omit.ci = pmodA->ci;
    omit.score = 0;

    omit.dfn = pmodA->dfn - pmodB->dfn;

    if (omit.ci == OLS || omit.ci == LOGIT || omit.ci == PROBIT) {
	omit.dfd = pmodA->dfd;
	if (pmodA->ifc && !pmodB->ifc) omit.dfn += 1;
	if (omit.ci == OLS) {
	    omit.F = ((pmodB->ess - pmodA->ess)/pmodA->ess)
		* omit.dfd/omit.dfn;
	} else {
	    omit.chisq = 2.0 * (pmodA->lnL - pmodB->lnL);
	    return omit;
	}
    }

    if (omit.ci == HCCM) {
	omit.chisq = pmodB->chisq;
    }

    for (i=0; i<8; i++) 
	if (pmodB->criterion[i] < pmodA->criterion[i]) omit.score++;

    return omit;
}

/* ........................................................... */

static double robust_lm_test (MODEL *unrest, MODEL *rest, 
			      const int *omitvars,
			      double ***pZ, DATAINFO *pdinfo)
{
    double **r = NULL;
    double lm = -1;
    MODEL aux;
    int *auxlist = NULL;
    int i, q = omitvars[0];
    int origv = pdinfo->v;

    r = malloc(q * sizeof *r);
    if (r == NULL) return lm;
    for (i=0; i<q; i++) r[i] = NULL;

    _init_model(&aux, pdinfo);

    for (i=0; i<q; i++) {
	rest->list[1] = omitvars[i + 1];
	aux = lsq(rest->list, pZ, pdinfo, OLS, 1, 0.0);
	if (aux.errcode) {
	    clear_model(&aux, pdinfo);
	    goto cleanup;
	}
	r[i] = aux.uhat;
	aux.uhat = NULL;
	clear_model(&aux, pdinfo);
    }
    rest->list[1] = unrest->list[1];

    if (dataset_add_vars(q, pZ, pdinfo)) goto cleanup;

    auxlist = malloc((q + 2) * sizeof *auxlist);
    if (auxlist == NULL) goto cleanup;

    auxlist[0] = q + 1;
    auxlist[1] = 0;

    for (i=0; i<q; i++) {
	int t;

	for (t=0; t<pdinfo->n; t++) {
	    if (na(rest->uhat[t]) || na(r[i][t])) {
		(*pZ)[origv + i][t] = NADBL;
	    } else {
		(*pZ)[origv + i][t] = rest->uhat[t] * r[i][t];
	    }
	}
	auxlist[i + 2] = origv + i;
    }
	
    aux = lsq(auxlist, pZ, pdinfo, OLS, 1, 0.0);
    if (!aux.errcode) {
	lm = (double) (aux.t2 - aux.t1 + 1) - aux.ess;
    }
    clear_model(&aux, pdinfo);

 cleanup:
    if (r != NULL) {
	for (i=0; i<q; i++) {
	    free(r[i]);
	}
	free(r);
    }
    free(auxlist);
    dataset_drop_vars(q, pZ, pdinfo);

    return lm;
}

/**
 * auxreg:
 * @addvars: list of variables to add to original model (or NULL)
 * @orig: pointer to original model.
 * @new: pointer to new (modified) model.
 * @model_count: count of models estimated so far.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @aux_code: code indicating what sort of aux regression to run.
 * @prn: gretl printing struct.
 * @test: hypothesis test results struct.
 *
 * Run an auxiliary regression, in order to test a given set of added
 * variables, or to test for non-linearity (squares, logs).
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int auxreg (LIST addvars, MODEL *orig, MODEL *new, int *model_count, 
	    double ***pZ, DATAINFO *pdinfo, int aux_code, 
	    PRN *prn, GRETLTEST *test)
{
    COMPARE add;  
    MODEL aux;
    int *newlist, *tmplist = NULL;
    int i, j, t, listlen, pos = 0, m = *model_count;
    const int n = pdinfo->n, orig_nvar = pdinfo->v; 
    double trsq = 0.0, rho = 0.0; 
    int newvars = 0, err = 0;

    if (orig->ci == TSLS || orig->ci == NLS) return E_NOTIMP;

    /* temporarily re-impose the sample that was in force when the
       original model was estimated */
    exchange_smpl(orig, pdinfo);

    _init_model(&aux, pdinfo);

    /* was a specific list of vars to add passed in, or should we
       concoct one? (e.g. "lmtest") */

    if (addvars != NULL) {
	/* specific list was given */
	err = _addtolist(orig->list, addvars, &newlist, pdinfo, m);
    } else {
	/* we should concoct one */
	listlen = orig->list[0] - orig->ifc;
	tmplist = malloc(listlen * sizeof *tmplist);
	if (tmplist == NULL) {
	    err = E_ALLOC;
	} else {
	    tmplist[0] = listlen - 1;
	    j = 2;
	    for (i=1; i<=tmplist[0]; i++) {
		if (orig->list[j] == 0) j++;
		tmplist[i] = orig->list[j++];
	    }
	    /* no cross-products yet */
	    if (aux_code == AUX_SQ) { 
		/* add squares of original variables */
		newvars = xpxgenr(tmplist, pZ, pdinfo, 0, 0);
		if (newvars < 0) {
		    fprintf(stderr, "gretl: generation of squares failed\n");
		    free(tmplist);
		    err = E_SQUARES;
		}
	    }
	    else if (aux_code == AUX_LOG) { /* add logs of orig vars */
		newvars = logs(tmplist, pZ, pdinfo);
		if (newvars < 0) {
		    fprintf(stderr, "gretl: generation of logs failed\n");
		    free(tmplist);
		    err = E_LOGS;
		}
	    }	    
	    /* now construct an "addvars" list including all the
	       vars that were just generated -- re-use tmplist */
	    if (!err) {
		tmplist = realloc(tmplist, (newvars + 2) * sizeof *tmplist);
		if (tmplist == NULL) {
		    err = E_ALLOC;
		} else {
		    tmplist[0] = pdinfo->v - orig_nvar;
		    for (i=1; i<=tmplist[0]; i++) { 
			tmplist[i] = i + orig_nvar - 1;
		    }
		    err = _addtolist(orig->list, tmplist, &newlist,
				     pdinfo, m);
		}
	    }
	} /* tmplist != NULL */
    }

    /* ADD: run an augmented regression, matching the original
       estimation method */
    if (!err && aux_code == AUX_ADD) {
	if (orig->ci == CORC || orig->ci == HILU) {
	    err = hilu_corc(&rho, newlist, pZ, pdinfo, 
			    NULL, 1, orig->ci, prn);
	}
	else if (orig->ci == WLS || orig->ci == AR) {
	    pos = _full_model_list(orig, &newlist);
	    if (pos < 0) err = E_ALLOC;
	}

	if (!err) {
	    /* select sort of model to estimate */
	    if (orig->ci == AR) {
		*new = ar_func(newlist, pos, pZ, pdinfo, model_count, prn);
		*model_count -= 1;
	    }
	    else if (orig->ci == ARCH) {
		*new = arch(orig->order, newlist, pZ, pdinfo, model_count, 
			    prn, NULL);
		*model_count -= 1;
	    } 
	    else if (orig->ci == LOGIT || orig->ci == PROBIT) {
		*new = logit_probit(newlist, pZ, pdinfo, orig->ci);
	    }
	    else {
		*new = lsq(newlist, pZ, pdinfo, orig->ci, 1, rho);
	    }

	    if (new->nobs < orig->nobs) 
		new->errcode = E_MISS;
	    if (new->errcode) {
		err = new->errcode;
		free(newlist);
		if (addvars == NULL) free(tmplist); 
		clear_model(new, pdinfo);
	    } else {
		++m;
		new->ID = m;
	    }
	}
    } /* end if AUX_ADD */

    /* non-linearity test? Run auxiliary regression here -- 
       Replace depvar with uhat from orig */
    else if (!err && (aux_code == AUX_SQ || aux_code == AUX_LOG)) {
	int df = 0;

	/* grow data set to accommodate new dependent var */
	if (dataset_add_vars(1, pZ, pdinfo)) {
	    err = E_ALLOC;
	} else {
	    for (t=0; t<n; t++) {
		(*pZ)[pdinfo->v - 1][t] = NADBL;
	    }
	    for (t=orig->t1; t<=orig->t2; t++) {
		(*pZ)[pdinfo->v - 1][t] = orig->uhat[t];
	    }
	    newlist[1] = pdinfo->v - 1;
	    pdinfo->extra = 1;

	    aux = lsq(newlist, pZ, pdinfo, OLS, 1, rho);
	    if (aux.errcode) {
		err = aux.errcode;
		fprintf(stderr, "auxiliary regression failed\n");
		free(newlist);
		if (addvars == NULL) free(tmplist); 
	    } else {
		aux.aux = aux_code;
		printmodel(&aux, pdinfo, prn);
		trsq = aux.rsq * aux.nobs;

		if (test != NULL) {
		    gretl_test_init(test);
		    df = newlist[0] - orig->list[0];
		    strcpy(test->type, (aux_code == AUX_SQ)?
			    N_("Non-linearity test (squares)") :
			    N_("Non-linearity test (logs)"));
		    strcpy(test->h_0, N_("relationship is linear"));
		    test->teststat = GRETL_TEST_TR2;
		    test->dfn = df;
		    test->value = trsq;
		    test->pvalue = chisq(trsq, df);
		}
	    } /* ! aux.errcode */
	    clear_model(&aux, pdinfo);
	    /* shrink for uhat */
	    dataset_drop_vars(1, pZ, pdinfo);
	    pdinfo->extra = 0;
	}
    }

    if (!err) {
	if (aux_code == AUX_ADD) {
	    new->aux = aux_code;
	    if (orig->ci == HCCM) {
		new->chisq = robust_lm_test(new, orig, addvars, pZ, pdinfo);
	    }
	}
	add = add_compare(orig, new);
	add.trsq = trsq;

	if (aux_code == AUX_ADD && new->ci != AR && new->ci != ARCH)
	    printmodel(new, pdinfo, prn);

	if (addvars != NULL) {
	    _difflist(new->list, orig->list, addvars);
	    gretl_print_add(&add, addvars, pdinfo, aux_code, prn);
	} else {
	    add.dfn = newlist[0] - orig->list[0];
	    gretl_print_add(&add, tmplist, pdinfo, aux_code, prn);
	}

	*model_count += 1;
	free(newlist);
	if (addvars == NULL) free(tmplist); 

	/* trash any extra variables generated (squares, logs) */
	if (pdinfo->v > orig_nvar) {
	    dataset_drop_vars(pdinfo->v - orig_nvar, pZ, pdinfo);
	}
    }

    /* put back into pdinfo what was there on input */
    exchange_smpl(orig, pdinfo);
    return err;
}

/**
 * omit_test:
 * @omitvars: list of variables to omit from original model.
 * @orig: pointer to original model.
 * @new: pointer to new (modified) model.
 * @model_count: count of models estimated so far.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 *
 * Re-estimate a given model after removing a list of 
 * specified variables.
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int omit_test (LIST omitvars, MODEL *orig, MODEL *new, 
	       int *model_count, double ***pZ, DATAINFO *pdinfo, 
	       PRN *prn)
{
    COMPARE omit;             /* Comparison struct for two models */
    int *tmplist, m = *model_count, pos = 0;
    int maxlag = 0, t1 = pdinfo->t1;
    double rho = 0.0;
    int err = 0;

    if (orig->ci == TSLS || orig->ci == NLS) return E_NOTIMP;

    /* temporarily impose the sample that was in force when the
       original model was estimated */
    exchange_smpl(orig, pdinfo);

    if (orig->ci == AR) 
	maxlag = orig->arinfo->arlist[orig->arinfo->arlist[0]];
    else if (orig->ci == ARCH) 
	maxlag = orig->order;
    pdinfo->t1 = orig->t1 - maxlag;

    tmplist = malloc((orig->ncoeff + 2) * sizeof *tmplist);
    if (tmplist == NULL) { 
	pdinfo->t1 = t1;
	err = E_ALLOC; 
    } else {
	err = _omitfromlist(orig->list, omitvars, tmplist, pdinfo, m);
	if (err) {
	    free(tmplist);
	}
    }

    if (!err) {
	if (orig->ci == CORC || orig->ci == HILU) {
	    err = hilu_corc(&rho, tmplist, pZ, pdinfo, 
			    NULL, 1, orig->ci, prn);
	    if (err) {
		free(tmplist);
	    }
	}
	else if (orig->ci == WLS || orig->ci == AR) {
	    pos = _full_model_list(orig, &tmplist);
	    if (pos < 0) {
		free(tmplist);
		err = E_ALLOC;
	    }
	}
    }

    if (!err) {
	if (orig->ci == AR) {
	    *new = ar_func(tmplist, pos, pZ, pdinfo, model_count, prn);
	    *model_count -= 1;
	}
	else if (orig->ci == ARCH) {
	    *new = arch(orig->order, tmplist, pZ, pdinfo, model_count, 
			prn, NULL);
	    *model_count -= 1;
	} 
	else if (orig->ci == LOGIT || orig->ci == PROBIT) {
	    *new = logit_probit(tmplist, pZ, pdinfo, orig->ci);
	    new->aux = AUX_OMIT;
	}
	else {
	    *new = lsq(tmplist, pZ, pdinfo, orig->ci, 1, rho);
	}

	if (new->errcode) {
	    pprintf(prn, "%s\n", gretl_errmsg);
	    free(tmplist);
	    err = new->errcode; 
	}
    }

    if (!err) {
	++m;
	new->ID = m;
	if (orig->ci == HCCM) { 
	    new->chisq = robust_lm_test(orig, new, omitvars, pZ, pdinfo);
	}
	omit = omit_compare(orig, new);
	if (orig->ci != AR && orig->ci != ARCH) 
	    printmodel(new, pdinfo, prn); 
	_difflist(orig->list, new->list, omitvars);
	gretl_print_omit(&omit, omitvars, pdinfo, prn);     

	*model_count += 1;
	free(tmplist);
	if (orig->ci == LOGIT || orig->ci == PROBIT)
	    new->aux = AUX_NONE;
    }

    pdinfo->t1 = t1;

    /* put back into pdinfo what was there on input */
    exchange_smpl(orig, pdinfo);

    return err;
}

static int ljung_box (int varno, int order, double **Z, 
		      DATAINFO *pdinfo, double *lb)
{
    double *x, *y, *acf;
    int k, l, nobs, n = pdinfo->n; 
    int t, t1 = pdinfo->t1, t2 = pdinfo->t2;
    int list[2];

    list[0] = 1;
    list[1] = varno;
    _adjust_t1t2(NULL, list, &t1, &t2, Z, NULL);
    nobs = t2 - t1 + 1;

    x = malloc(n * sizeof *x);
    y = malloc(n * sizeof *y);
    acf = malloc((order + 1) * sizeof *acf);
    if (x == NULL || y == NULL || acf == NULL)
	return E_ALLOC;    

    for (l=1; l<=order; l++) {
	for (t=t1+l; t<=t2; t++) {
	    k = t - (t1+l);
	    x[k] = Z[varno][t];
	    y[k] = Z[varno][t-l];
	}
	acf[l] = _corr(nobs-l, x, y);
    }

    /* compute Ljung-Box statistic */
    *lb = 0;
    for (t=1; t<=order; t++) { 
	*lb += acf[t] * acf[t] / (nobs - t);
    }
    *lb *= nobs * (nobs + 2.0);

    free(x);
    free(y);
    free(acf);

    return 0;
}

void gretl_test_init (GRETLTEST *test)
{
    test->type[0] = 0;
    test->h_0[0] = 0;
    test->param[0] = 0;
}

/**
 * reset_test:
 * @pmod: pointer to model to be tested.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * @test: hypothesis test results struct.
 *
 * Ramsey's RESET test for model specification.
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int reset_test (MODEL *pmod, double ***pZ, DATAINFO *pdinfo, 
		PRN *prn, GRETLTEST *test)
{
    int *newlist;
    MODEL aux;
    int i, t, v = pdinfo->v; 
    double RF;
    int err = 0;

    if (pmod->ci != OLS) return E_OLSONLY;

    _init_model(&aux, pdinfo);

    if (pmod->ncoeff + 2 >= pdinfo->t2 - pdinfo->t1)
	return E_DF;

    newlist = malloc((pmod->list[0] + 3) * sizeof *newlist);
    if (newlist == NULL) {
	err = E_ALLOC;
    } else {
	newlist[0] = pmod->list[0] + 2;
	for (i=1; i<=pmod->list[0]; i++) newlist[i] = pmod->list[i];
	if (dataset_add_vars(2, pZ, pdinfo)) {
	    err = E_ALLOC;
	}
    }

    if (!err) {
	/* add yhat^2, yhat^3 to data set */
	for (t = pmod->t1; t<=pmod->t2; t++) {
	    double xx = pmod->yhat[t];

	    (*pZ)[v][t] = xx * xx;
	    (*pZ)[v+1][t] = xx * xx * xx;
	}
	strcpy(pdinfo->varname[v], "yhat^2");
	strcpy(pdinfo->varname[v+1], "yhat^3");
	newlist[pmod->list[0] + 1] = v;
	newlist[pmod->list[0] + 2] = v + 1;
    }

    if (!err) {
	aux = lsq(newlist, pZ, pdinfo, OLS, 1, 0.0);
	err = aux.errcode;
	if (err) {
	    errmsg(aux.errcode, prn);
	}
    } 

    if (!err) {
	aux.aux = AUX_RESET;
	printmodel(&aux, pdinfo, prn);
	RF = ((pmod->ess - aux.ess) / 2) / (aux.ess / aux.dfd);

	pprintf(prn, "\n%s: F = %f,\n", _("Test statistic"), RF);
	pprintf(prn, "%s = P(F(%d,%d) > %g) = %.3g\n", _("with p-value"), 
		2, aux.dfd, RF, fdist(RF, 2, aux.dfd));

	if (test != NULL) {
	    gretl_test_init(test);
	    strcpy(test->type, N_("RESET test for specification"));
	    strcpy(test->h_0, N_("specification is adequate"));
	    test->teststat = GRETL_TEST_RESET;
	    test->dfn = 2;
	    test->dfd = aux.dfd;
	    test->value = RF;
	    test->pvalue = fdist(RF, test->dfn, test->dfd);
	}
    }

    free(newlist);
    dataset_drop_vars(2, pZ, pdinfo); 
    clear_model(&aux, pdinfo); 

    return err;
}

/* Below: apparatus for generating standard errors that are robust in 
   face of general serial correlation (see Wooldridge, Introductory
   Econometrics, chapter 12)
*/

static double get_vhat (double *ahat, int g, int t1, int t2)
{
    int t, h;
    double mult, a_cross_sum;
    double vhat;

    vhat = 0.0;

    for (t=t1; t<=t2; t++) {
	vhat += ahat[t] * ahat[t];
    }

    for (h=1; h<=g; h++) {
	mult = 1.0 - (double) h / (g + 1);
	a_cross_sum = 0.0;
	for (t=h+t1; t<=t2; t++) {
	    a_cross_sum += ahat[t] * ahat[t-h];
	}
	vhat += 2.0 * mult * a_cross_sum;
    }

    return vhat;
}

static int autocorr_standard_errors (MODEL *pmod, double ***pZ, 
				     DATAINFO *pdinfo, PRN *prn)
{
    int *auxlist = NULL;
    double *ahat = NULL;
    double *robust = NULL;
    double *tmp;
    int i, j, g;
    int aux = AUX_NONE, order = 0;
    MODEL auxmod;

    auxlist = malloc(pmod->list[0] * sizeof *auxlist);
    ahat = malloc(pdinfo->n * sizeof *ahat);
    robust = malloc(pmod->ncoeff * sizeof *robust);

    if (auxlist == NULL || ahat == NULL || robust == NULL) {
	free(auxlist);
	free(ahat);
	free(robust);
	return E_ALLOC;
    }

    /* Newey-West suggestion */
    g = 4.0 * pow(pmod->nobs/100.0, 2.0/9.0);

    auxlist[0] = pmod->list[0] - 1;

    _init_model(&auxmod, pdinfo);

    /* loop across the indep vars in the original model */
    for (i=2; i<=pmod->list[0]; i++) {
	double vhat = 0;
	double sderr;
	int k, t;

	/* set the given indep var as the dependent */
	auxlist[1] = pmod->list[i];

	k = 2;
	for (j=2; j<=pmod->list[0]; j++) {
	    /* add other indep vars as regressors */
	    if (pmod->list[j] == auxlist[1]) continue;
	    auxlist[k++] = pmod->list[j];
	}

	auxmod = lsq(auxlist, pZ, pdinfo, OLS, 0, 0.0);

	if (auxmod.errcode) {
	    fprintf(stderr, "Error estimating auxiliary model, code=%d\n", 
		    auxmod.errcode);
	    pmod->sderr[i-2] = NADBL;
	} else {
	    /* compute robust standard error */
	    for (t=pmod->t1; t<=pmod->t2; t++) {
		ahat[t] = pmod->uhat[t] * auxmod.uhat[t];
	    }
	    vhat = get_vhat(ahat, g, pmod->t1, pmod->t2);
	    sderr = pmod->sderr[i-2] / pmod->sigma;
	    sderr = sderr * sderr;
	    sderr *= sqrt(vhat);
	    robust[i-2] = sderr;
	}

	clear_model(&auxmod, pdinfo);
    }

    /* save original model data */
    tmp = pmod->sderr;
    aux = pmod->aux;
    order = pmod->order;

    /* adjust data for SC-robust version */
    pmod->sderr = robust;
    pmod->aux = AUX_SCR;
    pmod->order = g;

    /* print original model, showing robust std errors */
    printmodel(pmod, pdinfo, prn);  

    /* reset the original model data */
    pmod->sderr = tmp;
    pmod->aux = aux;
    pmod->order = order;

    free(auxlist);
    free(ahat);
    free(robust);
	
    return 0;
}

/**
 * autocorr_test:
 * @pmod: pointer to model to be tested.
 * @order: lag order for test.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * @test: hypothesis test results struct.
 *
 * Tests the given model for autocorrelation of order equal to
 * the specified value, or equal to the frequency of the data if
 * the supplied @order is zero. Gives TR^2 and LMF test statistics.
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int autocorr_test (MODEL *pmod, int order, 
		   double ***pZ, DATAINFO *pdinfo, 
		   PRN *prn, GRETLTEST *test)
{
    int *newlist;
    MODEL aux;
    int i, k, t, n = pdinfo->n, v = pdinfo->v; 
    double trsq, LMF, lb, pval = 1.0;
    int err = 0;

    if (pmod->ci == NLS) return E_NOTIMP;

    if (dataset_is_panel(pdinfo)) {
	void *handle;
	int (*panel_autocorr_test)(MODEL *, int, 
				   double **, DATAINFO *, 
				   PRN *, GRETLTEST *);

	if (open_plugin("panel_data", &handle)) {
	    pputs(prn, _("Couldn't access panel plugin\n"));
	    return 1;
	}

	panel_autocorr_test = get_plugin_function("panel_autocorr_test", 
						  handle);
	if (panel_autocorr_test == NULL) {
	    pputs(prn, _("Couldn't load plugin function\n"));
	    close_plugin(handle);
	    return 1;
	}

	err = panel_autocorr_test(pmod, order, *pZ, pdinfo,
				  prn, NULL);
	close_plugin(handle);
	return err;
    }

    exchange_smpl(pmod, pdinfo);
    _init_model(&aux, pdinfo);

    if (order <= 0) order = pdinfo->pd;

    if (pmod->ncoeff + order >= pdinfo->t2 - pdinfo->t1)
	return E_DF;

    k = order + 1;
    newlist = malloc((pmod->list[0] + k) * sizeof *newlist);

    if (newlist == NULL) {
	err = E_ALLOC;
    } else {
	newlist[0] = pmod->list[0] + order;
	for (i=2; i<=pmod->list[0]; i++) newlist[i] = pmod->list[i];
	if (dataset_add_vars(1, pZ, pdinfo)) {
	    k = 0;
	    err = E_ALLOC;
	}
    }

    if (!err) {
	/* add uhat to data set */
	for (t=0; t<n; t++)
	    (*pZ)[v][t] = NADBL;
	for (t = pmod->t1; t<= pmod->t2; t++)
	    (*pZ)[v][t] = pmod->uhat[t];
	strcpy(pdinfo->varname[v], "uhat");
	strcpy(VARLABEL(pdinfo, v), _("residual"));
	/* then lags of same */
	for (i=1; i<=order; i++) {
	    if (_laggenr(v, i, 1, pZ, pdinfo)) {
		sprintf(gretl_errmsg, _("lagging uhat failed"));
		err = E_LAGS;
	    } else {
		newlist[pmod->list[0] + i] = v+i;
	    }
	}
    }

    if (!err) {
	newlist[1] = v;
	/*  printlist(newlist); */
	aux = lsq(newlist, pZ, pdinfo, OLS, 1, 0.0);
	err = aux.errcode;
	if (err) {
	    errmsg(aux.errcode, prn);
	}
    } 

    if (!err) {
	aux.aux = AUX_AR;
	aux.order = order;
	printmodel(&aux, pdinfo, prn);
	trsq = aux.rsq * aux.nobs;
	LMF = (aux.rsq/(1.0 - aux.rsq)) * 
	    (aux.nobs - pmod->ncoeff - order)/order; 

	pprintf(prn, "\n%s: LMF = %f,\n", _("Test statistic"), LMF);
	pval = fdist(LMF, order, aux.nobs - pmod->ncoeff - order);
	pprintf(prn, "%s = P(F(%d,%d) > %g) = %.3g\n", _("with p-value"), 
		order, aux.nobs - pmod->ncoeff - order, LMF, pval);

	pprintf(prn, "\n%s: TR^2 = %f,\n", 
		_("Alternative statistic"), trsq);
	pprintf(prn, "%s = P(%s(%d) > %g) = %.3g\n\n", 	_("with p-value"), 
		_("Chi-square"), order, trsq, chisq(trsq, order));

	/* add Ljung-Box Q' */
	if (ljung_box(v, order, *pZ, pdinfo, &lb) == 0) {
	    pprintf(prn, "Ljung-Box Q' = %g %s = P(%s(%d) > %g) = %.3g\n", 
		    lb, _("with p-value"), _("Chi-square"), order,
		    lb, chisq(lb, order));
	}

	if (test != NULL) {
	    gretl_test_init(test);
	    strcpy(test->type, N_("LM test for autocorrelation up to order %s"));
	    strcpy(test->h_0, N_("no autocorrelation"));
	    sprintf(test->param, "%d", order);
	    test->teststat = GRETL_TEST_LMF;
	    test->dfn = order;
	    test->dfd = aux.nobs - pmod->ncoeff - order;
	    test->value = LMF;
	    test->pvalue = fdist(LMF, test->dfn, test->dfd);
	}
    }

    free(newlist);
    dataset_drop_vars(k, pZ, pdinfo); 
    clear_model(&aux, pdinfo); 

    if (pval < 0.05) {
	autocorr_standard_errors(pmod, pZ, pdinfo, prn);
    }

    exchange_smpl(pmod, pdinfo);

    return err;
}

/**
 * chow_test:
 * @line: command line for parsing.
 * @pmod: pointer to model to be tested.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * @test: hypothesis test results struct.
 *
 * Tests the given model for structural stability (Chow test).
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int chow_test (const char *line, MODEL *pmod, double ***pZ,
	       DATAINFO *pdinfo, PRN *prn, GRETLTEST *test)
{
    int *chowlist = NULL;
    int newvars = pmod->list[0] - 1;
    int i, t, v = pdinfo->v, n = pdinfo->n;
    char chowdate[9], s[9];
    MODEL chow_mod;
    double F;
    int split = 0, err = 0;

    if (pmod->ci != OLS) return E_OLSONLY;

    /* temporarily impose the sample that was in force when the
       original model was estimated */
    exchange_smpl(pmod, pdinfo);

    _init_model(&chow_mod, pdinfo);

    if (sscanf(line, "%*s %8s", chowdate) != 1) 
	err = E_PARSE;
    else {
	split = dateton(chowdate, pdinfo) - 1;
	if (split <= 0 || split >= pdinfo->n) 
	    err = E_SPLIT;
    }

    if (!err) {
	/* take the original regression list, add a split dummy
	   and interaction terms. */
	if (pmod->ifc == 0) newvars++;

	if (dataset_add_vars(newvars, pZ, pdinfo)) {
	    newvars = 0;
	    err = E_ALLOC;
	} else {
	    chowlist = malloc((pmod->list[0] + newvars + 1) * sizeof *chowlist);
	    if (chowlist == NULL) 
		err = E_ALLOC;
	}
    }

    if (!err) {
	chowlist[0] = pmod->list[0] + newvars;
	for (i=1; i<=pmod->list[0]; i++) { 
	    chowlist[i] = pmod->list[i];
	}

	/* generate the split variable */
	for (t=0; t<n; t++) 
	    (*pZ)[v][t] = (double) (t > split); 
	strcpy(pdinfo->varname[v], "splitdum");
	strcpy(VARLABEL(pdinfo, v), _("dummy variable for Chow test"));
	chowlist[pmod->list[0] + 1] = v;

	/* and the interaction terms */
	for (i=1; i<newvars; i++) {
	    int orig = i + 1 + pmod->ifc;

	    for (t=0; t<n; t++) {
		(*pZ)[v+i][t] = (*pZ)[v][t] * 
		    (*pZ)[pmod->list[orig]][t];
	    }
	    strcpy(s, pdinfo->varname[pmod->list[orig]]); 
	    _esl_trunc(s, 5);
	    strcpy(pdinfo->varname[v+i], "sd_");
	    strcat(pdinfo->varname[v+i], s);
	    sprintf(VARLABEL(pdinfo, v+i), "splitdum * %s", 
		    pdinfo->varname[pmod->list[orig]]);
	    chowlist[pmod->list[0]+1+i] = v+i;
	}

	chow_mod = lsq(chowlist, pZ, pdinfo, OLS, 1, 0.0);
	if (chow_mod.errcode) {
	    err = chow_mod.errcode;
	    errmsg(err, prn);
	} else {
	    chow_mod.aux = AUX_CHOW;
	    printmodel(&chow_mod, pdinfo, prn);
	    F = (pmod->ess - chow_mod.ess) * chow_mod.dfd / 
		(chow_mod.ess * newvars);
	    pprintf(prn, _("\nChow test for structural break at observation %s:\n"
		    "  F(%d, %d) = %f with p-value %f\n\n"), chowdate,
		    newvars, chow_mod.dfd, F, 
		    fdist(F, newvars, chow_mod.dfd)); 

	    if (test != NULL) {
		gretl_test_init(test);
		strcpy(test->type, N_("Chow test for structural break at "
			"observation %s"));
		strcpy(test->param, chowdate);
		strcpy(test->h_0, N_("no structural break"));
		test->teststat = GRETL_TEST_F;
		test->dfn = newvars;
		test->dfd = chow_mod.dfd;
		test->value = F;
		test->pvalue = fdist(F, newvars, chow_mod.dfd);
	    }
	}
	clear_model(&chow_mod, pdinfo);
    }

    /* clean up extra variables */
    dataset_drop_vars(newvars, pZ, pdinfo);
    free(chowlist);

    exchange_smpl(pmod, pdinfo);    

    return err;
}

/* ........................................................... */

static double vprime_M_v (double *v, double *M, int n)
     /* compute v'Mv, for symmetric M */
{
    int i, j, jmin, jmax, k;
    double xx, val = 0.0;

    k = jmin = 0;
    for (i=0; i<n; i++) {
	xx = 0.0;
	for (j=jmin; j<n; j++) {
	    xx += v[j] * M[k++];
	}
	val += xx * v[i];
	jmin++;
    }

    jmax = 1;
    for (i=1; i<n; i++) {
	k = i;
	xx = 0.0;
	for (j=0; j<jmax; j++) {
	    xx += v[j] * M[k];
	    k += n - j - 1;
	}
	val += xx * v[i];
	jmax++;
    }

    return val;
}

/**
 * cusum_test:
 * @pmod: pointer to model to be tested.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * @ppaths: path information struct.
 * @test: hypothesis test results struct.
 *
 * Tests the given model for parameter stability (CUSUM test).
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int cusum_test (MODEL *pmod, double ***pZ, DATAINFO *pdinfo, PRN *prn, 
		PATHS *ppaths, GRETLTEST *test)
{
    int n_est, i, j, t;
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    int xno, yno = pmod->list[1];
    const int T = pmod->t2 - pmod->t1 + 1;
    const int K = pmod->ncoeff;
    MODEL cum_mod;
    char cumdate[9];
    double xx, yy, sigma, hct, wbar = 0.0;
    double *cresid = NULL, *W = NULL, *xvec = NULL;
    FILE *fq = NULL;
    int err = 0;

    if (pmod->ci != OLS) return E_OLSONLY;

    n_est = T - K;
    /* set sample based on model to be tested */
    pdinfo->t1 = pmod->t1;
    pdinfo->t2 = pmod->t1 + K - 1;    

    cresid = malloc(n_est * sizeof *cresid);
    W = malloc(n_est * sizeof *W);
    xvec = malloc(K * sizeof *xvec);
    if (cresid == NULL || W == NULL || xvec == NULL) 
	err = E_ALLOC;

    if (!err) {
	_init_model(&cum_mod, pdinfo);
	for (j=0; j<n_est; j++) {
	    cum_mod = lsq(pmod->list, pZ, pdinfo, OLS, 1, 0.0);
	    err = cum_mod.errcode;
	    if (err) {
		errmsg(err, prn);
		clear_model(&cum_mod, pdinfo);
		break;
	    } else {
		t = pdinfo->t2 + 1;
		yy = 0.0;
		for (i=0; i<K; i++) {
		    xno = cum_mod.list[i+2];
		    xvec[i] = (*pZ)[xno][t];
		    yy += cum_mod.coeff[i] * (*pZ)[xno][t];
		}
		cresid[j] = (*pZ)[yno][t] - yy;
		cum_mod.ci = CUSUM;
		makevcv(&cum_mod);
		xx = vprime_M_v(xvec, cum_mod.vcv, K);
		cresid[j] /= sqrt(1.0 + xx);
		/*  printf("w[%d] = %g\n", t, cresid[j]); */
		wbar += cresid[j];
		clear_model(&cum_mod, pdinfo);
		pdinfo->t2 += 1;
	    }
	}
    }

    if (!err) {
	wbar /= T - K;
	pprintf(prn, "\n%s\n\n",
		_("CUSUM test for stability of parameters"));
	pprintf(prn, _("mean of scaled residuals = %g\n"), wbar);
	sigma = 0;
	for (j=0; j<n_est; j++) {
	    xx = (cresid[j] - wbar);
	    sigma += xx * xx;
	}
	sigma /= T - K - 1;
	sigma = sqrt(sigma);
	pprintf(prn, _("sigmahat                 = %g\n\n"), sigma);

	xx = 0.948*sqrt((double) (T-K));
	yy = 2.0*xx/(T-K);

	pputs(prn, _("Cumulated sum of scaled residuals\n"
		"('*' indicates a value outside of 95%% confidence band):\n\n"));
    
	for (j=0; j<n_est; j++) {
	    W[j] = 0.0;
	    for (i=0; i<=j; i++) W[j] += cresid[i];
	    W[j] /= sigma;
	    t = pmod->t1 + K + j;
	    ntodate(cumdate, t, pdinfo);
	    /* FIXME printing of number below? */
	    pprintf(prn, " %s %9.3f %s\n", cumdate, W[j],
		    (fabs(W[j]) > xx + (j+1)*yy)? "*" : "");
	}
	hct = (sqrt((double) (T-K)) * wbar) / sigma;
	pprintf(prn, _("\nHarvey-Collier t(%d) = %g with p-value %.4g\n\n"), 
		T-K-1, hct, tprob(hct, T-K-1));

	if (test != NULL) {
	    gretl_test_init(test);
	    strcpy(test->type, N_("CUSUM test for parameter stability"));
	    strcpy(test->h_0, N_("no change in parameters"));
	    test->teststat = GRETL_TEST_HARVEY_COLLIER;
	    test->dfn = T-K-1;
	    test->value = hct;
	    test->pvalue = tprob(hct, T-K-1);
	}

#ifdef ENABLE_NLS
        setlocale(LC_NUMERIC, "C");
#endif
	/* plot with 95% confidence bands, if not batch mode */
	if (prn->fp == NULL && gnuplot_init(ppaths, &fq) == 0) {
	    fputs("# CUSUM test\n", fq);
	    fprintf(fq, "set xlabel \"%s\"\n", I_("Observation"));
	    fputs("set xzeroaxis\n", fq);
	    fprintf(fq, "set title \"%s\"\n",
		    /* xgettext:no-c-format */
		    I_("CUSUM plot with 95% confidence band"));
	    fputs("set nokey\n", fq);
	    fprintf(fq, "plot %f+%f*x w l 1, \\\n", xx - K*yy, yy);
	    fprintf(fq, "%f-%f*x w l 1, \\\n", -xx + K*yy, yy);
	    fputs("'-' using 1:2 w lp\n", fq);
	    for (j=0; j<n_est; j++) { 
		t = pmod->t1 + K + j;
		fprintf(fq, "%d %f\n", t, W[j]);
	    }
	    fputs("e\n", fq);

#if defined(OS_WIN32) && !defined(GNUPLOT_PNG)
	    fputs("pause -1\n", fq);
#endif
	    fclose(fq);
	    err = gnuplot_display(ppaths);
	}
#ifdef ENABLE_NLS
        setlocale(LC_NUMERIC, "");
#endif
    }

    /* restore sample */
    pdinfo->t1 = t1;
    pdinfo->t2 = t2;
    
    free(cresid);
    free(W);
    free(xvec);

    return err;
}

/**
 * hausman_test:
 * @pmod: pointer to model to be tested.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 *
 * Tests the given pooled model for fixed and random effects.
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int hausman_test (MODEL *pmod, double ***pZ, DATAINFO *pdinfo, 
		  PRN *prn) 
{
    if (pmod->ci != POOLED) {
	pputs(prn, _("This test is only relevant for pooled models\n"));
	return 1;
    }

    if (pmod->ifc == 0) {
	pputs(prn, _("This test requires that the model contains a constant\n"));
	return 1;
    }

    if (!balanced_panel(pdinfo)) {
	pputs(prn, _("Sorry, can't do this test on an unbalanced panel.\n"
		"You need to have the same number of observations\n"
		"for each cross-sectional unit"));
	return 1;
    } else {
	void *handle;
	void (*panel_diagnostics)(MODEL *, double ***, DATAINFO *, PRN *);

	if (open_plugin("panel_data", &handle)) {
	    pputs(prn, _("Couldn't access panel plugin\n"));
	    return 1;
	}
	panel_diagnostics = get_plugin_function("panel_diagnostics", handle);
	if (panel_diagnostics == NULL) {
	    pputs(prn, _("Couldn't load plugin function\n"));
	    close_plugin(handle);
	    return 1;
	}
	(*panel_diagnostics) (pmod, pZ, pdinfo, prn);
	close_plugin(handle);
    }
    return 0;
}

/**
 * leverage_test:
 * @pmod: pointer to model to be tested.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * @ppaths: path information struct (should be NULL if a graph
 * is not wanted).
 *
 * Tests the data used in the given model for points with
 * high leverage and influence on the estimates
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int leverage_test (MODEL *pmod, double ***pZ, DATAINFO *pdinfo, 
		   PRN *prn, PATHS *ppaths)
{
    void *handle;
    int (*model_leverage) (const MODEL *, double ***, 
			   const DATAINFO *, PRN *, PATHS *);
    int err;

    if (pmod->ci != OLS) return E_OLSONLY;

    if (open_plugin("leverage", &handle)) return 1;

    model_leverage = get_plugin_function("model_leverage", handle);
    if (model_leverage == NULL) {
	close_plugin(handle);
	return 1;
    }

    err = (*model_leverage)(pmod, pZ, pdinfo, prn, ppaths);
    close_plugin(handle);

    return err;
}

int make_mp_lists (const LIST list, const char *str,
		   int **reglist, int **polylist)
{
    int i, pos;

    pos = atoi(str);

    *reglist = malloc(pos * sizeof **polylist);
    *polylist = malloc((list[0] - pos + 2) * sizeof **reglist);

    if (*reglist == NULL || *polylist == NULL) {
	free(*reglist);
	free(*polylist);
	return 1;
    }
    
    (*reglist)[0] = pos - 1;
    for (i=1; i<pos; i++) (*reglist)[i] = list[i];

    (*polylist)[0] = list[0] - pos;
    for (i=1; i<=(*polylist)[0]; i++) (*polylist)[i] = list[i+pos];

    return 0;
}

/**
 * mp_ols:
 * @list: specification of variables to use
 * @pos: string rep. of integer position in list at which
 * the regular list of variables ends and a list of polynomial
 * terms begins (or empty string in case of no polynomial terms)
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * 
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int mp_ols (const LIST list, const char *pos,
	    double ***pZ, DATAINFO *pdinfo, 
	    PRN *prn) 
{
    void *handle;
    int (*mplsq)(const int *, const int *, double ***, 
		 DATAINFO *, PRN *, char *, mp_results *);
    const int *reglist = NULL;
    int *polylist = NULL, *tmplist = NULL;
    mp_results *mpvals = NULL;
    int nc, err = 0;

    if (open_plugin("mp_ols", &handle)) {
	pputs(prn, _("Couldn't access GMP plugin\n"));
	return 1;
    }

    mplsq = get_plugin_function("mplsq", handle);
    if (mplsq == NULL) {
	pputs(prn, _("Couldn't load plugin function\n"));
	err = 1;
    }

    if (!err && *pos) { /* got a list of polynomial terms? */
	err = make_mp_lists(list, pos, &tmplist, &polylist);
	if (err) {
	    pputs(prn, _("Failed to parse mp_ols command\n"));
	}
	reglist = tmplist;
    } 

    if (!err && !*pos) {
	reglist = list;
    }

    nc = list[0] - 1;
    if (polylist != NULL) nc--;

    mpvals = gretl_mp_results_new(nc);
    if (mpvals == NULL || allocate_mp_varnames(mpvals)) {
	pprintf(prn, "%s\n", _("Out of memory!"));
	err = 1;
    }

    if (!err) {
	err = (*mplsq)(reglist, polylist, pZ, pdinfo, prn, 
		       gretl_errmsg, mpvals); 
    }

    if (!err) {
	print_mpols_results(mpvals, pdinfo, prn);
    }

    close_plugin(handle);

    free(polylist);
    free(tmplist);
    free_gretl_mp_results(mpvals);

    return err;
}

static int varmatch (int *sumvars, int test)
{
    int j;

    for (j=1; j<=sumvars[0]; j++) {
	if (sumvars[j] == test) return 1;
    }

    return 0;
}

static 
void fill_sum_var (double **Z, int n, int v, int vrepl, int vfirst)
{
    int t;

    for (t=0; t<n; t++) {
	Z[v][t] = Z[vrepl][t] - Z[vfirst][t];
    }
}

static 
int make_sum_test_list (MODEL *pmod, double **Z, DATAINFO *pdinfo,
			int *tmplist, int *sumvars, int vstart)
{
    int repl = 0;
    int newv = vstart;
    int testcoeff = 0;
    int nnew = sumvars[0] - 1;
    int i;

    tmplist[0] = pmod->list[0];
    tmplist[1] = pmod->list[1];

    for (i=2; i<=pmod->list[0]; i++) {
	if (nnew > 0 && varmatch(sumvars, pmod->list[i])) {
	    if (repl) {
		fill_sum_var(Z, pdinfo->n, newv, pmod->list[i], sumvars[1]);
		tmplist[i] = newv++;
		nnew--;
	    } else {
		tmplist[i] = pmod->list[i];
		testcoeff = i;
		repl = 1;
	    }
	} else {
	    tmplist[i] = pmod->list[i];
	}
    }

    if (nnew == 0) return testcoeff;
    else return -1;
}

/**
 * sum_test:
 * @sumvars: specification of variables to use.
 * @pmod: pointer to model.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @prn: gretl printing struct.
 * 
 * Calculates the sum of the coefficients, relative to the given model, 
 * for the variables given in @sumvars.  Prints this estimate along 
 * with its standard error.
 * 
 * Returns: 0 on successful completion, error code on error.
 */

int sum_test (LIST sumvars, MODEL *pmod, 
	      double ***pZ, DATAINFO *pdinfo, 
	      PRN *prn)
{
    int *tmplist;
    int add = sumvars[0] - 1;
    int v = pdinfo->v;
    double rho = 0.0;
    int pos = 0, err = 0;
    int testcoeff;
    MODEL summod;
    PRN *nullprn;

    if (sumvars[0] < 2) {
	pprintf(prn, _("Invalid input\n"));
	return E_DATA;
    }

    if (pmod->ci == TSLS) return E_NOTIMP;

    /* try the necessary allocation first */
    tmplist = malloc((pmod->list[0] + 1) * sizeof *tmplist);
    if (tmplist == NULL) return E_ALLOC;
    if (dataset_add_vars(add, pZ, pdinfo)) return E_ALLOC;

    nullprn = gretl_print_new(GRETL_PRINT_NULL, NULL);

    testcoeff = make_sum_test_list(pmod, *pZ, pdinfo, tmplist, sumvars, v);

    if (testcoeff < 0) {
	pprintf(prn, _("Invalid input\n"));
	free(tmplist);
	dataset_drop_vars(add, pZ, pdinfo);
	return E_DATA;
    }

    /* temporarily impose the sample that was in force when the
       original model was estimated */
    exchange_smpl(pmod, pdinfo);

    _init_model(&summod, pdinfo);

    if (pmod->ci == CORC || pmod->ci == HILU) {
	err = hilu_corc(&rho, tmplist, pZ, pdinfo, 
			NULL, 1, pmod->ci, prn);
    } else if (pmod->ci == WLS || pmod->ci == AR) {
	pos = _full_model_list(pmod, &tmplist);
	if (pos < 0) err = E_ALLOC;
    }

    if (!err) {
	if (pmod->ci == AR) {
	    summod = ar_func(tmplist, pos, pZ, pdinfo, NULL, nullprn);
	}
	else if (pmod->ci == ARCH) {
	    summod = arch(pmod->order, tmplist, pZ, pdinfo, NULL, 
			  nullprn, NULL);
	} 
	else if (pmod->ci == LOGIT || pmod->ci == PROBIT) {
	    summod = logit_probit(tmplist, pZ, pdinfo, pmod->ci);
	}
	else {
	    summod = lsq(tmplist, pZ, pdinfo, pmod->ci, 1, rho);
	}

	if (summod.errcode) {
	    pprintf(prn, "%s\n", gretl_errmsg);
	    err = summod.errcode; 
	} else {
	    int i;

#if 0
	    pprintf(prn, "testcoeff = %d\n", testcoeff);
#endif
	    pprintf(prn, "\n%s: ", _("Variables"));
	    for (i=1; i<=sumvars[0]; i++) {
		pprintf(prn, "%s ", pdinfo->varname[sumvars[i]]);
	    }
	    /* FIXME: check indexing of summod.coeff[] below */
	    pprintf(prn, "\n   %s = %g\n", _("Sum of coefficients"), 
		    summod.coeff[testcoeff - 2]);
	    if (!na(summod.sderr[testcoeff - 2])) {
		double tval;

		pprintf(prn, "   %s = %g\n", _("Standard error"),
			summod.sderr[testcoeff - 2]);
		tval = summod.coeff[testcoeff - 2] / 
		    summod.sderr[testcoeff - 2];
		pprintf(prn, "   t(%d) = %g ", summod.dfd, tval);
		pprintf(prn, _("with p-value = %f\n"), 
			tprob(tval, summod.dfd));
	    }
	}
    }

    free(tmplist);
    clear_model(&summod, pdinfo);
    dataset_drop_vars(add, pZ, pdinfo);
    gretl_print_destroy(nullprn);

    /* put back into pdinfo what was there on input */
    exchange_smpl(pmod, pdinfo);

    return err;
}
