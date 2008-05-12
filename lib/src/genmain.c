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

/* driver module for 'genr' and related commands */

#include "genparse.h"
#include "libset.h"
#include "gretl_func.h"

#if GENDEBUG
# define GDEBUG 1
#else
# define GDEBUG 0
#endif

static void gen_write_message (const parser *p, int oldv, PRN *prn)
{
    if (prn == NULL || !gretl_messages_on()) {
	return;
    }

    if (p->targ == NUM) {
	if (var_is_series(p->dinfo, p->lh.v)) {
	    pprintf(prn, _("Modified series %s (ID %d)"),
		    p->lh.name, p->lh.v);
	} else {
	    int t = lh_obs(p);
	    double x = (*p->Z)[p->lh.v][t];

	    if (p->lh.v < oldv) {
		pprintf(prn, _("Replaced scalar %s (ID %d)"),
			p->lh.name, p->lh.v);
	    } else {
		pprintf(prn, _("Generated scalar %s (ID %d)"),
			p->lh.name, p->lh.v);
	    }
	    if (na(x)) {
		pputs(prn, " = NA");
	    } else {
		pprintf(prn, " = %g", x);
	    }
	}
    } else if (p->targ == VEC) {
	if (p->lh.v < oldv) {
	    pprintf(prn, _("Replaced series %s (ID %d)"),
		    p->lh.name, p->lh.v);
	} else {
	    pprintf(prn, _("Generated series %s (ID %d)"),
		    p->lh.name, p->lh.v);
	}
    } else if (p->targ == MAT) {
	if ((p->flags & P_LHMAT) && p->lh.substr != NULL && 
	    *p->lh.substr != '\0') {
	    pprintf(prn, _("Modified matrix %s"), p->lh.name);
	} else if (p->flags & P_LHMAT) {
	    pprintf(prn, _("Replaced matrix %s"), p->lh.name);
	} else {
	    pprintf(prn, _("Generated matrix %s"), p->lh.name);
	}
    } else if (p->targ == LIST) {
	if (p->flags & P_LHLIST) {
	    pprintf(prn, _("Replaced list %s"), p->lh.name);
	} else {
	    pprintf(prn, _("Generated list %s"), p->lh.name);
	}
    } else if (p->targ == STR) {
	if (p->flags & P_LHSTR) {
	    pprintf(prn, _("Replaced string %s"), p->lh.name);
	} else {
	    pprintf(prn, _("Generated string %s"), p->lh.name);
	}
    } else {
	return;
    }

    pputc(prn, '\n');
}

static void gen_write_warning (const parser *p, PRN *prn)
{
    if (prn != NULL && !repeating_function_exec()) {
	if (*p->warning != '\0') {
	    pprintf(prn, "%s: %s\n", 
		    p->warning, _("missing values were generated"));
	} else {
	    pprintf(prn, "%s: %s\n", _("Warning"),
		    _("missing values were generated"));
	}
    }
}

static int maybe_record_lag_info (parser *p)
{
    const char *s = p->input;
    int n = strlen(p->lh.name);
    char vname[VNAMELEN];
    int lag;

    if (!strncmp(s, "genr ", 5)) {
	s += 5;
    } else if (!strncmp(s, "series ", 7)) {
	s += 7;
    }

    s += strspn(s, " ");

    if (!strncmp(s, p->lh.name, n)) {
	s += n;
	s += strspn(s, " ");
	if (*s == '=') s++;
	s += strspn(s, " ");
    }

    if (sscanf(s, "%15[^ ()](%d)", vname, &lag) == 2) {
	s = strchr(s, ')');
	if (s != NULL && string_is_blank(s + 1)) {
	    int pv = varindex(p->dinfo, vname);

	    if (pv < p->dinfo->v) {
		strcpy(p->dinfo->varinfo[p->lh.v]->parent, 
		   p->dinfo->varname[pv]);
		p->dinfo->varinfo[p->lh.v]->transform = LAGS;
		p->dinfo->varinfo[p->lh.v]->lag = -lag;
	    }
	}
    }

    return 0;
}

static void gen_write_label (parser *p, int oldv)
{
    char tmp[MAXLABEL];
    const char *src = "";
    size_t len = 0;

    if (p->targ != NUM && p->targ != VEC) {
	/* not relevant for matrices, lists */
	return;
    }

    if (p->lh.substr != NULL) {
	/* don't touch the label if we generated a single
	   observation in a series */
	return;
    }

    maybe_record_lag_info(p);

    *tmp = '\0';

    if (p->lh.v < oldv && p->targ == VEC) {
	int m = get_model_count();

	if (m > 0) {
	    sprintf(tmp, _("Replaced after model %d: "), m);
	    len = strlen(tmp);
	}
    }

    if (*p->lh.label != '\0' && (p->flags & P_UFRET)) {
	src = p->lh.label;
    } else if (*p->lh.label != '\0' && dollar_node(p->tree)) {
	src = p->lh.label;
    } else if (p->rhs != NULL && strcmp(p->rhs, "NA")) {
	src = p->rhs;
    }

    if (strlen(src) > MAXLABEL - 1 - len) {
	strncat(tmp, src, MAXLABEL - 4 - len);
	strcat(tmp, "...");
    } else {
	strncat(tmp, src, MAXLABEL - 1);
    }

    strcpy(VARLABEL(p->dinfo, p->lh.v), tmp);
    p->dinfo->varinfo[p->lh.v]->flags |= VAR_GENERATED;

#if GDEBUG
    fprintf(stderr, "varlabel: '%s'\n", VARLABEL(p->dinfo, p->lh.v));
#endif
}

/**
 * function_from_string:
 * @s: the string to look up.
 *
 * Returns: 1 if there is a function corresponding
 * to the name @s, or 0 if there is no such function.
 */

int function_from_string (const char *s)
{
    char word[9];
    const char *p;

    *word = 0;

    p = strchr(s, '(');
    if (p != NULL && p - s <= 8) {
	strncat(word, s, p - s);
    } else {
	strncat(word, s, 8);
    }

    if (function_lookup(word)) {
	return 1;
    }

    /* user-defined functions */
    if (get_user_function_by_name(s)) {
	return 1;
    }

    return 0;
}

/**
 * is_gretl_function_call:
 * @s: the string to check.
 *
 * Returns: 1 if @s starts with the name of a built-in 
 * gretl function, otherwise 0.
 */

int is_gretl_function_call (const char *s)
{
    char c, word[9];

    sscanf(s, "%8[^ (]", word);
    c = s[strlen(word)];
    if (c == ' ' || c == '(') {
	return function_lookup(word);
    } else {
	return 0;
    }
}

/* "reserved word" data */

static const char *res1[] = {
    "const",
    "CONST",
    "pi",
    "NA",
    "null"
};

static const char *res2[] = {
    "t",
    "obs",
    "scalar",
    "series",
    "matrix"
};

/**
 * gretl_reserved_word:
 * @str: string to be tested.
 *
 * Returns non-zero if @str is a reserved word that cannot 
 * figure as the name of a user-defined variable, otherwise 0.
 */

int gretl_reserved_word (const char *str)
{
    const char *ruses[] = {
	N_("a constant"),
	N_("an internal variable")
    };
    static int n1 = sizeof res1 / sizeof res1[0];
    static int n2 = sizeof res2 / sizeof res2[0];
    int i, ret = 0;

    for (i=0; i<n1 && !ret; i++) {
	if (!strcmp(str, res1[i])) {
	    ret = 1;
	}
    }

    for (i=0; i<n2 && !ret; i++) {
	if (!strcmp(str, res2[i])) {
	    ret = 2;
	}
    }

    if (ret > 0) {
	sprintf(gretl_errmsg, _("'%s' refers to %s and may not be used as a "
			    "variable name"), str, _(ruses[ret - 1])); 
    }
 
    return ret;
}

#define GEN_LEVEL_DEBUG 0

/**
 * varindex:
 * @pdinfo: data information struct.
 * @varname: name of variable to test.
 *
 * Returns: the ID number of the variable whose name is given,
 * or the next available ID number if there is no variable of
 * that name.
 */

int varindex (const DATAINFO *pdinfo, const char *varname)
{
    const char *s = varname;
    int fsd = 0;
    int i, ret = pdinfo->v;

    if (s == NULL || *s == 0) {
	return ret;
    }

    if (isdigit(*s)) {
	return ret;
    }

    if (!strcmp(s, "const")) {
	return 0;
    }

    fsd = gretl_function_depth();

    if (fsd > 0) {
	/* inside a function: see only vars at that level */
	for (i=1; i<pdinfo->v; i++) { 
	    if (STACK_LEVEL(pdinfo, i) == fsd && 
		!strcmp(pdinfo->varname[i], s)) {
		ret = i;
		break;
	    }
	}
    } else {
	/* see all vars */
	for (i=1; i<pdinfo->v; i++) { 
	    if (!strcmp(pdinfo->varname[i], s)) { 
		ret = i;
		break;
	    }
	}
    }

#if GEN_LEVEL_DEBUG
    fprintf(stderr, "varindex for '%s', fsd = %d: got %d (pdinfo->v = %d)\n", 
	    s, fsd, ret, pdinfo->v);
#endif 

    return ret;
}

int genr_special_word (const char *s)
{
    if (!strcmp(s, "dummy") ||
	!strcmp(s, "timedum") ||
	!strcmp(s, "unitdum") ||
	!strcmp(s, "time") ||
	!strcmp(s, "index") ||
	!strcmp(s, "unit") ||
	!strcmp(s, "weekday")) {
	return 1;
    } else {
	return 0;
    }
}

static int gen_special (const char *s, const char *line,
			double ***pZ, DATAINFO *pdinfo, 
			PRN *prn, parser *p)
{
    int orig_v = pdinfo->v;
    int msg = 0;
    int err = 0;

    if (!strcmp(s, "markers")) {
	return generate_obs_markers(line, pZ, pdinfo);
    } else if (!strcmp(s, "dummy")) {
	int di0 = dummy(pZ, pdinfo, 0);

	if (di0 == 0) {
	    err = 1;
	} else if (di0 == orig_v) {
	    pputs(prn, _("Periodic dummy variables generated.\n"));
	} else {
	    pputs(prn, _("Periodic dummy variables already present.\n"));
	}
    } else if (!strcmp(s, "timedum")) {
	err = panel_dummies(pZ, pdinfo, OPT_T);
	if (!err) {
	    pputs(prn, _("Panel dummy variables generated.\n"));
	}
    } else if (!strcmp(s, "unitdum")) {
	err = panel_dummies(pZ, pdinfo, OPT_NONE);
	if (!err) {
	    pputs(prn, _("Panel dummy variables generated.\n"));
	}
    } else if (!strcmp(s, "time")) {
	err = gen_time(pZ, pdinfo, 1);
	msg = 1;
    } else if (!strcmp(s, "index")) {
	err = gen_time(pZ, pdinfo, 0);
	msg = 1;
    } else if (!strcmp(s, "unit")) {
	err = gen_unit(pZ, pdinfo);
	msg = 1;
    } else if (!strcmp(s, "weekday")) {
	err = gen_wkday(pZ, pdinfo);
	msg = 1;
    } 

    if (!err && msg) {
	strcpy(p->lh.name, s);
	p->lh.v = varindex(pdinfo, s);
	p->Z = pZ;
	p->dinfo = pdinfo;
	p->targ = VEC;
	p->flags = 0;
	p->err = p->warn = 0;
	p->prn = prn;
	gen_write_message(p, orig_v, prn);
    }	    

    return err;
}

/* try for something of the form "genr x = stack(...)", 
   a special for fixing up panel data */

static int do_stack_vars (const char *s, char *vname, const char **rem)
{
    const char *p;
    int ret = 0;

    if (!strncmp(s, "genr ", 5)) {
	s += 5;
    } else if (!strncmp(s, "series ", 7)) {
	s += 7;
    }

    while (*s == ' ') s++;
    p = strchr(s, '=');

    if (p != NULL) {
	p++;
	while (*p == ' ') p++;
	if (!strncmp(p, "stack(", 6)) {
	    char *test = vname;
	    int n = 0;

	    while (*s && *s != ' ' && *s != '=' && n < VNAMELEN-1) {
		*vname++ = *s++;
		n++;
	    }
	    *vname = '\0';
	    *rem = p;
	    ret = n > 0 && check_varname(test) == 0;
	}
    }

    return ret;
}

static int is_gen_special (const char *s, char *spec, const char **rem)
{
    if (strncmp(s, "genr ", 5)) {
	return 0;
    }

    s += 5;
    while (*s == ' ') s++;

    if (genr_special_word(s)) {
	strcpy(spec, s);
	*rem = s;
	return 1;
    }

    if (!strncmp(s, "markers", 7) && strchr(s, '=')) {
	strcpy(spec, "markers");
	s = strchr(s, '=') + 1;
	while (*s == ' ') s++;
	*rem = s;
	return 1;
    }

    return 0;
}

#define gen_verbose(f) (!(f & P_PRINT) && \
                        !(f & P_DISCARD) && \
                        !(f & P_PRIVATE) && \
                        !(f & P_UFUN) && \
                        !(f & P_QUIET) && \
                        !(f & P_DECL))

int generate (const char *line, double ***pZ, DATAINFO *pdinfo,
	      gretlopt opt, PRN *prn)
{
    char vname[VNAMELEN];
    const char *subline = NULL;
    int oldv = pdinfo->v;
    int flags = 0;
    parser p;

    if (opt & OPT_P) {
	flags |= P_PRIVATE;
    }

    if (opt & OPT_U) {
	flags |= P_UFUN;
    }

    if (opt & OPT_Q) {
	flags |= P_QUIET;
    }

#if GDEBUG
    fprintf(stderr, "\n*** generate: line = '%s'\n", line);
#endif

    if (is_gen_special(line, vname, &subline)) {
	return gen_special(vname, subline, pZ, pdinfo, prn, &p);
    } else if (do_stack_vars(line, vname, &subline)) {
	return dataset_stack_variables(vname, subline, pZ, pdinfo, prn);
    }

    realgen(line, &p, pZ, pdinfo, prn, flags);

    if (!(opt & OPT_U)) {
	gen_save_or_print(&p, prn);
    }

    if (!p.err) {
	maybe_pick_up_sorted_markers(&p);
    }

    if (!p.err && gen_verbose(p.flags)) {
	gen_write_label(&p, oldv);
	if (!(opt & OPT_Q)) {
	    gen_write_message(&p, oldv, prn);
	}
    }

    if (!p.err && p.warn) {
	gen_write_warning(&p, prn);
    }

    gen_cleanup(&p);

#if GDEBUG
    fprintf(stderr, "generate: returning %d\n", p.err);
#endif

    return p.err;
}

/* simply retrieve a scalar result */

double generate_scalar (const char *s, double ***pZ, 
			DATAINFO *pdinfo, int *err)
{
    parser p;
    double x = NADBL;

    *err = realgen(s, &p, pZ, pdinfo, NULL, P_SCALAR | P_PRIVATE);

    if (!*err) {
	if (p.ret->t == MAT) {
	    x = p.ret->v.m->val[0];
	} else if (p.ret->t == NUM) {
	    x = p.ret->v.xval;
	} else {
	    *err = E_TYPES;
	}
    }

    gen_cleanup(&p);

    return x;
}

/* retrieve a series result directly */

double *generate_series (const char *s, double ***pZ, 
			 DATAINFO *pdinfo, int *err)
{
    parser p;
    double *x = NULL;

    *err = realgen(s, &p, pZ, pdinfo, NULL, P_SERIES | P_PRIVATE);

    if (!*err) {
	NODE *n = p.ret;

	if (n->t == VEC) {
	    if (n->flags & TMP_NODE) {
		/* steal the generated series */
		x = n->v.xvec;
		n->v.xvec = NULL;
	    } else {
		x = copyvec(n->v.xvec, p.dinfo->n);
	    }
	} else {
	    *err = E_TYPES;
	}
    }

    gen_cleanup(&p);

    return x;
}

/* retrieve a matrix result directly */

gretl_matrix *generate_matrix (const char *s, double ***pZ, 
			       DATAINFO *pdinfo, int *err)
{
    parser p;
    gretl_matrix *m = NULL;

    *err = realgen(s, &p, pZ, pdinfo, NULL, P_MATRIX | P_PRIVATE);

    if (!*err) {
	NODE *n = p.ret;

	if (n->t == MAT) {
	    if (n->flags & TMP_NODE) {
		/* steal the generated matrix */
		m = n->v.m;
		n->v.m = NULL;
	    } else {
		m = gretl_matrix_copy(n->v.m);
	    }
	} else {
	    *err = E_TYPES;
	}
    }

    gen_cleanup(&p);

    return m;
}

/* retrieve a string result directly */

char *generate_string (const char *s, double ***pZ, 
		       DATAINFO *pdinfo, int *err)
{
    parser p;
    char *ret = NULL;

    *err = realgen(s, &p, pZ, pdinfo, NULL, P_STRING | P_PRIVATE);

    if (!*err) {
	if (p.ret->t == STR) {
	    ret = gretl_strdup(p.ret->v.str);
	} else {
	    *err = E_TYPES;
	}
    }

    gen_cleanup(&p);

    return ret;
}

/* retrieve a list result directly */

int *generate_list (const char *s, double ***pZ, 
		    DATAINFO *pdinfo, int *err)
{
    parser p;
    int *ret = NULL;

    *err = realgen(s, &p, pZ, pdinfo, NULL, P_LIST | P_PRIVATE);

    if (!*err) {
	if (p.ret->t == LIST) {
	    const int *nlist = get_list_by_name(p.ret->v.str);

	    if (nlist == NULL) {
		*err = E_DATA;
	    } else {
		ret = gretl_list_copy(nlist);
	    }
	} else if (p.ret->t == LVEC) {
	    ret = p.ret->v.ivec;
	    p.ret->v.ivec = NULL;
	} else {
	    *err = E_TYPES;
	}
    }

    if (ret == NULL && !*err) {
	*err = E_ALLOC;
    }

    gen_cleanup(&p);

    return ret;
}

/* retrieve and print a variable from "within" a saved
   object
*/

int print_object_var (const char *oname, const char *param,
		      double ***pZ, DATAINFO *pdinfo,
		      PRN *prn)
{
    char line[MAXLEN];
    parser p;

    sprintf(line, "%s.%s", oname, param);
    realgen(line, &p, pZ, pdinfo, prn, P_DISCARD);
    gen_save_or_print(&p, prn);
    gen_cleanup(&p);

    return p.err;
}

/* create a parsed tree that can be evaluated later, 
   probably multiple times */

parser *genr_compile (const char *s, double ***pZ, DATAINFO *pdinfo, 
		      gretlopt opt, int *err)
{
    parser *p = malloc(sizeof *p);
    int flags = P_COMPILE;

#if GDEBUG
    fprintf(stderr, "\n*** genr_compile: s = '%s'\n", s);
#endif

    if (p == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    if (opt & OPT_P) {
	flags |= P_PRIVATE;
    }

    *err = realgen(s, p, pZ, pdinfo, NULL, flags);

#if GDEBUG
    fprintf(stderr, "genr_compile: err = %d\n", *err);
#endif

    return p;
}

/* run a previously compiled generator */

int execute_genr (parser *p, double ***pZ, DATAINFO *pdinfo,
		  gretlopt opt, PRN *prn)
{
    int flags = P_EXEC;

#if GDEBUG
    fprintf(stderr, "\n*** execute_genr: p=%p, LHS='%s'\n", 
	    (void *) p, p->lh.name);
#endif

    if (opt & OPT_L) {
	flags |= P_LOOP;
    } else if (opt & OPT_S) {
	flags |= P_SLAVE;
    }

    realgen(NULL, p, pZ, pdinfo, prn, flags);
    if (p->err == 0) {
	gen_save_or_print(p, prn);
    }
    gen_cleanup(p);
#if PRESERVE_AUX_NODES
    p->ecount += 1;
#endif

#if GDEBUG
    fprintf(stderr, "execute_genr: returning %d\n", p->err);
#endif

    return p->err;
}

/* destroy a previously compiled generator */

void destroy_genr (parser *p)
{
#if GDEBUG
    fprintf(stderr, "\n*** destroy_genr: p = %p\n", (void *) p);
#endif

    if (p != NULL) {
	p->flags = 0;
#if PRESERVE_AUX_NODES
	/* free this stuff only when finished */
	parser_free_aux_nodes(p);
#endif
	gen_cleanup(p);
	free(p);
    }
}

void genr_set_loopline (parser *p, int i)
{
    p->loopline = i;
}

int genr_get_loopline (parser *p)
{
    if (p == NULL) {
	return -1;
    } else {
	return p->loopline;
    }
}

int genr_get_output_varnum (const parser *p)
{
    return p->lh.v;
}

gretl_matrix *genr_get_output_matrix (const parser *p)
{
    if (p->targ == MAT) {
	return p->lh.m1;
    } else {
	return NULL;
    }
}

const char *genr_get_formula (const GENERATOR *genr)
{
    return genr->input;
}

int genr_is_print (const parser *p)
{
    return (p->flags & P_PRINT);
}

void genr_set_na_check (parser *p)
{
    p->flags |= P_NATEST;
}

void genr_unset_na_check (parser *p)
{
    p->flags &= ~P_NATEST;
}



