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

#include "libgretl.h"
#include "gretl_func.h"
#include "uservar.h"
#include "gretl_string_table.h"
#include "libset.h"
#include "dbread.h"

#define DDEBUG 0
#define FULLDEBUG 0

#define Z_COLS_BORROWED 2

#define dset_zcols_borrowed(d) (d->auxiliary == Z_COLS_BORROWED)

struct VARINFO_ {
    char *label;
    char display_name[MAXDISP];
    char parent[VNAMELEN];
    VarFlags flags;
    char compact_method;
    gint64 mtime;
    short transform;    /* note: command index of transform */
    short lag;
    short stack_level;
    short midas_period;
    char midas_freq;
    short orig_pd;
    series_table *st;
};

static int pad_daily_data (DATASET *dset, int pd, PRN *prn);

/**
 * check_dataset_is_changed:
 * @dset: dataset to check.
 *
 * Returns: 1 if @dset has been modified since
 * the last call to this function.
 */

int check_dataset_is_changed (DATASET *dset)
{
    int ret = dset->modflag;

    dset->modflag = 0;
    return ret;
}

/**
 * set_dataset_is_changed:
 * @dset: dataset.
 * @s: 1 or 0.
 *
 * Sets the internal boolean "changed" flag to @s.
 */

void set_dataset_is_changed (DATASET *dset, int s)
{
    if (dset != NULL && gretl_function_depth() == 0) {
	dset->modflag = s;
    }
}

static void dataset_set_nobs (DATASET *dset, int n)
{
    if (n != dset->n) {
	/* if the total number of observations in the dataset
	   has changed, the current "matrix_mask", if present
	   (see libset.c), will now be invalid
	*/
	destroy_matrix_mask();
	dset->n = n;
    }
}

/**
 * free_Z:
 * @dset: dataset information.
 *
 * Does a deep free on the data matrix.
 */

void free_Z (DATASET *dset)
{
    if (dset != NULL && dset->Z != NULL) {
	int i, v = dset_zcols_borrowed(dset) ? 1 : dset->v;

#if DDEBUG
	fprintf(stderr, "Freeing Z (%p): %d vars\n", (void *) dset->Z, v);
#endif
	for (i=0; i<v; i++) {
	    free(dset->Z[i]);
	}
	free(dset->Z);
	dset->Z = NULL;
    }
}

/**
 * dataset_destroy_obs_markers:
 * @dset: data information struct.
 *
 * Frees any allocated observation markers for @dset.
 */

void dataset_destroy_obs_markers (DATASET *dset)
{
    int i;

    if (dset->S != NULL) {
	for (i=0; i<dset->n; i++) {
	   free(dset->S[i]);
	}
	free(dset->S);
	dset->S = NULL;
	dset->markers = NO_MARKERS;
    }
}

static void free_varinfo (DATASET *dset, int v)
{
    if (dset->varinfo[v]->st != NULL) {
	series_table_destroy(dset->varinfo[v]->st);
    }
    if (dset->varinfo[v]->label != NULL) {
	free(dset->varinfo[v]->label);
    }
    free(dset->varinfo[v]);
}

/**
 * clear_datainfo:
 * @dset: data information struct.
 * @code: either %CLEAR_FULL or %CLEAR_SUBSAMPLE.
 *
 * Frees the allocated content of a data information struct;
 * note that @dset itself is not freed.
 */

void clear_datainfo (DATASET *dset, int code)
{
    int i;

    if (dset == NULL) return;

    if (dset->S != NULL) {
	dataset_destroy_obs_markers(dset);
    }
    if (dset->submask != NULL) {
	free_subsample_mask(dset->submask);
	dset->submask = NULL;
    }
    if (dset->restriction != NULL) {
	free(dset->restriction);
	dset->restriction = NULL;
    }
    if (dset->padmask != NULL) {
	free(dset->padmask);
	dset->padmask = NULL;
    }
    if (dset->pangrps != NULL) {
	free(dset->pangrps);
	dset->pangrps = NULL;
    }

    /* if this is not a sub-sample datainfo, free varnames, labels, etc. */

    if (code == CLEAR_FULL) {
	if (dset->varname != NULL) {
	    for (i=0; i<dset->v; i++) {
		free(dset->varname[i]);
	    }
	    free(dset->varname);
	    dset->varname = NULL;
	}
	if (dset->varinfo != NULL) {
	    for (i=0; i<dset->v; i++) {
		free_varinfo(dset, i);
	    }
	    free(dset->varinfo);
	    dset->varinfo = NULL;
	}
	if (dset->descrip != NULL) {
	    free(dset->descrip);
	    dset->descrip = NULL;
	}
	if (dset->mapfile != NULL) {
	    free(dset->mapfile);
	    dset->mapfile = NULL;
	}

	maybe_free_full_dataset(dset);

	dset->v = dset->n = 0;
    }
}

/**
 * destroy_dataset:
 * @dset: pointer to dataset.
 *
 * Frees all resources associated with @dset.
 */

void destroy_dataset (DATASET *dset)
{
    if (dset != NULL) {
	free_Z(dset);
	clear_datainfo(dset, CLEAR_FULL);
	free(dset);
    }
}

/**
 * copy_dataset_obs_info:
 * @targ: pointer to target dataset.
 * @src: pointer to source dataset.
 *
 * Sets the "date" or observations information in @targ to that
 * found in @src.
 */

void copy_dataset_obs_info (DATASET *targ, const DATASET *src)
{
    strcpy(targ->stobs, src->stobs);
    strcpy(targ->endobs, src->endobs);
    targ->sd0 = src->sd0;
    targ->pd = src->pd;
    targ->structure = src->structure;
}

/**
 * dataset_obs_info_default:
 * @dset: pointer to dataset.
 *
 * Sets the "date" or observations information in @dset to a
 * simple default of cross-sectional data, observations 1 to n,
 * where n is the %n element (number of observations) in @dset.
 */

void dataset_obs_info_default (DATASET *dset)
{
    strcpy(dset->stobs, "1");
    sprintf(dset->endobs, "%d", dset->n);
    dset->sd0 = 1.0;
    dset->pd = 1;
    dset->structure = CROSS_SECTION;
}

/**
 * dataset_allocate_obs_markers:
 * @dset: pointer to dataset
 *
 * Allocates space in @dset for strings indentifying the
 * observations and initializes all of the markers to empty
 * strings.  Note that These strings have a fixed maximum
 * length of #OBSLEN - 1.
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_allocate_obs_markers (DATASET *dset)
{
    char **S = NULL;
    int err = 0;

    if (dset->S == NULL) {
	/* not already allocated */
	S = strings_array_new_with_length(dset->n, OBSLEN);
	if (S == NULL) {
	    err = E_ALLOC;
	} else {
	    dset->S = S;
	}
    }

    if (dset->S != NULL) {
	dset->markers = REGULAR_MARKERS;
    }

    return err;
}

static void gretl_varinfo_init (VARINFO *vinfo)
{
    vinfo->label = NULL;
    vinfo->display_name[0] = '\0';
    vinfo->parent[0] = '\0';
    vinfo->flags = 0;
    vinfo->transform = 0;
    vinfo->lag = 0;
    vinfo->midas_period = 0;
    vinfo->midas_freq = 0;
    vinfo->orig_pd = 0;
    vinfo->compact_method = COMPACT_NONE;
    vinfo->mtime = 0;
    vinfo->stack_level = gretl_function_depth();
    vinfo->st = NULL;
}

static void copy_label (char **targ, const char *src)
{
    free(*targ);
    if (src == NULL) {
	*targ = NULL;
    } else {
	*targ = gretl_strdup(src);
    }
}

static int labels_differ (const char *s1, const char *s2)
{
    if ((s1 == NULL && s2 != NULL) || (s1 != NULL && s2 == NULL)) {
	return 1;
    } else if (s1 != NULL && s2 != NULL) {
	return strcmp(s1, s2) != 0;
    } else {
	return 0;
    }
}

/**
 * copy_varinfo:
 * @targ: target to which to copy.
 * @src: source to copy from.
 *
 * Copies all relevant information from @src to @targ.
 */

void copy_varinfo (VARINFO *targ, const VARINFO *src)
{
    if (src == NULL || targ == NULL) {
	return;
    }
    copy_label(&targ->label, src->label);
    strcpy(targ->display_name, src->display_name);
    strcpy(targ->parent, src->parent);
    targ->flags = src->flags;
    targ->transform = src->transform;
    targ->lag = src->lag;
    targ->midas_period = src->midas_period;
    targ->midas_freq = src->midas_freq;
    targ->orig_pd = src->orig_pd;
    targ->compact_method = src->compact_method;
    targ->stack_level = src->stack_level;
    if (src->st != NULL) {
	targ->st = series_table_copy(src->st);
    }
}

/* For use in the context of returning from a sub-sampled
   dataset to the full one: trim off series names and
   "varinfo" beyond the index @nv, which gives the number
   of series in the full dataset.
*/

int shrink_varinfo (DATASET *dset, int nv)
{
    char **vnames;
    VARINFO **vi;
    int i, err = 0;

    if (nv > dset->v) {
	return E_DATA;
    } else if (nv == dset->v) {
	return 0;
    }

    for (i=nv; i<dset->v; i++) {
	free(dset->varname[i]);
	free(dset->varinfo[i]);
    }

    vnames = realloc(dset->varname, nv * sizeof *vnames);
    if (vnames == NULL) {
	err = E_ALLOC;
    } else {
	dset->varname = vnames;
    }

    vi = realloc(dset->varinfo, nv * sizeof *vi);
    if (vi == NULL) {
	err = E_ALLOC;
    } else {
	dset->varinfo = vi;
    }

    return err;
}

/**
 * dataset_allocate_varnames:
 * @dset: pointer to dataset.
 *
 * Given a blank @dset, which should have been obtained using
 * datainfo_new(), allocate space for the names of variables.
 * The @v member of @dset (representing the number of variables,
 * including the automatically added constant at position 0) must be
 * set before calling this function.
 *
 * Returns: 0 on sucess, E_ALLOC on failure.
 */

int dataset_allocate_varnames (DATASET *dset)
{
    int i, j, v = dset->v;
    int err = 0;

    dset->varname = strings_array_new_with_length(v, VNAMELEN);
    if (dset->varname == NULL) {
	return E_ALLOC;
    }

    dset->varinfo = malloc(v * sizeof *dset->varinfo);
    if (dset->varinfo == NULL) {
	free(dset->varname);
	return E_ALLOC;
    }

    for (i=0; i<v; i++) {
	dset->varinfo[i] = malloc(sizeof **dset->varinfo);
	if (dset->varinfo[i] == NULL) {
	    for (j=0; j<i; j++) {
		free(dset->varinfo[j]);
	    }
	    free(dset->varinfo);
	    dset->varinfo = NULL;
	    err = E_ALLOC;
	    break;
	} else {
	    gretl_varinfo_init(dset->varinfo[i]);
	}
    }

    if (!err) {
	strcpy(dset->varname[0], "const");
	series_set_label(dset, 0, _("auto-generated constant"));
    }

    return err;
}

/**
 * datainfo_init:
 * @dset: pointer to DATASET struct.
 *
 * Zeros all members of @dset and sets it as a plain cross-section.
 * Designed for use with a DATASET structure that has not been
 * obtained via datainfo_new().
 */

void datainfo_init (DATASET *dset)
{
    dset->v = 0;
    dset->n = 0;
    dset->pd = 1;
    dset->structure = CROSS_SECTION;
    dset->sd0 = 1.0;
    dset->t1 = 0;
    dset->t2 = 0;
    dset->stobs[0] = '\0';
    dset->endobs[0] = '\0';

    dset->Z = NULL;
    dset->varname = NULL;
    dset->varinfo = NULL;

    dset->markers = NO_MARKERS;
    dset->modflag = 0;

    dset->S = NULL;
    dset->descrip = NULL;
    dset->submask = NULL;
    dset->restriction = NULL;
    dset->padmask = NULL;
    dset->mapfile = NULL;
    dset->pangrps = NULL;
    dset->panel_pd = 0;
    dset->panel_sd0 = 0;

    dset->auxiliary = 0;
    dset->rseed = 0;
}

/**
 * datainfo_new:
 *
 * Creates a new data information struct pointer from scratch,
 * properly initialized as empty (no variables, no observations).
 *
 * Returns: pointer to data information struct, or NULL on error.
 */

DATASET *datainfo_new (void)
{
    DATASET *dset = malloc(sizeof *dset);

    if (dset != NULL) {
	datainfo_init(dset);
    }

    return dset;
}

static DATASET *real_create_new_dataset (int nvar, int nobs,
					 gretlopt opt)
{
    DATASET *dset = datainfo_new();

    if (dset == NULL) return NULL;

    dset->v = nvar;
    dset->n = nobs;
    dset->Z = NULL;

    if (start_new_Z(dset, opt)) {
	free(dset);
	return NULL;
    }

    if (opt & OPT_M) {
	if (dataset_allocate_obs_markers(dset)) {
	    free_datainfo(dset);
	    return NULL;
	}
    }

    dataset_obs_info_default(dset);

    return dset;
}

/**
 * create_new_dataset:
 * @nvar: number of variables.
 * @nobs: number of observations per variable.
 * @markers: 1 if space should be allocated for "case markers" for
 * the observations, 0 otherwise.
 *
 * Allocates space in the dataset to hold the specified number
 * of variables and observations.
 *
 * Returns: pointer to dataset struct, or NULL on error.
 */

DATASET *create_new_dataset (int nvar, int nobs, int markers)
{
    gretlopt opt = markers ? OPT_M : OPT_NONE;

    return real_create_new_dataset(nvar, nobs, opt);
}

DATASET *create_auxiliary_dataset (int nvar, int nobs, gretlopt opt)
{
    DATASET *dset = real_create_new_dataset(nvar, nobs, opt);

    if (dset != NULL) {
	if (opt & OPT_B) {
	    dset->auxiliary = Z_COLS_BORROWED;
	} else {
	    dset->auxiliary = 1;
	}
    }

    return dset;
}

static double **make_borrowed_Z (int v, int n)
{
    double **Z = malloc(v * sizeof *Z);

    if (Z != NULL) {
	int i;

	for (i=0; i<v; i++) {
	    Z[i] = NULL;
	}

	Z[0] = malloc(n * sizeof **Z);

	if (Z[0] == NULL) {
	    free(Z);
	    Z = NULL;
	} else {
	    for (i=0; i<n; i++) {
		Z[0][i] = 1.0;
	    }
	}
    }

    return Z;
}

/**
 * allocate_Z:
 * @dset: pointer to dataset.
 * @opt: may include OPT_B to indicate that the data columns
 * will be "borrowed".
 *
 * Allocates the two-dimensional data array Z,
 * based on the v (number of variables) and n (number of
 * observations) members of @dset.  The variable at
 * position 0 is initialized to all 1s; other variables
 * are initialized to #NADBL (unless OPT_B is given).
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int allocate_Z (DATASET *dset, gretlopt opt)
{
    int i, t;
    int err = 0;

    if (dset->Z != NULL) {
	fprintf(stderr, "*** error: allocate_Z called with non-NULL Z\n");
    }

    if (opt & OPT_B) {
	dset->Z = make_borrowed_Z(dset->v, dset->n);
    } else {
	dset->Z = doubles_array_new(dset->v, dset->n);
    }

    if (dset->Z == NULL) {
	err = E_ALLOC;
    } else if (!(opt & OPT_B)) {
	for (i=0; i<dset->v; i++) {
	    for (t=0; t<dset->n; t++) {
		dset->Z[i][t] = (i == 0)? 1.0 : NADBL;
	    }
	}
    }

    return err;
}

/**
 * start_new_Z:
 * @dset: pointer to dataset.
 * @opt: if includes OPT_R we're sub-sampling from a full data set;
 * if includes OPT_P, do not null out dset->S and markers.
 *
 * Initializes the data array within @dset (adding the constant in
 * position 0).
 *
 * Returns: 0 on successful completion, non-zero on error.
 */

int start_new_Z (DATASET *dset, gretlopt opt)
{
    if (allocate_Z(dset, opt)) {
	return E_ALLOC;
    }

    dset->t1 = 0;
    dset->t2 = dset->n - 1;

    if (opt & OPT_R) {
	/* sub-sampling */
	dset->varname = NULL;
	dset->varinfo = NULL;
    } else if (dataset_allocate_varnames(dset)) {
	free_Z(dset);
	dset->Z = NULL;
	return E_ALLOC;
    }

    if (!(opt & OPT_P)) {
	dset->S = NULL;
	dset->markers = NO_MARKERS;
    }

    dset->descrip = NULL;
    dset->submask = NULL;
    dset->restriction = NULL;
    dset->padmask = NULL;
    dset->mapfile = NULL;

    if (!(opt & OPT_R)) {
	dset->pangrps = NULL;
    }

    return 0;
}

static int reallocate_markers (DATASET *dset, int n)
{
    char **S;
    int t;

    S = realloc(dset->S, n * sizeof *S);
    if (S == NULL) {
	return 1;
    }

    for (t=dset->n; t<n; t++) {
	S[t] = malloc(OBSLEN);
	if (S[t] == NULL) {
	    int j;

	    for (j=dset->n; j<t; j++) {
		free(S[j]);
	    }
	    free(S);
	    return 1;
	}
	S[t][0] = '\0';
    }

    dset->S = S;

    return 0;
}

/* Allow for the possibility of centered seasonal dummies: usually
   xon = 1 and xoff = 0, but in the centered case xon = 1 - 1/pd
   and xoff = -1/pd.
*/

static int get_xon_xoff (const double *x, int n, int pd, double *xon, double *xoff)
{
    double cfac = 1.0 / pd;
    double xc = 1.0 - cfac, yc = -cfac;
    double x0 = 999, y0 = 999;
    int t, ret = 1;

    for (t=0; t<n && ret; t++) {
	if (x[t] == 1.0) {
	    if (x0 == 999) x0 = 1.0;
	    else if (x[t] != x0) ret = 0;
	} else if (x[t] == 0.0) {
	    if (y0 == 999) y0 = 0.0;
	    else if (x[t] != y0) ret = 0;
	} else if (x[t] == xc) {
	    if (x0 == 999) x0 = xc;
	    else if (x[t] != x0) ret = 0;
	} else if (x[t] == yc) {
	    if (y0 == 999) y0 = yc;
	    else if (x[t] != y0) ret = 0;
	} else {
	    ret = 0;
	}
    }

    if (ret) {
	*xon = x0;
	*xoff = y0;
    }

    return ret;
}

static int real_periodic_dummy (const double *x, int n,
				int *pd, int *offset,
				double *pxon, double *pxoff)
{
    double xon = 1.0, xoff = 0.0;
    int onbak = 0;
    int gap = 0;
    int trail = 0;
    int t, m = n - 1, ret = 1;

    if (!get_xon_xoff(x, n, *pd, &xon, &xoff)) {
	return 0;
    }

    *pd = -1;
    *offset = -1;
    trail = 0;

    /* find number of trailing "off" values */
    for (t=n-1; t>0; t--) {
	if (x[t] == xoff) {
	    trail++;
	} else {
	    if (x[t] == xon) {
		m = t;
	    } else {
		ret = 0;
	    }
	    break;
	}
    }

    /* check for dummyhood and periodicity */
    for (t=0; t<=m && ret; t++) {
	if (x[t] == xoff) {
	    onbak = 0;
	    gap++;
	} else if (x[t] == xon) {
	    if (onbak) {
		ret = 0;
	    } else if (*offset < 0) {
		*offset = gap;
	    } else if (*pd < 0) {
		*pd = gap + 1;
		if (*pd < *offset + 1) {
		    ret = 0;
		}
	    } else if (gap != *pd - 1) {
		ret = 0;
	    } else if (gap < trail) {
		ret = 0;
	    }
	    gap = 0;
	    onbak = 1;
	} else {
	    ret = 0;
	    break;
	}
    }

    if (ret && pxon != NULL && pxoff != NULL) {
	*pxon = xon;
	*pxoff = xoff;
    }

    return ret;
}

/**
 * is_periodic_dummy:
 * @x: array to examine.
 * @dset: pointer to dataset.
 *
 * Returns: 1 if @x is a periodic dummy variable,
 * 0 otherwise.
 */

int is_periodic_dummy (const double *x, const DATASET *dset)
{
    int offset, pd = dset->pd;

    return real_periodic_dummy(x, dset->n, &pd, &offset, NULL, NULL);
}

static int is_linear_trend (const double *x, int n)
{
    int t, ret = 1;

    for (t=1; t<n; t++) {
	if (x[t] != x[t-1] + 1.0) {
	    ret = 0;
	    break;
	}
    }

    return ret;
}

static int is_quadratic_trend (const double *x, int n)
{
    double t2;
    int t, ret = 1;

    for (t=0; t<n; t++) {
	t2 = (t + 1) * (t + 1);
	if (x[t] != t2) {
	    ret = 0;
	    break;
	}
    }

    return ret;
}

/**
 * is_trend_variable:
 * @x: array to examine.
 * @n: number of elements in array.
 *
 * Returns: 1 if @x is a simple linear trend variable, with each
 * observation equal to the preceding observation plus 1, or
 * if @x is a quadratic trend starting at 1 for the first
 * observation in the data set, and 0 otherwise.
 */

int is_trend_variable (const double *x, int n)
{
    int ret = 0;

    if (is_linear_trend(x, n)) {
	ret = 1;
    } else if (is_quadratic_trend(x, n)) {
	ret = 1;
    }

    return ret;
}

static void maybe_extend_trends (DATASET *dset, int oldn)
{
    int i, t;

    for (i=1; i<dset->v; i++) {
	if (is_linear_trend(dset->Z[i], oldn)) {
	    for (t=oldn; t<dset->n; t++) {
		dset->Z[i][t] = dset->Z[i][t-1] + 1.0;
	    }
	} else if (is_quadratic_trend(dset->Z[i], oldn)) {
	    for (t=oldn; t<dset->n; t++) {
		dset->Z[i][t] = (t + 1) * (t + 1);
	    }
	}
    }
}

static void maybe_extend_dummies (DATASET *dset, int oldn)
{
    int pd = dset->pd;
    double xon = 1.0, xoff = 0.0;
    int offset;
    int i, t;

    for (i=1; i<dset->v; i++) {
	if (real_periodic_dummy(dset->Z[i], oldn, &pd, &offset, &xon, &xoff)) {
	    for (t=oldn; t<dset->n; t++) {
		dset->Z[i][t] = ((t - offset) % pd)? xoff : xon;
	    }
	}
    }
}

/* regular, not panel-time, version */

static int real_dataset_add_observations (DATASET *dset, int n,
					  gretlopt opt)
{
    double *x;
    int oldn = dset->n;
    int i, t, bign;
    int err = 0;

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    if (n <= 0) {
	return 0;
    }

    if (dataset_is_panel(dset) && n % dset->pd != 0) {
	return E_PDWRONG;
    }

    bign = oldn + n;

    for (i=0; i<dset->v; i++) {
	x = realloc(dset->Z[i], bign * sizeof *x);
	if (x == NULL) {
	    return E_ALLOC;
	}
	dset->Z[i] = x;
	for (t=oldn; t<bign; t++) {
	    dset->Z[i][t] = (i == 0)? 1.0 : NADBL;
	}
    }

    if (dataset_has_markers(dset)) {
	if (opt & OPT_D) {
	    dataset_destroy_obs_markers(dset);
	} else {
	    if (reallocate_markers(dset, bign)) {
		return E_ALLOC;
	    }
	    for (t=oldn; t<bign; t++) {
		sprintf(dset->S[t], "%d", t + 1);
	    }
	}
    }

    if (dset->t2 == dset->n - 1) {
	dset->t2 = bign - 1;
    }

    dataset_set_nobs(dset, bign);

    if (opt & OPT_A) {
	maybe_extend_trends(dset, oldn);
	maybe_extend_dummies(dset, oldn);
    }

    /* does daily data need special handling? */
    ntolabel(dset->endobs, bign - 1, dset);

    return err;
}

static int panel_dataset_extend_time (DATASET *dset, int n)
{
    double *utmp, *vtmp;
    char **S = NULL;
    int newT, oldT = dset->pd;
    int oldn = dset->n;
    int n_units;
    int i, j, s, t, bign;
    size_t usz, vsz;
    int err = 0;

    if (!dataset_is_panel(dset)) {
	return E_PDWRONG;
    }

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    if (n <= 0) {
	return 0;
    }

    n_units = oldn / oldT;
    newT = oldT + n;
    bign = n_units * newT;

    usz = newT * sizeof *utmp;
    vsz = bign * sizeof *vtmp;

    utmp = malloc(usz);
    if (utmp == NULL) {
	return E_ALLOC;
    }

    if (dataset_has_markers(dset)) {
	S = strings_array_new_with_length(bign, OBSLEN);
	if (S == NULL) {
	    free(utmp);
	    return E_ALLOC;
	}
    }

    for (i=0; i<dset->v; i++) {
	int uconst = 1, utrend = 1, dtrend = 1;
	double xbak = NADBL;
	guint32 dt = 0, dbak = 0;
	int ed_err;

	vtmp = malloc(vsz);
	if (vtmp == NULL) {
	    err = E_ALLOC;
	    goto bailout;
	}

	s = 0;
	for (j=0; j<n_units; j++) {
	    for (t=0; t<oldT; t++) {
		utmp[t] = dset->Z[i][s++];
		if (dtrend) {
		    dt = epoch_day_from_ymd_basic(utmp[t]);
		}
		if (t == 0) {
		    xbak = utmp[t];
		    dbak = dt;
		} else {
		    if (uconst && (utmp[t] != xbak)) {
			uconst = 0;
		    }
		    if (utrend && (utmp[t] != xbak + 1)) {
			utrend = 0;
		    }
		    if (dtrend && (dt != dbak + 1)) {
			dtrend = 0;
		    }
		}
		xbak = utmp[t];
		dbak = dt;
	    }
	    for (t=oldT; t<newT; t++) {
		if (i == 0) {
		    utmp[t] = 1.0;
		} else if (uconst) {
		    utmp[t] = utmp[t-1];
		} else if (utrend) {
		    utmp[t] = utmp[t-1] + 1;
		} else if (dtrend) {
		    dt = epoch_day_from_ymd_basic(utmp[t-1]);
		    utmp[t] = ymd_basic_from_epoch_day(dt+1, 0, &ed_err);
		} else {
		    utmp[t] = NADBL;
		}
	    }
	    memcpy(vtmp + j*newT, utmp, usz);
	}
	free(dset->Z[i]);
	dset->Z[i] = vtmp;
    }

    if (S != NULL) {
	int k = 0;

	s = 0;
	for (j=0; j<n_units; j++) {
	    for (t=0; t<newT; t++) {
		if (t < oldT) {
		    strcpy(S[k], dset->S[s++]);
		} else {
		    sprintf(S[k], "%d:%d", j+1, t+1);
		}
		k++;
	    }
	}
	strings_array_free(dset->S, oldn);
	dset->S = S;
	S = NULL;
    }

    if (dset->t2 == dset->n - 1) {
	dset->t2 = bign - 1;
    }

    dataset_set_nobs(dset, bign);
    dset->pd = newT;
    ntolabel(dset->endobs, bign - 1, dset);

 bailout:

    free(utmp);
    if (S != NULL) {
	strings_array_free(S, bign);
    }

    return err;
}

/**
 * dataset_add_observations:
 * @dset: pointer to dataset.
 * @n: number of observations to add.
 * @opt: use OPT_A to attempt to recognize and
 * automatically extend simple deterministic variables such
 * as a time trend and periodic dummy variables;
 * use OPT_D to drop any observation markers rather than
 * expanding the set of markers and padding it out with
 * dummy values; use OPT_T to extend in the time dimension
 * in the case of panel data.
 *
 * Extends all series in the dataset by the specified number of
 * extra observations.  The added values are initialized to
 * the missing value code, #NADBL, with the exception of
 * simple deterministic variables when OPT_A is given.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int dataset_add_observations (DATASET *dset, int n, gretlopt opt)
{
    if (opt & OPT_T) {
	return panel_dataset_extend_time(dset, n);
    } else {
	return real_dataset_add_observations(dset, n, opt);
    }
}

static int real_insert_observation (int pos, DATASET *dset)
{
    double *x;
    int n = dset->n + 1;
    int i, t;
    int err = 0;

    for (i=0; i<dset->v; i++) {
	x = realloc(dset->Z[i], n * sizeof *x);
	if (x == NULL) {
	    return E_ALLOC;
	}
	dset->Z[i] = x;
	for (t=dset->n; t>pos; t--) {
	    dset->Z[i][t] = dset->Z[i][t-1];
	}
	dset->Z[i][pos] = (i == 0)? 1.0 : NADBL;
    }

    if (dataset_has_markers(dset)) {
	if (reallocate_markers(dset, n)) {
	    return E_ALLOC;
	}
	for (t=dset->n; t>pos; t--) {
	    strcpy(dset->S[t], dset->S[t-1]);
	}
	sprintf(dset->S[pos], "%d", pos + 1);
    }

    if (dset->t2 == dset->n - 1) {
	dset->t2 = n - 1;
    }

    dataset_set_nobs(dset, n);
    ntolabel(dset->endobs, n - 1, dset);

    return err;
}

/**
 * dataset_drop_observations:
 * @dset: pointer to dataset.
 * @n: number of observations to drop.
 *
 * Deletes @n observations from the end of each series in the
 * dataset.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int dataset_drop_observations (DATASET *dset, int n)
{
    double *x;
    int i, newn;

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    if (n <= 0) {
	return 0;
    }

    if (dataset_is_panel(dset) && n % dset->pd != 0) {
	return E_PDWRONG;
    }

    newn = dset->n - n;

    if (newn == 0) {
	free_Z(dset);
	clear_datainfo(dset, CLEAR_FULL);
	return 0;
    }

    for (i=0; i<dset->v; i++) {
	x = realloc(dset->Z[i], newn * sizeof *x);
	if (x == NULL) {
	    return E_ALLOC;
	}
	dset->Z[i] = x;
    }

    if (dataset_has_markers(dset)) {
	if (reallocate_markers(dset, newn)) {
	    return E_ALLOC;
	}
    }

    if (dset->t2 > newn - 1) {
	dset->t2 = newn - 1;
    }

    dataset_set_nobs(dset, newn);

    /* does daily data need special handling? */
    ntolabel(dset->endobs, newn - 1, dset);

    return 0;
}

/**
 * dataset_shrink_obs_range:
 * @dset: pointer to dataset.
 *
 * Truncates the range of observations in the dataset, based on
 * the current values of the t1 and t2 members of @dset.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int dataset_shrink_obs_range (DATASET *dset)
{
    int offset = dset->t1;
    int newn = dset->t2 - dset->t1 + 1;
    int tail = dset->n - newn;
    int err = 0;

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    if (offset > 0) {
	/* If the revised dataset starts at an offset into
	   the original, shift each series back to the start of
	   its Z[i] array.
	*/
	int i, mvsize;

	mvsize = newn * sizeof **dset->Z;
	for (i=0; i<dset->v; i++) {
	    memmove(dset->Z[i], dset->Z[i] + offset, mvsize);
	}

	if (dataset_has_markers(dset)) {
	    for (i=0; i<offset; i++) {
		free(dset->S[i]);
	    }
	    mvsize = newn * sizeof *dset->S;
	    memmove(dset->S, dset->S + offset, mvsize);
	}

	if (dset->structure == CROSS_SECTION) {
	    ntolabel(dset->stobs, 0, dset);
	} else {
	    /* FIXME panel? */
	    ntolabel(dset->stobs, dset->t1, dset);
	    dset->sd0 = get_date_x(dset->pd, dset->stobs);
	}

	dset->t1 = 0;
    }

    err = dataset_drop_observations(dset, tail);

    return err;
}

static int
dataset_expand_varinfo (int v0, int newvars, DATASET *dset)
{
    char **varname = NULL;
    VARINFO **varinfo = NULL;
    int bigv = v0 + newvars;
    int i, v, err = 0;

    varname = realloc(dset->varname, bigv * sizeof *varname);
    if (varname == NULL) {
	err = E_ALLOC;
    } else {
	dset->varname = varname;
    }

    for (i=0; i<newvars && !err; i++) {
	v = v0 + i;
	dset->varname[v] = malloc(VNAMELEN);
	if (dset->varname[v] == NULL) {
	    err = E_ALLOC;
	} else {
	    dset->varname[v][0] = '\0';
	}
    }

    if (!err && dset->varinfo != NULL) {
	varinfo = realloc(dset->varinfo, bigv * sizeof *varinfo);
	if (varinfo == NULL) {
	    err = E_ALLOC;
	} else {
	    dset->varinfo = varinfo;
	}
	for (i=0; i<newvars && !err; i++) {
	    v = v0 + i;
	    dset->varinfo[v] = malloc(sizeof **varinfo);
	    if (dset->varinfo[v] == NULL) {
		err = E_ALLOC;
	    } else {
		gretl_varinfo_init(dset->varinfo[v]);
	    }
	}
    }

    return err;
}

/* note: values of series newly added here are left uninitialized:
   that is the responsibility of the caller
*/

static int real_add_series (int newvars, double *x,
			    DATASET *dset)
{
    double **newZ;
    int v0 = dset->v;
    int i, err = 0;

    if (newvars == 0) {
	/* no-op */
	return 0;
    }

    newZ = realloc(dset->Z, (v0 + newvars) * sizeof *newZ);

#if DDEBUG
    fprintf(stderr, "real_add_series: add %d vars, Z = %p\n",
	    newvars, (void *) newZ);
#endif

    if (newZ == NULL) {
	err = E_ALLOC;
    } else {
	dset->Z = newZ;
    }

    if (!err) {
	if (newvars == 1 && x != NULL) {
	    /* a single new var, storage pre-allocated */
	    newZ[v0] = x;
	} else {
	    for (i=0; i<newvars && !err; i++) {
		newZ[v0+i] = malloc(dset->n * sizeof **newZ);
		if (newZ[v0+i] == NULL) {
		    err = E_ALLOC;
		}
	    }
	}
    }

    if (!err && dset != fetch_full_dataset()) {
	/* don't expand varinfo if we're adding a series
	   to the full dataset when currently sub-sampled,
	   since in that case varinfo is shared between
	   the two datasets
	*/
	err = dataset_expand_varinfo(v0, newvars, dset);
    }

    if (!err) {
	dset->v += newvars;
    }

    return err;
}

/**
 * dataset_add_series:
 * @dset: pointer to dataset.
 * @newvars: number of series to add.
 *
 * Adds space for the specified number of additional series
 * in the dataset. Values are initialized to zero.
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_add_series (DATASET *dset, int newvars)
{
    int v0 = dset->v;
    int err;

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    err = real_add_series(newvars, NULL, dset);

    if (!err) {
	int i, v, t;

	for (i=0; i<newvars; i++) {
	    v = v0 + i;
	    for (t=0; t<dset->n; t++) {
		dset->Z[v][t] = 0.0;
	    }
	}
    }

    return err;
}

/**
 * dataset_add_NA_series:
 * @dset: pointer to dataset.
 * @newvars: number of series to add.
 *
 * Adds space for the specified number of additional series
 * in the dataset. Values are initialized to NA.
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_add_NA_series (DATASET *dset, int newvars)
{
    int v0 = dset->v;
    int err;

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    err = real_add_series(newvars, NULL, dset);

    if (!err) {
	int i, v, t;

	for (i=0; i<newvars; i++) {
	    v = v0 + i;
	    for (t=0; t<dset->n; t++) {
		dset->Z[v][t] = NADBL;
	    }
	}
    }

    return err;
}

/**
 * dataset_add_allocated_series:
 * @dset: pointer to dataset.
 * @x: one-dimensional data array.
 *
 * Adds @x as an additional series in the dataset.
 * The array @x is not copied; it should be treated as
 * belonging to @dset after this operation.
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_add_allocated_series (DATASET *dset, double *x)
{
    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    } else {
	return real_add_series(1, x, dset);
    }
}

/**
 * dataset_add_series_as:
 * @dset: pointer to dataset.
 * @x: array to be added.
 * @name: name to give the new variable.
 *
 * Adds to the dataset a new series with name @name and
 * values given by @x.  The new variable is added at one
 * level "deeper" (in terms of function execution) than the
 * current level.  This is for use with user-defined functions.
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_add_series_as (DATASET *dset, double *x, const char *name)
{
    int v, t, err = 0;

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    if (dset->varinfo == NULL) {
	gretl_errmsg_set(_("Please open a data file first"));
	return 1;
    }

#if DDEBUG
    fprintf(stderr, "dataset_add_series_as: incoming Z=%p, name='%s'\n",
	    (void *) dset->Z, name);
#endif

    err = real_add_series(1, NULL, dset);

    if (!err) {
	v = dset->v - 1;
	for (t=0; t<dset->n; t++) {
	    dset->Z[v][t] = x[t];
	}
	strcpy(dset->varname[v], name);
	dset->varinfo[v]->stack_level += 1;
    }

    return err;
}

/**
 * dataset_copy_series_as:
 * @dset: pointer to dataset.
 * @v: index number of variable to copy.
 * @name: name to give the copy.
 *
 * Makes a copy of series @v under the name @name.
 * The copy exists in a variable namespace one level "deeper"
 * (in terms of function execution) than the variable being copied.
 * This is for use with user-defined functions: a variable
 * supplied to a function as an argument is copied into the
 * function's namespace under the name it was given as a
 * parameter.
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_copy_series_as (DATASET *dset, int v, const char *name)
{
    int t, err;

    err = real_add_series(1, NULL, dset);

    if (!err) {
	int vnew = dset->v - 1;

	for (t=0; t<dset->n; t++) {
	    dset->Z[vnew][t] = dset->Z[v][t];
	}
	strcpy(dset->varname[vnew], name);
	copy_varinfo(dset->varinfo[vnew], dset->varinfo[v]);
	if (dset->varinfo[v]->flags & VAR_LISTARG) {
	    dset->varinfo[vnew]->flags &= ~VAR_LISTARG;
	}
	dset->varinfo[vnew]->stack_level = gretl_function_depth() + 1;
#if 0
	fprintf(stderr, "copied var %d ('%s', level %d) as var %d ('%s', level %d): ",
		v, dset->varname[v], dset->varinfo[v]->stack_level,
		vnew, name, dset->varinfo[vnew]->stack_level);
	fprintf(stderr, "Z[%d][0] = %g\n", vnew, dset->Z[vnew][0]);
#endif
    }

    return err;
}

enum {
    DROP_NORMAL,
    DROP_SPECIAL
};

/* DROP_SPECIAL is used when deleting variables from the "full" shadow
   of a sub-sampled dataset, after deleting those same variables from
   the sub-sampled version: in that case we don't mess with the
   pointer elements of the DATASET struct, because these are shared
   between the full and sub-sampled versions.
*/

static int shrink_dataset_to_size (DATASET *dset, int nv, int drop)
{
    double **newZ;

#if DDEBUG
    fprintf(stderr, "shrink_dataset_to_size: dset at %p, dset->v=%d, nv=%d\n"
	    " drop = %s\n", (void *) dset, dset->v, nv,
	    (drop == DROP_NORMAL)? "DROP_NORMAL" : "DROP_SPECIAL");
#endif

    if (drop == DROP_NORMAL) {
	char **varname;
	VARINFO **varinfo;

	varname = realloc(dset->varname, nv * sizeof *varname);
	if (varname == NULL) {
	    return E_ALLOC;
	}
	dset->varname = varname;

	varinfo = realloc(dset->varinfo, nv * sizeof *varinfo);
	if (varinfo == NULL) {
	    return E_ALLOC;
	}
	dset->varinfo = varinfo;
    }

    newZ = realloc(dset->Z, nv * sizeof *newZ);
    if (newZ == NULL) {
	return E_ALLOC;
    }

    dset->Z = newZ;
    dset->v = nv;

    return 0;
}

static int vars_renumbered (const int *list, DATASET *dset,
			    int dmin)
{
    int i, ndel = 0;

    for (i=dmin; i<dset->v; i++) {
	if (in_gretl_list(list, i)) {
	    ndel++;
	} else if (ndel > 0 && !series_is_hidden(dset, i)) {
	    return 1;
	}
    }

    return 0;
}

int overwrite_err (const char *name)
{
    gretl_errmsg_sprintf(_("The variable %s is read-only"), name);

    return E_DATA;
}

/**
 * series_is_parent:
 * @dset: dataset information.
 * @v: ID number of series to test.
 *
 * Returns: 1 if variable @v is "parent" to a transformed
 * variable (e.g. a log, lag or difference), othewise 0.
 */

int series_is_parent (const DATASET *dset, int v)
{
    const char *s = dset->varname[v];
    int i;

    if (*s == '\0') {
	return 0;
    }

    for (i=1; i<dset->v; i++) {
	if (i != v && !strcmp(s, dset->varinfo[i]->parent)) {
	    return 1;
	}
    }

    return 0;
}

/**
 * dataset_rename_series:
 * @dset: dataset information.
 * @v: ID number of the series to be renamed.
 * @name: new name to give the series.
 *
 * Returns: 0 on success, non-zero on error.
 */

int dataset_rename_series (DATASET *dset, int v, const char *name)
{
    int err = 0;

    if (v <= 0 || v >= dset->v || name == NULL) {
	err = E_DATA;
    } else {
	err = check_varname(name);
    }

    if (!err) {
	GretlType type;

	type = user_var_get_type_by_name(name);
	if (type != GRETL_TYPE_NONE) {
	    gretl_errmsg_set("There is already an object of this name");
	    err = E_DATA;
	}
    }

    if (!err && current_series_index(dset, name) >= 0) {
	gretl_errmsg_set("There is already a series of this name");
	err = E_DATA;
    }

    if (!err && (object_is_const(dset->varname[v], v) ||
		 series_is_parent(dset, v))) {
	err = overwrite_err(dset->varname[v]);
    }

    if (!err && strcmp(dset->varname[v], name)) {
	dset->varname[v][0] = '\0';
	strncat(dset->varname[v], name, VNAMELEN-1);
	set_dataset_is_changed(dset, 1);
    }

    return err;
}

/**
 * dataset_replace_series:
 * @dset: pointer to dataset.
 * @v: ID number of the series to be replaced.
 * @x: replacement values.
 * @descrip: replacement description.
 * @flag: if = DS_GRAB_VALUES then replace dset->Z[@v]
 * with @x, otherwise copy the values in @x to dset->Z[@v].
 *
 * Replaces the description and numerical content of
 * series @v with the information provided.
 *
 * Returns: 0 on success, non-zero on error.
 */

int dataset_replace_series (DATASET *dset, int v,
			    double *x, const char *descrip,
			    DataCopyFlag flag)
{
    if (v < 0 || v >= dset->v) {
	/* out of bounds */
	return E_DATA;
    }

    if (object_is_const(dset->varname[v], v) ||
	series_is_parent(dset, v)) {
	return overwrite_err(dset->varname[v]);
    }

    gretl_varinfo_init(dset->varinfo[v]);
    series_set_label(dset, v, descrip);

    if (flag == DS_GRAB_VALUES) {
	free(dset->Z[v]);
	dset->Z[v] = x;
    } else {
	int t;

	for (t=0; t<dset->n; t++) {
	    dset->Z[v][t] = x[t];
	}
    }

    set_dataset_is_changed(dset, 1);

    return 0;
}

/**
 * dataset_replace_series_data:
 * @dset: pointer to dataset.
 * @v: ID number of the series to be modified.
 * @x: replacement values.
 * @t1: start of sample range.
 * @t2: end of sample range.
 * @descrip: replacement description.
 *
 * Replaces the description and numerical content of
 * series @v over the given sample range, with the
 * information provided.
 *
 * Returns: 0 on success, non-zero on error.
 */

int dataset_replace_series_data (DATASET *dset, int v,
				 const double *x,
				 int t1, int t2,
				 const char *descrip)
{
    int t, s;

    if (v < 0 || v >= dset->v) {
	/* out of bounds */
	return E_DATA;
    }

    if (object_is_const(dset->varname[v], v) ||
	series_is_parent(dset, v)) {
	return overwrite_err(dset->varname[v]);
    }

    gretl_varinfo_init(dset->varinfo[v]);
    series_set_label(dset, v, descrip);

    s = 0;
    for (t=t1; t<=t2; t++) {
	dset->Z[v][t] = x[s++];
    }

    set_dataset_is_changed(dset, 1);

    return 0;
}

static int real_drop_listed_vars (int *list, DATASET *dset,
				  int *renumber, int drop,
				  PRN *prn)
{
    int oldv = dset->v, vmax = dset->v;
    char vname[VNAMELEN] = {0};
    int d0, d1;
    int delmin = oldv;
    int i, v, ndel = 0;
    int err = 0;

    if (renumber != NULL) {
	*renumber = 0;
    }

    if (list == NULL || list[0] == 0) {
	/* no-op */
	return 0;
    }

    d0 = list[0];

    check_variable_deletion_list(list, dset);
    d1 = list[0];
    if (prn != NULL && d1 == 1) {
	strcpy(vname, dset->varname[list[1]]);
    }

    if (d1 == 0) {
	goto finish;
    }

#if DDEBUG
    fprintf(stderr, "real_drop_listed_variables: dropping %d vars:\n",
	    list[0]);
#endif

    /* check that no vars to be deleted are marked "const", and do
       some preliminary accounting while we're at it */

    for (i=1; i<=list[0]; i++) {
	v = list[i];
	if (v > 0 && v < oldv) {
	    if (object_is_const(dset->varname[v], v)) {
		return overwrite_err(dset->varname[v]);
	    }
	    if (v < delmin) {
		delmin = v;
	    }
	    ndel++;
	}
    }

    if (ndel == 0) {
	return 0;
    }

    if (renumber != NULL) {
	*renumber = vars_renumbered(list, dset, delmin);
    }

#if DDEBUG
    fprintf(stderr, "real_drop_listed_variables: lowest ID of deleted var"
	    " = %d\n", delmin);
#endif

    /* free and set to NULL all the vars to be deleted */
    for (i=1; i<=list[0]; i++) {
	v = list[i];
	if (v > 0 && v < oldv) {
	    free(dset->Z[v]);
	    dset->Z[v] = NULL;
	    if (drop == DROP_NORMAL) {
		free(dset->varname[v]);
		free(dset->varinfo[v]);
	    }
	}
    }

    /* repack pointers if necessary */

    for (v=1; v<vmax; v++) {
	if (dset->Z[v] == NULL) {
	    int gap = 1;

	    for (i=v+1; i<vmax; i++) {
		if (dset->Z[i] == NULL) {
		    gap++;
		} else {
		    break;
		}
	    }

	    if (i < vmax) {
		vmax -= gap;
		for (i=v; i<vmax; i++) {
		    if (drop == DROP_NORMAL) {
			dset->varname[i] = dset->varname[i + gap];
			dset->varinfo[i] = dset->varinfo[i + gap];
		    }
		    dset->Z[i] = dset->Z[i + gap];
		}
	    } else {
		/* deleting all subsequent vars: done */
		break;
	    }
	}
    }

    err = shrink_dataset_to_size(dset, oldv - ndel, drop);

 finish:

    /* report results, if appropriate */

    if (!err && prn != NULL) {
	if (d0 == d1) {
	    if (gretl_messages_on()) {
		if (*vname != '\0') {
		    pprintf(prn, _("Deleted %s"), vname);
		} else {
		    pprintf(prn, _("Deleted %d variables"), d1);
		}
		pputc(prn, '\n');
	    }
	} else {
	    if (d1 == 0) {
		pputs(prn, _("No variables were deleted"));
	    } else if (*vname != '\0') {
		pprintf(prn, _("Deleted %s"), vname);
	    } else {
		pprintf(prn, _("Deleted %d variables"), d1);
	    }
	    pprintf(prn, " (%s)\n", _("some data were in use"));
	}
    }

    return err;
}

static int *make_dollar_list (DATASET *dset, int *err)
{
    int *list = NULL;
    int i;

    for (i=1; i<dset->v; i++) {
	if (dset->varname[i][0] == '$') {
	    list = gretl_list_append_term(&list, i);
	    if (list == NULL) {
		*err = E_ALLOC;
		break;
	    }
	}
    }

    return list;
}

/**
 * dataset_drop_listed_variables:
 * @list: list of variable to drop, by ID number.
 * @dset: pointer to dataset.
 * @renumber: location for return of information on whether
 * remaining variables have been renumbered as a result, or
 * NULL.
 * @prn: pointer to printing struct.
 *
 * Deletes the variables given in @list from the dataset.  Remaining
 * variables may have their ID numbers changed as a consequence. If
 * @renumber is not NULL, this location receives 1 in case variables
 * have been renumbered, 0 otherwise.
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_drop_listed_variables (int *list,
				   DATASET *dset,
				   int *renumber,
				   PRN *prn)
{
    int oldv = dset->v;
    int *dlist = NULL;
    int dupv, free_dlist = 0;
    int err = 0;

    if (dset->n == 0 || dset->v == 0) {
	return E_NODATA;
    }

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    if (list == NULL) {
	/* signal to drop internal "$" variables */
	dlist = make_dollar_list(dset, &err);
	if (err) {
	    return err;
	} else if (dlist == NULL) {
	    /* no-op */
	    return 0;
	}
	free_dlist = 1;
    } else if (list[0] == 0) {
	/* no-op */
	return 0;
    } else {
	dlist = list;
    }

    dupv = gretl_list_duplicates(dlist, DELEET);
    if (dupv >= 0) {
	gretl_errmsg_sprintf(_("variable %d duplicated in the "
			       "command list."), dupv);
	return E_DATA;
    }

    err = real_drop_listed_vars(dlist, dset, renumber,
				DROP_NORMAL, prn);

    if (dlist[0] > 0) {
	if (!err && !dset->auxiliary) {
	    err = gretl_lists_revise(dlist, 0);
	}

	if (!err && complex_subsampled()) {
	    DATASET *fdset = fetch_full_dataset();

	    err = real_drop_listed_vars(dlist, fdset, NULL,
					DROP_SPECIAL, NULL);
	}
    }

    if (free_dlist) {
	free(dlist);
    } else if (dset->v != oldv) {
	set_dataset_is_changed(dset, 1);
    }

    return err;
}

/**
 * dataset_drop_variable:
 * @v: ID number of variable to drop.
 * @dset: pointer to dataset.
 *
 * Deletes variable @v from the dataset.
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_drop_variable (int v, DATASET *dset)
{
    int list[2] = {1, v};

    if (v <= 0 || v >= dset->v) {
	return E_DATA;
    }

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    return dataset_drop_listed_variables(list, dset, NULL, NULL);
}

/**
 * dataset_renumber_variable:
 * @v_old: original ID number of variable.
 * @v_new: new ID number.
 * @dset: dataset information.
 *
 * Moves the variable that was originally at position @v_old
 * in the datset to position @v_new, renumbering other
 * variables as required.
 *
 * Returns: 0 on success, error code on error;
 */

int dataset_renumber_variable (int v_old, int v_new,
			       DATASET *dset)
{
    double *x;
    VARINFO *vinfo;
    char vname[VNAMELEN];
    int i;

    if (complex_subsampled()) {
	/* too tricky */
	gretl_errmsg_set(_("dataset is subsampled"));
	return E_DATA;
    }

    if (dset_zcols_borrowed(dset)) {
	fprintf(stderr, "*** Internal error: modifying borrowed data\n");
	return E_DATA;
    }

    if (v_old < 1 || v_old > dset->v - 1 ||
	v_new < 1 || v_new > dset->v - 1) {
	/* out of bounds */
	return E_DATA;
    }

    if (v_old == v_new) {
	/* no-op */
	return 0;
    }

    x = dset->Z[v_old];
    vinfo = dset->varinfo[v_old];
    strcpy(vname, dset->varname[v_old]);

    if (v_new < v_old) {
	/* moving up in ordering */
	for (i=v_old; i>v_new; i--) {
	    dset->Z[i] = dset->Z[i-1];
	    strcpy(dset->varname[i], dset->varname[i-1]);
	    dset->varinfo[i] = dset->varinfo[i-1];
	}
    } else {
	/* moving down in ordering */
	for (i=v_old; i<v_new; i++) {
	    dset->Z[i] = dset->Z[i+1];
	    strcpy(dset->varname[i], dset->varname[i+1]);
	    dset->varinfo[i] = dset->varinfo[i+1];
	}
    }

    dset->Z[v_new] = x;
    strcpy(dset->varname[v_new], vname);
    dset->varinfo[v_new] = vinfo;

    set_dataset_is_changed(dset, 1);

    return 0;
}

/**
 * dataset_destroy_hidden_variables:
 * @dset: pointer to dataset.
 * @vmin: do not drop variables with ID numbers less than this.
 *
 * Deletes from the dataset any "hidden" variables that have
 * been added automatically (for example, auto-generated variables
 * used for the x-axis in graph plotting), and that have ID
 * numbers greater than or equal to @vmin.  Never deletes the
 * automatically generated constant (ID number 0).
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_destroy_hidden_variables (DATASET *dset, int vmin)
{
    int i, nhid = 0;
    int err = 0;

    if (vmin <= 1) vmin = 1;

    for (i=vmin; i<dset->v; i++) {
	if (series_is_hidden(dset, i)) {
	    nhid++;
	}
    }

    if (nhid > 0) {
	int *list = gretl_list_new(nhid);

	if (list == NULL) {
	    err = 1;
	} else {
	    int j = 1;

	    for (i=vmin; i<dset->v; i++) {
		if (series_is_hidden(dset, i)) {
		    list[j++] = i;
		}
	    }
	    err = dataset_drop_listed_variables(list, dset, NULL, NULL);
	    free(list);
	}
    }

    return err;
}

int dataset_set_matrix_name (DATASET *dset, const char *name)
{
    int err = 0;

    if (dset->descrip != NULL) {
	free(dset->descrip);
	dset->descrip = NULL;
    }

    if (name != NULL && *name != '\0') {
	dset->descrip = malloc(strlen(name) + 8);
	if (dset->descrip == NULL) {
	    err = E_ALLOC;
	} else {
	    sprintf(dset->descrip, "matrix:%s", name);
	}
    }

    return err;
}

const char *dataset_get_matrix_name (const DATASET *dset)
{
    if (dset->descrip != NULL &&
	strlen(dset->descrip) > 7 &&
	!strncmp(dset->descrip, "matrix:", 7)) {
	return dset->descrip + 7;
    } else {
	return NULL;
    }
}

const char *dataset_get_mapfile (const DATASET *dset)
{
    return dset == NULL ? NULL : dset->mapfile;
}

const char *dataset_period_label (const DATASET *dset)
{
    if (dset == NULL) {
	return _("periods");
    } else if (quarterly_or_monthly(dset)) {
	return dset->pd == 4 ? _("quarters") : _("months");
    } else if (annual_data(dset)) {
	return _("years");
    } else if (dataset_is_weekly(dset)) {
	return _("weeks");
    } else if (dataset_is_daily(dset)) {
	return _("days");
    } else if (dataset_is_hourly(dset)) {
	return _("hours");
    } else {
	return _("periods");
    }
}

/* intended for use with newly imported data: trash any
   series that contain nothing but NAs
*/

int maybe_prune_dataset (DATASET **pdset, void *p)
{
    DATASET *dset = *pdset;
    int allmiss, prune = 0, err = 0;
    int i, t;

    for (i=1; i<dset->v; i++) {
	allmiss = 1;
	for (t=0; t<dset->n; t++) {
	    if (!na(dset->Z[i][t])) {
		allmiss = 0;
		break;
	    }
	}
	if (allmiss) {
	    prune = 1;
	    break;
	}
    }

    if (prune) {
	char *mask = calloc(dset->v, 1);
	DATASET *newset = NULL;
	int ndrop = 0;

	if (mask == NULL) {
	    return E_ALLOC;
	}

	for (i=1; i<dset->v; i++) {
	    allmiss = 1;
	    for (t=0; t<dset->n; t++) {
		if (!na(dset->Z[i][t])) {
		    allmiss = 0;
		    break;
		}
	    }
	    if (allmiss) {
		mask[i] = 1;
		ndrop++;
	    }
	}

	newset = datainfo_new();
	if (newset == NULL) {
	    err = E_ALLOC;
	} else {
	    newset->v = dset->v - ndrop;
	    newset->n = dset->n;
	    err = start_new_Z(newset, 0);
	}

	if (!err) {
	    gretl_string_table *st = (gretl_string_table *) p;
	    size_t ssize = dset->n * sizeof **newset->Z;
	    int k = 1;

	    for (i=1; i<dset->v; i++) {
		if (!mask[i]) {
		    memcpy(newset->Z[k], dset->Z[i], ssize);
		    strcpy(newset->varname[k], dset->varname[i]);
		    copy_label(&newset->varinfo[k]->label,
			       dset->varinfo[i]->label);
		    if (st != NULL && k < i) {
			gretl_string_table_reset_column_id(st, i, k);
		    }
		    k++;
		}
	    }

	    destroy_dataset(dset);
	    *pdset = newset;

	    fprintf(stderr, "pruned dataset to %d variables\n", newset->v);
	}

	free(mask);
    }

    return err;
}

/* apparatus for sorting entire dataset */

typedef struct spoint_t_ spoint_t;

struct spoint_t_ {
    int obsnum;
    int nvals;
    double *vals;
};

static void free_spoints (spoint_t *sv, int n)
{
    int i;

    for (i=0; i<n; i++) {
	free(sv[i].vals);
    }

    free(sv);
}

static spoint_t *allocate_spoints (int n, int v)
{
    spoint_t *sv;
    int i;

    sv = malloc(n * sizeof *sv);

    if (sv != NULL) {
	for (i=0; i<n; i++) {
	    sv[i].vals = NULL;
	}
	for (i=0; i<n; i++) {
	    sv[i].vals = malloc(v * sizeof(double));
	    if (sv[i].vals == NULL) {
		free_spoints(sv, n);
		sv = NULL;
		break;
	    }
	}
    }

    return sv;
}

static int compare_vals_up (const void *a, const void *b)
{
    const spoint_t *pa = (const spoint_t *) a;
    const spoint_t *pb = (const spoint_t *) b;
    int i, ret = 0;

    for (i=0; i<pa->nvals && !ret; i++) {
	if (isnan(pa->vals[i]) || isnan(pb->vals[i])) {
	    if (!isnan(pa->vals[i])) {
		ret = -1;
	    } else if (!isnan(pb->vals[i])) {
		ret = 1;
	    }
	} else {
	    ret = (pa->vals[i] > pb->vals[i]) - (pa->vals[i] < pb->vals[i]);
	}
    }

    return ret;
}

static int compare_vals_down (const void *a, const void *b)
{
    const spoint_t *pa = (const spoint_t *) a;
    const spoint_t *pb = (const spoint_t *) b;
    int i, ret = 0;

    for (i=0; i<pa->nvals && !ret; i++) {
	if (isnan(pa->vals[i]) || isnan(pb->vals[i])) {
	    if (!isnan(pa->vals[i])) {
		ret = 1;
	    } else if (!isnan(pb->vals[i])) {
		ret = -1;
	    }
	} else {
	    ret = (pa->vals[i] < pb->vals[i]) - (pa->vals[i] > pb->vals[i]);
	}
    }

    return ret;
}

/* Turn a string-valued series into an integer-valued series
   representing the places of the strings in lexical order.
*/

typedef struct lexval_ {
    const char *s;
    int code;
} lexval;

static int compare_lexvals (const void *a, const void *b)
{
    const lexval *lva = (const lexval *) a;
    const lexval *lvb = (const lexval *) b;

    return g_utf8_collate(lva->s, lvb->s);
}

static int series_to_lexvals (DATASET *dset, int v, int *targ)
{
    int i, t, ct, n_strs;
    series_table *st = series_get_string_table(dset, v);
    char **strs = series_table_get_strings(st, &n_strs);
    lexval *lexvals;

    lexvals = calloc(n_strs, sizeof *lexvals);
    if (lexvals == NULL) {
	return E_ALLOC;
    }

    for (i=0; i<n_strs; i++) {
	lexvals[i].s = strs[i];
	lexvals[i].code = i+1;
    }

    qsort(lexvals, n_strs, sizeof *lexvals, compare_lexvals);

    for (t=0; t<dset->n; t++) {
	if (na(dset->Z[v][t])) {
	    targ[t] = INT_MAX;
	} else {
	    ct = (int) dset->Z[v][t];
	    targ[t] = 0;
	    for (i=0; i<n_strs; i++) {
		if (ct == lexvals[i].code) {
		    targ[t] = i+1;
		    break;
		}
	    }
	}
    }

    free(lexvals);

    return 0;
}

int dataset_sort_by (DATASET *dset, const int *list, gretlopt opt)
{
    spoint_t *sv = NULL;
    double *x = NULL;
    int *xs = NULL;
    int *xsi = NULL;
    char **S = NULL;
    int ns = list[0];
    int nsvals = 0;
    int i, t, v;
    int err = 0;

    sv = allocate_spoints(dset->n, ns);
    if (sv == NULL) {
	return E_ALLOC;
    }

    x = malloc(dset->n * sizeof *x);
    if (x == NULL) {
	free_spoints(sv, dset->n);
	return E_ALLOC;
    }

    if (dset->S != NULL) {
	S = strings_array_new_with_length(dset->n, OBSLEN);
	if (S == NULL) {
	    err = E_ALLOC;
	    goto bailout;
	}
    }

    for (i=0; i<ns; i++) {
	if (is_string_valued(dset, list[i+1])) {
	    nsvals++;
	}
    }
    if (nsvals > 0) {
	xs = malloc(nsvals * dset->n * sizeof *xs);
	if (xs == NULL) {
	    err = E_ALLOC;
	} else {
	    xsi = xs;
	    for (i=0; i<ns && !err; i++) {
		v = list[i+1];
		if (is_string_valued(dset, v)) {
		    err = series_to_lexvals(dset, v, xsi);
		    xsi += dset->n;
		}
	    }
	}
	if (err) {
	    goto bailout;
	}
    }

    for (t=0; t<dset->n; t++) {
	sv[t].obsnum = t;
	sv[t].nvals = ns;
    }
    xsi = xs;
    for (i=0; i<ns; i++) {
	v = list[i+1];
	if (is_string_valued(dset, v)) {
	    for (t=0; t<dset->n; t++) {
		if (xsi[t] == INT_MAX) {
		    sv[t].vals[i] = NADBL;
		} else {
		    sv[t].vals[i] = (double) xsi[t];
		}
	    }
	    xsi += dset->n;
	} else {
	    for (t=0; t<dset->n; t++) {
		sv[t].vals[i] = dset->Z[v][t];
	    }
	}
    }

    if (opt & OPT_D) {
	/* descending */
	qsort(sv, dset->n, sizeof *sv, compare_vals_down);
    } else {
	qsort(sv, dset->n, sizeof *sv, compare_vals_up);
    }

    for (i=1; i<dset->v; i++) {
	for (t=0; t<dset->n; t++) {
	    x[t] = dset->Z[i][sv[t].obsnum];
	}
	for (t=0; t<dset->n; t++) {
	    dset->Z[i][t] = x[t];
	}
    }

    if (S != NULL) {
	for (t=0; t<dset->n; t++) {
	    strcpy(S[t], dset->S[sv[t].obsnum]);
	}
	strings_array_free(dset->S, dset->n);
	dset->S = S;
    }

 bailout:

    free_spoints(sv, dset->n);
    free(x);
    free(xs);

    return err;
}

static int dataset_sort (DATASET *dset, const int *list,
			 gretlopt opt)
{
    if (dataset_is_time_series(dset) ||
	dataset_is_panel(dset)) {
	gretl_errmsg_set("You can only do this with undated data");
	return E_DATA;
    }

    if (list == NULL || list[0] < 1) {
	return E_DATA;
    }

    return dataset_sort_by(dset, list, opt);
}

/**
 * dataset_drop_last_variables:
 * @dset: pointer to dataset.
 * @delvars: number of variables to be dropped.
 *
 * Deletes from the dataset the number @delvars of variables
 * that were added most recently (that have the highest ID numbers).
 *
 * Returns: 0 on success, E_ALLOC on error.
 */

int dataset_drop_last_variables (DATASET *dset, int delvars)
{
    int newv = dset->v - delvars;
    int i, err = 0;

    if (delvars <= 0) {
	return 0;
    }

#if FULLDEBUG
    fprintf(stderr, "*** dataset_drop_last_variables: dropping %d, newv = %d\n",
	    delvars, newv);
#endif

    if (newv < 1) {
	fprintf(stderr, "dataset_drop_last_vars: dset->v = %d, delvars = %d "
		" -> newv = %d\n (dset = %p)\n", dset->v, delvars,
		newv, (void *) dset);
	return E_DATA;
    }

#if FULLDEBUG
    for (i=0; i<dset->v; i++) {
	if (dset->Z[i] == NULL) {
	    fprintf(stderr, "var %d (%s, level %d, val = NULL) %s\n",
		    i, dset->varname[i], dset->varinfo[i]->stack_level,
		    (i >= newv)? "deleting" : "");
	} else {
	    fprintf(stderr, "var %d (%s, level %d, val[0] = %g) %s\n",
		    i, dset->varname[i], dset->varinfo[i]->stack_level,
		    dset->Z[i][0], (i >= newv)? "deleting" : "");
	}
    }
#endif

#if 0
    fprintf(stderr, "dataset_drop_last_variables: origv=%d, newv=%d\n",
	    dset->v, newv);
    for (i=1; i<dset->v; i++) {
	fprintf(stderr, "before: var[%d] = '%s'\n", i, dset->varname[i]);
    }
#endif

    for (i=newv; i<dset->v; i++) {
	free(dset->varname[i]);
	free_varinfo(dset, i);
	free(dset->Z[i]);
	dset->Z[i] = NULL;
    }

    err = shrink_dataset_to_size(dset, newv, DROP_NORMAL);

#if 0
    for (i=1; i<dset->v; i++) {
	fprintf(stderr, "after: var[%d] = '%s'\n", i, dset->varname[i]);
    }
#endif

    if (!err && !dset->auxiliary) {
	err = gretl_lists_revise(NULL, newv);
    }

    if (!err && complex_subsampled()) {
	DATASET *fset = fetch_full_dataset();

	/*
	   Context: we're deleting @delvars variables at the end of
	   dset->Z, leaving @newv variables.  The dataset is currently
	   subsampled.

	   Question: should we delete any variables at the end of
	   fset->Z to keep the two arrays in sync?

	   If @newv < fset->v, this must mean that at least some of
	   the extra vars we're deleting from the current sub-sampled
	   Z have already been synced to the full Z, so we should do
	   the deletion from full Z.
	*/

	if (newv < fset->v) {
#if FULLDEBUG
	    fprintf(stderr, "prior fset->v = %d: shrinking full Z to %d vars\n",
		    fset->v, newv);
#endif
	    for (i=newv; i<fset->v; i++) {
		free(fset->Z[i]);
		fset->Z[i] = NULL;
	    }
	    err = shrink_dataset_to_size(fset, newv, DROP_SPECIAL);
	}
    }

    return err;
}

/**
 * build_stacked_series:
 * @pstack: location for returning stacked series.
 * @list: list of series to be stacked.
 * @length: number of observations to use per input series.
 * @offset: offset at which to start drawing observations.
 * @dset: pointer to dataset.
 *
 * Really for internal use. Don't worry about it.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int build_stacked_series (double **pstack, int *list,
			  int length, int offset,
			  DATASET *dset)
{
    double *xstack = NULL;
    int nv, oldn, bign, tmax;
    int i, err = 0;

    if (dset == NULL || dset->n == 0) {
	return E_NODATA;
    } else if (dataset_is_subsampled(dset)) {
	gretl_errmsg_set("stack: this function cannot be used when the dataset "
			 "is sub-sampled");
	return E_DATA;
    } else if (list == NULL || list[0] <= 0) {
	return E_INVARG;
    } else if (length <= 0) {
	return E_INVARG;
    } else if (length + offset > dset->n) {
	return E_INVARG;
    }

    nv = list[0];

#if PDEBUG
    fprintf(stderr, "nv = %d, length = %d, offset = %d\n", nv, length, offset);
#endif

    bign = nv * length;
    if (bign < dset->n) {
	bign = dset->n;
    }

#if PDEBUG
    fprintf(stderr, "bign = %d, allocating xstack (oldn = %d)\n", bign, dset->n);
#endif

    /* allocate container for stacked data */
    xstack = malloc(bign * sizeof *xstack);
    if (xstack == NULL) {
	return E_ALLOC;
    }

    /* extend length of all series? */
    oldn = dset->n;
    if (bign > oldn) {
	err = dataset_add_observations(dset, bign - oldn, OPT_NONE);
	if (err) {
	    return err;
	}
    }

    tmax = offset + length;

    /* construct stacked series */
    for (i=0; i<nv; i++) {
	int j = list[i+1];
	int bigt = length * i;
	int t;

	for (t=offset; t<tmax; t++) {
	    xstack[bigt] = dset->Z[j][t];
	    if (dset->S != NULL && bigt != t) {
		strcpy(dset->S[bigt], dset->S[t]);
	    }
	    bigt++;
	}
	if (i == nv - 1) {
	    for (t=bigt; t<bign; t++) {
		xstack[bigt++] = NADBL;
	    }
	}
    }

    *pstack = xstack;

    return err;
}

static int found_log_parent (const char *s, char *targ)
{
    int len = gretl_namechar_spn(s);

    if (len < VNAMELEN && s[len] == ')') {
	char fmt[8];

	sprintf(fmt, "%%%d[^)]", VNAMELEN-1);
	sscanf(s, fmt, targ);
	return 1;
    }

    return 0;
}

/**
 * series_is_log:
 * @dset: dataset information.
 * @i: ID number of series.
 * @parent: location to which to write the name of the
 * "parent" variable if any.
 *
 * Tries to determine if the variable with ID number @i is
 * the logarithm of some other variable.
 *
 * Returns: 1 if variable @i appears to be a log, else 0.
 */

int series_is_log (const DATASET *dset, int i, char *parent)
{
    const char *s = series_get_label(dset, i);

    *parent = '\0';

    if (s != NULL && *s != '\0') {
	char fmt[16];

	sprintf(fmt, "= log of %%%ds", VNAMELEN-1);

	if (sscanf(s, fmt, parent) == 1) {
	    return 1;
	} else if (!strncmp(s, "log(", 4)) {
	    return found_log_parent(s + 4, parent);
	} else {
	    s += strcspn(s, "=");
	    if (!strncmp(s, "=log(", 5)) {
		return found_log_parent(s + 5, parent);
	    }
	}
    }

    return 0;
}

/**
 * series_set_discrete:
 * @dset: pointer to data information struct.
 * @i: index number of series.
 * @s: non-zero to mark variable as discrete, zero to
 * mark as not discrete.
 *
 * Mark a variable as being discrete or not.
 */

void series_set_discrete (DATASET *dset, int i, int s)
{
    if (i > 0 && i < dset->v) {
	int flags = dset->varinfo[i]->flags;

	if (s && !(flags & VAR_DISCRETE)) {
	    dset->varinfo[i]->flags |= VAR_DISCRETE;
	    set_dataset_is_changed(dset, 1);
	} else if (!s && (flags & VAR_DISCRETE)) {
	    dset->varinfo[i]->flags &= ~VAR_DISCRETE;
	    set_dataset_is_changed(dset, 1);
	}
    }
}

int series_record_label (DATASET *dset, int i,
			 const char *s)
{
    char *targ = dset->varinfo[i]->label;

    if (labels_differ(targ, s)) {
	copy_label(&dset->varinfo[i]->label, s);
	set_dataset_is_changed(dset, 1);
    }

    return 0;
}

int series_record_display_name (DATASET *dset, int i,
				const char *s)
{
    char *targ = dset->varinfo[i]->display_name;

    if (strcmp(targ, s)) {
	*targ = '\0';
	strncat(targ, s, MAXDISP - 1);
	set_dataset_is_changed(dset, 1);
    }

    return 0;
}

const char *series_get_graph_name (const DATASET *dset, int i)
{
    const char *ret = dset->varname[i];

    if (dset->varinfo != NULL && dset->varinfo[i] != NULL) {
	if (dset->varinfo[i]->display_name[0] != '\0') {
	    ret = dset->varinfo[i]->display_name;
	}
    }

    return ret;
}

static int add_obs (int n, DATASET *dset, gretlopt opt, PRN *prn)
{
    int err = 0;

    if (complex_subsampled()) {
	pprintf(prn, _("The data set is currently sub-sampled.\n"));
	err = E_DATA;
    } else if (n <= 0) {
	err = E_PARSE;
    } else if (opt & OPT_T) {
	/* extending panel time */
	err = panel_dataset_extend_time(dset, n);
	if (!err) {
	    pprintf(prn, _("Panel time extended by %d observations"), n);
	    pputc(prn, '\n');
	}
    } else {
	err = dataset_add_observations(dset, n, OPT_A);
	if (!err) {
	    pprintf(prn, _("Dataset extended by %d observations"), n);
	    pputc(prn, '\n');
	    extend_function_sample_range(n);
	}
    }

    return err;
}

static int insert_obs (int n, DATASET *dset, PRN *prn)
{
    int err = 0;

    if (complex_subsampled()) {
	pprintf(prn, _("The data set is currently sub-sampled.\n"));
	err = E_DATA;
    } else if (dataset_is_panel(dset)) {
	err = E_PDWRONG;
    } else if (n <= 0 || n > dset->n) {
	err = E_DATA;
    } else {
	err = real_insert_observation(n - 1, dset);
    }

    return err;
}

int dataset_op_from_string (const char *s)
{
    int op = DS_NONE;

    if (s == NULL || *s == '\0') {
	return DS_NONE;
    }

    if (!strcmp(s, "addobs")) {
	op = DS_ADDOBS;
    } else if (!strcmp(s, "compact")) {
	op = DS_COMPACT;
    } else if (!strcmp(s, "expand")) {
	op = DS_EXPAND;
    } else if (!strcmp(s, "transpose")) {
	op = DS_TRANSPOSE;
    } else if (!strcmp(s, "delete")) {
	op = DS_DELETE;
    } else if (!strcmp(s, "keep")) {
	op = DS_KEEP;
    } else if (!strcmp(s, "sortby")) {
	op = DS_SORTBY;
    } else if (!strcmp(s, "dsortby")) {
	op = DS_DSORTBY;
    } else if (!strcmp(s, "resample")) {
	op = DS_RESAMPLE;
    } else if (!strcmp(s, "restore")) {
	op = DS_RESTORE;
    } else if (!strcmp(s, "clear")) {
	op = DS_CLEAR;
    } else if (!strcmp(s, "renumber")) {
	op = DS_RENUMBER;
    } else if (!strcmp(s, "insobs")) {
	op = DS_INSOBS;
    } else if (!strcmp(s, "pad-daily")) {
	op = DS_PAD_DAILY;
    }

    return op;
}

static int dataset_int_param (const char **ps, int op,
			      DATASET *dset, int *err)
{
    const char *s = *ps;
    char test[32];
    int k = 0;

    if ((op == DS_COMPACT || op == DS_EXPAND) &&
	!dataset_is_time_series(dset)) {
	*err = E_PDWRONG;
	return 0;
    }

    if (op == DS_PAD_DAILY && !dated_daily_data(dset)) {
	*err = E_PDWRONG;
	return 0;
    }

    *test = '\0';
    sscanf(s, "%31s", test);
    *ps += strlen(test);

    k = gretl_int_from_string(test, err);
    if (*err) {
	return 0;
    }

    if (k <= 0 || (op == DS_RESAMPLE && k < 1)) {
	*err = E_DATA;
    } else if (op == DS_INSOBS) {
	if (k > dset->n) {
	    *err = E_DATA;
	}
    } else if (op == DS_COMPACT) {
	*err = E_PDWRONG;
	if (dset->pd == 12 && (k == 4 || k == 1)) {
	    *err = 0;
	} else if (dset->pd == 4 && k == 1) {
	    *err = 0;
	} else if (dset->pd == 52 && k == 12) {
	    *err = 0;
	} else if (dated_daily_data(dset) && (k == 52 || k == 12)) {
	    *err = 0;
	} else if (dataset_is_daily(dset) && k == 4) {
	    if (strstr(*ps, "spread")) {
		*err = 0;
	    }
	}
    } else if (op == DS_EXPAND) {
	*err = E_PDWRONG;
	if (dset->pd == 1 && (k == 4 || k == 12)) {
	    *err = 0;
	} else if (dset->pd == 4 && k == 12) {
	    *err = 0;
	}
    } else if (op == DS_PAD_DAILY) {
	if (k < 5 || k > 7 || k < dset->pd) {
	    *err = E_PDWRONG;
	}
    }

    if (*err == E_PDWRONG) {
	gretl_errmsg_set("This conversion is not supported");
    }

    return k;
}

static int compact_data_set_wrapper (const char *s, DATASET *dset,
				     int k)
{
    CompactMethod method = COMPACT_AVG;

    if (s != NULL) {
	s += strspn(s, " ");
	if (!strcmp(s, "sum")) {
	    method = COMPACT_SUM;
	} else if (!strcmp(s, "first") || !strcmp(s, "sop")) {
	    method = COMPACT_SOP;
	} else if (!strcmp(s, "last") || !strcmp(s, "eop")) {
	    method = COMPACT_EOP;
	} else if (!strcmp(s, "spread")) {
	    method = COMPACT_SPREAD;
	} else if (!strcmp(s, "avg") || !strcmp(s, "average")) {
	    method = COMPACT_AVG;
	} else if (*s != '\0') {
	    return E_PARSE;
	}
    }

    return compact_data_set(dset, k, method, 0, 0);
}

static unsigned int resample_seed;

unsigned int get_resampling_seed (void)
{
    return resample_seed;
}

/* resample the dataset by observation, with replacement */

int dataset_resample (DATASET *dset, int n, unsigned int seed)
{
    DATASET *rset = NULL;
    char **S = NULL;
    unsigned int state;
    int T = sample_size(dset);
    int v = dset->v;
    int i, j, s, t;
    int err = 0;

    if (v < 2) {
	return E_DATA;
    }

    rset = datainfo_new();
    if (rset == NULL) {
	return E_ALLOC;
    }

    rset->Z = malloc(v * sizeof *rset->Z);
    if (rset->Z == NULL) {
	free(rset);
	return E_ALLOC;
    }

    for (i=0; i<v; i++) {
	rset->Z[i] = NULL;
    }

    rset->v = v;

    j = 0;
    for (i=0; i<dset->v && !err; i++) {
	rset->Z[j] = malloc(n * sizeof **rset->Z);
	if (rset->Z[j] == NULL) {
	    err = E_ALLOC;
	} else if (i == 0) {
	    for (t=0; t<n; t++) {
		rset->Z[j][t] = 1.0;
	    }
	}
	j++;
    }

    if (err) {
	goto bailout;
    }

    if (dset->markers == REGULAR_MARKERS) {
	S = strings_array_new_with_length(n, OBSLEN);
    }

    if (seed > 0) {
	resample_seed = seed;
	gretl_rand_set_seed(seed);
    } else {
	resample_seed = gretl_rand_get_seed();
    }

    state = gretl_rand_int();

    for (t=0; t<n; t++) {
	s = gretl_rand_int_max(T) + dset->t1;
	j = 1;
	for (i=1; i<dset->v; i++) {
	    rset->Z[j][t] = dset->Z[i][s];
	    j++;
	}
	if (S != NULL) {
	    strcpy(S[t], dset->S[s]);
	}
    }

    if (S != NULL) {
	rset->S = S;
	rset->markers = REGULAR_MARKERS;
    }

    rset->varname = dset->varname;
    rset->varinfo = dset->varinfo;
    rset->descrip = dset->descrip;

    rset->n = n;
    rset->t1 = 0;
    rset->t2 = n - 1;
    dataset_obs_info_default(rset);

    set_dataset_resampled(rset, state);

 bailout:

    if (err) {
	free_Z(rset);
	clear_datainfo(rset, CLEAR_SUBSAMPLE);
	free(rset);
    } else {
	backup_full_dataset(dset);
	*dset = *rset;
	free(rset);
    }

    return err;
}

/* note: @list should contain a single series ID, that of the
   target series, and @param should hold a numeric string
   giving the position to which @targ should be moved;
   @fixmax is the greatest series ID number that cannot be
   changed (based on saved models, etc., as determined by the
   caller)
*/

int renumber_series_with_checks (const int *list,
				 const char *param,
				 int fixmax,
				 DATASET *dset,
				 PRN *prn)
{
    char vname[VNAMELEN];
    int v_old, v_new;
    int f1, err = 0;

    if (list == NULL || list[0] != 1 ||
	param == NULL || *param == '\0') {
	return E_INVARG;
    }

    if (sscanf(param, "%d", &v_new) != 1) {
	return E_INVARG;
    }

    v_old = list[1];

    if (v_old < 1 || v_old > dset->v - 1 ||
	v_new < 1 || v_new > dset->v - 1) {
	/* out of bounds */
	return E_INVARG;
    } else if (v_new == v_old) {
	/* no-op */
	return 0;
    }

    f1 = max_varno_in_saved_lists();

    if (f1 > fixmax) {
	fixmax = f1;
    }

    strcpy(vname, dset->varname[v_old]);

    if (v_old <= fixmax) {
	gretl_errmsg_sprintf(_("Variable %s cannot be renumbered"), vname);
	err = E_DATA;
    } else if (v_new <= fixmax) {
	gretl_errmsg_sprintf(_("Target ID %d is not available"), v_new);
	err = E_DATA;
    } else {
	err = dataset_renumber_variable(v_old, v_new, dset);
    }

    if (!err && gretl_messages_on()) {
	pprintf(prn, _("Renumbered %s as variable %d\n"),
		vname, v_new);
	maybe_list_series(dset, prn);
    }

    return err;
}

/* alternate forms:

           @op        @list  @param
   dataset addobs            24
   dataset compact           1
   dataset compact           4 last
   dataset expand            4
   dataset transpose
   dataset sortby     x1
   dataset resample          500
   dataset clear
   dataset renumber   orig   2
   dataset insobs            13
   dataset pad-daily         7

*/

int modify_dataset (DATASET *dset, int op, const int *list,
		    const char *param, gretlopt opt, PRN *prn)
{
    static int resampled;
    int k = 0, err = 0;

    if (dset == NULL || dset->Z == NULL) {
	return E_NODATA;
    }

#if 0
    fprintf(stderr, "modify_dataset: op=%d, param='%s'\n", op, param);
    printlist(list, "list");
#endif

    if (op == DS_CLEAR || op == DS_RENUMBER) {
	/* must be handled by the calling program */
	return E_NOTIMP;
    }

    if (gretl_function_depth() > 0) {
	if (op == DS_ADDOBS && !complex_subsampled() &&
	    dset->t2 == dset->n - 1 && !(opt & OPT_T)) {
	    /* experimental, 2015-07-28: allow "addobs" within a
	       function provided the dataset is not subsampled
	    */
	    goto proceed;
	} else {
	    gretl_errmsg_set(_("The 'dataset' command is not available "
			       "within functions"));
	    return 1;
	}
    }

    if (gretl_looping() && op != DS_RESAMPLE &&
	op != DS_RESTORE && op != DS_SORTBY) {
	pputs(prn, _("Sorry, this command is not available in loop mode\n"));
	return 1;
    }

    if (op == DS_RESAMPLE && resampled) {
	/* repeated "resample": implicitly restore first */
	err = restore_full_sample(dset, NULL);
	if (err) {
	    return err;
	} else {
	    resampled = 0;
	}
    }

    if (op != DS_RESTORE && complex_subsampled()) {
	gretl_errmsg_set(_("The data set is currently sub-sampled"));
	return 1;
    }

 proceed:

    if (op == DS_ADDOBS || op == DS_INSOBS ||
	op == DS_COMPACT || op == DS_RESAMPLE ||
	op == DS_PAD_DAILY) {
	if (param == NULL) {
	    err = E_ARGS;
	} else {
	    k = dataset_int_param(&param, op, dset, &err);
	}
	if (err) {
	    return err;
	}
    } else if (op == DS_EXPAND) {
	if (param != NULL) {
	    k = dataset_int_param(&param, op, dset, &err);
	} else if (dset->pd == 1) {
	    k = 4;
	} else if (dset->pd == 4) {
	    k = 12;
	} else {
	    err = E_PDWRONG;
	}
	if (err) {
	    return err;
	}
    }

    if (op == DS_ADDOBS) {
	err = add_obs(k, dset, opt, prn);
    } else if (op == DS_INSOBS) {
	err = insert_obs(k, dset, prn);
    } else if (op == DS_COMPACT) {
	err = compact_data_set_wrapper(param, dset, k);
    } else if (op == DS_EXPAND) {
	err = expand_data_set(dset, k);
    } else if (op == DS_PAD_DAILY) {
	err = pad_daily_data(dset, k, prn);
    } else if (op == DS_TRANSPOSE) {
	err = transpose_data(dset);
    } else if (op == DS_SORTBY) {
	err = dataset_sort(dset, list, OPT_NONE);
    } else if (op == DS_DSORTBY) {
	err = dataset_sort(dset, list, OPT_D);
    } else if (op == DS_RESAMPLE) {
	err = dataset_resample(dset, k, 0);
	if (!err) {
	    resampled = 1;
	}
    } else if (op == DS_RESTORE) {
	if (resampled) {
	    err = restore_full_sample(dset, NULL);
	    resampled = 0;
	} else {
	    pprintf(prn, _("dataset restore: dataset is not resampled\n"));
	    err = E_DATA;
	}
    } else if (op == DS_DELETE) {
	pprintf(prn, "dataset delete: not ready yet\n");
    } else if (op == DS_KEEP) {
	pprintf(prn, "dataset keep: not ready yet\n");
    } else {
	err = E_PARSE;
    }

    return err;
}

int dataset_get_structure (const DATASET *dset)
{
    if (dset == NULL || dset->n == 0) {
	return DATA_NONE;
    } else if (dataset_is_panel(dset)) {
	return DATA_PANEL;
    } else if (dataset_is_time_series(dset)) {
	return DATA_TS;
    } else {
	return DATA_XSECT;
    }
}

/**
 * panel_sample_size:
 * @dset: pointer to data information struct.
 *
 * Returns: the numbers of units/individuals in the current
 * sample range, or 0 if the dataset is not a panel.
 */

int panel_sample_size (const DATASET *dset)
{
    int ret = 0;

    if (dataset_is_panel(dset)) {
	ret = (dset->t2 - dset->t1 + 1) / dset->pd;
    }

    return ret;
}

/**
 * multi_unit_panel_sample:
 * @dset: pointer to dataset.
 *
 * Returns: 1 if the dataset is a panel and the current sample
 * range includes two or more individuals, otherwise 0.
 */

int multi_unit_panel_sample (const DATASET *dset)
{
    int ret = 0;

    if (dataset_is_panel(dset)) {
	ret = (dset->t2 - dset->t1 + 1) > dset->pd;
    }

    return ret;
}

/**
 * dataset_purge_missing_rows:
 * @dset: pointer to dataset.
 *
 * Removes empty rows from the dataset -- that is, observations
 * at which there are no non-missing values.  This is intended
 * for daily data only.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int dataset_purge_missing_rows (DATASET *dset)
{
    int new_n, missrow, totmiss = 0;
    int t1 = dset->t1;
    int t2 = dset->t2;
    char **S = NULL;
    double *Zi = NULL;
    size_t sz;
    int i, t, s;
    int err = 0;

    for (t=0; t<dset->n; t++) {
	missrow = 1;
	for (i=1; i<dset->v; i++) {
	    if (!na(dset->Z[i][t])) {
		missrow = 0;
		break;
	    }
	}
	if (missrow) {
	    totmiss++;
	    if (t < dset->t1) {
		t1--;
	    }
	    if (t < dset->t2) {
		t2--;
	    }
	}
    }

    if (totmiss == 0) {
	/* no-op */
	return 0;
    }

    if (dated_daily_data(dset) && dset->S == NULL) {
	err = dataset_allocate_obs_markers(dset);
	if (!err) {
	    for (t=0; t<dset->n; t++) {
		calendar_date_string(dset->S[t], t, dset);
	    }
	}
    }

    for (t=0; t<dset->n; t++) {
	missrow = 1;
	for (i=1; i<dset->v; i++) {
	    if (!na(dset->Z[i][t])) {
		missrow = 0;
		break;
	    }
	}
	if (missrow) {
	    sz = (dset->n - t) * sizeof **dset->Z;
	    for (i=1; i<dset->v; i++) {
		memmove(dset->Z[i] + t, dset->Z[i] + t + 1, sz);
	    }
	    if (dset->S != NULL) {
		free(dset->S[t]);
		for (s=t; s<dset->n - 1; s++) {
		    dset->S[s] = dset->S[s+1];
		}
	    }
	}
    }

    new_n = dset->n - totmiss;

    for (i=1; i<dset->v; i++) {
	Zi = realloc(dset->Z[i], new_n * sizeof *Zi);
	if (Zi == NULL) {
	    err = E_ALLOC;
	} else {
	    dset->Z[i] = Zi;
	}
    }

    if (!err && dset->S != NULL) {
	S = realloc(dset->S, new_n * sizeof *S);
	if (S == NULL) {
	    err = E_ALLOC;
	} else {
	    dset->S = S;
	    if (dated_daily_data(dset)) {
		strcpy(dset->stobs, dset->S[0]);
		strcpy(dset->endobs, dset->S[new_n-1]);
		dset->sd0 = get_epoch_day(dset->stobs);
	    }
	}
    }

    dataset_set_nobs(dset, new_n);
    dset->t1 = t1;
    dset->t2 = t2;

    return err;
}

/**
 * dataset_set_time_series:
 * @dset: pointer to dataset.
 * @pd: time series annual frequency (1 for annual, 4
 * for quarterly or 12 for monthly).
 * @yr0: starting year.
 * @minor0: starting "minor" period, 1-based (quarter or
 * month).
 *
 * Sets time-series properties on @dset: frequency @pd with
 * starting observation @yr0, @minor0. If the data are
 * annual (@pd = 1) then @minor0 is ignored.

 * Returns: 0 on success, non-zero code on error.
 */

int dataset_set_time_series (DATASET *dset, int pd,
			     int yr0, int minor0)
{
    int err = 0;

    if (pd != 1 && pd != 4 && pd != 12) {
	err = E_DATA;
    } else if (yr0 < 1) {
	err = E_DATA;
    } else if (pd > 1 && (minor0 < 1 || minor0 > pd)) {
	err = E_DATA;
    } else {
	gchar *stobs = NULL;

	dataset_destroy_obs_markers(dset);
	dset->structure = TIME_SERIES;
	dset->pd = pd;

	if (pd == 1) {
	    stobs = g_strdup_printf("%d", yr0);
	} else if (pd == 4) {
	    stobs = g_strdup_printf("%d.%d", yr0, minor0);
	} else {
	    stobs = g_strdup_printf("%d.%02d", yr0, minor0);
	}

	dset->sd0 = dot_atof(stobs);
	ntolabel(dset->stobs, 0, dset);
	ntolabel(dset->endobs, dset->n - 1, dset);
	g_free(stobs);
    }

    return err;
}

void dataset_clear_sample_record (DATASET *dset)
{
    if (dset->restriction != NULL) {
	free(dset->restriction);
	dset->restriction = NULL;
    }
}

/**
 * series_is_discrete:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: non-zero iff series @i should be treated as discrete.
 */

int series_is_discrete (const DATASET *dset, int i)
{
    return dset->varinfo[i]->flags & VAR_DISCRETE;
}

/**
 * series_is_hidden:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: non-zero iff series @i is hidden.
 */

int series_is_hidden (const DATASET *dset, int i)
{
    return dset->varinfo[i]->flags & VAR_HIDDEN;
}

/**
 * series_is_generated:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: non-zero iff series @i was generated using
 * a formula or transformation function.
 */

int series_is_generated (const DATASET *dset, int i)
{
    return dset->varinfo[i]->flags & VAR_GENERATED;
}

/**
 * series_is_listarg:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: non-zero iff series @i has been marked as
 * belonging to a list argument to a function.
 */

int series_is_listarg (const DATASET *dset, int i)
{
    return dset->varinfo[i]->flags & VAR_LISTARG;
}

/**
 * series_is_coded:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: non-zero iff series @i has been marked as
 * "coded", meaning that its numerical values represent
 * an arbitrary encoding of qualitative characteristics.
 */

int series_is_coded (const DATASET *dset, int i)
{
    return dset->varinfo[i]->flags & VAR_CODED;
}

/**
 * series_is_integer_valued:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: non-zero iff all values in series @i are
 * representable as integers (ignoring missing values).
 */

int series_is_integer_valued (const DATASET *dset, int i)
{
    const double *x = dset->Z[i];
    int t, n_ok = 0, ret = 1;

    for (t=0; t<dset->n; t++) {
	if (!na(x[t])) {
	    n_ok++;
	    if (x[t] != floor(x[t])) {
		ret = 0;
		break;
	    } else if (x[t] > INT_MAX || x[t] < INT_MIN) {
		ret = 0;
		break;
	    }
	}
    }

    if (n_ok == 0) {
	/* don't let an entirely missing series count as
	   "integer-valued"
	*/
	ret = 0;
    }

    return ret;
}

/**
 * series_set_flag:
 * @dset: pointer to dataset.
 * @i: index number of series.
 * @flag: flag to set.
 *
 * Sets the given @flag on series @i.
 */

void series_set_flag (DATASET *dset, int i, VarFlags flag)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->flags |= flag;
    }
}

/**
 * series_unset_flag:
 * @dset: pointer to dataset.
 * @i: index number of series.
 * @flag: flag to remove.
 *
 * Unsets the given @flag on series @i.
 */

void series_unset_flag (DATASET *dset, int i, VarFlags flag)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->flags &= ~flag;
    }
}

/**
 * series_get_flags:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the flags set series @i.
 */

VarFlags series_get_flags (const DATASET *dset, int i)
{
    if (i >= 0 && i < dset->v) {
	return dset->varinfo[i]->flags;
    } else {
	return 0;
    }
}

/**
 * series_zero_flags:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Sets flags on series @i to zero.
 */

void series_zero_flags (DATASET *dset, int i)
{
    if (i >= 0 && i < dset->v) {
	dset->varinfo[i]->flags = 0;
    }
}

/**
 * series_get_label:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the descriptive label for series @i.
 */

const char *series_get_label (const DATASET *dset, int i)
{
    if (i >= 0 && i < dset->v) {
	return dset->varinfo[i]->label;
    } else {
	return NULL;
    }
}

/**
 * series_get_display_name:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the display name for series @i.
 */

const char *series_get_display_name (const DATASET *dset, int i)
{
    if (i >= 0 && i < dset->v) {
	return dset->varinfo[i]->display_name;
    } else {
	return NULL;
    }
}

/**
 * series_get_parent_name:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the name of the "parent" of series @i
 * (e.g. if series @i is a lag of some other series)
 * or NULL if the series has no parent.
 */

const char *series_get_parent_name (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	if (dset->varinfo[i]->parent[0] != '\0') {
	    return dset->varinfo[i]->parent;
	}
    }

    return NULL;
}

/**
 * series_get_parent_id:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the ID number of the "parent" of series @i
 * (e.g. if series @i is a lag of some other series)
 * or -1 if the series has no parent.
 */

int series_get_parent_id (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	const char *pname = dset->varinfo[i]->parent;

	if (*pname != '\0') {
	    return current_series_index(dset, pname);
	}
    }

    return -1;
}

int series_get_lag (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	return dset->varinfo[i]->lag;
    } else {
	return 0;
    }
}

int series_get_transform (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	return dset->varinfo[i]->transform;
    } else {
	return 0;
    }
}

/**
 * series_get_compact_method:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the compaction method set for series @i.
 */

int series_get_compact_method (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	return dset->varinfo[i]->compact_method;
    } else {
	return 0;
    }
}

/**
 * series_get_stack_level:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the stack level of series @i.
 */

int series_get_stack_level (const DATASET *dset, int i)
{
    if (i >= 0 && i < dset->v) {
	return dset->varinfo[i]->stack_level;
    } else {
	return 0;
    }
}

void series_set_mtime (DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->mtime = gretl_monotonic_time();
    }
}

gint64 series_get_mtime (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	return dset->varinfo[i]->mtime;
    } else {
	return 0;
    }
}

void series_set_label (DATASET *dset, int i,
		       const char *s)
{
    if (i > 0 && i < dset->v) {
	copy_label(&dset->varinfo[i]->label, s);
    }
}

void series_set_display_name (DATASET *dset, int i,
			      const char *s)
{
    if (i > 0 && i < dset->v) {
	char *targ = dset->varinfo[i]->display_name;

	*targ = '\0';
	strncat(targ, s, MAXDISP-1);
    }
}

void series_set_compact_method (DATASET *dset, int i,
				int method)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->compact_method = method;
    }
}

void series_set_parent (DATASET *dset, int i,
			const char *parent)
{
    if (i > 0 && i < dset->v) {
	strcpy(dset->varinfo[i]->parent, parent);
    }
}

void series_set_transform (DATASET *dset, int i,
			   int transform)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->transform = transform;
    }
}

void series_set_lag (DATASET *dset, int i, int lag)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->lag = lag;
    }
}

void series_set_stack_level (DATASET *dset, int i, int level)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->stack_level = level;
    }
}

void series_increment_stack_level (DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->stack_level += 1;
    }
}

void series_decrement_stack_level (DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->stack_level -= 1;
    }
}

void series_delete_metadata (DATASET *dset, int i)
{
    if (i > 0 && i < dset->v &&
	dset->varinfo != NULL &&
	dset->varinfo[i] != NULL) {
	dset->varinfo[i]->lag = 0;
	dset->varinfo[i]->transform = 0;
	dset->varinfo[i]->parent[0] = '\0';
    }
}

void series_ensure_level_zero (DATASET *dset)
{
    if (dset != NULL) {
	int i, n = 0;

	for (i=1; i<dset->v; i++) {
	    if (dset->varinfo[i]->stack_level > 0) {
		dset->varinfo[i]->stack_level = 0;
		n++;
	    }
	}
#if 0
	if (n > 0) {
	    fprintf(stderr, "Unauthorized access to series detected!\n");
	}
#endif
    }
}

void series_attach_string_table (DATASET *dset, int i, void *ptr)
{
    if (dset != NULL && i > 0 && i < dset->v) {
	series_set_discrete(dset, i, 1);
	dset->varinfo[i]->st = ptr;
    }
}

void series_destroy_string_table (DATASET *dset, int i)
{
    if (dset != NULL && i > 0 && i < dset->v) {
	series_table_destroy(dset->varinfo[i]->st);
	dset->varinfo[i]->st = NULL;
    }
}

/**
 * is_string_valued:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: 1 if series @i has a table of string values
 * (that is, a mapping from numerical values to associated
 * string values), otherwise 0.
 */

int is_string_valued (const DATASET *dset, int i)
{
    if (dset != NULL && i > 0 && i < dset->v) {
	return dset->varinfo[i]->st != NULL;
    } else {
	return 0;
    }
}

/**
 * series_get_string_table:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the string table attched to series @i or NULL if
 * there is no such table.
 */

series_table *series_get_string_table (const DATASET *dset, int i)
{
    if (dset != NULL && i > 0 && i < dset->v) {
	return dset->varinfo[i]->st;
    } else {
	return NULL;
    }
}

/**
 * series_get_string_for_obs:
 * @dset: pointer to dataset.
 * @i: index number of series.
 * @t: 0-based index of observation.
 *
 * Returns: the string associated with the numerical value of
 * series @i at observation @t, or NULL if there is no such string.
 */

const char *series_get_string_for_obs (const DATASET *dset, int i,
				       int t)
{
    const char *ret = NULL;

    if (i > 0 && i < dset->v && dset->varinfo[i]->st != NULL) {
	ret = series_table_get_string(dset->varinfo[i]->st,
				      dset->Z[i][t]);
    }

    return ret;
}

/**
 * series_get_string_for_value:
 * @dset: pointer to dataset.
 * @i: index number of series.
 * @val: the value to look up.
 *
 * Returns: the string associated with numerical value @val of
 * series @i, or NULL if there is no such string.
 */

const char *series_get_string_for_value (const DATASET *dset, int i,
					 double val)
{
    const char *ret = NULL;

    if (i > 0 && i < dset->v && dset->varinfo[i]->st != NULL) {
	ret = series_table_get_string(dset->varinfo[i]->st, val);
    }

    return ret;
}

/**
 * series_set_string_val:
 * @dset: pointer to dataset.
 * @i: index number of series.
 * @t: 0-based index of observation.
 * @s: the string value to set.
 *
 * Attempts to set the string value for observation @t of series @i
 * to @s. This will fail if the series in question does not have
 * an associated table of string values.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int series_set_string_val (DATASET *dset, int i, int t, const char *s)
{
    int err = 0;

    if (i <= 0 || i >= dset->v) {
	err = E_DATA;
    } else if (dset->varinfo[i]->st == NULL) {
	err = E_TYPES;
    } else {
	series_table *st = dset->varinfo[i]->st;
	double x = series_table_get_value(st, s);

	if (na(x)) {
	    int k = series_table_add_string(st, s);

	    if (k < 0) {
		err = E_ALLOC;
	    } else {
		dset->Z[i][t] = k;
	    }
	} else {
	    dset->Z[i][t] = x;
	}
    }

    return err;
}

/**
 * string_series_assign_value:
 * @dset: pointer to dataset.
 * @i: index number of string-valued series.
 * @t: 0-based index of observation.
 * @x: the numeric value to set.
 *
 * Attempts to set the value for observation @t of series @i
 * to @x. This will fail if the @x falls outside of the range
 * of values supported by the string table for the series.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int string_series_assign_value (DATASET *dset, int i,
				int t, double x)
{
    series_table *st = NULL;
    int err = 0;

    if (i <= 0 || i >= dset->v) {
	err = E_DATA;
    } else if (na(x)) {
	dset->Z[i][t] = x;
    } else if (x != floor(x)) {
	err = E_TYPES;
    } else if ((st = dset->varinfo[i]->st) == NULL) {
	err = E_TYPES;
    } else if (series_table_get_string(st, x) == NULL) {
	err = E_DATA;
    } else {
	dset->Z[i][t] = x;
    }

    return err;
}

/**
 * series_decode_string:
 * @dset: pointer to dataset.
 * @i: index number of series.
 * @s: string to decode.
 *
 * Returns: the numerical value associated with the string
 * @s for series @i, or #NADBL if there's no such value.
 */

double series_decode_string (const DATASET *dset, int i, const char *s)
{
    double ret = NADBL;

    if (i > 0 && i < dset->v && dset->varinfo[i]->st != NULL) {
	ret = series_table_get_value(dset->varinfo[i]->st, s);
    }

    return ret;
}

/**
 * series_get_string_vals:
 * @dset: pointer to dataset.
 * @i: index number of series.
 * @n_strs: location to receive the number of strings, or NULL.
 * @subsample: non-zero to restrict to current sample range.
 *
 * Returns: the array of strings associated with distinct numerical
 * values of series @i, or NULL if there's no such array. The returned
 * array should not be modified in any way; copy the strings first if
 * you need to modify them.
 */

char **series_get_string_vals (const DATASET *dset, int i,
			       int *n_strs, int subsample)
{
    char **strs = NULL;
    int n = 0;

    if (i > 0 && i < dset->v && dset->varinfo[i]->st != NULL) {
	strs = series_table_get_strings(dset->varinfo[i]->st, &n);
    }

    if (strs != NULL && subsample && dataset_is_subsampled(dset)) {
	static char **substrs = NULL;
	const double *x = dset->Z[i] + dset->t1;
	int T = dset->t2 - dset->t1 + 1;
	gretl_matrix *valid;
	int err = 0;

	if (substrs != NULL) {
	    free(substrs);
	    substrs = NULL;
	}
	valid = gretl_matrix_values(x, T, OPT_NONE, &err);
	if (err) {
	    strs = NULL;
	    n = 0;
	} else {
	    int j, k, nv = valid->rows;

	    substrs = strings_array_new(nv);
	    for (j=0; j<nv; j++) {
		k = gretl_vector_get(valid, j) - 1;
		substrs[j] = strs[k];
	    }
	    strs = substrs;
	    n = nv;
	    gretl_matrix_free(valid);
	}
    }

    if (n_strs != NULL) {
	*n_strs = n;
    }

    return strs;
}

/**
 * series_get_string_width:
 * @dset: pointer to dataset.
 * @i: index number of series.
 *
 * Returns: the maximum of (a) the number of characters in the
 * name of series @i and (b) the number of bytes in the longest
 * "string value" attached to series @i, if applicable; or 0
 * if @i is not a valid series index.
 */

int series_get_string_width (const DATASET *dset, int i)
{
    int n = 0;

    if (i > 0 && i < dset->v) {
	n = strlen(dset->varname[i]);
	if (dset->varinfo[i]->st != NULL) {
	    char **S;
	    int j, ns, m;

	    S = series_table_get_strings(dset->varinfo[i]->st, &ns);
	    for (j=0; j<ns; j++) {
		m = g_utf8_strlen(S[j], -1);
		if (m > n) {
		    n = m;
		}
	    }
	}
    }

    return n;
}

/**
 * steal_string_table:
 * @l_dset: pointer to recipient dataset.
 * @lvar: index number of target series.
 * @r_dset: pointer to donor dataset.
 * @rvar: index number of source series.
 *
 * Detaches the string table from @rvar in @r_dset and attaches it
 * to @lvar in @l_dset,
 *
 * Returns: 0 on success, non-zero code on error.
 */

int steal_string_table (DATASET *l_dset, int lvar,
			DATASET *r_dset, int rvar)
{
    if (l_dset != r_dset || lvar != rvar) {
	l_dset->varinfo[lvar]->st = r_dset->varinfo[rvar]->st;
	r_dset->varinfo[rvar]->st = NULL;
	series_set_discrete(l_dset, lvar, 1);
    }

    return 0;
}

/**
 * merge_string_tables:
 * @l_dset: pointer to current dataset.
 * @lvar: index number of target series.
 * @r_dset: pointer to dataset to be appended.
 * @rvar: index number of source series.
 *
 * Translates the encoding of the string-values of series @rvar
 * in @r_dset to that of series @lvar in @l_dset, adding extra
 * strings as needed.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int merge_string_tables (DATASET *l_dset, int lvar,
			 DATASET *r_dset, int rvar)
{
    series_table *lst = l_dset->varinfo[lvar]->st;
    double dx, *x = r_dset->Z[rvar];
    const char *sr;
    int t, idx, err = 0;

    for (t=0; t<r_dset->n && !err; t++) {
	if (na(x[t])) {
	    continue;
	}
	/* get the right-hand side string associated with x[t] */
	sr = series_get_string_for_value(r_dset, rvar, x[t]);
	/* and look up its numeric code on the left */
	dx = series_decode_string(l_dset, lvar, sr);
	if (!na(dx)) {
	    /* got a match: apply the code from @lst */
	    x[t] = dx;
	} else {
	    /* no match: we need to add a string to @lst */
	    idx = series_table_add_string(lst, sr);
	    if (idx < 0) {
		err = E_ALLOC;
	    } else {
		x[t] = (double) idx;
	    }
	}
    }

    return err;
}

static void maybe_adjust_label (DATASET *dset, int v,
				char **S, int ns)
{
    int i, len = 3 * ns; /* "=" + ", " */
    char *tmp;

    for (i=0; i<ns; i++) {
	len += strlen(S[i]) + 1 + floor(log10(1.0 + i));
    }

    /* let's not create a super-long series label */
    if (len > 255) {
	return;
    }

    tmp = calloc(len + 1, 1);

    if (tmp != NULL) {
	char bit[16];

	for (i=0; i<ns; i++) {
	    sprintf(bit, "%d=", i+1);
	    strcat(tmp, bit);
	    strcat(tmp, S[i]);
	    if (i < ns - 1) {
		strcat(tmp, ", ");
	    }
	}
	copy_label(&dset->varinfo[v]->label, tmp);
	free(tmp);
    }
}

/* Encode the strings in @a into numerical values in series
   @v of dataset @dset. "Return" via @pU the array of unique
   string values and via @pnu the number of such values.
*/

static int alt_set_strvals (DATASET *dset, int v, gretl_array *a,
			    char ***pU, int *pnu)
{
    char **S, **U = NULL;
    double *x = dset->Z[v];
    int i, pos, ns, nu = 0;
    int err = 0;

    S = gretl_array_get_strings(a, &ns);

    for (i=0; i<ns && !err; i++) {
	err = strings_array_add_uniq(&U, &nu, S[i], &pos);
	if (!err) {
	    x[i] = pos + 1;
	}
    }

    if (!err) {
	*pU = U;
	*pnu = nu;
    } else if (U != NULL) {
	strings_array_free(U, nu);
    }

    return err;
}

/* Recognize the case where we have an "empty" series
   and an array of strings of full dataset length.
*/

static int alt_strvals_case (DATASET *dset, int v, gretl_array *a)
{
    double *x = dset->Z[v];
    double x0 = dset->Z[v][0];
    int i, xconst = 1;

    for (i=1; i<dset->n && xconst; i++) {
	if (na(x0)) {
	    if (!na(x[i])) {
		xconst = 0;
	    }
	} else if (x[i] != x0) {
	    xconst = 0;
	}
    }

    return xconst && gretl_array_get_length(a) == dset->n;
}

/* here we're trying to set strings values on a series from
   scratch */

int series_set_string_vals (DATASET *dset, int i, void *ptr)
{
    gretl_array *a = ptr;
    gretl_matrix *vals = NULL;
    char **S = NULL;
    int ns = 0;
    int err = 0;

    if (a == NULL || dset == NULL || i < 1 || i >= dset->v) {
	return E_DATA;
    }

    if (0 && alt_strvals_case(dset, i, a)) {
	err = alt_set_strvals(dset, i, a, &S, &ns);
	if (err) {
	    return err;
	} else {
	    goto do_strtable;
	}
    }

    /* get sorted vector of unique values */
    vals = gretl_matrix_values(dset->Z[i], dset->n, OPT_S, &err);

    if (!err) {
	int i, nvals = gretl_vector_get_length(vals);
	double x0 = gretl_vector_get(vals, 0);
	double x1 = gretl_vector_get(vals, nvals - 1);

	if (x0 < 1.0) {
	    gretl_errmsg_set("The minimum value of the target series "
			     "must be >= 1");
	    err = E_DATA;
	} else {
	    /* the values should all be integers */
	    for (i=0; i<nvals && !err; i++) {
		x1 = gretl_vector_get(vals, i);
		if (x1 != floor(x1)) {
		    gretl_errmsg_set("The series values must be integers");
		    err = E_DATA;
		}
	    }
	}

	if (!err) {
	    S = gretl_array_get_stringify_strings(a, (int) x1, &ns, &err);
	}

	if (!err) {
	    /* the strings should all be UTF-8 */
	    for (i=0; i<ns && !err; i++) {
		if (!g_utf8_validate(S[i], -1, NULL)) {
		    gretl_errmsg_sprintf("String %d is not valid UTF-8", i+1);
		    err = E_DATA;
		}
	    }
	}
    }

 do_strtable:

    if (!err) {
	series_table *st = series_table_new(S, ns);

	if (st == NULL) {
	    err = E_ALLOC;
	} else {
	    if (dset->varinfo[i]->st != NULL) {
		/* remove any pre-existing table */
		series_table_destroy(dset->varinfo[i]->st);
	    }
	    series_set_discrete(dset, i, 1);
	    dset->varinfo[i]->st = st;
	    maybe_adjust_label(dset, i, S, ns);
	}
    }

    if (err && S != NULL && ns > 0) {
	strings_array_free(S, ns);
    }

    gretl_matrix_free(vals);

    return err;
}

/* The pre-checked case: we know that series @i is suitable
   for stringifying, and that @S contains the right number of
   strings.
*/

int series_set_string_vals_direct (DATASET *dset, int i,
				   char **S, int ns)
{
    series_table *st = series_table_new(S, ns);
    int err = 0;

    if (st == NULL) {
	err = E_ALLOC;
    } else {
	if (dset->varinfo[i]->st != NULL) {
	    /* remove any pre-existing table */
	    series_table_destroy(dset->varinfo[i]->st);
	}
	series_set_discrete(dset, i, 1);
	dset->varinfo[i]->st = st;
	maybe_adjust_label(dset, i, S, ns);
    }

    if (err && S != NULL && ns > 0) {
	strings_array_free(S, ns);
    }

    return err;
}

/**
 * series_recode_strings:
 * @dset: pointer to dataset.
 * @v: index number of target string-valued series.
 * @opt: may contain OPT_P (see below).
 * @changed: location to receive "changed" feedback, or NULL.
 *
 * This function "trims" the array of string values associated
 * with series @v so that it contains no redundant elements --
 * that is, values of which there is no instance in the
 * current sample -- and resets the numeric codes for the
 * strings if necessary.
 *
 * By default the original "series_table" attached to series @v
 * is destroyed, but if @opt contains OPT_P it is replaced but
 * not freed; this make sense only if another pointer to the
 * original table exists.
 *
 * If it happens that the current sample contains
 * instances of all the strings in the full dataset, this
 * function will not actually make any changes to @dset. The
 * @changed argument provides a means of determining
 * whether any change has been made.
 *
 * Returns: 0 on success, non-zero code on error.
 */

int series_recode_strings (DATASET *dset, int v, gretlopt opt,
			   int *changed)
{
    double *x = dset->Z[v] + dset->t1;
    int n = sample_size(dset);
    gretl_matrix *vals = NULL;
    gretl_matrix *repl = NULL;
    char **S = NULL;
    const char *si;
    int ns, nu = 0;
    int err = 0;

    if (changed != NULL) {
	*changed = 0;
    }

    ns = series_table_get_n_strings(dset->varinfo[v]->st);
    vals = gretl_matrix_values(x, n, OPT_NONE, &err);

    if (!err) {
	/* number of unique values */
	nu = vals->rows;
	if (nu == ns) {
	    /* nothing to be done */
	    gretl_matrix_free(vals);
	    return 0;
	}
	repl = gretl_zero_matrix_new(nu, 1);
	S = strings_array_new(nu);
	if (repl == NULL || S == NULL) {
	    free(S);
	    err = E_ALLOC;
	}
    }

    if (!err) {
	int i;

	for (i=0; i<nu; i++) {
	    si = series_get_string_for_value(dset, v, vals->val[i]);
	    S[i] = gretl_strdup(si);
	    repl->val[i] = i + 1;
	}

	substitute_values(x, x, n, vals->val, nu, repl->val, nu);

	if (!(opt & OPT_P)) {
	    series_table_destroy(dset->varinfo[v]->st);
	}
	/* the series table takes ownership of @S */
	dset->varinfo[v]->st = series_table_new(S, nu);

	if (changed != NULL) {
	    *changed = 1;
	}
    }

    gretl_matrix_free(vals);
    gretl_matrix_free(repl);

    return err;
}

int set_panel_groups_name (DATASET *dset, const char *vname)
{
    if (dset->pangrps != NULL) {
	free(dset->pangrps);
    }

    dset->pangrps = gretl_strdup(vname);

    return (dset->pangrps == NULL)? E_ALLOC : 0;
}

/* This should be called only after the "group names"
   property of @dset has been (recently) validated, via
   panel_group_names_ok().
*/

const char *get_panel_group_name (const DATASET *dset, int obs)
{
    const char *s = NULL;

    if (dataset_is_panel(dset) && dset->pangrps != NULL &&
	obs >= 0 && obs < dset->n) {
	int v = current_series_index(dset, dset->pangrps);
	series_table *st;

	if ((st = series_get_string_table(dset, v)) != NULL) {
	    s = series_table_get_string(st, dset->Z[v][obs]);
	}
    }

    return (s != NULL)? s : "??";
}

int panel_group_names_ok (const DATASET *dset, int maxlen)
{
    int ok = 0;

    if (dataset_is_panel(dset) && dset->pangrps != NULL) {
	int ns, v = current_series_index(dset, dset->pangrps);

	if (v > 0 && v < dset->v) {
	    char **S = series_get_string_vals(dset, v, &ns, 0);

	    if (S != NULL && ns >= dset->n / dset->pd) {
		ok = 1; /* provisional */
		if (maxlen > 0) {
		    int i;

		    for (i=0; i<ns; i++) {
			if (strlen(S[i]) > maxlen) {
			    ok = 0;
			    break;
			}
		    }
		}
	    }
	}
    }

    return ok;
}

const char *panel_group_names_varname (const DATASET *dset)
{
    if (dataset_is_panel(dset) && dset->pangrps != NULL) {
	int ns, v = current_series_index(dset, dset->pangrps);

	if (v > 0 && v < dset->v) {
	    char **S = series_get_string_vals(dset, v, &ns, 0);

	    if (S != NULL) {
		int ng = dset->n / dset->pd;

		if (ns >= ng) {
		    return dset->pangrps;
		}
	    }
	}
    }

    return NULL;
}

int is_panel_group_names_series (const DATASET *dset, int v)
{
    if (dataset_is_panel(dset) && dset->pangrps != NULL) {
	return v == current_series_index(dset, dset->pangrps);
    } else {
	return 0;
    }
}

static int suitable_group_names_series (const DATASET *dset,
					int maxlen,
					int exclude)
{
    int i, vfound = 0;

    for (i=1; i<dset->v && !vfound; i++) {
	if (i == exclude) {
	    continue;
	}
	if (is_string_valued(dset, i)) {
	    int ns = 0;
	    char **S = series_get_string_vals(dset, i, &ns, 0);

	    if (S != NULL && ns >= dset->n / dset->pd) {
		const char *sbak = NULL;
		int t, u, ubak = -1;
		int fail = 0;

		for (t=dset->t1; t<=dset->t2 && !fail; t++) {
		    const char *st = series_get_string_for_obs(dset, i, t);

		    u = t / dset->pd;
		    if (st == NULL || sbak == NULL) {
			fail = 1;
		    } else if (u == ubak && strcmp(st, sbak)) {
			/* same unit, different label: no */
			fail = 1;
		    } else if (ubak >= 0 && u != ubak && !strcmp(st, sbak)) {
			/* different unit, same label: no */
			fail = 2;
		    }
		    if (!fail && maxlen > 0 && strlen(st) > maxlen) {
			fail = 1;
		    }
		    ubak = u;
		    sbak = st;
		}
		if (!fail) {
		    vfound = i;
		}
	    }
	}
    }

    return vfound;
}

/* For plotting purposes, try to get labels for panel groups,
   subject to the constraint that they should be no longer
   than @maxlen. If successful, this will return an array of
   at least N strings, where N is the cross-sectional
   dimension of the panel. This array should be treated as
   read-only.
*/

series_table *get_panel_group_table (const DATASET *dset,
				     int maxlen, int *pv)
{
    series_table *st = NULL;
    int vpg = 0;

    if (dset->pangrps != NULL) {
	vpg = current_series_index(dset, dset->pangrps);
    }

    /* first see if we have valid group labels set explicitly */
    if (vpg > 0 && panel_group_names_ok(dset, maxlen)) {
	st = dset->varinfo[vpg]->st;
    }

    if (st == NULL) {
	/* can we find a suitable string-valued series? */
	int altv = suitable_group_names_series(dset, maxlen, vpg);

	if (altv > 0) {
	    vpg = altv;
	    st = dset->varinfo[vpg]->st;
	}
    }

    *pv = (st != NULL)? vpg : 0;

    return st;
}

int is_dataset_series (const DATASET *dset, const double *x)
{
    int i;

    for (i=dset->v-1; i>=0; i--) {
	if (x == dset->Z[i]) {
	    return 1;
	}
    }

    return 0;
}

static int effective_daily_skip (int delta, int wd, int pd)
{
    int k, skip = delta - 1;

    if (pd < 7) {
	skip = 0;
	for (k=1; k<delta; k++) {
	    wd = (wd == 0)? 6 : wd - 1;
	    if (pd == 6) {
		skip += (wd != 0);
	    } else {
		skip += (wd != 0 && wd != 6);
	    }
	}
    }

    return skip;
}

/* If we get here we've already checked that @dset is dated daily
   data, and that @pd is a valid daily periodicity greater than or
   equal to the current dset->pd.
*/

static int pad_daily_data (DATASET *dset, int pd, PRN *prn)
{
    DATASET *bigset = NULL;
    char datestr[OBSLEN];
    guint32 ed, ed0 = 0, edbak = 0;
    int wd, skip, totskip = 0;
    int t, err = 0;

    for (t=0; t<dset->n; t++) {
	ntolabel(datestr, t, dset);
	if (t == 0) {
	    ed0 = edbak = get_epoch_day(datestr);
	} else {
	    wd = weekday_from_date(datestr);
	    ed = get_epoch_day(datestr);
	    skip = effective_daily_skip(ed - edbak, wd, pd);
	    totskip += skip;
	    edbak = ed;
	}
    }

    if (totskip == 0) {
	pprintf(prn, "Dataset is already complete for %d-day calendar", pd);
	return 0;
    }

    bigset = create_new_dataset(dset->v, dset->n + totskip, NO_MARKERS);

    if (bigset == NULL) {
	err = E_ALLOC;
    } else {
	int i, s = 0;

	edbak = ed0;

	for (t=0; t<dset->n; t++) {
	    if (t > 0) {
		ntolabel(datestr, t, dset);
		wd = weekday_from_date(datestr);
		ed = get_epoch_day(datestr);
		s += 1 + effective_daily_skip(ed - edbak, wd, pd);
		edbak = ed;
	    }
	    for (i=1; i<dset->v; i++) {
		bigset->Z[i][s] = dset->Z[i][t];
	    }
	}

	bigset->varname = dset->varname;
	bigset->varinfo = dset->varinfo;
	bigset->descrip = dset->descrip;

	bigset->pd = pd;
	bigset->structure = TIME_SERIES;
	bigset->sd0 = (double) ed0;
	strcpy(bigset->stobs, dset->stobs);
	ntolabel(bigset->endobs, bigset->n - 1, bigset);

	dset->varname = NULL;
	dset->varinfo = NULL;
	dset->descrip = NULL;
	dataset_destroy_obs_markers(dset);
	free_Z(dset);
	clear_datainfo(dset, CLEAR_SUBSAMPLE);

	*dset = *bigset;
    }

    return err;
}

/* MIDAS-related functions */

/* postprocess: fill missing slots in daily data array
   with the period (month or quarter) average
   (FIXME support interpolation as an option?)
*/

int postprocess_daily_data (DATASET *dset, const int *list)
{
    double *x, xbar, xsum;
    int t, i, n_ok, n_miss;
    int err = 0;

    for (t=dset->t1; t<=dset->t2; t++) {
	xsum = 0.0;
	n_ok = n_miss = 0;
	for (i=1; i<=list[0]; i++) {
	    x = dset->Z[list[i]];
	    if (na(x[t])) {
		n_miss++;
	    } else {
		xsum += x[t];
		n_ok++;
	    }
	}
	if (n_miss > 0 && n_ok > 0) {
	    xbar = xsum / n_ok;
	    for (i=1; i<=list[0]; i++) {
		x = dset->Z[list[i]];
		if (na(x[t])) {
		    x[t] = xbar;
		}
	    }
	}
    }

    return err;
}

int series_get_midas_period (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	return dset->varinfo[i]->midas_period;
    }

    return 0;
}

void series_set_midas_period (const DATASET *dset, int i,
			      int period)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->midas_period = period;
    }
}

int series_get_midas_freq (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	return dset->varinfo[i]->midas_freq;
    }

    return 0;
}

int series_set_midas_freq (const DATASET *dset, int i,
			   int freq)
{
    int err = 0;

    if (i > 0 && i < dset->v) {
	if (freq < 5 || freq > 12) {
	    err = E_DATA;
	} else {
	    dset->varinfo[i]->midas_freq = freq;
	}
    } else {
	err = E_DATA;
    }

    return err;
}

int series_is_midas_anchor (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v &&
	(dset->varinfo[i]->flags & VAR_HFANCHOR)) {
	return dset->varinfo[i]->midas_period;
    }

    return 0;
}

void series_set_midas_anchor (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->flags |= VAR_HFANCHOR;
    }
}

/* end MIDAS-related functions */

int series_get_orig_pd (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	return dset->varinfo[i]->orig_pd;
    } else {
	return 0;
    }
}

void series_set_orig_pd (const DATASET *dset, int i, int pd)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->orig_pd = pd;
    }
}

void series_unset_orig_pd (const DATASET *dset, int i)
{
    if (i > 0 && i < dset->v) {
	dset->varinfo[i]->orig_pd = 0;
    }
}

void *series_info_bundle (const DATASET *dset, int i,
			  int *err)
{
    gretl_bundle *b = NULL;

    if (dset != NULL && i >= 0 && i < dset->v) {
	b = gretl_bundle_new();
	if (b == NULL) {
	    *err = E_ALLOC;
	}
    } else {
	*err = E_DATA;
    }

    if (b != NULL) {
	VARINFO *vinfo = dset->varinfo[i];

	gretl_bundle_set_string(b, "name", dset->varname[i]);
	if (vinfo->label != NULL) {
	    gretl_bundle_set_string(b, "description", vinfo->label);
	}
	if (vinfo->display_name[0] != '\0') {
	    gretl_bundle_set_string(b, "graph_name", vinfo->display_name);
	}
	gretl_bundle_set_int(b, "discrete", vinfo->flags & VAR_DISCRETE ?
			     1 : 0);
	gretl_bundle_set_int(b, "coded", vinfo->flags & VAR_CODED ?
			     1 : 0);
	gretl_bundle_set_string(b, "parent", vinfo->parent);
	if (vinfo->transform > 0) {
	    gretl_bundle_set_string(b, "transform",
				    gretl_command_word(vinfo->transform));
	} else {
	    gretl_bundle_set_string(b, "transform", "none");
	}
	gretl_bundle_set_int(b, "lag", vinfo->lag);
	gretl_bundle_set_int(b, "has_string_table", vinfo->st != NULL);
	if (vinfo->midas_period > 0) {
	    gretl_bundle_set_int(b, "midas_period", vinfo->midas_period);
	}
	if (vinfo->midas_freq > 0) {
	    gretl_bundle_set_int(b, "midas_freq", vinfo->midas_freq);
	}
	if (vinfo->orig_pd > 0) {
	    gretl_bundle_set_int(b, "orig_pd", vinfo->orig_pd);
	}
    }

    return b;
}

/* Given a series label @s, see if it can be recognized
   as identifying the series as the product of two others,
   and if so write the names of the others into @targ1
   and @targ2.
*/

static int get_interaction_names (const char *s,
				  char *targ1,
				  char *targ2)
{
    const char *p;
    int n1, n2, ret = 0;

    *targ1 = *targ2 = '\0';

    p = strchr(s, '*');
    if (p == NULL || strchr(p+1, '*') != NULL) {
	/* the label string does not contain a single '*' */
	return 0;
    }

    s += strspn(s, " ");
    n1 = gretl_namechar_spn(s);
    p++;
    p += strspn(p, " ");
    n2 = gretl_namechar_spn(p);

    if (n1 > 0 && n1 < VNAMELEN &&
	n2 > 0 && n2 < VNAMELEN) {
	strncat(targ1, s, n1);
	strncat(targ2, p, n2);
	ret = 1;
    }

    return ret;
}

/* Given a series label @s, see if it can be recognized as
   identifying the series as the square of another, and if
   so write the name of the other into @targ.
*/

static int get_square_parent_name (const char *s, char *targ,
				   char *targ2)
{
    const char *p;
    int n1, n2, ret = 0;

    *targ = '\0';

    if (*s == '=' && (p = strstr(s, "squared")) != NULL) {
	/* "= PARENT squared" */
	s++;
	s += strspn(s, " ");
	n1 = gretl_namechar_spn(s);
	n2 = p - s - 1;
	if (n1 > 0 && n1 < VNAMELEN && n2 == n1) {
	    strncat(targ, s, n1);
	    ret = 1;
	}
    } else if (strchr(s, '^') != NULL) {
	/* "PARENT^2" */
	n1 = gretl_namechar_spn(s);
	if (n1 > 0 && n1 < VNAMELEN) {
	    p = s + n1;
	    if (!strcmp(p, "^2")) {
		strncat(targ, s, n1);
		ret = 1;
	    }
	}
    } else if ((p = strstr(s, "square of ")) != NULL) {
	p += 9;
	p += strspn(p, " ");
	n1 = gretl_namechar_spn(p);
	if (n1 > 0 && n1 < VNAMELEN) {
	    strncat(targ, p, n1);
	    ret = 1;
	}
    } else if (get_interaction_names(s, targ, targ2)) {
	/* "x * x" ? */
	if (!strcmp(targ, targ2)) {
	    ret = 1;
	}
    }

    return ret;
}

/* Given either (a) two series identified by ID numbers
   i, j where the second is supposed to be the square
   of the first, or (b) three series i, j, k where the
   third is supposed to be the product of the first two,
   check that the putative relationship actually holds
   over the current sample range. Return 1 if so, else 0.
*/

static int validate_relationship (int i, int j, int k,
				  const DATASET *dset)
{
    double xi, xj;
    int t;

    for (t=dset->t1; t<=dset->t2; t++) {
	xi = dset->Z[i][t];
	xj = dset->Z[j][t];
	if (k > 0) {
	    /* interaction test: xk = xi*xj */
	    if (!na(xi) && !na(xj) && dset->Z[k][t] != xi*xj) {
		return 0;
	    }
	} else {
	    /* square test: xj = xi*xi */
	    if (!na(xi) && xj != xi*xi) {
		return 0;
	    }
	}
    }

    return 1;
}

/* In case we find more interaction terms that can be fitted into
   the current column-size of the "list info" matrix, add two more
   (since the encoding of each interaction for a given "primary"
   series requires two columns).
*/

static int resize_listinfo_matrix (gretl_matrix *m)
{
    int newc = m->cols + 2;
    int i, err = 0;

    err = gretl_matrix_realloc(m, m->rows, newc);
    if (!err) {
	for (i=0; i<m->rows; i++) {
	    gretl_matrix_set(m, i, newc-2, 0);
	    gretl_matrix_set(m, i, newc-1, 0);
	}
    }

    return err;
}

static int get_iact_column (gretl_matrix *m, int i, int *err)
{
    int j;

    for (j=3; j<m->cols; j+=2) {
	if (gretl_matrix_get(m, i, j) == 0) {
	    return j;
	}
    }

    /* looks like we need more columns */
    *err = resize_listinfo_matrix(m);
    return *err ? -1 : m->cols - 2;
}

/* The (optionally) "condensed" version of the listinfo_matrix
   includes only primary terms (and excludes the constant).
   The first column of the full matrix is replaced by the
   position in @list of each primary term.
*/

static int condense_listinfo_matrix (gretl_matrix *m,
				     const int *list,
				     const DATASET *dset)
{
    gretl_matrix *mc = NULL;
    char **S = NULL;
    double x;
    int i, j, ic, n = 0;

    for (i=0; i<m->rows; i++) {
	if (m->val[i] == 1) {
	    n++;
	}
    }

    if (n == m->rows) {
	/* nothing to be done */
	return 0;
    }

    mc = gretl_matrix_alloc(n, m->cols);
    if (mc == NULL) {
	return E_ALLOC;
    }

    S = strings_array_new(n);

    ic = 0;
    for (i=0; i<m->rows; i++) {
	if (m->val[i] == 1) {
	    gretl_matrix_set(mc, ic, 0, i+1);
	    for (j=1; j<m->cols; j++) {
		x = gretl_matrix_get(m, i, j);
		gretl_matrix_set(mc, ic, j, x);
	    }
	    S[ic] = gretl_strdup(dset->varname[list[i+1]]);
	    ic++;
	}
    }

    gretl_matrix_reuse(m, n, m->cols);
    gretl_matrix_copy_values(m, mc);
    gretl_matrix_free(mc);
    gretl_matrix_set_rownames(m, S);

    return 0;
}

static gretl_matrix *
linfo_matrix_via_labels (const int *list,
			 const DATASET *dset,
			 gretlopt opt,
			 int *err)
{
    gretl_matrix *ret = NULL;
    const char *label;
    char targ1[VNAMELEN];
    char targ2[VNAMELEN];
    int i, vi, j, vj;
    int pcol = 0, dcol = 1;
    int iacol, sqcol = 2;
    int n;

    if (list == NULL || list[0] == 0) {
	*err = E_DATA;
	return ret;
    }

    n = list[0];
    ret = gretl_zero_matrix_new(n, 5);
    if (ret == NULL) {
	*err = E_ALLOC;
	return ret;
    }

    for (i=1; i<=n && !*err; i++) {
	/* default to series is primary */
	gretl_matrix_set(ret, i-1, pcol, 1);
	vi = list[i];
	if (vi == 0) {
	    /* mark as non-primary and move on */
	    gretl_matrix_set(ret, i-1, pcol, 0);
	    continue;
	}
	if (gretl_isdummy(dset->t1, dset->t2, dset->Z[vi])) {
	    /* insert dummy flag in this row */
	    gretl_matrix_set(ret, i-1, dcol, 1);
	}
	label = series_get_label(dset, vi);
	if (label == NULL) {
	    continue;
	}
	if (get_square_parent_name(label, targ1, targ2)) {
	    /* looks like this could be a squared term */
	    for (j=1; j<=n; j++) {
		if (j == i) continue;
		vj = list[j];
		if (!strcmp(targ1, dset->varname[vj]) &&
		    validate_relationship(vj, vi, 0, dset)) {
		    /* mark this series as non-primary, and as square */
		    gretl_matrix_set(ret, i-1, pcol, 0);
		    gretl_matrix_set(ret, i-1, sqcol, j);
		    /* insert square ref in parent's row */
		    gretl_matrix_set(ret, j-1, sqcol, i);
		    break;
		}
	    }
	    continue;
	}
	if (get_interaction_names(label, targ1, targ2)) {
	    /* looks like this could be an interaction term */
	    int ia1 = 0, ia2 = 0;

	    for (j=1; j<=n; j++) {
		if (j == i) continue;
		vj = list[j];
		if (!strcmp(targ1, dset->varname[vj])) {
		    ia1 = j;
		} else if (!strcmp(targ2, dset->varname[vj])) {
		    ia2 = j;
		}
	    }
	    if (ia1 > 0 && ia2 > 0 &&
		validate_relationship(list[ia1], list[ia2], vi, dset)) {
		/* mark this series as non-primary, interaction */
		gretl_matrix_set(ret, i-1, pcol, 0);
		gretl_matrix_set(ret, i-1, 3, ia1);
		gretl_matrix_set(ret, i-1, 4, ia2);
		/* we may need to expand the number of columns */
		iacol = get_iact_column(ret, i, err);
		if (!*err) {
		    /* insert cross references in parents' rows */
		    gretl_matrix_set(ret, ia1-1, iacol, ia2);
		    gretl_matrix_set(ret, ia1-1, iacol+1, i);
		    gretl_matrix_set(ret, ia2-1, iacol, ia1);
		    gretl_matrix_set(ret, ia2-1, iacol+1, i);
		}
	    }
	}
    }

    if (*err) {
	gretl_matrix_free(ret);
	ret = NULL;
    } else if (opt & OPT_C) {
	condense_listinfo_matrix(ret, list, dset);
    } else {
	/* convenience: attach series names to rows */
	char **S;
	int serr = 0;

	S = gretl_list_get_names_array(list, dset, &serr);
	if (S != NULL) {
	    gretl_matrix_set_rownames(ret, S);
	}
    }

    return ret;
}

static gretl_matrix *
linfo_matrix_via_data (const int *list,
		       const DATASET *dset,
		       gretlopt opt,
		       int *err)
{
    gretl_matrix *ret = NULL;
    int i, vi, j, vj, k, vk;
    int pcol = 0, dcol = 1;
    int iacol, sqcol = 2;
    int n;

    if (list == NULL || list[0] == 0) {
	*err = E_DATA;
	return ret;
    }

    n = list[0];
    ret = gretl_zero_matrix_new(n, 5);
    if (ret == NULL) {
	*err = E_ALLOC;
	return ret;
    }

    for (i=1; i<=n && !*err; i++) {
	int matched = 0;

	/* default to series is primary */
	gretl_matrix_set(ret, i-1, pcol, 1);
	vi = list[i];
	if (vi == 0) {
	    /* mark as non-primary and move on */
	    gretl_matrix_set(ret, i-1, pcol, 0);
	    continue;
	}
	if (gretl_isdummy(dset->t1, dset->t2, dset->Z[vi])) {
	    /* insert dummy flag in this row */
	    gretl_matrix_set(ret, i-1, dcol, 1);
	}
	for (j=1; j<=n && !matched; j++) {
	    vj = list[j];
	    if (j == i || vj == 0) continue;
	    if (validate_relationship(vj, vi, 0, dset)) {
		/* mark this series as non-primary, square */
		gretl_matrix_set(ret, i-1, pcol, 0);
		gretl_matrix_set(ret, i-1, sqcol, j);
		/* insert square ref in parent's row */
		gretl_matrix_set(ret, j-1, sqcol, i);
		matched = 1;
	    }
	    for (k=1; k<=n && !matched; k++) {
		vk = list[k];
		if (k == i || k == j || vk == 0) continue;
		if (validate_relationship(vj, vk, vi, dset)) {
		    /* mark this series as non-primary, interaction */
		    gretl_matrix_set(ret, i-1, pcol, 0);
		    gretl_matrix_set(ret, i-1, 3, j);
		    gretl_matrix_set(ret, i-1, 4, k);
		    /* we may need to expand the number of columns */
		    iacol = get_iact_column(ret, i, err);
		    if (!*err) {
			/* insert cross references in parents' rows */
			gretl_matrix_set(ret, j-1, iacol, k);
			gretl_matrix_set(ret, j-1, iacol+1, i);
			gretl_matrix_set(ret, k-1, iacol, j);
			gretl_matrix_set(ret, k-1, iacol+1, i);
		    }
		    matched = 1;
		}
	    }
	}
    }

    if (*err) {
	gretl_matrix_free(ret);
	ret = NULL;
    } else if (opt & OPT_C) {
	condense_listinfo_matrix(ret, list, dset);
    } else {
	/* convenience: attach series names to rows */
	char **S;
	int serr = 0;

	S = gretl_list_get_names_array(list, dset, &serr);
	if (S != NULL) {
	    gretl_matrix_set_rownames(ret, S);
	}
    }

    return ret;
}

/* Construct a matrix providing information about the relations
   between the series in @list. This will have rows equal to the
   number of series and at least 5 columns (shown as 1-based here).
   All elements of the matrix are zero unless otherwise specified.

   col 1: Holds 1 if the series is "primary" (neither the square
   of another series in the list, nor the interaction of two
   series in the list).

   col 2: Holds 1 if the series is a 0/1 dummy.

   col 3: If the series is primary and its square is also
   present in the list, holds the list position of the square,
   or if the series itself is a squared term, holds the list
   position of the series of which it's the square.

   cols 4, 5: If the series features in an interaction term,
   col 4 holds the list position of its "partner" and col 5 the
   list position of the interaction term. If the series features
   in more than one interaction term, subsequent interaction info
   goes into cols 6 and 7 or higher (these being added as required).
   If the series itself is an interaction term, cols 4 and 5 get
   the list positions of the two source series.
*/

void *list_info_matrix (const int *list, const DATASET *dset,
			gretlopt opt, int *err)
{
    if (opt & OPT_B) {
	return linfo_matrix_via_data(list, dset, opt, err);
    } else {
	return linfo_matrix_via_labels(list, dset, opt, err);
    }
}
