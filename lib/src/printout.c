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
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA.
 *
 */

/*  printout.c - simple text print routines for some gretl structs */ 

#include "libgretl.h"
#include "internal.h"
#include "version.h"
#include <time.h>

#ifdef OS_WIN32
#define isnan(x) ((x) != (x))
#endif

static void print_coeff_interval (const DATAINFO *pdinfo, const MODEL *pmod, 
				  int c, double t, PRN *prn);
void _mxout (const double *rr, const int *list, int ci,
	     const DATAINFO *pdinfo, int pause, PRN *prn);


/* ........................................................ */
  
void _bufspace (int n, PRN *prn)
{
    if (n > 0) while (n--) pprintf(prn, " ");
}

/**
 * printxx:
 * @xx: number to print.
 * @str: buffer into which to print.
 * @ci: command index (PRINT or SUMMARY).
 *
 * Print a string representation of the double-precision value @xx
 * to the buffer @str, in a format that depends on @ci.
 */

void printxx (const double xx, char *str, int ci)
{
    int d = 6;

    switch (ci) {
    case PRINT:
	d = 8;  
	break;
    case SUMMARY:
	d = 6;
	break;
    default:
	break;
    }

    sprintf(str, "%#*.*g", d, GRETL_DIGITS, xx);
}

/* ......................................................... */ 

static void covhdr (PRN *prn)
{
    pprintf(prn, "\n%s\n\n", _("COVARIANCE MATRIX OF REGRESSION COEFFICIENTS"));
}

/**
 * session_time:
 * @fp: stream onto which to print.
 *
 * Print the current time to the specified stream.
 */

void session_time (FILE *fp)
{
    time_t runtime = time(NULL);

    fprintf(fp, _("Current session: %s"), ctime(&runtime));
}

/**
 * logo:
 *
 * Print to stdout gretl version information.
 */

void logo (void)
{
    printf(_("gretl client, for library version %s,\n"), version_string);
    puts(_("copyright Ramu Ramanathan and Allin Cottrell."));
    puts(_("This is free software with ABSOLUTELY NO WARRANTY."));
}

/**
 * gui_logo:
 * @fp: stream onto which to print.
 *
 * Print gretl GUI version information to the specified stream.
 */

void gui_logo (FILE *fp)
{
    fprintf(fp, _("gretl: gui client for gretl version %s,\n"), version_string);
    fputs(_("copyright Allin Cottrell.\n"), fp);
    fputs(_("This is free software with ABSOLUTELY NO WARRANTY.\n"), fp);
}

/**
 * print_model_confints:
 * @pmod: pointer to gretl model.
 * @pdinfo: data information struct.
 * @prn: gretl printing struct.
 *
 * Print to @prn the 95 percent confidence intervals for the parameter
 * estimates in @pmod.
 */

void print_model_confints (const MODEL *pmod, const DATAINFO *pdinfo, 
			   PRN *prn)
{
    int i, ncoeff = pmod->list[0];
    double t = _tcrit95(pmod->dfd);

    pprintf(prn, "t(%d, .025) = %.3f\n\n", pmod->dfd, t);
    pprintf(prn, _("      VARIABLE      COEFFICIENT      95%% CONFIDENCE "
	    "INTERVAL\n\n"));      

    if (pmod->ifc) {
	print_coeff_interval(pdinfo, pmod, ncoeff, t, prn);
	ncoeff--;
    }

    for (i=2; i<=ncoeff; i++) {
	print_coeff_interval(pdinfo, pmod, i, t, prn);
    }

    pprintf(prn, "\n");
}

/* ........................................................... */

void gretl_print_add (const COMPARE *add, const int *addvars, 
		      const DATAINFO *pdinfo, int aux_code, PRN *prn)
{
    int i;
    char spc[3];

    if (aux_code != AUX_SQ && aux_code != AUX_LOG) {
	strcpy(spc, "  ");
	pprintf(prn, _("Comparison of Model %d and Model %d:\n"), 
		add->m1, add->m2);
    } else spc[0] = '\0';

    if (aux_code == AUX_ADD && addvars[0] > 1 && add->ols) {
	pprintf(prn, _("\n%sNull hypothesis: the regression parameters are "
		"zero for the added variables\n\n"), spc);
	for (i = 1; i<=addvars[0]; i++) 
	    pprintf(prn, "%s  %s\n", spc, pdinfo->varname[addvars[i]]);	
	pprintf(prn, "\n  %s: F(%d, %d) = %f, ", _("Test statistic"), 
		add->dfn, add->dfd, add->F);
	pprintf(prn, _("with p-value = %f\n"), 
		fdist(add->F, add->dfn, add->dfd));
    }
    else if (aux_code == AUX_ADD && addvars[0] > 1 && add->discrete) {
	pprintf(prn, _("\n%sNull hypothesis: the regression parameters are "
		"zero for the added variables\n\n"), spc);
	for (i = 1; i<=addvars[0]; i++) 
	    pprintf(prn, "%s  %s\n", spc, pdinfo->varname[addvars[i]]);	
	pprintf(prn, "\n  %s: %s(%d) = %f, ", _("Test statistic"), 
		_("Chi-square"), add->dfn, add->chisq);
	pprintf(prn, _("with p-value = %f\n\n"), 
		chisq(add->chisq, add->dfn));
	return;
    }
    else if (aux_code == AUX_SQ || aux_code == AUX_LOG) {
	pprintf(prn, "\n%s: TR^2 = %f,\n", _("Test statistic"), add->trsq);
	pprintf(prn, _("with p-value = prob(Chi-square(%d) > %f) = %f\n\n"), 
		add->dfn, add->trsq, chisq(add->trsq, add->dfn));
	return;
    }
    pprintf(prn, _("%sOf the 8 model selection statistics, %d "), 
	    spc, add->score);
    if (add->score == 1) pprintf(prn, _("has improved.\n"));
    else pprintf(prn, _("have improved.\n\n"));
}

/* ........................................................... */

void gretl_print_omit (const COMPARE *omit, const int *omitvars, 
		       const DATAINFO *pdinfo, PRN *prn)
{
    int i;

    pprintf(prn, _("Comparison of Model %d and Model %d:\n\n"),
	    omit->m1, omit->m2);
    if (omit->ols && omit->dfn > 0 && omitvars[0] > 1) {
	pprintf(prn, _("  Null hypothesis: the regression parameters "
		"are zero for the variables\n\n"));
	for (i = 1; i<=omitvars[0]; i++) {
	    pprintf(prn, "    %s\n", pdinfo->varname[omitvars[i]]);	
	} 
	pprintf(prn, "\n  %s: F(%d, %d) = %f, ", _("Test statistic"), 
		omit->dfn, omit->dfd, omit->F);
	pprintf(prn, _("with p-value = %f\n"), 
		fdist(omit->F, omit->dfn, omit->dfd));
    }
    else if (omit->discrete && omit->dfn > 0 && omitvars[0] > 1) {
	pprintf(prn, _("  Null hypothesis: the regression parameters "
		"are zero for the variables\n\n"));
	for (i = 1; i<=omitvars[0]; i++) {
	    pprintf(prn, "    %s\n", pdinfo->varname[omitvars[i]]);	
	} 
	pprintf(prn, "\n  %s: %s(%d) = %f, ",  _("Test statistic"),
		_("Chi-square"), omit->dfn, omit->chisq);
	pprintf(prn, _("with p-value = %f\n\n"), 
		chisq(omit->chisq, omit->dfn));
	return;
    } 
    pprintf(prn, _("  Of the 8 model selection statistics, %d %s\n\n"), 
	    omit->score, (omit->score == 1)? 
	    _("has improved") : _("have improved"));
}

/* ........................................................ */

static int make_list (int **plist, const DATAINFO *pdinfo)
{
    int i, n = 1;
    int *trylist;

    trylist = malloc(pdinfo->v * sizeof *trylist);
    if (trylist == NULL) return 1;
    for (i=1; i<pdinfo->v; i++) {
	if (hidden_var(i, pdinfo)) continue;
	if (pdinfo->vector[i] == 0) continue;
	trylist[n++] = i;
    }
    trylist[0] = n - 1;
    *plist = trylist;
    return 0;
}

/**
 * printcorr:
 * @corrmat: gretl correlation matrix struct.
 * @pdinfo: data information struct.
 * @prn: gretl printing struct.
 *
 * Print correlation matrix to @prn in a simple columnar format.
 * 
 */

void printcorr (const CORRMAT *corrmat, const DATAINFO *pdinfo, 
		PRN *prn)
{
    int i = 1, j, k = 0, m, ncoeffs;
    char corrstring[25];

    m = corrmat->list[0];
    ncoeffs = (m * (m + 1))/2;

    pprintf(prn, _("\nPairwise correlation coefficients:\n\n"));
    while (k < ncoeffs) {
        for (i=1; i<=m; i++) {
	    k++;
	    for (j=i+1; j<=m; j++) {
		sprintf(corrstring, "corr(%s, %s)", 
			pdinfo->varname[corrmat->list[i]], 
			pdinfo->varname[corrmat->list[j]]);
		if (na(corrmat->xpx[k]))
		    pprintf(prn, "  %-24s    %s\n", 
			    corrstring, _("undefined"));
		
		else if (corrmat->xpx[k] < 0.) 
		    pprintf(prn, "  %-24s = %.3f\n", corrstring, 
			    corrmat->xpx[k]);
		else 
		    pprintf(prn, "  %-24s =  %.3f\n", corrstring, 
			    corrmat->xpx[k]);
		k++;
	    }
        }
    }
    pprintf(prn, "\n");
}

/**
 * printfreq:
 * @freq: gretl frequency distribution struct.
 * @prn: gretl printing struct.
 *
 * Print frequency distribution to @prn.
 * 
 */

void printfreq (FREQDIST *freq, PRN *prn)
{
    int i, k, nlw, K = freq->numbins - 1;
    char word[32];

    pprintf(prn, _("\nFrequency distribution for %s, obs %d-%d "
	    "(%d valid observations)\n"),
	   freq->varname, freq->t1 + 1, freq->t2 + 1, freq->n);
    pprintf(prn, _("number of bins = %d, mean = %.3f, sd = %.3f\n"), 
	   freq->numbins, freq->xbar, freq->sdx);
    pprintf(prn, _("\n       interval          midpt      frequency\n\n"));

    for (k=0; k<=K; k++) {
	*word = '\0';
	if (k == 0) pprintf(prn, "          <  ");
	else if (k == K) pprintf(prn, "          >= ");
	else pprintf(prn, "%10.3g - ", freq->endpt[k]);
	if (k == K) sprintf(word, "%.3g", freq->endpt[k]);
	else sprintf(word, "%.3g", freq->endpt[k+1]);
	pprintf(prn, "%s", word);
	nlw = 10 - strlen(word);
	_bufspace(nlw, prn);
	sprintf(word, " %.3g", freq->midpt[k]);
	pprintf(prn, "%s", word);
	nlw = 10 - strlen(word);
	_bufspace(nlw, prn);
	pprintf(prn, "%6d  ", freq->f[k]);
	i = 36.0 * freq->f[k]/freq->n;
	while (i--) pprintf(prn, "*");
	pprintf(prn, "\n");
    }

    pprintf(prn, "\n%s:\n", _("Test for null hypothesis of normal distribution"));
    
    pprintf(prn, "%s(2) = %.3f %s %.5f\n", 
	    _("Chi-square"), freq->chisqu, 
	    _("with p-value"), chisq(freq->chisqu, 2));
}

/**
 * print_smpl:
 * @pdinfo: data information struct
 * @fulln: full length of data series.
 * @prn: gretl printing struct.
 *
 * Print current sample information to @prn.
 * 
 */

void print_smpl (const DATAINFO *pdinfo, int fulln, PRN *prn)
{
    char date1[9], date2[9];

    if (fulln) {
	pprintf(prn, _("Full data set: %d observations\n"
		"Current sample: %d observations\n"), 
		fulln, pdinfo->n);
	return;
    }

    ntodate(date1, pdinfo->t1, pdinfo);
    ntodate(date2, pdinfo->t2, pdinfo);
    pprintf(prn, "%s: %s - %s (n = %d)\n", _("Full data range"), 
	    pdinfo->stobs, pdinfo->endobs, pdinfo->n);
    pprintf(prn, "%s:  %s - %s", ("Current sample"), date1, date2);
    if (pdinfo->t1 == 0 && pdinfo->t2 == pdinfo->n - 1) 
	pprintf(prn, "\n");
    else pprintf(prn, " (n = %d)\n", pdinfo->t2 - pdinfo->t1 + 1);  
}

/* ......................................................... */

/* Some C libraries (e.g. MS) print an "extra" zero in the exponent
   when using scientific notation, e.g. "1.45E-002".  The following 
   function checks for this and cuts it out if need be. */ 

static void fix_exponent (char *s)
{
    char *p;
    int k;

    if ((p = strstr(s, "+00")) || (p = strstr(s, "-00"))) {
	if (sscanf(p + 1, "%d", &k) == 1)
	    sprintf(p + 1, "0%d", k);
    }
}

/* For some reason sprintf using "%#G" seems to stick an extra
   zero on the end of some numbers -- i.e. when using a precision
   of 6 you can get a result of "1.000000", with 6 trailing
   zeros.  The following function checks for this and lops it
   off if need be. */

static void cut_extra_zero (char *numstr, char decpoint, int digits)
{
    char *p, tmp[16];

    *tmp = decpoint;
    memset(tmp + 1, '0', digits);
    tmp[digits + 1] = '\0';

    if ((p = strstr(numstr, tmp))) {
	tmp[digits] = '\0';
	strcpy(p, tmp);
    }
}

/* The following function formats a double in such a way that the
   decimal point will be printed in the same position for all
   numbers printed this way.  The total width of the number
   string (including possible padding on left or right) is 
   2*P + 4 characters, where P denotes the precision ("digits"). 
*/

void gretl_print_fullwidth_double (double x, int digits, PRN *prn)
{
    char numstr[32], final[32];
    char *p;
    size_t i, tmp, forept = 0;
    char decpoint = '.';

#ifdef ENABLE_NLS
    decpoint = get_local_decpoint();
#endif

    sprintf(numstr, "%#.*G", digits, x);
    fix_exponent(numstr);

    p = strchr(numstr, decpoint);
    if (p != NULL) forept = p - numstr;
    tmp = digits - forept;
    *final = 0;
    for (i=0; i<tmp; i++) strcat(final, " ");

    tmp = strlen(numstr) - 1;
    if (numstr[tmp] == decpoint) numstr[tmp] = 0;
    cut_extra_zero(numstr, decpoint, digits);

    strcat(final, numstr);

    tmp = 2 * digits + 4 - strlen(final);
    for (i=0; i<tmp; i++) strcat(final, " ");

    pprintf(prn, "%s", final);
}

/* ......................................................... */ 

void gretl_print_value (double x, PRN *prn)
{
    gretl_print_fullwidth_double(x, GRETL_DIGITS, prn);  
}

/* ......................................................... */ 

static void print_coeff_interval (const DATAINFO *pdinfo, const MODEL *pmod, 
				  int c, double t, PRN *prn)
{
    double maxerr;

    pprintf(prn, " %3d) %8s ", pmod->list[c], 
	   pdinfo->varname[pmod->list[c]]);

    _bufspace(3, prn);

    if (isnan(pmod->coeff[c-1])) {
	pprintf(prn, "%16s", _("undefined"));
    } else {
	gretl_print_value (pmod->coeff[c-1], prn);
    }

    _bufspace(2, prn);

    if (isnan(pmod->sderr[c-1])) {
	pprintf(prn, "%10s", _("undefined"));
    } else {
	maxerr = (pmod->sderr[c-1] > 0)? t * pmod->sderr[c-1] : 0;
	pprintf(prn, " (%#.*g, %#.*g)", 
		GRETL_DIGITS, pmod->coeff[c-1] - maxerr,
		GRETL_DIGITS, pmod->coeff[c-1] + maxerr);
    }
    pprintf(prn, "\n");
}

/**
 * outcovmx:
 * @pmod: pointer to model.
 * @pdinfo: data information struct.
 * @pause: if non-zero, pause after displaying each screen of information.
 * @prn: gretl printing struct.
 * 
 * Print to @prn the variance-covariance matrix for the parameter
 * estimates in @pmod.
 *
 * Returns: 0 on successful completion, error code on error.
 */

int outcovmx (MODEL *pmod, const DATAINFO *pdinfo, int pause, 
	      PRN *prn)
{
    int k, nbetas;
    int *tmplist = NULL;

    nbetas = pmod->list[0] - 1;
    if (copylist(&tmplist, pmod->list)) return E_ALLOC;
    for (k=1; k<=nbetas; k++) tmplist[k] = pmod->list[k+1];
    tmplist[0] = nbetas;

    if (pmod->vcv == NULL && makevcv(pmod)) return E_ALLOC;
    _mxout(pmod->vcv, tmplist, pmod->ci, pdinfo, pause, prn);  

    free(tmplist);
    return 0;
}

/**
 * print_white_vcv:
 * @pmod: pointer to model.
 * @prn: gretl printing struct.
 * 
 * Print to @prn White's heteroskedasticity-adjusted variance-covariance 
 * matrix for the parameter estimates in @pmod.
 *
 */

void print_white_vcv (const MODEL *pmod, PRN *prn)
{
    int i, j, index, ncoeff;

    ncoeff = pmod->list[0] - 1;
    covhdr(prn);
    index = 1;
    for (i=1; i<=ncoeff; i++) { 
	for (j=i; j<=ncoeff; j++) {
	    pprintf(prn, "\tCov(%3d, %3d) = %15g\n",
                   pmod->list[i+1], pmod->list[j+1], pmod->vcv[index]);
	    index++;
	}
    }
    pprintf(prn, "\n");
}

/* ......................................................... */ 

static void outxx (const double xx, int ci, PRN *prn)
{
    if (ci == CORR) {
	if (na(xx)) pprintf(prn, " %13s", _("undefined"));
	else pprintf(prn, " %13.4f", xx);
    } else {
	if (xx > -0.001 && xx < 0.001)
	    pprintf(prn, " %13e", xx);
	else pprintf(prn, " %13g", xx);
    }
}

static int takenotes (int quit_option)
{
    char s[4];

    if (quit_option)
	puts(_("\nTake notes then press return key to continue (or q to quit)"));
    else
	puts(_("\nTake notes then press return key to continue"));
    fflush(stdout);
    fgets(s, 3, stdin);
    if (quit_option && s[0] == 'q') return 1;
    return 0;
}

/**
 * page_break:
 * @n: line offset (will be added to *lineno).
 * @lineno: pointer to line number (or NULL).
 * @quit_option: if non-zero, give the user the option of quitting.
 * 
 * Break "page" when printing a large amount of information.
 * 
 * Returns: 1 if @quit_option is non-zero and the user chose to quit,
 * otherwise 0.
 */

int page_break (int n, int *lineno, int quit_option)
{
    if (lineno != NULL && *lineno + n <= 20) return 0;
    if (takenotes(quit_option)) return 1;
    if (lineno != NULL) *lineno = 1;
    return 0;
}

/* ........................................................ */

void _mxout (const double *rr, const int *list, int ci,
	     const DATAINFO *pdinfo, int pause, PRN *prn)
     /*  Given a single dimensional array, which represents a
	 symmetric matrix, prints out an upper triangular matrix
	 of any size. 

	 Due to screen and printer column limitations the program breaks up
	 a large upper triangular matrix into 5 variables at a time. For
	 example, if there were 10 variables the program would first print
	 an upper triangular matrix of the first 5 rows and columns, then
	 it would print a rectangular matrix of the first 5 rows but now
	 columns 6 - 10, and finally an upper triangular matrix of rows 6
	 - 10 and columns 6 - 10
     */
{
    register int i, j;
    int lo, ljnf, nf, li2, p, k, index, ij2, lineno = 0;
    char s[16];
    enum { FIELDS = 5 };

    if (ci != CORR) covhdr(prn);

    lo = list[0];
    for (i=0; i<=lo/FIELDS; i++) {
	nf = i * FIELDS;
	li2 = lo - nf;
	p = (li2 > FIELDS) ? FIELDS : li2;
	if (p == 0) break;
	if (pause) page_break(3, &lineno, 0);

	/* print the varname headings */
	for (j=1; j<=p; ++j)  {
	    ljnf = list[j + nf];
	    strcpy(s, pdinfo->varname[ljnf]);
	    _bufspace(9 - strlen(s), prn);
	    pprintf(prn, "%3d) %s", ljnf, s);
	}
	pprintf(prn, "\n");
	lineno += 2;

	/* print rectangular part, if any, of matrix */
	for (j=1; j<=nf; j++) {
	    if (pause) page_break(1, &lineno, 0);
	    lineno++;
	    for (k=1; k<=p; k++) {
		index = ijton(j, nf+k, lo);
		outxx(rr[index], ci, prn);
	    }
	    pprintf(prn, "   (%d\n", list[j]);
	}

	/* print upper triangular part of matrix */
	for (j=1; j<=p; ++j) {
	    if (pause) page_break(1, &lineno, 0);
	    lineno++;
	    ij2 = nf + j;
	    _bufspace(14 * (j - 1), prn);
	    for (k=j; k<=p; k++) {
		index = ijton(ij2, nf+k, lo);
		outxx(rr[index], ci, prn);
	    }
	    pprintf(prn, "   (%d\n", list[ij2]);
	}
	pprintf(prn, "\n");
    }
}


/* ........................................................ */

static void printgx (const double xx, PRN *prn)
{
    static char word[32];
    int lw;

    sprintf(word, "%11g", xx);
    lw = strlen(word);
    pprintf(prn, "%s", word);
    _bufspace(13 - lw, prn);
} 

/* ........................................................ */

void _graphyzx (const int *list, const double *zy1, const double *zy2, 
		const double *zx, int n, const char *yname, 
		const char *xname, const DATAINFO *pdinfo, 
		int oflag, PRN *prn)
/*
  if n > 0 graphs zy1 against zx, otherwise
  graphs zy1[i] and zy2[i] against zx[i] for i = 1, 2, .... n
  no of rows = 40 if oflag = 1, else it is = 18 or 16
*/
{
    register int i, j;
    int ix, iy1, iy2, lx, ly, xzero, yzero, nrows, nr2, ncols, nc2,
	ls, lw, t1, t2, option = 0;
    double xmin, xmax, xrange, ymin, ymax, yrange, y1min, y1max; 
    double xx, y2min, y2max;
    char p[41][132];
    static char word[32];

    if (pdinfo != NULL) {
	t1 = pdinfo->t1;
	t2 = pdinfo->t2;
    } else {
	t1 = 0;
	t2 = (n < 0)? -n - 1 : n - 1;
    }

    if (n < 0) {
	n = -n;
	option = 1;
	_minmax(t1, t2, zy1, &y1min, &y1max);
	_minmax(t1, t2, zy2, &y2min, &y2max);
	ymin = (y1min < y2min)? y1min : y2min;
	ymax = (y1max > y2max)? y1max : y2max;
    }
    else _minmax(t1, t2, zy1, &ymin, &ymax);
    yrange = ymax - ymin;
    xzero = yzero = 0;
    /* setting the number of columns and rows to be used */
    ncols = 60;
    if (oflag == OPT_O) nrows = 40;
    else nrows = option ? 16 : 18 ;
    nr2 = nrows/2;
    nc2 = ncols/2;
    _minmax(t1, t2, zx, &xmin, &xmax);
    xrange = xmax - xmin;

    /* Initialize picture matrix */
    for (i=0; i<=nrows; ++i) {
	p[i][0] = (i%5 == 0)? '+' : '|'; 
	for (j=1; j<=ncols+1; j++) p[i][j] = ' ';
    }
    /*
      if min is < 0 and max > 0, draw line at zero value
    */
    if (xmin <0 && xmax >0) {
	xzero = 0.5 -1.0*xmin*ncols/xrange;
	for (i=0; i<=nrows; i++) p[i][xzero+1] = '|';
    }
    if (ymin <0 && ymax >0) {
	yzero = 0.5 -1.0*ymin*nrows/yrange;
	for (j=0; j<=ncols; j++) p[yzero][j+1] = '-';
    }
    /*  loop replaces blanks in PICTURE with o's that correspond to the
	scaled values of the specified variables */
    if (option) for (i=0; i<n; ++i) {
	ix = (floatneq(xrange, 0.0))? 
	    ((zx[i] - xmin)/xrange)*ncols : nc2;
	iy1 = (floatneq(yrange, 0.0))? 
	    ((zy1[i] - ymin)/yrange)*nrows : nr2;
	iy2 = (floatneq(yrange, 0.0))? 
	    ((zy2[i] - ymin)/yrange)*nrows : nr2;
	if (iy1 != iy2) {
	    p[iy1][ix+1] = 'o';
	    p[iy2][ix+1] = 'x';
	}
	else p[iy1][ix+1] = '+';
    }
    else for (i=0; i<n; ++i) {
	ix = (floatneq(xrange, 0.0))? 
	    ((zx[i] - xmin)/xrange)*ncols : nc2;
	iy1 = (floatneq(yrange, 0.0))? 
	    ((zy1[i] - ymin)/yrange)*nrows : nr2;
	p[iy1][ix+1] = 'o';
    }

    /* loop prints out the matrix PICTURE that is stored in the
       2-dimensional p matrix. */
    if (!option) pprintf(prn, "%14s\n", yname);
    else if (list) 
	pprintf(prn, _("%7co stands for %s and x stands for %s (+ means they "
		"are equal)\n\n%9s, %s\n"), ' ', 
		yname, pdinfo->varname[list[2]], yname, 
		pdinfo->varname[list[2]]);
    for (i=nrows; i>=0; --i) {
	if (i && i == yzero) pprintf(prn, "        0.0  ");
	else if (i == nrows || i%5 == 0) {
	    xx = ymin + ((ymax-ymin) * i/nrows);
	    printgx(xx, prn);
	}
	else _bufspace(13, prn);
	for (j=0; j<=ncols+1; ++j) pprintf(prn, "%c", p[i][j]);
	pprintf(prn, "\n");
    }
    _bufspace(13, prn);
    pprintf(prn, "|");
    for (j=0; j<=ncols; j++) if (j%10 == 0) pprintf(prn, "+");
    else pprintf(prn, "-");
    pprintf(prn, "\n");
    _bufspace(14, prn);
    sprintf(word, "%g", xmin);
    lx = strlen(word);
    lw = 13 + lx;
    pprintf(prn, "%s", word);
    sprintf(word, "%s", xname);
    ly = strlen(word);
    ls = 30 - lx - ly/2;
    _bufspace(ls, prn);
    pprintf(prn, "%s", word);
    lw = lw + ls + ly; 
    sprintf(word, "%g", xmax);

    ls = strlen(word);
    if (ls < 7) _bufspace(73 - lw, prn);
    else { 
	lw = lw + ls;
	_bufspace(79 - lw, prn);
    }
    pprintf(prn, "%s\n\n", word);
}

/* ........................................................... */

static void fit_resid_head (const MODEL *pmod, const DATAINFO *pdinfo, 
			    PRN *prn)
{
    int i, t2 = pmod->t2;
    char label[9], date1[9], date2[9]; 

    if (pmod->data != NULL) 
        t2 += get_misscount(pmod);

    ntodate(date1, pmod->t1, pdinfo);
    ntodate(date2, t2, pdinfo);
    pprintf(prn, _("\nFull data range: %s - %s (n = %d)\n"),
	    pdinfo->stobs, pdinfo->endobs, pdinfo->n);
    pprintf(prn, _("Model estimation range: %s - %s"), date1, date2);
    if (pmod->nobs == pdinfo->n) pprintf(prn, "\n");
    else pprintf(prn, " (n = %d)\n", pmod->nobs); 

    pprintf(prn, _("Standard error of residuals = %f\n"), pmod->sigma);
    
    pprintf(prn, "\n     Obs ");
    for (i=1; i<4; i++) {
	if (i == 1) strcpy(label, pdinfo->varname[pmod->list[1]]);
	if (i == 2) strcpy(label, _("fitted"));
	if (i == 3) strcpy(label, _("residual"));
	pprintf(prn, "%13s", label);
    }
    pprintf(prn, "\n");
}

/* ........................................................... */

static void varheading (int v1, int v2, 
			const DATAINFO *pdinfo, const int *list,
			PRN *prn)
/*  skips to new page and prints names of variables
    from v1 to v2 */
{
    int mv;
        
    pprintf(prn, "\n     Obs ");
    for (mv=v1; mv<=v2; ++mv) 
	pprintf(prn, "%13s", pdinfo->varname[list[mv]]);
    pprintf(prn, "\n\n");
}

/* ........................................................... */

void _printxs (double xx, int n, int ci, PRN *prn)
{
    int ls;
    char s[32];

    printxx(xx, s, ci);
    ls = strlen(s);
    pprintf(prn, " ");
    _bufspace(n-3-ls, prn);
    pprintf(prn, "%s", s);
}

/* ........................................................ */

static void printstr (PRN *prn, const double xx, int *ls)
{
    int lwrd;
    char str[32];

    printxx(xx, str, 0);
    strcat(str, "  ");
    lwrd = strlen(str);
    if (*ls+lwrd > 78) {
	*ls = 0;
	pprintf(prn, "\n");
    }
    pprintf(prn, "%s", str);
    *ls += lwrd;
}

/* ........................................................... */

static void printz (const double *z, const DATAINFO *pdinfo, 
		    PRN *prn)
/* prints series z from current sample t1 to t2 */
{
    int t, t1 = pdinfo->t1, t2 = pdinfo->t2, ls = 0;
    double xx;

    if (_isconst(t1, t2, z)) printstr(prn, z[t1], &ls);
    else for (t=t1; t<=t2; t++) {
	xx = z[t];
	printstr(prn, xx, &ls);
    }
    pprintf(prn, "\n");
}

#define SMAX 7  /* stipulated max. significant digits */

/* #define PRN_DEBUG */

static int get_signif (double *x, int n)
     /* return either (a) the number of significant digits in
	a data series (+), or (b) the number of decimal places to
	use when printing the series (-) */
{
    static char numstr[48];
    int i, j, s, smax = 0;
    int lead, leadmax = 0, leadmin = 99;
    double xx;
    int allfrac = 1;
    char decpoint = '.';

#ifdef ENABLE_NLS
    decpoint = get_local_decpoint();
#endif

    for (i=0; i<n; i++) {
	if (na(x[i])) continue;
	xx = fabs(x[i]);
	if (xx >= 1.0) allfrac = 0;
	sprintf(numstr, "%.12f", xx);
#ifdef PRN_DEBUG
	fprintf(stderr, "get_signif: numstr = '%s'\n", numstr);
#endif
	s = strlen(numstr) - 1;
	for (j=s; j>0; j--) {
	    if (numstr[j] == '0') s--;
	    else if (numstr[j] == decpoint) {
		if (xx < 10000) break;
		else continue;
	    }
	    else break;
	}
	if (s > smax) smax = s;
#ifdef PRN_DEBUG
	fprintf(stderr, "get_signif: set smax = %d\n", smax);
#endif
	lead = 0;
	for (j=0; j<=s; j++) {
	    if (xx >= 1.0 && numstr[j] != decpoint) lead++;
	    else break;
	}
	if (lead > leadmax) leadmax = lead;
	if (lead < leadmin) leadmin = lead;
    }
    if (smax > SMAX) smax = SMAX;
    if ((leadmin < leadmax) && (leadmax < smax)) {
#ifdef PRN_DEBUG
	fprintf(stderr, "get_signif: setting smax = -(%d - %d)\n", 
		smax, leadmax);
#endif	
	smax = -1 * (smax - leadmax); /* # of decimal places */
    } else if (leadmax == smax) {
	smax = 0;
    } else if (leadmax == 0 && !allfrac) {
#ifdef PRN_DEBUG
	fprintf(stderr, "get_signif: setting smax = -(%d - 1)\n", smax);
#endif
	smax = -1 * (smax - 1);
    } 
#ifdef PRN_DEBUG
    fprintf(stderr, "get_signif: returning smax = %d\n", smax);
#endif
    return smax;
}

/* ........................................................... */

static int bufprintnum (char *buf, double x, int signif, int width)
{
    static char numstr[24];
    int i, l;

    if (signif < 0) {
#ifdef PRN_DEBUG
	    fprintf(stderr, "got %d for signif: "
		    "printing with %%.%df\n", signif, signif);
#endif
	sprintf(numstr, "%.*f", -1 * signif, x);
    } else if (signif == 0) {
#ifdef PRN_DEBUG
	    fprintf(stderr, "got 0 for signif: "
		    "printing with %%.0f\n");
#endif
	sprintf(numstr, "%.0f", x);
    } else {
	double z = fabs(x);

	if (z < 1) l = 0;
	else if (z < 10) l = 1;
	else if (z < 100) l = 2;
	else if (z < 1000) l = 3;
	else if (z < 10000) l = 4;
	else if (z < 100000) l = 5;
	else if (z < 1000000) l = 6;
	else l = 7;
	if (l >= signif) { 
#ifdef PRN_DEBUG
	    fprintf(stderr, "got %d for leftvals, %d for signif: "
		    "printing with %%.%dG\n", l, signif, signif);
#endif
	    sprintf(numstr, "%.*G", signif, x);
	} else if (z >= .10) {
#ifdef PRN_DEBUG
	    fprintf(stderr, "got %d for leftvals, %d for signif: "
		    "printing with %%.%df\n", l, signif, signif-l);
#endif
	    sprintf(numstr, "%.*f", signif - l, x);
	} else {
	    if (signif > 4) signif = 4;
#ifdef PRN_DEBUG
	    fprintf(stderr, "got %d for leftvals, %d for signif: "
		    "printing with %%.%dG\n", l, signif, signif);
#endif
	    sprintf(numstr, "%.*G", signif, x);
	}
    }

    l = width - strlen(numstr);
    for (i=0; i<l; i++)
	strcat(buf, " ");
    strcat(buf, numstr);

    return 0;
}

/**
 * print_obs_marker:
 * @t: observation number.
 * @pdinfo: data information struct.
 * @prn: gretl printing struct.
 *
 * Print a string (label, date or obs number) representing the given @t.
 *
 */

void print_obs_marker (int t, const DATAINFO *pdinfo, PRN *prn)
{
    if (pdinfo->markers) { 
	pprintf(prn, "%8s ", pdinfo->S[t]); 
    } else {
	char tmp[9]; 

	ntodate(tmp, t, pdinfo);
	pprintf(prn, "%8s ", tmp);
    }
}

/**
 * printdata:
 * @list: list of variables to print.
 * @pZ: pointer to data matrix.
 * @pdinfo: data information struct.
 * @pause: if non-zero, pause after each screen of data.
 * @byobs: if non-zero, print the data by observation (series in columns).
 * @prn: gretl printing struct.
 *
 * Print the data for the variables in @list, from observations t1 to
 * t2.
 *
 * Returns: 0 on successful completion, 1 on error.
 */

int printdata (LIST list, double ***pZ, const DATAINFO *pdinfo, 
	       int pause, int byobs, PRN *prn)
{
    int l0, j, v, v1, v2, j5, nvj5, lineno, ncol;
    register int t;
    int gui, isconst; 
    int *pmax = NULL; 
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    double xx;
    int *tmplist = NULL, freelist = 0;
    char line[96];

    if (prn->buf != NULL) gui = 1;
    else gui = 0;

    lineno = 1;
    if (list == NULL) {
	if (make_list(&tmplist, pdinfo)) return 1;
	list = tmplist;
	freelist = 1;
    }
    l0 = list[0];

    if (l0 == 0) {
	pprintf(prn, "No data\n");
	if (freelist) free(list);
	return 0;
    }

    /* screen out any scalars and print them first */
    for (j=1; j<=list[0]; j++) {
	if (!pdinfo->vector[list[j]]) {
	    pprintf(prn, "\n%8s = %10g", pdinfo->varname[list[j]], 
		    (*pZ)[list[j]][0]);
	    list_exclude(j, list);
	    j--;
	} 
    }
    if (list[0] < l0) {
	pprintf(prn, "\n");
	l0 = list[0];
    }

    /* special case: all vars have constant value over sample */
    isconst = 1;
    for (j=1; j<=list[0]; j++) {
	for (t=t1+1; t<=t2; t++) {
	    if (floatneq((*pZ)[list[j]][t], (*pZ)[list[j]][t1])) {
		isconst = 0;
		break;
	    }
	}
	if (!isconst) break;
    }
    if (isconst) {
	for (j=1; j<=list[0]; j++) 
	    pprintf(prn, "%8s = %10g\n", pdinfo->varname[list[j]], 
		    (*pZ)[list[j]][t1]);
	if (freelist) free(list);
	return 0;
    }

    if (!byobs) {
	if (list[0] > 0) pprintf(prn, "\n");
	/* print data by variables */
	for (j=1; j<=list[0]; j++) {
	    pprintf(prn, _("Varname: %s\n"), pdinfo->varname[list[j]]);
	    print_smpl (pdinfo, 0, prn);
	    pprintf(prn, "\n");
	    printz((*pZ)[list[j]], pdinfo, prn);
	    pprintf(prn, "\n");
	}
	return 0;
    }

    /* experimental */
    pmax = malloc(l0 * sizeof *pmax);
    if (pmax == NULL) return 1;
    for (j=1; j<=l0; j++) {
	/* this runs fairly quickly, even for large dataset */
	pmax[j-1] = get_signif(&(*pZ)[list[j]][t1], t2-t1+1);
    }

    /* print data by observations */
    ncol = 5;
    for (j=0; j<=l0/ncol; j++) {
	j5 = j * ncol;
	nvj5 = l0 - j5;
	v1 = j5 +1;
	if (nvj5) {
	    v2 = (ncol > nvj5)? nvj5 : ncol;
	    v2 += j5;
	    varheading(v1, v2, pdinfo, list, prn);
	    if (pause && page_break(1, &lineno, 1)) return 0;
	    lineno++;
	    for (t=t1; t<=t2; t++)   {
		if (pdinfo->markers) { /* data marker strings present */
		    sprintf(line, "%8s ", pdinfo->S[t]);
		} else {
		    char tmp[9];

		    ntodate(tmp, t, pdinfo);
		    sprintf(line, "%8s ", tmp);
		} /* end print obs marker */
		for (v=v1; v<=v2; v++) {
		    xx = (*pZ)[list[v]][t];
		    if (na(xx)) {
			strcat(line, "             ");
		    } else { 
			bufprintnum(line, xx, pmax[v-1], 13);
		    }
		}
		if (pprintf(prn, "%s\n", line))
		    return 1;
		if (pause && page_break(1, &lineno, 1)) return 0;
		lineno++;
		if (pause) {
		    if ((t-t1+1) % 21 == 0) {
			varheading(v1, v2, pdinfo, list, prn);
			if (page_break(1, &lineno, 1)) return 0;
			lineno++;
		    }
		}
	    } /* end of t loop */
	} /* end if nvj5 */
    } /* end for j loop */
    pprintf(prn, "\n");
    lineno++;
    if (freelist) free(list);
    free(pmax);
    return 0;
}

/**
 * print_fit_resid:
 * @pmod: pointer to gretl model.
 * @pZ: pointer to data matrix.
 * @pdinfo: data information struct.
 * @prn: gretl printing struct.
 *
 * Print to @prn the fitted values and residuals from @pmod.
 *
 * Returns: 0 on successful completion, 1 on error.
 */

int print_fit_resid (const MODEL *pmod, double ***pZ, 
		     DATAINFO *pdinfo, PRN *prn)
{
    int pmax, depvar, t, nfit, anyast = 0;
    int t1 = pmod->t1, t2 = pmod->t2, n = pdinfo->n;
    double xx;
    char fcastline[32];

    depvar = pmod->list[1];

    if (pmod->data != NULL) 
	t2 += get_misscount(pmod);

    sprintf(fcastline, "fcast %s %s fitted", pdinfo->stobs, 
	    pdinfo->endobs);
    nfit = fcast(fcastline, pmod, pdinfo, pZ); 
    if (nfit < 0) return 1; 

    if (isdummy(depvar, t1, t2, *pZ) > 0)
	pmax = get_precision((*pZ)[nfit], n);
    else
	pmax = get_precision((*pZ)[depvar], n);

    fit_resid_head(pmod, pdinfo, prn);

    for (t=0; t<n; t++) {
	if (t == t1 && t) pprintf(prn, "\n");
	if (t == t2 + 1) pprintf(prn, "\n");

	print_obs_marker(t, pdinfo, prn);

	if (na((*pZ)[depvar][t]) || na((*pZ)[nfit][t])) { 
	    pprintf(prn, "\n");
	} else {
	    int ast;

	    xx = (*pZ)[depvar][t] - (*pZ)[nfit][t];
	    ast = (fabs(xx) > 2.5 * pmod->sigma);
	    if (ast) anyast = 1;
	    pprintf(prn, "%13.*f%13.*f%13.*f%s\n", 
		    pmax, (*pZ)[depvar][t],
		    pmax, (*pZ)[nfit][t], pmax, xx,
		    (ast)? " *" : "");
	}
    }
    pprintf(prn, "\n");
    if (anyast) pprintf(prn, _("Note: * denotes a residual in excess of "
			       "2.5 standard errors\n"));
    return 0;
}

/**
 * gretl_print_destroy:
 * @prn: pointer to gretl printing struct.
 *
 * Close a gretl printing struct and free any associated resources.
 *
 */

void gretl_print_destroy (PRN *prn)
{
    if (prn == NULL) return;

    if (prn->fp != stdout && prn->fp != stderr && prn->fp != NULL)
	fclose(prn->fp);
    prn->fp = NULL;
    if (prn->buf != NULL) {
#ifdef PRN_DEBUG
  	fprintf(stderr, "freeing buffer at %p\n", (void *) prn->buf); 
#endif
	free(prn->buf);
    }
    prn->buf = NULL;
    free(prn);
    prn = NULL;
}

/**
 * gretl_print_new:
 * @prncode: code indicating the desired printing mode (see #prn_codes).
 * @fname: filename for opening in case of GRETL_PRINT_FILE, otherwise
 * NULL.
 * 
 * Create and initialize a gretl printing struct so that it is
 * ready for printing.
 *
 * Returns: pointer to newly created struct, or NULL on failure.
 */

PRN *gretl_print_new (int prncode, const char *fname)
{
    PRN *prn = NULL;

    if (prncode == GRETL_PRINT_FILE && fname == NULL) {
	fprintf(stderr, _("gretl_prn_new: Must supply a filename\n"));
	return NULL;
    }

    prn = malloc(sizeof *prn);
    if (prn == NULL) {
	fprintf(stderr, _("gretl_prn_new: out of memory\n"));
	return NULL;
    }

    if (prncode == GRETL_PRINT_NULL) {
	prn->fp = NULL;
	prn->buf = NULL;
    }	
	
    else if (prncode == GRETL_PRINT_FILE) {
	prn->buf = NULL;
	prn->fp = fopen(fname, "w");
	if (prn->fp == NULL) {
	    fprintf(stderr, _("gretl_prn_new: couldn't open %s\n"), fname);
	    free(prn);
	    return NULL;
	}
    }

    else if (prncode == GRETL_PRINT_STDOUT) {
	prn->buf = NULL;
	prn->fp = stdout;
    }

    else if (prncode == GRETL_PRINT_STDERR) {
	prn->buf = NULL;
	prn->fp = stderr;
    }	    

    else if (prncode == GRETL_PRINT_BUFFER) {
	prn->fp = NULL;
	if (pprintf(prn, "@init")) {
	    fprintf(stderr, _("gretl_prn_new: out of memory\n"));
	    free(prn);
	    return NULL;
	}
    }

    prn->format = GRETL_PRINT_FORMAT_PLAIN;

    return prn;
}
