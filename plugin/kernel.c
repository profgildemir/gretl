/*
 *  Copyright (c) 2004 by Allin Cottrell
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
#include "gretl_private.h"

#undef KDEBUG

/* For discussion of kernel density estimation see Davidson and
   MacKinnon, Econometric Theory and Methods, Section 15.5.
*/

enum {
    GAUSSIAN_KERNEL,
    EPANECHNIKOV_KERNEL
};

#define ROOT5 2.23606797749979
#define EPMULT 0.3354101966249685


static double ep_pdf (double z)
{
    if (fabs(z) >= ROOT5) {
	return 0.0;
    } else {
	return EPMULT * (1.0 - z * z / 5.0);
    }
}

static double
kernel (const double *x, int n, double x0, double h, int ktype)
{
    double den = 0.0;
    int in_range = 0;
    int t;

    for (t=0; t<n; t++) {
	double z = (x0 - x[t]) / h;

	if (ktype == GAUSSIAN_KERNEL) {
	    den += normal_pdf(z);
	} else {
	    double dt = ep_pdf(z);

	    if (!in_range && dt > 0) {
		in_range = 1;
	    } else if (in_range && dt == 0.0) {
		break;
	    }

	    den += ep_pdf(z);
	}
    }

    den /= h * n;

    return den;
}

static void get_xmin_xmax (const double *x, double s, int n,
			   double *xmin, double *xmax)
{
    double xbar = gretl_mean(0, n - 1, x);
    double xm4 = xbar - 4.0 * s;
    double xp4 = xbar + 4.0 * s;

    if (xp4 > x[n-1]) {
#ifdef KDEBUG
	fprintf(stderr, "raising xmax from %g to %g\n", x[n-1], xp4);
#endif
	*xmax = xp4;
    } else {
	*xmax = x[n-1];
    }
    
    if (xm4 < x[0]) {
#ifdef KDEBUG
	fprintf(stderr, "lowering xmin from %g to %g\n", x[0], xm4);
#endif
	*xmin = xm4;
    } else {
	*xmin = x[0];
    }

    if (*xmin < 0.0 && x[0] >= 0.0) {
	/* if data are non-negative, don't set a negative min */
	*xmin = x[0];
#ifdef KDEBUG
	fprintf(stderr, "respecting non-negative data: xmin  = %g\n", *xmin);
#endif

    }
}

static int density_plot (const double *x, double s, double h, 
			 int n, int kn, gretlopt opt, int vnum,
			 const DATAINFO *pdinfo)
{
    FILE *fp = NULL;
    char tmp[128];
    double xstep, xmin, xmax;
    double xt, xdt;
    int ktype, t;

    if (gnuplot_init(0, &fp)) {
	return E_FOPEN;
    }

    if (opt & OPT_O) {
	ktype = EPANECHNIKOV_KERNEL;
#ifdef KDEBUG
	fprintf(stderr, "Using the Epanechnikov kernel\n");
#endif
    } else {
	ktype = GAUSSIAN_KERNEL;
#ifdef KDEBUG
	fprintf(stderr, "Using the Gaussian kernel\n");
#endif
    }

    get_xmin_xmax(x, s, n, &xmin, &xmax);
    xstep = (xmax - xmin) / kn;

#ifdef ENABLE_NLS
    setlocale(LC_NUMERIC, "C");
#endif

    fputs("# kernel density plot\n", fp);
    fputs("set nokey\n", fp); 
    fprintf(fp, "set xrange [%g:%g]\n", xmin, xmax);

    fprintf(fp, "set label '%s' at graph .65, graph .97\n",
	    (ktype == GAUSSIAN_KERNEL)? I_("Gaussian kernel") :
	    I_("Epanechnikov kernel"));
    sprintf(tmp, I_("bandwidth = %g"), h);
    fprintf(fp, "set label '%s' at graph .65, graph .93\n", tmp);

    sprintf(tmp, I_("Estimated density of %s"), pdinfo->varname[vnum]);
    fprintf(fp, "set title '%s'\n", tmp);

    fputs("plot \\\n'-' using 1:2 w lines\n", fp);

    xt = xmin;
    for (t=0; t<=kn; t++) {
	xdt = kernel(x, n, xt, h, ktype);
	fprintf(fp, "%g %g\n", xt, xdt);
	xt += xstep;
    }
    fputs("e\n", fp);

#ifdef ENABLE_NLS
    setlocale(LC_NUMERIC, "");
#endif

    fclose(fp);

    return 0;
}

static double quartiles (const double *x, int n,
			 double *q1, double *q3)
{
    int n2;
    double xx;

    n2 = n / 2;
    xx = (n % 2)? x[n2] : 0.5 * (x[n2 - 1] + x[n2]);

    if (q1 != NULL && q3 != NULL) {
        if (n % 2) {
            *q1 = quartiles(x, n2 + 1, NULL, NULL);
            *q3 = quartiles(x + n2, n2 + 1, NULL, NULL);
        } else {
            *q1 = quartiles(x, n2, NULL, NULL);
            *q3 = quartiles(x + n2, n2, NULL, NULL);
        }
    }

    return xx;
}


static double 
silverman_bandwidth (const double *x, double s, int n)
{
    double n5 = pow((double) n, -0.20);
    double w, q1, q3, r;

    quartiles(x, n, &q1, &q3);
    r = (q3 - q1) / 1.349;

#ifdef KDEBUG
    fprintf(stderr, "Silverman bandwidth: s=%g, q1=%g, q3=%g, IQR=%g\n",
	    s, q1, q3, q3 - q1);
#endif

    if (r < s) {
#ifdef KDEBUG
	fprintf(stderr, "Silverman bandwidth: using IQR/1.349\n");
#endif
	w = r;
    } else {
#ifdef KDEBUG
	fprintf(stderr, "Silverman bandwidth: using std. dev.\n");
#endif
	w = s;
    }

    return 0.9 * w * n5;
}

static int count_obs (const double *x, int n)
{
    int t, m = 0;

    for (t=0; t<n; t++) {
	if (!na(x[t])) m++;
    }

    return m;
}

static int get_kn (int nobs)
{
    int kn;

    if (nobs >= 200) {
	kn = 200;
    } else if (nobs >= 100) {
	kn = 100;
    } else {
	kn = 50;
    }

    return kn;
}

#define MINOBS 30

int 
kernel_density (int varnum, const double **Z, const DATAINFO *pdinfo,
		double bw, gretlopt opt)
{
    int len = pdinfo->t2 - pdinfo->t1 + 1;
    int nobs, kn;
    double h, s;
    double *x;
    int err = 0;

    nobs = count_obs(Z[varnum] + pdinfo->t1, len);
    if (nobs < MINOBS) {
	return E_DATA;
    }

    x = malloc(nobs * sizeof *x);
    if (x == NULL) {
	return E_ALLOC;
    }

    ztox(varnum, x, Z, pdinfo);

    qsort(x, nobs, sizeof *x, gretl_compare_doubles);

    s = gretl_stddev(0, nobs - 1, x);

    if (na(bw)) {
	h = silverman_bandwidth(x, s, nobs);
    } else {
	h = bw;
    }

    kn = get_kn(nobs);

#ifdef KDEBUG
    fprintf(stderr, "kernel_density: nobs=%d, kn=%d, bandwidth=%g\n",
	    nobs, kn, h);
#endif

    err = density_plot(x, s, h, nobs, kn, opt, varnum, pdinfo);

    free(x);

    return err;
}
    


