/* 
    The functions here are derived from the Fortran in VSGARCMX.FOR,
    from the Fiorentini, Calzolari and Panattoni GARCH implementation
    (Journal of Applied Econometrics, 1996), the original notice for
    which is reproduced infra.  The relevant code was extracted from
    its original Monte Carlo context, translated to C using f2c, and
    then edited extensively to turn it into more idiomatic C, and
    to convert from fixed-size arrays to dynamic memory allocation.

    Allin Cottrell, Wake Forest University, March 2004.
*/

#include "libgretl.h"
#include "fcp.h"

/* #define FDEBUG */

#define NLL    50   /* number of iterative results to store */
#define ABNUM   4   /* max number of GARCH lags */

#define LOG_SQRT_2_PI 0.9189385332056725
#define SMALL_HT      1.0e-7   

int global_np;

#define vix(i,j) ((i) + global_np * (j))

/* private functions */

static int invert (double *g, int dim);

static  
double garch_ll (double *c, int nc, double *res2, 
		 double *res, double *yhat, 
		 double *ystoc, const double **X, 
		 int nexo, int t1, int t2, double *param, 
		 double *b, double *alfa0, 
		 double *alfa, double *beta, int nalfa, 
		 int nbeta, double *h);

static int 
garch_info_matrix (int t1, int t2, 
		   const double **X, int nexo, 
		   double *yhat, double *c, int nc, double *res2,
		   double *res, double *ystoc, double toler, 
		   int *ivolta, double *vc5, const double **g,
		   double *aux3, double *param, int nparam,
		   double *b, double *alfa0, double *alfa,
		   double *beta, int nalfa, int nbeta, double *h,
		   double **dhdp, double *zt);

static int 
garch_full_hessian (int t1, int t2, 
		    const double **X, int nexo, 
		    double *yhat, double *c, int nc, double *res2,
		    double *res, double *ystoc, double toler, 
		    int *ivolta, double *vc5, const double **g,
		    double *aux3, double *param, int nparam,
		    double *b, double *alfa0, double *alfa,
		    double *beta, int nalfa, int nbeta, double *h,
		    double **dhdp, double *zt);

/* Gabriele FIORENTINI, Giorgio CALZOLARI, Lorenzo PANATTONI
   Journal of APPLIED ECONOMETRICS, 1996 

   mixed gradient algorithm

   garch(p,q) estimates of a linear equation 
   SEE BOLLERSLEV, JOE 31(1986),307-327. 

   remember to put enough lagged observations in the data file 
   (at least = max(p,q)) to have residuals at time 0, -1, etc. 
*/

/*
  VC = matr di comodo che serve al calcolo --- inoltre si usa come
       inversa della mat. di cov. dei soli coeff. per la mat. di inf.
  VC5= matrice di informazione. 
  VC8= inversa della mat. di cov. dei soli coeff. per l'fulhessiano 
  VC9= fulhessian of unconcentrated log-lik. (nparam,nparam). 
  VC10=matrix as in white (1982,p.....), computed using the complete 
       inverse of VC9 and the full VC6 matrix. 
       consistent and robust. 

       dhtdp sono le derivate di ht rispetto a tutti i parametri 
*/

static void free_2d_array (double **X, int k)
{
    int i;

    if (X == NULL) return;

    for (i=0; i<k; i++) free(X[i]);
    free(X);
}

static double **allocate_2d_array (int k, int T)
{
    double **X;
    int i, j;

    X = malloc(k * sizeof *X);
    if (X == NULL) return NULL;

    for (i=0; i<k; i++) {
	X[i] = malloc(T * sizeof **X);
	if (X[i] == NULL) {
	    for (j=0; j<i; j++) free(X[j]);
	    free(X);
	    return NULL;
	}
    }

    return X;
}

static int vs_allocate (double ***pdhdp, double ***pg, double **ph,
			double **pparam, double **paux3, double **psvc5,
			double **pc, double **paux, double **pvc5,
			double **pparpre, double ***ppartrc,
			int np, int nrc, int T, int nll)
{
    double *h = NULL, *param = NULL, *aux3 = NULL, *svc5 = NULL;
    double *c = NULL, *aux = NULL, *vc5 = NULL, *parpre = NULL;
    double **D = NULL, **G = NULL, **P = NULL;

    h = malloc(T * sizeof *h);
    if (h == NULL) return 1;

    param = malloc(np * sizeof *param);
    aux3 = malloc(np * sizeof *aux3);
    svc5 = malloc(np * sizeof *svc5);
    parpre = malloc(np * sizeof *parpre);
    if (param == NULL || aux3 == NULL || 
	svc5 == NULL || parpre == NULL) {
	goto bailout;
    }

    c = malloc(nrc * sizeof *c);
    aux = malloc(nrc * sizeof *aux);
    if (c == NULL || aux == NULL) {
	goto bailout;
    }

    vc5 = malloc(np * np * sizeof *vc5);
    if (vc5 == NULL) {
	goto bailout;
    }

    D = allocate_2d_array(np, T);
    if (D == NULL) {
	goto bailout;
    }

    G = allocate_2d_array(nrc, T);
    if (G == NULL) {
	goto bailout;
    }

    P = allocate_2d_array(np, nll);
    if (P == NULL) {
	goto bailout;
    }

    *pdhdp = D;
    *pg = G;
    *ph = h;
    *pparam = param;
    *paux3 = aux3;
    *psvc5 = svc5;
    *pc = c;
    *paux = aux;
    *pvc5 = vc5;
    *pparpre = parpre;
    *ppartrc = P;

    return 0;

 bailout:

    free(h);
    free(param);
    free(aux3);
    free(svc5);
    free(c);
    free(aux);
    free(vc5);
    free(parpre);
    free_2d_array(D, np);
    free_2d_array(G, nrc);
    free_2d_array(P, np);

    return 1;
}

static void vs_free (double **dhdp, int np, double **g, int nrc, 
		     double *h, double *param, double *aux3, double *svc5,
		     double *c, double *aux, double *vc5,
		     double *parpre, double **partrc)
{
    free_2d_array(dhdp, np);
    free_2d_array(g, nrc);
    free_2d_array(partrc, np);
    free(h);
    free(param);
    free(aux3);
    free(svc5);
    free(c);
    free(aux);
    free(vc5);
    free(parpre);
}

static void print_iter_info (int iter, double *theta, int m, double ll,
			     int hess, PRN *prn)
{
    int i;

    pprintf(prn, "\n*** %s %d%s: theta, ll ***\n", ("iteration"), iter,
	    (hess)? _(" (using Hessian)") : "");
    for (i=0; i<m; i++) {
	if (i && i % 5 == 0) pputc(prn, '\n');
	pprintf(prn, "%#12.5g ", theta[i]);
    }
    pprintf(prn, "\n    ll = %f\n", ll);
}

static double 
get_yhat (const double **X, int nx, int t, const double *a)
{
    int j;
    double yhat = a[0];

    for (j = 0; j < nx; j++) {
	yhat += a[j+1] * X[j][t];
    }

    return yhat;
} 

static void copy_coeff (const double *c, int nc, double *b)
{
    int i;

    for (i = 0; i < nc; ++i) {
	b[i] = c[i];
    }
} 

int ols_ (int t1, int t2,  const double **X, int nx, 
	  double *c, int nc, double *vc, double *ystoc, 
	  double *amax, double *aux, double *b, double **g)
{
    int i, j, k, t, err;
    double deltc, deriv, relinc = 0.5;
    double oldc, yy;

    copy_coeff(c, nc, b);

    for (t = t1; t <= t2; ++t) {
	amax[t] = get_yhat(X, nx, t, b);
    }

    for (i = 0; i < nc; ++i) {
	aux[i] = 0.0;
	for (j = 0; j < nc; ++j) {
	    vc[i + j * nc] = 0.0;
	}
    }

    for (t = t1; t <= t2; ++t) {
	for (k = 0; k < nc; ++k) {
	    oldc = c[k];
	    deltc = relinc;
	    if (oldc != 0.0) {
		deltc = oldc * relinc;
	    }
	    c[k] = oldc + deltc;
	    copy_coeff(c, nc, b);
	    yy = get_yhat(X, nx, t, b);
	    deltc = c[k] - oldc;
	    deriv = (yy - amax[t]) / deltc;
	    c[k] = oldc;
	    g[k][t] = deriv;
	}
	copy_coeff(c, nc, b);

	/* cumulates all the w'z into diagonal blocks of vc 
	   and w'y into elements of aux */
	for (i = 0; i < nc; ++i) {
	    aux[i] += g[i][t] * ystoc[t];
	    for (j = 0; j < nc; ++j) {
		vc[i + j * nc] += g[i][t] * g[j][t];
	    }
	}
    }

    err = invert(vc, nc);

    if (err == 0) {
	/* compute coefficients */
	for (i = 0; i < nc; ++i) {
	    c[i] = 0.0;
	}
	for (i = 0; i < nc; ++i) {
	    for (j = 0; j < nc; ++j) {
		c[i] += vc[i + j * nc] * aux[j];
	    }
	}
	copy_coeff(c, nc, b);
    } else {
	fputs("OLS: matrix is singular, initial coefficients are unchanged\n",
	      stderr);

	for (i = 0; i < nc; ++i) {
	    for (j = 0; j < nc; ++j) {
		vc[i + j * nc] = 0.0;
	    }
	}
    }

    return 0;
} 

int garch_estimate (int t1, int t2, int nobs, 
		    const double **X, int nx, double *yhat, 
		    double *coeff, int nc, double *vc, 
		    double *res2, double *res,
		    double *ystoc, double *amax, double *b, 
		    int *iters, PRN *prn)
{
    int i, j;

    int izo, nzo, nzo1;
    int ivolta, ivolt2;
    int nalfa, nbeta, nparam;

    double alfa[ABNUM], beta[ABNUM];
    double zt[6];   /* max alpha + beta ? */

    double pappo, toler1, toler2;
    double reldis, rellog, tollog, sumgra, totdis; 
    double ll, s_2, alfa0, s_1;

    double *param = NULL, *aux3 = NULL, *svc5 = NULL;
    double *h = NULL, **dhdp = NULL, **g = NULL;
    double *c = NULL, *aux = NULL;
    double *parpre, **partrc;
    double *vc5 = NULL;

    int err = 0;

    nalfa = (int) amax[1];
    nbeta = (int) amax[2];

    /* number of parameters of unconcentrated likelihood */
    nparam = nc + 1 + nalfa + nbeta;
    global_np = nparam;

    if (vs_allocate(&dhdp, &g, &h, &param, &aux3, &svc5, 
		    &c, &aux, &vc5, &parpre, &partrc,
		    nparam, nc, nobs, NLL)) {
	pprintf(prn, "Out of memory\n");
	return E_ALLOC;
    }

#if FFDEBUG
    fprintf(stderr, "read nalfa=%d, nbeta=%d\n", nalfa, nbeta);
#endif

    for (i = 0; i < nalfa; ++i) {
	param[nc + i + 1] = amax[3 + i];
#if FFDEBUG
	fprintf(stderr, "read initial alpha[%d] = %.9g\n", i, 
		param[nc + i + 1]);
#endif
    }

    for (i = 0; i < nbeta; ++i) {
	param[nc + nalfa + i + 1] = amax[3 + nalfa + i];
#if FFDEBUG
	fprintf(stderr, "read initial beta[%d] = %.9g\n", i, 
		param[nc + nalfa + i + 1]);
#endif
    }

    param[nc] = amax[0];

    toler1 = .05;
    toler2 = 1e-8;
    tollog = log10(toler2);

    for (i = 0; i < nc; ++i) {
	c[i] = coeff[i];
    }

    for (i = 0; i < nparam; ++i) {
	svc5[i] = 0.0;
    }

    for (i = 0; i < nc; ++i) {
	param[i] = coeff[i];
    }

    /* this is only to calculate matrix of regressors (g) */
    ols_(t1, t2, X, nx, c, nc, vc, ystoc, amax, aux, b, g);

#ifdef FFDEBUG
    for (i = 0; i < 3; i++) {
	fprintf(stderr, "after ols g[%d] = %.9g\n", i, g[i]);
    } 
#endif    

    /* iterative estimation */

    ivolta = 0;
    nzo = 0;

    for (izo = 1; izo <= 100; ++izo) {

	ll = garch_ll(c, nc, res2, res, yhat, ystoc, 
		      X, nx, t1, t2, param, b, &alfa0, alfa, 
		      beta, nalfa, nbeta, h);

	print_iter_info(izo, param, nparam, ll, 0, prn);

	if (++nzo > NLL) {
	    nzo = NLL;
	}
	
	/* store previous coefficients */
	for (i = 0; i < nparam; ++i) {
	    parpre[i] = param[i];
	    partrc[i][nzo-1] = param[i]; /* FIXME? */
#ifdef FDEBUG
	    fprintf(stderr, "param[%d] = %.9g\n", i, param[i]);	    
#endif
	}


#ifdef FDEBUG
	fprintf(stderr, "*** Calling garch_info_matrix, ivolta=%d\n", ivolta);	    
#endif

	garch_info_matrix(t1, t2, X, nx, yhat, c, 
			  nc, res2, res, ystoc,
			  toler1, &ivolta, vc5, (const double **) g, 
			  aux3, param, nparam, b, 
			  &alfa0, alfa, beta, nalfa,
			  nbeta, h, dhdp, zt);

	/* if relative euclidean distance is used as converg. */
	s_1 = s_2 = 0.0;
	for (i = 0; i < nparam; ++i) {
	    s_1 += parpre[i] * parpre[i];
	    pappo = param[i] - parpre[i];
	    s_2 += pappo * pappo;
	}

	if (s_1 == 0.0) {
	    s_1 = 1e-10;
	}

	if (s_2 / s_1 <= toler1 * toler1) {
	    break;
	}
    }

#ifdef FDEBUG
    fprintf(stderr, "\n\n*** Now going to Hessian ***\n\n");
#endif

    /* full hessian and search */
    ivolt2 = 0;
    nzo1 = 0;

    for (izo = 1; izo <= 100; ++izo) {

	/* compute residuals for covariance matrix */
	ll = garch_ll(c, nc, res2, res, yhat, ystoc, 
		      X, nx, t1, t2, param, b, &alfa0, alfa, 
		      beta, nalfa, nbeta, h);

	print_iter_info(nzo + 1, param, nparam, ll, 1, prn);

	if (++nzo > NLL) {
	    nzo = NLL;
	}

	/* store previous coefficients */
	for (i = 0; i < nparam; ++i) {
	    parpre[i] = param[i];
	    partrc[i][nzo-1] = param[i]; 
	}

#ifdef FDEBUG
	fprintf(stderr, "*** Calling garch_full_hessian, ivolt2=%d\n", ivolt2);	    
#endif

	garch_full_hessian(t1, t2, X, nx, yhat, c, nc, 
			   res2, res, ystoc, toler2, &ivolt2, vc5, 
			   (const double **) g, 
			   aux3, param, nparam, b, &alfa0, alfa, beta, 
			   nalfa, nbeta, h, dhdp, zt);

	/* if relative euclidean distance is used as converg. */
	s_1 = 0.0;
	s_2 = 0.0;

	for (i = 0; i < nparam; ++i) {
	    s_1 += parpre[i] * parpre[i];
	    pappo = param[i] - parpre[i];
	    s_2 += pappo * pappo;
	}

	if (s_1 == 0.0) s_1 = 1e-10;

	if (s_2 / s_1 > toler2 * toler2) {
	    goto L6765;
	}

	sumgra = 0.0;
	for (i = 0; i < nparam; ++i) {
	    sumgra += aux3[i] * aux3[i];
	}
	if (sumgra >= 1.0e-4) {
	    fprintf(stderr, "Sum of gradients = %.9g\n", (double) sumgra);
	    err = E_NOCONV;
	    goto L999;
	}

	pprintf(prn, "\nFull Hessian convergence at iteration %d, tol = %.9g\n",
		nzo, toler2);

	*iters = nzo;
	amax[0] = ll;

	tollog = 0.0;
	totdis = 0.0;
	for (j = 0; j < nparam; ++j) {
	    /* Compute 2nd power */
	    double d = param[j] - partrc[j][0];

	    totdis += d * d;
	}
	totdis = sqrt(totdis);

	for (i = 0; i < nzo; ++i) {
	    /* if euclidean distance or distance in log-likel. is used */
	    s_2 = 0.0;
	    for (j = 0; j < nparam; ++j) {
		/* Compute 2nd power */
		double d = param[j] - partrc[j][i];

		s_2 += d * d;
	    }
	    s_2 = sqrt(s_2);

	    reldis = (totdis != 0.0)? s_2 / totdis : 0.0;
	    rellog = (reldis != 0.0)? reldis : tollog;
	}

	if (++nzo1 > NLL) {
	    goto L8767;
	}
    L8767:
	goto L6766;
    L6765:
	;
    }

    err = E_NOCONV;
    goto L999;

L6766:
    /* si mette provvisorio, nel programma serio ci vuole la covart */
    if (!err) {
	double sderr;

	for (i = 0; i < nparam; ++i) {
	    if (vc5[vix(i,i)] > 0.0) {
		sderr = sqrt(vc5[vix(i,i)]);
	    } else {
		sderr = 0.0;
	    }
	    amax[i+1] = param[i];
	    amax[i+1+nparam] = sderr;
	}
    }

 L999:
    vs_free(dhdp, nparam, g, nc, h, param, aux3, svc5, c, aux, vc5,
	    parpre, partrc);

    return err;
}




/* compute the log-likelihood function.
   i parametri sono passati nel vettore param(nparam). 
   alfa0, alfa e beta vengono ricavati dal vettore param in garch_ll.
   res, res2 e ht devono essere calcolati dentro valunc.
   res2 contiene i residui al quadrato.
*/

static double 
garch_ll (double *c, int nc, double *res2, 
	  double *res, double *yhat, 
	  double *ystoc, const double **X, int nx,
	  int t1, int t2, double *param, double *b,  
	  double *alfa0, double *alfa, double *beta, int nalfa,
	  int nbeta, double *h)
{
    int i, t, lag;
    int n = t2 - t1 + 1;
    double uncvar, ll;

    for (i = 0; i < nc; ++i) {
	c[i] = param[i];
    }

    *alfa0 = param[nc];
#ifdef FDEBUG
    fprintf(stderr, "garch_ll: alfa0 = %.9g\n", *alfa0);
#endif

    for (i = 0; i < nalfa; ++i) {
	alfa[i] = param[nc + 1 + i];
#ifdef FDEBUG
	fprintf(stderr, " alfa[%d] = %.9g\n", i, alfa[i]);
#endif	
    }

    for (i = 0; i < nbeta; ++i) {
	beta[i] = param[nc + nalfa + 1 + i];
#ifdef FDEBUG
	fprintf(stderr, " beta[%d] = %.9g\n", i, beta[i]);
#endif	
    }

    /* calcola residui ecc. nel periodo vero di stima */
    copy_coeff(c, nc, b);

    for (t = t1; t <= t2; ++t) {
	yhat[t] = get_yhat(X, nx, t, b);
    }

    uncvar = 0.0;
    for (t = t1; t <= t2; ++t) {
	res[t] = ystoc[t] - yhat[t];
	res2[t] = res[t] * res[t];
	uncvar += res2[t];
    }

#ifdef FDEBUG
    fprintf(stderr, "uncvar = %.9g/%d = %.9g\n", uncvar, n,
	    uncvar / n);
#endif
    uncvar /= n;

    /* come valore iniziale (ai tempi 0, -1, -2, ecc.) del residuo al
       quadrato e di ht si impiega la varianza noncondizionata
       calcolata dal campione; come valore iniziale dei residui si usa
       zero.
    */

    lag = (nbeta > nalfa)? nbeta : nalfa;

    for (t = t1-lag; t < t1; ++t) { 
	res[t] = 0.0;
	res2[t] = h[t] = uncvar;
    }

    for (t = t1; t <= t2; ++t) {
	h[t] = *alfa0;

	for (i = 1; i <= nalfa; ++i) {
	    h[t] += res2[t-i] * alfa[i-1];
	}

	for (i = 1; i <= nbeta; ++i) {
	    h[t] += h[t-i] * beta[i-1];
	}

	/* arbitrario */
	if (h[t] <= 0.0) h[t] = SMALL_HT;
    }

    ll = 0.0;
    for (t = t1; t <= t2; ++t) {
	ll = ll - .5 * log(h[t]) - .5 * res2[t] / h[t] - LOG_SQRT_2_PI;
    }

    return ll;
} 

static void check_ht (double *param, int nparam)
{
    /*
     This routine checks that the values of the parameters of the
     conditional variance ht are in the set of the admissible values.
     If alfa0 is less or equal than zero it is set to 0.0000001.  If
     alfa and beta are less than zero they are set to zero; also if
     the sum of alfa and beta is greater than 1.0 alfa and beta are
     normalized (divided by sum).
    */

    int i;
    double sum = 0.;

    if (param[0] <= 0.) {
	param[0] = 1.0e-7;
    }

    for (i = 1; i < nparam; ++i) {
	if (param[i] < 0.) {
	    param[i] = 0.0;
	}
	sum += param[i];
    }

    if (sum > 1.0) {
	for (i = 1; i < nparam; ++i) {
	    param[i] /= sum;
	}
    }
} 


/* matrice di informazione diagonale a blocchi */

/* i parametri sono passati dentro il vettore param 
   c, alfa e beta si ricavano all'inizio da param 
*/

static int 
garch_info_matrix (int t1, int t2, 
		   const double **X, int nx, double *yhat, double *c, 
		   int nc, double *res2, double *res, double *ystoc,
		   double toler, int *ivolta, double *vc5, 
		   const double **g, double *aux3, 
		   double *param, int nparam, double *b,  
		   double *alfa0, double *alfa, double *beta, int nalfa,
		   int nbeta, double *h, double **dhdp, double *zt)
{
    int i, j, t;
    double d, d0, d1, d2, ll2, d3;
    double ll3, d12, d31, d23, dd;
    double di, ff, dm;
    double ds;
    int iv, nexp;
    double a1s, a2s, a3s;
    int it1, it2, it3, it4, it5;
    double dac;
    double d12s, dub, d23s, d31s;
    int isp, ier5;
    double bigd, s_2;
    double stre, rsuh, s_1, r2suh;
    double cappa;
    int ncall, n;
    double r2suh3;
    int nvparm, lag;
    double oldstp = 9.0e+39;
    static double ll1 = 0.0, fs = 0.0;

    double *gg, *step, *asum2;

    gg = malloc(nparam * sizeof *gg);
    if (gg == NULL) {
	return 1;
    }

    step = malloc(nparam * sizeof *step);
    if (step == NULL) {
	free(gg);
	return 1;
    } 

    asum2 = malloc(nc * sizeof *asum2);
    if (asum2 == NULL) {
	free(gg);
	free(step);
	return 1;
    }

    iv = 0;
    it1 = 0;
    it2 = 0;
    it3 = 0;
    it4 = 0;
    it5 = 0;
    nvparm = nalfa + nbeta + 1;

    ++(*ivolta);

    for (i = 0; i < nc; ++i) {
	c[i] = param[i];
    }

    *alfa0 = param[nc];
#ifdef FDEBUG
    fprintf(stderr, "garcim: alfa0=%.9g\n", *alfa0);
#endif

    for (i = 0; i < nalfa; ++i) {
	alfa[i] = param[nc + 1 + i];
#ifdef FDEBUG
	fprintf(stderr, "garcim: alfa[%d]=%.9g\n", i, alfa[i]);
#endif
    }

    for (i = 0; i < nbeta; ++i) {
	beta[i] = param[nc + 1 + nalfa + i];
#ifdef FDEBUG
	fprintf(stderr, "garcim: beta[%d]=%.9g\n", i, beta[i]);
#endif
    }

    /* inizio del calcolo di dhtdp parte relativa ai parametri alfa e
       beta si comincia calcolando le derivate dei valori iniziali
       avendo scelto come val. iniziali la varianza noncondizionata
       come valore iniziale (ai tempi 0, -1, -2, ecc.)  di ht si
       impiega la varianza noncondizionata calcolata dai residui.
    */

    for (t = 1; t <= nbeta; ++t) { /* FIXME?? */
	for (i = 0; i < nvparm; ++i) {
	    dhdp[nc+i][t1-t] = 0.0;
	}
    }

    /* costruzione matrice dhtdp, parte relativa a alfa e beta (eq. 21) */

    for (t = t1; t <= t2; ++t) {

	/* si riempie zt al tempo t (p. 315) */
	
	zt[0] = 1.0;

	for (i = 1; i <= nalfa; ++i) {
	    zt[i] = res2[t - i];
	}

	for (i = 1; i <= nbeta; ++i) {
	    zt[nalfa + i] = h[t - i];
	}

	/*  si riempie dhtdp al tempo t la parte relativa ai parametri 
	    alfa e beta (eq. 21 p. 316) */

	for (i = 0; i < nvparm; ++i) {
	    dhdp[nc+i][t] = 0.0;
	}	

	for (i = 0; i < nvparm; ++i) {
	    dhdp[nc+i][t] += zt[i];
	}

	for (i = 0; i < nvparm; ++i) {
	    for (j = 1; j <= nbeta; ++j) { /* FIXME?? */
		dhdp[nc+i][t] += dhdp[nc+i][t-j] * beta[j-1];
	    }
	}
#if FFDEBUG
	if (t < 10) {
	    for (i=0; i<nvparm; i++) {
		 fprintf(stderr, "I(%d,%d) = %.9g\n", nc+i, t, 
			 dhdp[nc+i][t]);
	    }
	}
#endif

    }

#if FFDEBUG
    for (i=0; i<=nalfa + nbeta; i++) {
	fprintf(stderr, "zt[%d] = %.9g\n", i, zt[i]);
    }
#endif    

    /* costruzione matrice dhtdp, parte relativa ai coefficienti
       (eq.24) come valori iniziali (tempo 0, -1, ecc.) delle derivate
       di ht rispetto ai coefficienti si prende zero.  come valori
       iniziali dei residui si prende zero (gia' fatto dentro valunc)
    */

    lag = (nbeta > nalfa)? nbeta : nalfa;
    n = t2 - t1 + 1;

    for (t = t1-lag; t < t1; ++t) {
	for (i = 0; i < nc; ++i) {
	    asum2[i] = 0.0;
	    for (isp = t1; isp <= t2; ++isp) {
		asum2[i] -= res[isp] * 2.0 * g[i][isp];
	    }
	    asum2[i] /= n;
#ifdef FDEBUG
	    fprintf(stderr, "t=t1-lag=%d-%d=%d; writing to dhdp[%d][%d]\n",
		    t1, lag, t1-lag, i, t);
#endif
	    dhdp[i][t] = asum2[i];
	}
    }

    for (t = t1; t <= t2; ++t) {

	for (i = 0; i < nc; ++i) {
	    dhdp[i][t] = 0.0;
	}

	for (i = 0; i < nc; ++i) {
	    for (j = 1; j <= nalfa; ++j) {
		if (t - nalfa < t1) {
		    dhdp[i][t] += alfa[j-1] * asum2[i];
		} else {
		    dhdp[i][t] -= 
			alfa[j-1] * 2.0 * g[i][t-j] * res[t-j];
		}
	    }
	}

	for (i = 0; i < nc; ++i) {
	    for (j = 1; j <= nbeta; ++j) {
		dhdp[i][t] += dhdp[i][t-j] * beta[j-1];
	    }
	}
    }

    /* si inizia il calcolo del gradiente aux3 */

    for (i = 0; i < nparam; ++i) {
	aux3[i] = 0.0;
    }

    for (t = t1; t <= t2; ++t) {

	/* prima parte relativa ai coefficienti (eq. 22 pag. 316) err. di 
	   stampa nel secondo termine c'e' un *ht invece di /ht 
	*/
	rsuh = res[t] / h[t];
	r2suh = rsuh * res[t];

	for (i = 0; i < nc; ++i) {
	    aux3[i] += rsuh * g[i][t] + .5 / h[t] * 
		dhdp[i][t] * (r2suh - 1.0);
	}

	/* seconda parte relativa ad alfa e beta (eq. 19 pag. 315) */
	for (i = 0; i < nvparm; ++i) {
	    aux3[nc + i] += 
		.5 / h[t] * dhdp[nc+i][t] * (r2suh - 1.0);
	}
    }

    /* ora si riempie la matinf */

    for (i = 0; i < nparam; ++i) {
	for (j = 0; j < nparam; ++j) {
	    vc5[vix(i,j)] = 0.0;
	}
    }

    for (t = t1; t <= t2; ++t) {

	rsuh = res[t] / h[t];
	r2suh = rsuh * res[t];
	r2suh3 = r2suh / (h[t] * h[t]);

	/* parte relativa ai coefficienti (eq. 23 pag. 316) 
	   si ricorda che si prende il valore atteso e restano solo i primi
	   due termini 
	*/

	for (i = 0; i < nc; ++i) {
	    for (j = 0; j < nc; ++j) {
		vc5[vix(i,j)] = vc5[vix(i,j)] 
		    - g[i][t] * g[j][t] / h[t] 
		    - dhdp[i][t] * .5 * dhdp[j][t] / (h[t] * h[t]);
	    }
	}

	/* parte relativa ad alfa e beta  (eq. 20 pag. 315) 
	   si ricorda che si prende il valore atteso e resta solo il secondo 
	   termine 
	*/

	for (i = nc; i < nparam; ++i) {
	    for (j = nc; j < nparam; ++j) {
		vc5[vix(i,j)] -= .5 * dhdp[i][t] * 
		    dhdp[j][t] / (h[t] * h[t]);
	    }
	}
    }

#if FFDEBUG
    for (i=0; i<nparam * nparam; i++) {
	fprintf(stderr, "vc5[%d] = %.9g\n", i, vc5[i]);
    }
#endif

    /* adesso si inverte la matinf */

    ier5 = invert(vc5, nparam);
    if (ier5 != 0) {
	fprintf(stderr, "matrix inversion failed\n");
    }

    /* adesso si comincia con le iterazioni */

    /* calcolare lo step per i nuovi coefficenti */
    s_2 = 0.0;
    for (i = 0; i < nparam; ++i) {
	gg[i] = param[i];
	step[i] = 0.0;
	for (j = 0; j < nparam; ++j) {
	    step[i] -= vc5[vix(i,j)] * aux3[j];
	}
	s_2 += step[i] * step[i];
    }

    /* if relative euclidean distance is used as converg. */
    s_1 = 0.0;
    for (i = 0; i < nparam; ++i) {
	s_1 += param[i] * param[i];
    }
    if (s_1 == 0.0) s_1 = 1e-10;
    stre = s_2 / s_1;
    s_2 = sqrt(s_2);
    stre = sqrt(stre);

#ifdef FDEBUG
    fprintf(stderr, "s_2 = %.9g\n", s_2);
#endif

    oldstp = s_2;
    for (i = 0; i < nparam; ++i) {
	step[i] /= s_2;
    }

    it4 += iv;
    ds = s_2;
    if (stre <= toler) {
	goto L496;
    }

    ncall = 0;
    nexp = *ivolta / 5;
    if (nexp > 5) {
	nexp = 5;
    }

    cappa = pow(2.0, nexp);
    d0 = s_2;
    d0 /= cappa;
    dac = d0 * .001;
    dub = d0 * 4.0;

    /* print d0, dac, dub? */

    if (*ivolta == 1) {
	ll1 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
			X, nx, t1, t2, param, b, alfa0, alfa, 
			beta, nalfa, nbeta, h);
#ifdef FFDEBUG
	fprintf(stderr, "ivolta=1, ll1=%.9g\n", ll1);
#endif
    }
    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + step[i] * d0;
    }
    check_ht(param + nc, nvparm);

#ifdef FDEBUG
    for (i=0; i<nparam; i++) {
	fprintf(stderr, "param[%d] in matinf(2) = %.9g\n", i, param[i]);
    }
#endif 

    ll2 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);
#ifdef FDEBUG
    fprintf(stderr, "ll2=%.9g, ll1=%.9g\n", ll2, ll1);
#endif    
    if (ll2 > ll1) {
	goto L307;
    }

    d1 = 0.0;
    d2 = d0;
    d3 = d0 + d0;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + step[i] * d3;
    }
    check_ht(param + nc, nvparm);

#ifdef FDEBUG
    for (i=0; i<nparam; i++) {
	fprintf(stderr, "param[%d] in matinf(3) = %.9g\n", i, param[i]);
    }
#endif    

    ll3 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);
    goto L325;

 L307:
    d1 = -d0;
    d2 = 0.0;
    d3 = d0;
    ll3 = ll2;
    ll2 = ll1;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + step[i] * d1;
    }
    check_ht(param + nc, nvparm);

#ifdef FDEBUG
    for (i=0; i<nparam; i++) {
	fprintf(stderr, "param[%d] in matinf(4) = %.9g\n", i, param[i]);
    }
#endif 

    ll1 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);

 L325:
    d23 = d2 - d3;
    d31 = d3 - d1;
    d12 = d1 - d2;
    di = d23 * ll1 + d31 * ll2 + d12 * ll3;
    bigd = di * -2.0 / (d23 * d31 * d12);

    if (bigd > 0.0) {
	goto L400;
    }
    if (ll3 <= ll1) {
	goto L341;
    }

 L329:
    d3 = d2;
    ll3 = ll2;
    d2 = d1;
    ll2 = ll1;
    d1 -= dub;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + d1 * step[i];
    }
    check_ht(param + nc, nvparm);

#ifdef FDEBUG
    for (i=0; i<nparam; i++) {
	fprintf(stderr, "param[%d] in matinf(5) = %.9g\n", i, param[i]);
    }
#endif 

    ll1 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);

    if (++ncall > 100) {
	goto L490;
    }
    goto L325;

 L341:
    d1 = d2;
    ll1 = ll2;
    d2 = d3;
    ll2 = ll3;
    d3 += dub;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + d3 * step[i];
    }
    check_ht(param + nc, nvparm);

#ifdef FDEBUG
    for (i=0; i<nparam; i++) {
	fprintf(stderr, "param[%d] in matinf(6) = %.9g\n", i, param[i]);
    }
#endif

    ll3 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);

    if (++ncall > 100) {
	goto L490;
    }
    goto L325;

 L400:
    d23s = d23 * (d2 + d3);
    d31s = d31 * (d3 + d1);
    d12s = d12 * (d1 + d2);
    ds = (d23s * ll1 + d31s * ll2 + d12s * ll3) * .5 / di;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + step[i] * ds;
    }
    check_ht(param + nc, nvparm);

#ifdef FDEBUG
    for (i=0; i<nparam; i++) {
	fprintf(stderr, "param[%d] in matinf(7) = %.9g\n", i, param[i]);
    }
#endif

    fs = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		   X, nx, t1, t2, param, b, alfa0, alfa, 
		   beta, nalfa, nbeta, h);

    if (++ncall > 100) {
	goto L490;
    }

    a1s = (d = d1 - ds, fabs(d));
    a2s = (d = d2 - ds, fabs(d));
    a3s = (d = d3 - ds, fabs(d));

    dm = a1s;
    if (a3s < dm) {
	dm = a3s;
    }
    if (dub >= dm) {
	goto L422;
    }
    if (ds < d1 - dub) {
	goto L329;
    }
    if (ds > d3 + dub) {
	goto L341;
    }

 L422:
    if (a1s < dac || a2s < dac || a3s < dac) {
	goto L490;
    }
    if (ll1 < ll2 || ll1 < ll3) {
	goto L434;
    }
    d1 = ds;
    ll1 = fs;
    goto L459;

 L434:
    if (ll2 < ll3 || ll2 < ll1) {
	goto L447;
    }
    d2 = ds;
    ll2 = fs;
    goto L459;

 L447:
    d3 = ds;
    ll3 = fs;

 L459:
    if (d2 <= d3) {
	goto L463;
    }
    dd = d2;
    ff = ll2;
    d2 = d3;
    ll2 = ll3;
    d3 = dd;
    ll3 = ff;

 L463:
    if (d1 <= d2) {
	goto L325;
    }
    dd = d1;
    ff = ll1;
    d1 = d2;
    ll1 = ll2;
    d2 = dd;
    ll2 = ff;
    goto L459;

 L490:
    if (fs <= ll1) {
	goto L491;
    }
    fs = ll1;
    ds = d1;

 L491:
    if (fs <= ll2) {
	goto L492;
    }
    fs = ll2;
    ds = d2;

 L492:
    if (fs <= ll3) {
	goto L496;
    }
    fs = ll3;
    ds = d3;

 L496:
    for (i = 0; i <nparam; ++i) {
	param[i] = gg[i] + ds * step[i];
    }
    check_ht(param + nc, nvparm);

#ifdef FDEBUG
    for (i=0; i<nparam; i++) {
	fprintf(stderr, "param[%d] in matinf(8) = %.9g\n", i, param[i]);
    }
#endif

    ll1 = fs;
    fs = -fs;
    it5 += iv;

    /* si cambia il segno alla matrice */
    for (i = 0; i < nparam; ++i) {
	for (j = 0; j < nparam; ++j) {
	    vc5[vix(i,j)] *= -1.0;
	}
    }

    free(gg);
    free(step);
    free(asum2);

    return 0;
} /* garch_info_matrix */

static void free_dhdpdp (double ***H, int np)
{
    int i, j;

    for (i=0; i<np; i++) {
	for (j=0; j<np; j++) {
	    free(H[i][j]);
	}
	free(H[i]);
    }
    
    free(H);
}

static double ***allocate_dhdpdp (int np, int nvp)
{
    double ***H;
    int i, j;

    H = malloc(np * sizeof *H);
    
    for (i=0; i<np; i++) {
	H[i] = malloc(np * sizeof **H);
	for (j=0; j<np; j++) {
	    H[i][j] = malloc(nvp * sizeof ***H);
	}
    }

    return H;
}

/*  i parametri sono passati dentro il vettore param c, alfa e beta 
    si ricavano all'inizio da param */

static int 
garch_full_hessian (int t1, int t2, 
		    const double **X, int nx, double *yhat, double *c, 
		    int nc, double *res2, double *res, double *ystoc,
		    double toler, int *ivolta, double *vc5, 
		    const double **g, double *aux3, 
		    double *param, int nparam, double *b, 
		    double *alfa0, double *alfa, double *beta, int nalfa,
		    int nbeta, double *h, double **dhdp, double *zt)
{
    int i, j, k, t;
    double d;
    double d0, d1, d2, ll2, d3; /* static? */
    double ll3, d12, d31, d23, dd, di, ff; /* static? */
    int iv, ncall, n, nexp, nvparm;
    double dm, ds; /* static? */
    double a1s, a2s, a3s;
    int it1, it2, it3, it4, it5;
    double dac, d12s, dub, d23s, d31s; /* static? */
    int isp, ier5;
    double bigd, s_2;
    double stre, rsuh, s_1, r2suh, r2suh3, usuh2;
    double cappa;
    int lag;
    static double ll1 = 0.0, fs = 0.0;
    double oldstp = 9.0e+39;

    double *gg, *step, *asum2;
    double ***H;

    gg = malloc(nparam * sizeof *gg);
    if (gg == NULL) {
	return 1;
    }

    step = malloc(nparam * sizeof *step);
    if (step == NULL) {
	free(gg);
	return 1;
    } 

    asum2 = malloc(nc * sizeof *asum2);
    if (asum2 == NULL) {
	free(gg);
	free(step);
	return 1;
    } 

    H = allocate_dhdpdp(nparam, nalfa + nbeta);
    if (H == NULL) {
	free(gg);
	free(step);
	free(asum2);
	return 1;
    }

    iv = 0;
    it1 = 0;
    it2 = 0;
    it3 = 0;
    it4 = 0;
    it5 = 0;
    nvparm = nalfa + nbeta + 1;

    ++(*ivolta);

    for (i = 0; i < nc; ++i) {
	c[i] = param[i];
    }
    *alfa0 = param[nc];
#ifdef FDEBUG
    fprintf(stderr, "hessian: alfa0=%.9g\n", *alfa0);
#endif

    for (i = 0; i < nalfa; ++i) {
	alfa[i] = param[nc + 1 + i];
#ifdef FDEBUG
	fprintf(stderr, "hessian: alfa[%d]=%.9g\n", i, alfa[i]);
#endif
    }

    for (i = 0; i < nbeta; ++i) {
	beta[i] = param[nc + 1 + nalfa + i];
#ifdef FDEBUG
	fprintf(stderr, "hessian: beta[%d]=%.9g\n", i, beta[i]);
#endif
    }

    /* 
       Inizio del calcolo di dhtdp e dhdpdp parte relativa ai parametri
       alfa e beta. Si comincia calcolando le derivate dei valori iniziali
       avendo scelto come val. iniziali la varianza noncondizionata. Come
       valore iniziale (ai tempi 0, -1, -2, ecc.) di ht si impiega la
       varianza noncondizionata calcolata dai residui.
    */

    for (k = 0; k < nbeta; ++k) { /* or 1 to <= nbeta ? */
	for (i = 0; i < nvparm; ++i) {
	    dhdp[nc+i][t1-k] = 0.0;
	    for (j = 0; j < nvparm; ++j) {
		H[nc+i][nc+j][k+1] = 0.0;
	    }
	}
    }

    /* costruzione matrice dhtdp, parte relativa a alfa e beta (eq. 21) */

    for (t = t1; t <= t2; ++t) {

	/* si riempie zt al tempo t (p. 315) */

	zt[0] = 1.0;

	for (i = 1; i <= nalfa; ++i) {
	    zt[i] = res2[t-i];
	}

	for (i = 1; i <= nbeta; ++i) {
	    zt[nalfa+i] = h[t-i];
	}

	/*  si riempie dhtdp al tempo t la parte relativa ai parametri 
	    alfa e beta (eq. 21 p. 316) */

	for (i = 0; i < nvparm; ++i) {
	    dhdp[nc+i][t] = 0.0;
	}	

	for (i = 0; i < nvparm; ++i) {
	    dhdp[nc+i][t] += zt[i];
	}

	for (i = 0; i < nvparm; ++i) {
	    for (j = 1; j <= nbeta; ++j) {
		dhdp[nc+i][t] += dhdp[nc+i][t-j] * beta[j-1];
	    }
	}
    }

    /* 
       costruzione matrice dhtdp, parte relativa ai coefficienti
       (eq.24) come valori iniziali (tempo 0, -1, ecc.) delle derivate
       di ht rispetto ai coefficienti si prende der di uncvar rispetto
       ai coeff come valori iniziali dei residui si prende zero (gia'
       fatto dentro valunc) costruzione della matrice dhdpdp iniziale
    */

    lag = (nbeta > nalfa)? nbeta : nalfa;
    n = t2 - t1 + 1;

    for (t = t1-lag; t < t1; ++t) {
	for (i = 0; i < nc; ++i) {
	    asum2[i] = 0.0;
	    for (isp = t1; isp <= t2; ++isp) {
		asum2[i] -= res[isp] * 2.0 * g[i][isp];
	    }
	    asum2[i] /= n;
	    dhdp[i][t] = asum2[i];
	}
    }

    /*  i valori iniziali di dhdpdp sono 2/t x'x e zero per i blocchi 
	fuori diagonale */

    for (t = 0; t < lag; ++t) {
	for (i = 0; i < nc; ++i) {
	    for (j = 0; j < nc; ++j) {
		H[i][j][t+1] = 0.; /* FIXME t or t+1? */
	    }
	}
	for (isp = t1; isp <= t2; ++isp) {
	    for (i = 0; i < nc; ++i) {
		for (j = 0; j < nc; ++j) {
		    H[i][j][t+1] += 2.0 *  /* t or t+1? */
			g[i][isp] * g[j][isp] / n;
#ifdef FFDEBUG  
		    if (isp == t1 || isp == t2) {
			fprintf(stderr, "set H[%d][%d][%d] = %.9g\n",
				i,j,t+1, H[i][j][t+1]); /* t or t+1? */
		    }
#endif
		}
	    }
	}
	for (i = 0; i < nc; ++i) {
	    for (j = 0; j < nvparm; ++j) {
		H[i][nc+j][t+1] = 0.0; /* t or t+1? */
	    }
	}
    }

    for (t = t1; t <= t2; ++t) {

	for (i = 0; i < nc; ++i) {
	    dhdp[i][t] = 0.0;
	}

	for (i = 0; i < nc; ++i) {
	    for (j = 1; j <= nalfa; ++j) {
		if (t - nalfa < t1) {
		    dhdp[i][t] += alfa[j-1] * asum2[i];
		} else {
		    dhdp[i][t] -= 2.0 *
			alfa[j-1] * g[i][t-j] * res[t-j];
		}
	    }
	}

	for (i = 0; i < nc; ++i) {
	    for (j = 1; j <= nbeta; ++j) {
		dhdp[i][t] += dhdp[i][t-j] * beta[j-1];
	    }
	}

#ifdef FFDEBUG
	if (t == t1 || t == t2) {
	    for (i = 0; i < nc; ++i) {
		fprintf(stderr, "I(%d,%d) = %.9g\n", i,t, dhdp[i][t]);
	    }
	}
#endif

    }

    /*  si inizia il calcolo del gradiente aux3 */

    for (i = 0; i < nparam; ++i) {
	aux3[i] = 0.0;
    }

    for (t = t1; t <= t2; ++t) {

	/*  prima parte relativa ai coefficienti (eq. 22, p. 316) err. di 
	    stampa nel secondo termine c'e' un *ht invece di /ht 
	*/
	rsuh = res[t] / h[t];
	r2suh = rsuh * res[t];

	for (i = 0; i < nc; ++i) {
	    aux3[i] += rsuh * g[i][t] 
		+ .5 / h[t] * dhdp[i][t] * (r2suh - 1.0);
	}

#ifdef FFDEBUG
	if (t == t1 || t == t2) {
	    for (i=0; i<nc; i++) {
		fprintf(stderr, " (prima) aux3[%d] = %.9g\n", i, aux3[i]);
	    }
	}
#endif

	/* seconda parte relativa ad alfa e beta (eq. 19,  p. 315) */
	for (i = 0; i < nvparm; ++i) {
	    aux3[nc + i] += 
		.5 / h[t] * dhdp[nc+i][t] * (r2suh - 1.0);
	}

#ifdef FFDEBUG
	if (t == t1 || t == t2) {
	    for (i=0; i<nvparm; i++) {
		fprintf(stderr, " (seconda) aux3[%d] = %.9g\n", nc+i, aux3[nc+i]);
	    }
	}
#endif

    }

#ifdef FFDEBUG
    for (i=0; i<nvparm; i++) {
	fprintf(stderr, " aux3[%d] = %.9g\n", i, aux3[i]);
    }
#endif

    /* ora si riempie la hess */

    for (i = 0; i < nparam; ++i) {
	for (j = 0; j < nparam; ++j) {
	    vc5[vix(i,j)] = 0.0;
	}
    }

    for (t = t1; t <= t2; ++t) {

	rsuh = res[t] / h[t];
	r2suh = rsuh * res[t];
	r2suh3 = r2suh / (h[t] * h[t]);
	usuh2 = 1.0 / (h[t] * h[t]);

	for (i = 0; i < nparam; ++i) {
	    for (j = 0; j < nparam; ++j) {
		H[i][j][0] = 0.0; 
	    }
	}

	if (lag <= 0) {
	    goto L90;
	}

	for (k = 1; k <= nalfa; ++k) {
	    for (i = 0; i < nc; ++i) {
		for (j = 0; j < nc; ++j) {
		    if (t - nalfa < t1) {
			H[i][j][0] += H[i][j][nalfa] * alfa[k-1];
		    } else {
			H[i][j][0] += 2.0 *
			    g[i][t-k] * g[j][t-k] * alfa[k-1];
		    }
#ifdef FFDEBUG
		    if (t==t1 || t==t2) {
			fprintf(stderr, "step1 H[%d][%d][0]=%.9g\n", 
				i, j, H[i][j][0]);
		    }
#endif
		}
	    }
	}

	for (k = 1; k <= nbeta; ++k) {
	    for (i = 0; i < nc; ++i) {
		for (j = 0; j < nc; ++j) {
		    H[i][j][0] += H[i][j][k] * beta[k-1];
		}
#ifdef FFDEBUG
		if (t==t1 || t==t2) {
		    fprintf(stderr, "step2 H[%d][%d][0]=%.9g\n", 
			    i, j, H[i][j][0]);
		}
#endif
	    }
	}

	for (i = 0; i < nc; ++i) {
	    for (k = 1; k <= nalfa; ++k) {
		if (t - nalfa < t1) {
		    H[i][nc+k][0] += asum2[i];
		} else {
		    H[i][nc+k][0] -= 2.0 * g[i][t-k] * res[t-k];
		}
#ifdef FFDEBUG
		if (t==t1 || t==t2) {
		    fprintf(stderr, "step3 H[%d][%d][0]=%.9g\n", 
			    i, nc+k, H[i][nc+k][0]);
		}
#endif
	    }
	    for (k = 1; k <= nbeta; ++k) {
		H[i][nc+nalfa+k][0] += dhdp[i][t-k];
	    }
#ifdef FFDEBUG
	    if (t==t1 || t==t2) {
		fprintf(stderr, "step4 H[%d][%d][0]=%.9g\n", 
			i, nc+nalfa+k, H[i][nc+nalfa+k][0]);
	    }
#endif
	}

	for (k = 1; k <= nbeta; ++k) { 
	    for (i = 0; i < nc; ++i) {
		for (j = 0; j < nvparm; ++j) {
		    /* possible uninitialized data */
		    H[i][nc+j][0] += H[i][nc+j][k] * beta[k-1];
#ifdef FFDEBUG
		    if (t==t1 || t==t2) {
			fprintf(stderr, "step5 H[%d][%d][0] += "
				"H[%d][%d][%d] * beta[%d]: %.9g*%.9g -> %.9g\n", 
				i, nc+j,i,nc+j,k,k-1,H[i][nc+j][k], beta[k-1],
				H[i][nc+j][0]);
		    }
#endif
		}
	    }
	}
    L90:

	/*  parte relativa ai coefficienti (eq. 23 pag. 316) 
	    si ricorda che si prende il valore atteso e restano solo i primi 
	    due termini 
	*/
	for (i = 0; i < nc; ++i) {
	    for (j = 0; j < nc; ++j) {
		vc5[vix(i,j)] = vc5[vix(i,j)] 
		    - g[i][t] * g[j][t] / h[t] 
		    - .5* r2suh3 * dhdp[i][t] * dhdp[j][t] 
		    - (rsuh * g[j][t] * dhdp[i][t]) / h[t] 
		    - (rsuh * g[i][t] * dhdp[j][t]) / h[t] 
		    + 0.5 * (r2suh - 1.0) * 
		    (H[i][j][0] / h[t] - dhdp[i][t] 
		     * dhdp[j][t] / (h[t] * h[t]));
	    }
	}

	/*  parte relativa ad alfa e beta  (eq. 20 pag. 315) 
	    si ricorda che si prende il valore atteso e resta solo il secondo 
	    termine 
	*/

	/*  calcolo di dhdpdp al tempo t che va' nel posto 1 del terzo indice */

	if (nbeta > 0) {
	    for (i = 0; i < nvparm; ++i) {
		for (j = 1; j <= nbeta; ++j) {
		    H[nc+i][nc+nalfa+j][0] += dhdp[nc+i][t-j];
		}
	    }
	    for (i = 1; i <= nbeta; ++i) {
		for (j = 0; j < nvparm; ++j) {
		    H[nc+nalfa+i][nc+j][0] += dhdp[nc+j][t-i];
		}
	    }
	    for (k = 1; k <= nbeta; ++k) {
		for (i = 0; i < nvparm; ++i) {
		    for (j = 0; j < nvparm; ++j) { 
			/* FIXME uninitialized data */
			H[nc+i][nc+j][0] += H[nc+i][nc+j][k] * beta[k-1];
#ifdef FDEBUG
			if ((t==t1 || t==t2)) {
			    fprintf(stderr, "Set H[%d][%d][0] = %.9g\n",
				    nc+i, nc+j, H[nc+i][nc+j][0]);
			    fprintf(stderr, "incr = %.9g * %.9g\n",
				    H[nc+i][nc+j][k], beta[k-1]);
			}
#endif
		    }
		}
	    }
	}

	for (i = nc; i < nparam; ++i) {
	    for (j = nc; j < nparam; ++j) {
		vc5[vix(i,j)] = vc5[vix(i,j)]
		    + .5 * usuh2 * dhdp[i][t] * dhdp[j][t] 
		    - r2suh3 * dhdp[i][t] * dhdp[j][t] 
		    + .5 * (r2suh - 1.0) / h[t] * H[i][j][0];
#ifdef FDEBUG
		    if ((t==t1 || t==t2)) {
			fprintf(stderr, "Set vc5[%d] = %.9g (using Hval %.9g)\n",
				vix(i,j), vc5[vix(i,j)], H[i][j][0]);
		    }
#endif
	    }
	}

	/*  parte mista in alto destra */
	for (i = 0; i < nc; ++i) {
	    for (j = 0; j < nvparm; ++j) {
		vc5[vix(i,nc+j)] = vc5[vix(i,nc+j)]
		    - g[i][t] * rsuh * dhdp[nc+j][t] / h[t] 
		    - .5 * (r2suh - 1.0) * dhdp[nc+j][t] * 
		    dhdp[i][t] / (h[t] * h[t]) 
		    + .5 * (r2suh - 1.0) * H[i][nc+j][0] / h[t] 
		    - .5 * r2suh * usuh2 * dhdp[i][t] * dhdp[nc+j][t];
	    }
	}

	/* prima di uscire dal tempo t, si risistema la dhdpdp */
	for (k = 0; k < lag; ++k) { /* index, "lag", FIXME? */
	    for (i = 0; i < nparam; ++i) {
		for (j = 0; j < nparam; ++j) {
		    H[i][j][lag-k] = H[i][j][lag-k-1];
#ifdef FFDEBUG
		    if (t==t1 || t==t2) {
			fprintf(stderr, "Set H[%d][%d][%d] = H[%d][%d][%d] = %.9g\n",
				i,j,lag-k,i,j,lag-k-1, H[i][j][lag-k-1]);
		    }
#endif		    
		}
	    }
	}
    }

    /*  il do 25 sul tempo e' finito e allora si riempie la parte 
	mista in basso a sinistra 
    */

    for (i = 0; i < nc; ++i) {
	for (j = 0; j < nvparm; ++j) {
#ifdef FDEBUG
	    fprintf(stderr, "setting vc5[%d]=vc5[%d]=%.9g\n",
		    vix(nc+j, i), vix(i, nc+j), vc5[vix(i, nc+j)]);
#endif
	    vc5[vix(nc+j, i)] = vc5[vix(i, nc+j)];
	}
    }

    /* adesso si inverte la hess */

    ier5 = invert(vc5, nparam);
    if (ier5 != 0) {
	fprintf(stderr, "matrix inversion failed\n");
    }

    /* adesso si comincia con le iterazioni */

    /* calcolare lo step per i nuovi coefficenti */
    s_2 = 0.0;
    for (i = 0; i < nparam; ++i) {
	gg[i] = param[i];
	step[i] = 0.0;
	for (j = 0; j < nparam; ++j) {
	    step[i] -= vc5[vix(i,j)] * aux3[j];
	}
	s_2 += step[i] * step[i];
    }

    /* if relative euclidean distance is used as converg. */
    s_1 = 0.0;
    for (i = 0; i < nparam; ++i) {
	s_1 += param[i] * param[i];
    }
    if (s_1 == 0.0) s_1 = 1e-10;
    stre = s_2 / s_1;
    s_2 = sqrt(s_2);
    stre = sqrt(stre);

    oldstp = s_2;
    for (i = 0; i < nparam; ++i) {
	step[i] /= s_2;
    }

    it4 += iv;
    ds = s_2;
    if (stre <= toler) {
	goto L496;
    }

    ncall = 0;
    nexp = *ivolta / 5;
    if (nexp > 5) {
	nexp = 5;
    }

    cappa = pow(2.0, nexp);
    d0 = s_2;
    d0 /= cappa;
    dac = d0 * .001;
    dub = d0 * 4.0;

    if (*ivolta == 1) {
	ll1 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
			X, nx, t1, t2, param, b, alfa0, alfa, 
			beta, nalfa, nbeta, h);
#ifdef FDEBUG
	fprintf(stderr, "hess: ll1 = %.9g\n", ll1);
#endif  
    }

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + step[i] * d0;
    }
    check_ht(param + nc, nvparm);

    ll2 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);
#ifdef FDEBUG
    fprintf(stderr, "hess: ll2 = %.9g\n", ll2);
#endif    
    if (ll2 > ll1) {
	goto L307;
    }

    d1 = 0.0;
    d2 = d0;
    d3 = d0 + d0;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + step[i] * d3;
    }
    check_ht(param + nc, nvparm);

    ll3 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);
#ifdef FDEBUG
    fprintf(stderr, "hess: ll3 = %.9g\n", ll3);
#endif    
    goto L325;

 L307:
    d1 = -d0;
    d2 = 0.0;
    d3 = d0;
    ll3 = ll2;
    ll2 = ll1;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + step[i] * d1;
    }
    check_ht(param + nc, nvparm);

    ll1 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);
#ifdef FDEBUG
    fprintf(stderr, "hess: ll1(2) = %.9g\n", ll1);
#endif    


 L325:
    d23 = d2 - d3;
    d31 = d3 - d1;
    d12 = d1 - d2;
    di = d23 * ll1 + d31 * ll2 + d12 * ll3;
    bigd = di * -2.0 / (d23 * d31 * d12);
#ifdef FDEBUG
    fprintf(stderr, "hess: bigd = %.9g\n", bigd);
#endif    
    if (bigd > 0.0) {
	goto L400;
    }
    if (ll3 <= ll1) {
	goto L341;
    }

 L329:
    d3 = d2;
    ll3 = ll2;
    d2 = d1;
    ll2 = ll1;
    d1 -= dub;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + d1 * step[i];
    }
    check_ht(param + nc, nvparm);

    ll1 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);
#ifdef FDEBUG
    fprintf(stderr, "hess: ll1(3) = %.9g\n", ll1);
#endif    

    if (++ncall > 100) {
	goto L490;
    }
    goto L325;

 L341:
    d1 = d2;
    ll1 = ll2;
    d2 = d3;
    ll2 = ll3;
    d3 += dub;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + d3 * step[i];
    }
    check_ht(param + nc, nvparm);

    ll3 = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		    X, nx, t1, t2, param, b, alfa0, alfa, 
		    beta, nalfa, nbeta, h);
#ifdef FDEBUG
    fprintf(stderr, "hess: ll3(2) = %.9g\n", ll3);
#endif    

    if (++ncall > 100) {
	goto L490;
    }
    goto L325;

 L400:
    d23s = d23 * (d2 + d3);
    d31s = d31 * (d3 + d1);
    d12s = d12 * (d1 + d2);
    ds = (d23s * ll1 + d31s * ll2 + d12s * ll3) * .5 / di;

    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + step[i] * ds;
    }
    check_ht(param + nc, nvparm);

    fs = -garch_ll(c, nc, res2, res, yhat, ystoc, 
		   X, nx, t1, t2, param, b, alfa0, alfa, 
		   beta, nalfa, nbeta, h);
#ifdef FDEBUG
    fprintf(stderr, "hess: fs = %.9g, ds = %.9g\n", fs, ds);
#endif    

    if (++ncall > 100) {
	goto L490;
    }

    a1s = (d = d1 - ds, fabs(d));
    a2s = (d = d2 - ds, fabs(d));
    a3s = (d = d3 - ds, fabs(d));

    dm = a1s;
    if (a3s < dm) {
	dm = a3s;
    }
    if (dub >= dm) {
	goto L422;
    }
    if (ds < d1 - dub) {
	goto L329;
    }
    if (ds > d3 + dub) {
	goto L341;
    }

 L422:
    if (a1s < dac || a2s < dac || a3s < dac) {
	goto L490;
    }
    if (ll1 < ll2 || ll1 < ll3) {
	goto L434;
    }
    d1 = ds;
    ll1 = fs;
    goto L459;

 L434:
    if (ll2 < ll3 || ll2 < ll1) {
	goto L447;
    }
    d2 = ds;
    ll2 = fs;
    goto L459;

 L447:
    d3 = ds;
    ll3 = fs;

 L459:
    if (d2 <= d3) {
	goto L463;
    }
    dd = d2;
    ff = ll2;
    d2 = d3;
    ll2 = ll3;
    d3 = dd;
    ll3 = ff;

 L463:
    if (d1 <= d2) {
	goto L325;
    }
    dd = d1;
    ff = ll1;
    d1 = d2;
    ll1 = ll2;
    d2 = dd;
    ll2 = ff;
    goto L459;
 
L490:
    if (fs <= ll1) {
	goto L491;
    }
    fs = ll1;
    ds = d1;

 L491:
    if (fs <= ll2) {
	goto L492;
    }
    fs = ll2;
    ds = d2;

 L492:
    if (fs <= ll3) {
	goto L496;
    }
    fs = ll3;
    ds = d3;

 L496:
    for (i = 0; i < nparam; ++i) {
	param[i] = gg[i] + ds * step[i];
    }

    check_ht(param + nc, nvparm);

    ll1 = fs;
    fs = -fs;
    it5 += iv;

    /* si cambia il segno alla matrice */
    for (i = 0; i < nparam; ++i) {
	for (j = 0; j < nparam; ++j) {
	    vc5[vix(i,j)] *= -1.0;
	}
    }

    free(gg);
    free(step);
    free(asum2);
    free_dhdpdp(H, nparam);

    return 0;
} /* garch_full_hessian */


/* Standard for models with no restrictions on the structural 
   coefficients, such as distributed lags, or restrictions on 
   the sum (Cobb-Douglas), etc.
*/


static int invert (double *g, int dim)
{
    integer m = dim;
    integer info, lwork;
    integer *ipiv;
    double *work;

#ifdef FDEBUG
    if (1) {
	int i, j;
	static int k;

	fprintf(stderr, "Mat invert[%d], got n=%d\n", k, dim);

	for (i=0; i<dim; i++) {
	    for (j=0; j<dim; j++) {
		fprintf(stderr, "%14.9g ", g[i + dim * j]);
	    }
	    fputc('\n', stderr);
	}
	k++;
    }
#endif

    ipiv = malloc(m * sizeof *ipiv);
    if (ipiv == NULL) {
	return 1;
    }

    work = malloc(sizeof *work);
    if (work == NULL) {
	free(ipiv);
	return 1;
    }  

    dgetrf_(&m, &m, g, &m, ipiv, &info);   

    if (info != 0) {
	free(ipiv);
	free(work);
	return 1;
    }

    lwork = -1;
    dgetri_(&m, g, &m, ipiv, work, &lwork, &info);

    if (info != 0 || work[0] <= 0.0) {
	free(ipiv);
	free(work);
	return 1;
    }

    lwork = (integer) work[0];

    work = realloc(work, lwork * sizeof *work);
    if (work == NULL) {
	free(ipiv);
	return 1;
    }  

    dgetri_(&m, g, &m, ipiv, work, &lwork, &info);

    free(work);
    free(ipiv);

    return (int) info;
}
