/* gretl - The Gnu Regression, Econometrics and Time-series Library
 * Copyright (C) 1999-2000 Ramu Ramanathan and Allin Cottrell
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

#ifndef LIBGRETL_H
#define LIBGRETL_H

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#define MAXLABEL 128
#define MAXLEN 512
#define ERRLEN 256

enum format_codes {
    TEXT,
    LATEX,
    HTML,
    RTF
};

/* information on data set */
typedef struct { 
    int v;              /* number of variables */
    int n;              /* number of observations */
    int pd;             /* periodicity or frequency of data */
    int bin;            /* data file is binary? (1 or 0) */
    int extra;
    double sd0;         /* float representation of stobs */    
    int t1, t2;         /* start and end of current sample */
    char stobs[8];      /* string representation of starting obs (date) */
    char endobs[8];     /* string representation of ending obs */
    char **varname;     /* array of names of variables */
    char **label;       /* array of descriptive labels for vars */
    short markers;
    short time_series;
    char **S;           /* to hold observation markers */
} DATAINFO;

typedef struct {
    char currdir[MAXLEN];
    char userdir[MAXLEN];
    char gretldir[MAXLEN];
    char datadir[MAXLEN];
    char scriptdir[MAXLEN];
    char helpfile[MAXLEN];
    char cmd_helpfile[MAXLEN];
    char hdrfile[MAXLEN];
    char datfile[MAXLEN];
    char lblfile[MAXLEN];
    char plotfile[MAXLEN];
    char binbase[MAXLEN];
    char ratsbase[MAXLEN];
    char gnuplot[MAXLEN];
#ifdef OS_WIN32
    char pgnuplot[MAXLEN];
#endif
    char dbhost_ip[16];
} PATHS;

typedef struct {
    char type[72];
    char h_0[64];
    char teststat[48];
    char pvalue[48];
} GRETLTEST;

typedef struct {
    int n;
    int *list;
    double *xskew, *xkurt, *xmedian, *coeff, *sderr, *xpx, *xpy;
} GRETLSUMMARY;

/* struct to hold model results */
typedef struct {
    int ID;                      /* ID number for model */
    int t1, t2, nobs;            /* starting observation, ending
                                    observation, and number of obs */
    double *subdum;              /* keep track of sub-sample in force
                                    when model was estimated */
    int ncoeff, dfn, dfd;        /* number of coefficents; degrees of
                                    freedom in numerator and denominator */
    int *list;                   /* list of variables by ID number */
    int ifc;                     /* = 1 if the equation includes a constant,
				    else = 0 */
    int ci;                      /* "command index" -- depends on 
				    estimation method */
    int nwt;                     /* ID of the weight variable (WLS) */
    int wt_dummy;                /* Is the weight var a 0/1 dummy? */
    int archp;                   /* ARCH lag order */
    int aux;                     /* code representing the sort of
				    auxiliary regression this is (or not) */
    int ldepvar;                 /* = 1 if there's a lag of the
				    dependent variable on the RHS, else 0 */
    int correct;                 /* cases 'correct' (binary depvar) */
    double *coeff;               /* array of coefficient estimates */
    double *sderr;               /* array of estimated std. errors */
    double *uhat;                /* regression residuals */
    double *yhat;                /* fitted values from regression */
    double *xpx;
    double *vcv;                 /* VCV matrix for coeff. estimates */
    double ess, tss;             /* Error and Total Sums of Squares */
    double sigma;                /* Standard error of regression */
    double ess_wt;               /* ESS using weighted data (WLS) */
    double sigma_wt;             /* same thing for std.error */
    double rsq, adjrsq;          /* Unadjusted and adjusted R^2 */     
    double fstt;                 /* F-statistic */
    double lnL;                  /* log-likelihood */
    double chisq;                /* Chi-square */
    double ybar, sdy;            /* mean and std. dev. of dependent var. */
    double criterion[8];         /* array of model selection statistics */
    double dw, rho;              /* Durbin-Watson stat. and estimated 1st
				    order autocorrelation coefficient */
    double rho_in;               /* the rho value input into the 
				    regression (e.g. CORC) */
    int *arlist;                 /* list of autoreg lags */
    double *rhot;                /* array of autoreg. coeffs. */
    double *slope;               /* for nonlinear models */
    int errcode;                 /* Error code in case of failure */
    char errmsg[ERRLEN];
    char infomsg[ERRLEN];
    char *name;
    int ntests;
    GRETLTEST *tests;
} MODEL;

typedef struct {
    char *cmd;
    double *subdum;
} MODELSPEC;

typedef struct {
    int ID;
    char *name;
    char *fname;
} GRAPHT; 

typedef struct {
    int nmodels;
    int ngraphs;
    MODEL **models;
    GRAPHT **graphs;
} SESSION; 

typedef struct {
    int nmodels;
    int *model_ID;
    char **model_name;
} session_t;

typedef struct {
    FILE *fp;
    char *buf;
    size_t bufsize;
} print_t;


#include "commands.h"
#include "errors.h"
#include "estimate.h"
#include "generate.h"
#include "utils.h"
#include "compare.h"
#include "pvalues.h"
#include "dataio.h"
#include "strutils.h"
#include "describe.h"
#include "printout.h"
#include "texprint.h"
#include "var.h"
#include "interact.h"
#include "graphing.h"
#include "monte_carlo.h"
#include "nonparam.h"
#include "discrete.h"
#include "subsample.h"
#ifdef OS_WIN32
# include "win32.h"
#endif

#endif /* LIBGRETL_H */
