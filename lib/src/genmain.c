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
#include "genr_optim.h"
#include "gretl_typemap.h"

#include <errno.h>

#if GENDEBUG
# define GDEBUG 1
#else
# define GDEBUG 0
#endif

#define setting_obsval(p) (p->flags & P_OBSVAL)

static void write_scalar_message (const parser *p, PRN *prn)
{
    double x = gretl_scalar_get_value(p->lh.name, NULL);

    if (p->lh.t == NUM) {
	pprintf(prn, _("Replaced scalar %s"), p->lh.name);
    } else {
	pprintf(prn, _("Generated scalar %s"), p->lh.name);
    }

    if (na(x)) {
	pputs(prn, " = NA");
    } else {
	pprintf(prn, " = %g", x);
    }
}

/* Note: p->lh.name may be empty but it's never NULL, while
   the vname member of a NODE may be NULL.
*/

static void gen_write_message (const parser *p, int oldv, PRN *prn)
{
    const char *name = p->lh.name;
    int targ = p->targ;
    int vnum = p->lh.vnum;
    int t = p->lh.t;

    if (p->lhres != NULL) {
	/* compound LHS object */
	NODE *lhs = p->lhres;
	NODE *lh1 = lhs->L;

	if (lh1->vname != NULL) {
	    /* not an "embedded" object */
	    name = lh1->vname;
	    t = targ = lh1->t;
	    if (t == SERIES) {
		vnum = lh1->vnum;
		targ = NUM;
	    }
	} else {
	    return;
	}
	if (t != SERIES && t != LIST && t != MAT) {
	    /* we'll not print a message for modification
	       of container types (bundle, array)
	    */
	    return;
	}
    }

    if (targ == NUM) {
	if (setting_obsval(p)) {
	    /* setting specific observation in series */
	    pprintf(prn, _("Modified series %s (ID %d)"),
		    name, vnum);
	} else {
	    write_scalar_message(p, prn);
	}
    } else if (targ == SERIES) {
	if (vnum == 0 && (p->flags & P_STRVEC)) {
	    pprintf(prn, _("Generated series %s (ID %d)"), name, p->ret->vnum);
	} else if (vnum < oldv) {
	    pprintf(prn, _("Replaced series %s (ID %d)"), name, vnum);
	} else {
	    pprintf(prn, _("Generated series %s (ID %d)"), name, vnum);
	}
    } else if (targ == MAT) {
	gretl_matrix *m = get_matrix_by_name(name);

	if (p->lhres != NULL) {
	    pprintf(prn, _("Modified matrix %s"), name);
	} else if (t == MAT) {
	    pprintf(prn, _("Replaced matrix %s"), name);
	} else {
	    pprintf(prn, _("Generated matrix %s"), name);
	}
	if (gretl_matrix_is_scalar(m)) {
	    pprintf(prn, " = {%g}", m->val[0]);
	}
    } else if (targ == LIST) {
	if (p->lhres != NULL) {
	    pprintf(prn, _("Modified list %s"), name);
	} else if (t == LIST) {
	    pprintf(prn, _("Replaced list %s"), name);
	} else {
	    pprintf(prn, _("Generated list %s"), name);
	}
    } else if (targ == STR) {
	if (p->lhres != NULL) {
	    pprintf(prn, _("Modified string %s"), name);
	} else if (t == STR) {
	    pprintf(prn, _("Replaced string %s"), name);
	} else {
	    pprintf(prn, _("Generated string %s"), name);
	}
    } else {
	return;
    }

    pputc(prn, '\n');
}

static int maybe_record_lag_info (parser *p)
{
    const char *s = p->input;
    int n = strlen(p->lh.name);
    char vname[VNAMELEN];
    char fmt[16];
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

    /* ensure we don't preserve stale metadata */
    if (p->lh.vnum > 0) {
	series_delete_metadata(p->dset, p->lh.vnum);
    }

    sprintf(fmt, "%%%d[^ ()](%%d)", VNAMELEN-1);

    if (sscanf(s, fmt, vname, &lag) == 2) {
	s = strchr(s, ')');
	if (s != NULL && string_is_blank(s + 1)) {
	    int pv = series_index(p->dset, vname);

	    if (pv < p->dset->v && function_lookup(vname)) {
		/* rule out the case of a series name shadowing
		   a built-in function
		*/
		pv = -1;
	    }
	    if (pv > 0 && pv < p->dset->v) {
		series_set_parent(p->dset, p->lh.vnum, p->dset->varname[pv]);
		series_set_transform(p->dset, p->lh.vnum, LAGS);
		series_set_lag(p->dset, p->lh.vnum, -lag);
	    }
	}
    }

    return 0;
}

static void gen_write_label (parser *p, int oldv)
{
    const char *src = NULL;

    if (p->targ != SERIES) {
	/* this is relevant only for series */
	return;
    }

    if (p->lh.expr != NULL) {
	/* don't touch the label if we generated a single
	   observation in a series
	*/
	return;
    }

    maybe_record_lag_info(p);

    if (p->lh.vnum < oldv && p->targ == SERIES) {
	series_set_mtime(p->dset, p->lh.vnum);
    }

    if (p->lh.label != NULL && (p->flags & P_UFRET)) {
	src = p->lh.label;
    } else if (p->rhs != NULL && strcmp(p->rhs, "NA")) {
	src = p->rhs;
    }

    if (src != NULL && *src != '\0') {
	if (strlen(src) > MAXLABEL - 1) {
	    /* truncate if necessary */
	    char tmp[MAXLABEL];

	    *tmp = '\0';
	    strncat(tmp, src, MAXLABEL - 4);
	    strcat(tmp, "...");
	    series_set_label(p->dset, p->lh.vnum, tmp);
	} else {
	    series_set_label(p->dset, p->lh.vnum, src);
	}
	if (src == p->rhs) {
	    /* in case the label is a formula */
	    series_set_flag(p->dset, p->lh.vnum, VAR_GENERATED);
	}
    }
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

static const char *reswords[] = {
    /* constants */
    "const",
    "NA",
    "null",
    "obs", /* not exactly a constant, but hey */
    /* types */
    "scalar",
    "series",
    "matrix",
    "string",
    "list",
    "bundle",
    "array",
    "void",
    /* control flow */
    "for",
    /* debugging instructions, etc. */
    "continue",
    "next",
    "to"
};

/**
 * gretl_reserved_word:
 * @str: string to be tested.
 *
 * Returns: non-zero if @str is a reserved word that cannot
 * figure as the name of a user-defined variable, otherwise 0.
 */

int gretl_reserved_word (const char *str)
{
    static int n = sizeof reswords / sizeof reswords[0];
    int i, ret = gretl_command_number(str);

    /* the names of built-in functions are deliberately
       not reserved */

    for (i=0; i<n && !ret; i++) {
	if (!strcmp(str, reswords[i])) {
	    ret = E_INVARG;
	}
    }

    if (ret) {
	gretl_errmsg_sprintf(_("'%s' is a reserved word"), str);
    }

    return ret;
}

/**
 * extract_varname:
 * @targ: target string into which to write name.
 * @src: source string.
 * @len: location to receive the length of the extracted portion.
 *
 * Writes up to #VNAMELEN - 1 characters from @s into @vname.
 *
 * Returns: 0 on success, non-zero if the number of valid varname
 * characters in @s is greater than #VNAMELEN - 1.
 */

int extract_varname (char *targ, const char *src, int *len)
{
    int err = 0;

    *targ = '\0';
    *len = gretl_namechar_spn(src);

    if (*len >= VNAMELEN) {
	/* too long to be a valid variable name */
	err = E_UNKVAR;
    } else {
	strncat(targ, src, *len);
    }

    return err;
}

static int try_for_listvar (const DATASET *dset, const char *s)
{
    char vname[VNAMELEN];
    char lname[VNAMELEN];
    char fmt[16];

    sprintf(fmt, "%%%d[^.].%%%ds", VNAMELEN-1, VNAMELEN-1);

    if (sscanf(s, fmt, lname, vname) == 2) {
	int *list = get_list_by_name(lname);

	if (list != NULL) {
	    int i, vi;

	    for (i=1; i<=list[0]; i++) {
		vi = list[i];
		if (!strcmp(vname, dset->varname[vi])) {
		    return vi;
		}
	    }
	}
    }

    return dset->v;
}

#define GEN_LEVEL_DEBUG 0

/**
 * series_index:
 * @dset: data information struct.
 * @varname: name of variable to test.
 *
 * Returns: the ID number of the variable whose name is given,
 * or the next available ID number if there is no variable of
 * that name.
 */

int series_index (const DATASET *dset, const char *varname)
{
    const char *s = varname;
    int fd = 0, ret = -1;

    if (dset != NULL) {
	int i;

	ret = dset->v; /* initialize to "next" series ID */

	if (s == NULL || *s == '\0' || isdigit(*s)) {
	    goto bailout;
	}

	if (strcmp(s, "const") == 0) {
	    ret = 0;
	    goto bailout;
	}

	if (strchr(s, '.') != NULL) {
	    ret = try_for_listvar(dset, s);
	    goto bailout;
	}

	fd = gretl_function_depth();

	if (fd == 0) {
	    /* not inside a user function: easy */
	    for (i=1; i<dset->v; i++) {
		if (strcmp(dset->varname[i], s) == 0) {
		    ret = i;
		    break;
		}
	    }
	} else {
	    /* The condition for recognizing a series by name, if we're
	       inside a user function: it must exist at the current level
	       of function execution, and its tenure at that level must
	       not just be the result of its being a member of a list
	       that was passed as an argument.
	    */
	    for (i=1; i<dset->v; i++) {
		if (fd == series_get_stack_level(dset, i) &&
		    !series_is_listarg(dset, i, NULL) &&
		    strcmp(dset->varname[i], s) == 0) {
		    ret = i;
		    break;
		}
	    }
	}
    }

 bailout:

#if GEN_LEVEL_DEBUG
    fprintf(stderr, "series_index for '%s', fd = %d: got %d (dset->v = %d)\n",
	    s, fd, ret, dset->v);
#endif

    return ret;
}

/**
 * series_greatest_index:
 * @dset: data information struct.
 * @varname: name of variable to test.
 *
 * Returns: the ID number of the variable whose name is given,
 * or the next available ID number if there is no variable of
 * that name. In contrast to series_index() this variant searches
 * down from the greatest current series ID.
 */

int series_greatest_index (const DATASET *dset, const char *varname)
{
    const char *s = varname;
    int fd = 0, ret = -1;

    if (dset != NULL) {
	int i;

	ret = dset->v;

	if (s == NULL || *s == '\0' || isdigit(*s)) {
	    goto bailout;
	}

	if (strcmp(s, "const") == 0) {
	    ret = 0;
	    goto bailout;
	}

	if (strchr(s, '.') != NULL) {
	    ret = try_for_listvar(dset, s);
	    goto bailout;
	}

	fd = gretl_function_depth();

	if (fd == 0) {
	    /* not inside a user function: easy */
	    for (i=dset->v-1; i>0; i--) {
		if (strcmp(dset->varname[i], s) == 0) {
		    ret = i;
		    break;
		}
	    }
	} else {
	    /* The condition for recognizing a series by name, if we're
	       inside a user function: it must exist at the current level
	       of function execution, and its tenure at that level must
	       not just be the result of its being a member of a list
	       that was passed as an argument.
	    */
	    for (i=dset->v-1; i>0; i--) {
		if (fd == series_get_stack_level(dset, i) &&
		    !series_is_listarg(dset, i, NULL) &&
		    strcmp(dset->varname[i], s) == 0) {
		    ret = i;
		    break;
		}
	    }
	}
    }

    if (ret <= 0 && strcmp(s, "const")) {
	ret = dset->v;
    }

 bailout:

#if GEN_LEVEL_DEBUG
    fprintf(stderr, "series_index for '%s', fd = %d: got %d (dset->v = %d)\n",
	    s, fd, ret, dset->v);
#endif

    return ret;
}

int current_series_index (const DATASET *dset, const char *vname)
{
    int v = -1;

    if (dset != NULL && dset->v > 0 &&
	vname != NULL && *vname != '\0') {
	v = series_index(dset, vname);
	if (v >= dset->v) {
	    v = -1;
	}
    }

    return v;
}

int gretl_is_series (const char *name, const DATASET *dset)
{
    if (dset == NULL) {
	return 0;
    } else {
	int v = series_index(dset, name);

	return (v >= 0 && v < dset->v);
    }
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

static GretlType genr_last_type;

GretlType genr_get_last_output_type (void)
{
    return genr_last_type;
}

static int gen_special (const char *s, const char *line,
			DATASET *dset, PRN *prn, parser *p)
{
    const char *msg = NULL;
    int orig_v = dset->v;
    int write_label = 0;
    int vnum = -1;
    int err = 0;

    if (dset == NULL || dset->n == 0) {
	return E_NODATA;
    }

    if (!strcmp(s, "markers")) {
	return generate_obs_markers(line, dset);
    } else if (!strcmp(s, "dummy")) {
	err = gen_seasonal_dummies(dset, 0);
	if (!err) {
	    msg = N_("Periodic dummy variables generated.\n");
	}
    } else if (!strcmp(s, "timedum")) {
	err = gen_panel_dummies(dset, OPT_T, prn);
	if (!err) {
	    msg = N_("Panel dummy variables generated.\n");
	}
    } else if (!strcmp(s, "unitdum")) {
	err = gen_panel_dummies(dset, OPT_NONE, prn);
	if (!err) {
	    msg = N_("Panel dummy variables generated.\n");
	}
    } else if (!strcmp(s, "time")) {
	err = gen_time(dset, 1, &vnum);
	write_label = 1;
    } else if (!strcmp(s, "index")) {
	err = gen_time(dset, 0, &vnum);
	write_label = 1;
    } else if (!strcmp(s, "unit")) {
	err = gen_unit(dset, &vnum);
	write_label = 1;
    } else if (!strcmp(s, "weekday")) {
	err = gen_wkday(dset, &vnum);
	write_label = 1;
    }

    if (msg != NULL && gretl_messages_on()) {
	pputs(prn, _(msg));
    }

    if (!err && write_label) {
	strcpy(p->lh.name, s);
	p->lh.vnum = vnum;
	p->dset = dset;
	p->targ = SERIES;
	p->flags = 0;
	p->lhres = NULL;
	p->err = 0;
	p->prn = prn;
	if (prn != NULL && gretl_messages_on()) {
	    gen_write_message(p, orig_v, prn);
	}
    }

    if (dset->v > orig_v) {
	set_dataset_is_changed(dset, 1);
	genr_last_type = GRETL_TYPE_SERIES;
    }

    return err;
}

static int get_oldstyle_stack_args (const char *s, char **arg,
				    char **opt1, char **opt2)
{
    const char *p1, *p2;
    int len;

    /* Let the marker for old-style uage be the presence of
       one or more legacy option flags */

    p1 = strstr(s, "--length=");
    if (p1 != NULL) {
	len = strcspn(p1 + 9, " \n");
	*opt1 = gretl_strndup(p1 + 9, len);
    }

    p2 = strstr(s, "--offset=");
    if (p2 != NULL) {
	len = strcspn(p2 + 9, " \n");
	*opt2 = gretl_strndup(p2 + 9, len);
    }

    if (p1 != NULL || p2 != NULL) {
	len = strcspn(s, ")");
	*arg = gretl_strndup(s, len);
	return 1;
    } else {
	return 0;
    }
}

int stack_update_parser_input (parser *p)
{
    char *arg = NULL, *opt1 = NULL, *opt2 = NULL;
    char *s, *start;
    gchar *tmp = NULL;
    GString *gs;
    int offset;

    offset = p->point - p->input;
    start = gretl_strndup(p->input, offset);
    gs = g_string_new(start);
    free(start);

    s = strstr(p->input, "stack(") + 6;
    get_oldstyle_stack_args(s, &arg, &opt1, &opt2);
    if (arg != NULL) {
	gs = g_string_append(gs, arg);
	free(arg);
    }
    if (opt1 != NULL) {
	gs = g_string_append_c(gs, ',');
	gs = g_string_append(gs, opt1);
	free(opt1);
    }
    if (opt2 != NULL) {
	gs = g_string_append_c(gs, ',');
	gs = g_string_append(gs, opt2);
	free(opt2);
    }
    gs = g_string_append_c(gs, ')');
    tmp = g_string_free(gs, FALSE);

    p->input = tmp;
    p->point = p->input + offset;
    p->flags |= P_ALTINP;

    return 0;
}

static int is_genr_special (const char *s, char *spec, const char **rem)
{
    if (strncmp(s, "genr ", 5)) {
	return 0;
    }

    s += 5;
    while (*s == ' ') s++;

    if (genr_special_word(s)) {
	if (spec != NULL) {
	    strcpy(spec, s);
	}
	if (rem != NULL) {
	    *rem = s;
	}
	return 1;
    }

    if (!strncmp(s, "markers", 7) && strchr(s, '=')) {
	if (spec != NULL) {
	    strcpy(spec, "markers");
	}
	if (rem != NULL) {
	    s = strchr(s, '=') + 1;
	    while (*s == ' ') s++;
	    *rem = s;
	}
	return 1;
    }

    return 0;
}

static int maybe_unassigned_fncall (const char *s)
{
    return s[strlen(s)-1] == ')';
}

#define gen_silent(f) (f & (P_DISCARD | P_PRIV | P_DECL))

int generate (const char *line, DATASET *dset,
	      GretlType gtype, gretlopt opt,
	      PRN *prn)
{
    char vname[VNAMELEN] = {0};
    const char *subline = NULL;
    int oldv, flags = 0;
    int targtype = UNK;
    parser p;

    if (line == NULL) {
	return E_ARGS;
    }

    if (gtype == GRETL_TYPE_NONE) {
	flags |= P_DISCARD;
    } else if (gtype == GRETL_TYPE_DOUBLE) {
	targtype = NUM;
    } else if (gtype == GRETL_TYPE_SERIES) {
	targtype = SERIES;
    } else if (gtype == GRETL_TYPE_MATRIX) {
	targtype = MAT;
    } else if (gtype == GRETL_TYPE_STRING) {
	targtype = STR;
    } else if (gtype == GRETL_TYPE_BUNDLE) {
	targtype = BUNDLE;
    } else if (gtype == GRETL_TYPE_LIST) {
	targtype = LIST;
    } else if (gtype == GRETL_TYPE_BOOL) {
        targtype = NUM;
        flags |= P_ANON;
    } else if (gretl_array_type(gtype)) {
	targtype = gtype;
    }

    if (opt & OPT_P) {
	/* internal use of generate() */
	flags |= P_PRIV;
    }

    if (opt & OPT_Q) {
	flags |= P_QUIET;
    }

    if (opt & OPT_C) {
	flags |= P_CATCH;
    }

    if (opt & OPT_O) {
	/* special for function call, no assignment */
	if (maybe_unassigned_fncall(line)) {
	    targtype = EMPTY;
	    flags |= P_VOID;
	} else {
	    return E_PARSE;
	}
    }

    oldv = (dset != NULL)? dset->v : 0;

#if GDEBUG
    fprintf(stderr, "\n*** generate: line = '%s'\n", line);
    fprintf(stderr, "    gtype=%s, targtype=%s\n", gretl_type_get_name(gtype),
	    getsymb(targtype));
#endif

    if (is_genr_special(line, vname, &subline)) {
	return gen_special(vname, subline, dset, prn, &p);
    }

    realgen(line, &p, dset, prn, flags, targtype);

    if (!p.err && targtype != EMPTY) {
	gen_save_or_print(&p, prn);
	if (!p.err && !gen_silent(p.flags)) {
	    gen_write_label(&p, oldv);
	    if (gretl_messages_on() && prn != NULL && !(opt & OPT_Q)) {
		gen_write_message(&p, oldv, prn);
	    }
	}
    }

    genr_last_type = genr_get_output_type(&p);
    if (genr_last_type == GRETL_TYPE_SERIES) {
	set_dataset_is_changed(dset, 1);
    }

    if (p.err == 1) {
	/* a fairly good guess? */
	p.err = E_PARSE;
    }

    gen_cleanup(&p);

#if GDEBUG
    fprintf(stderr, "generate: returning %d\n", p.err);
#endif

    return p.err;
}

/* retrieve a scalar result directly */

static double generate_scalar_full (const char *s, DATASET *dset,
				    PRN *prn, int *err)
{
    parser p;
    double x = NADBL;

    *err = realgen(s, &p, dset, prn, P_PRIV | P_ANON, NUM);

    if (!*err) {
	if (p.ret->t == MAT) {
	    gretl_matrix *m = p.ret->v.m;

	    if (gretl_matrix_is_scalar(m)) {
		x = p.ret->v.m->val[0];
	    } else if (!gretl_is_null_matrix(m)) {
		fprintf(stderr, "generate_scalar: got %d x %d matrix\n",
			m->rows, m->cols);
		*err = E_TYPES;
	    }
	} else if (p.ret->t == NUM) {
	    x = p.ret->v.xval;
	} else {
	    *err = E_TYPES;
	}
    } else if (*err == 1) {
	*err = E_PARSE;
    }

    gen_cleanup(&p);

    return x;
}

double generate_scalar (const char *s, DATASET *dset, int *err)
{
    return generate_scalar_full(s, dset, NULL, err);
}

/* retrieve a boolean result directly: called only from
   flow_control.c
*/

double generate_boolean (const char *s, DATASET *dset, PRN *prn, int *err)
{
    double x = generate_scalar_full(s, dset, prn, err);

    return (*err || na(x)) ? NADBL : (double) (x != 0.0);
}

/* retrieve an integer result directly */

int generate_int (const char *s, DATASET *dset, int *err)
{
    double x = generate_scalar_full(s, dset, NULL, err);
    int ret = -1;

    if (!*err) {
	ret = gretl_int_from_double(x, err);
    }

    return ret;
}

/* Execute statement @s, bypassing the command tokenizer,
   when we know that it contains a call to a function whose
   return value (if any) we do not want to assign. Prime
   example: the bundle-print function for a function package.
*/

int generate_void (const char *s, DATASET *dset, PRN *prn)
{
    parser p;
    int err;

    err = realgen(s, &p, dset, prn, P_PRIV | P_VOID, EMPTY);

    gen_cleanup(&p);

    return err;
}

/* retrieve a series result directly */

double *generate_series (const char *s, DATASET *dset, PRN *prn,
			 int *err)
{
    parser p;
    double *x = NULL;

    *err = realgen(s, &p, dset, prn, P_PRIV | P_ANON, SERIES);

    if (!*err) {
	NODE *n = p.ret;

	if (n->t == SERIES) {
	    if (n->flags & TMP_NODE) {
		/* steal the generated series */
		x = n->v.xvec;
		n->v.xvec = NULL;
	    } else {
		x = copyvec(n->v.xvec, p.dset->n);
	    }
	} else {
	    *err = E_TYPES;
	}
    } else if (*err == 1) {
	*err = E_PARSE;
    }

    gen_cleanup(&p);

    return x;
}

/* retrieve a matrix result directly */

gretl_matrix *generate_matrix (const char *s, DATASET *dset,
			       int *err)
{
    gretl_matrix *m = NULL;
    parser p;

    *err = realgen(s, &p, dset, NULL, P_PRIV | P_ANON, MAT);

    if (!*err) {
	NODE *n = p.ret;

	if (n->t == MAT) {
	    if (n->flags & TMP_NODE) {
		/* steal the generated matrix */
		m = n->v.m;
		n->v.m = NULL;
	    } else {
		m = gretl_matrix_copy(n->v.m);
		if (m == NULL) {
		    *err = E_ALLOC;
		}
	    }
	} else if (n->t == NUM) {
	    if (na(n->v.xval)) {
		*err = E_NAN;
	    } else {
		m = gretl_matrix_alloc(1, 1);
		if (m == NULL) {
		    *err = E_ALLOC;
		} else {
		    m->val[0] = n->v.xval;
		}
	    }
	} else {
	    *err = E_TYPES;
	}
    } else if (*err == 1) {
	*err = E_PARSE;
    }

    gen_cleanup(&p);

    return m;
}

/* retrieve a string result directly */

char *generate_string (const char *s, DATASET *dset, int *err)
{
    parser p;
    char *ret = NULL;

    *err = realgen(s, &p, dset, NULL, P_PRIV | P_ANON, STR);

    if (!*err) {
	NODE *n = p.ret;

	if (n->t == STR) {
	    if (n->flags & TMP_NODE) {
		/* steal the generated string */
		ret = n->v.str;
		n->v.str = NULL;
	    } else {
		ret = gretl_strdup(n->v.str);
	    }
	} else {
	    *err = E_TYPES;
	}
    } else if (*err == 1) {
	*err = E_PARSE;
    }

    gen_cleanup(&p);

    return ret;
}

/* retrieve a list result directly */

int *generate_list (const char *s, DATASET *dset, int *err)
{
    int *ret = NULL;
    parser p;

    if (dset == NULL) {
	*err = E_NODATA;
	return NULL;
    }

    *err = realgen(s, &p, dset, NULL, P_PRIV | P_ANON, LIST);

    if (!*err) {
	ret = node_get_list(p.ret, &p);
	*err = p.err;
    }

    gen_cleanup(&p);

    return ret;
}

/* create a parsed tree that can be evaluated later,
   probably multiple times */

parser *genr_compile (const char *s, DATASET *dset,
		      GretlType gtype, gretlopt opt,
		      PRN *prn, int *err)
{
    parser *p;
    int flags = P_COMPILE;
    int targtype = UNK;

#if GDEBUG
    fprintf(stderr, "\n*** genr_compile: s = '%s'\n", s);
#endif

    if (is_genr_special(s, NULL, NULL)) {
	*err = E_EQN;
	return NULL;
    }

    p = malloc(sizeof *p);

    if (p == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    if (gtype == GRETL_TYPE_NONE) {
	flags |= P_DISCARD;
    } else if (gtype == GRETL_TYPE_DOUBLE) {
	targtype = NUM;
    } else if (gtype == GRETL_TYPE_SERIES) {
	targtype = SERIES;
    } else if (gtype == GRETL_TYPE_MATRIX) {
	targtype = MAT;
    } else if (gtype == GRETL_TYPE_STRING) {
	targtype = STR;
    } else if (gtype == GRETL_TYPE_BUNDLE) {
	targtype = BUNDLE;
    } else if (gtype == GRETL_TYPE_LIST) {
	targtype = LIST;
    } else if (gtype == GRETL_TYPE_BOOL) {
        targtype = NUM;
        flags |= P_ANON;
    } else if (gretl_array_type(gtype)) {
	targtype = gtype;
    }

    if (opt & OPT_P) {
	/* internal use of generate() */
	flags |= P_PRIV;
    }

    if (opt & OPT_O) {
	/* special for function call, no assignment */
	targtype = EMPTY;
        flags |= P_VOID;
    }

    if (opt & OPT_N) {
	/* "no exec": compile but don't run */
	flags |= P_NOEXEC;
    }

    if (opt & OPT_A) {
	/* anonymous: no assignment to named variable */
	flags |= P_ANON;
    }

    *err = realgen(s, p, dset, prn, flags, targtype);

    if (*err == 0 && p != NULL &&
	!(opt & OPT_N) && p->targ != EMPTY) {
	gen_save_or_print(p, prn);
	if (p->err) {
	    *err = p->err;
	}
    }

    if (*err) {
	destroy_genr(p);
	p = NULL;
    }

#if GDEBUG
    fprintf(stderr, "genr_compile: err = %d\n", *err);
#endif

    return p;
}

/* run a previously compiled generator */

int execute_genr (parser *p, DATASET *dset, PRN *prn)
{
#if GDEBUG
    fprintf(stderr, "\n*** execute_genr: p=%p, LHS='%s', Z=%p, prn=%p\n",
	    (void *) p, p->lh.expr ? p->lh.expr : p->lh.name,
	    (void *) dset->Z, (void *) prn);
#endif

    realgen(NULL, p, dset, prn, P_EXEC, UNK);

    if (!p->err && p->targ != EMPTY) {
	gen_save_or_print(p, prn);
    }

    if (p->err) {
	gen_cleanup(p);
    }

#if GDEBUG
    fprintf(stderr, "execute_genr: returning %d\n", p->err);
#endif

    return p->err;
}

double evaluate_scalar_genr (parser *p, DATASET *dset,
			     PRN *prn, int *err)
{
    double x = NADBL;

    *err = realgen(NULL, p, dset, NULL, P_EXEC | P_PRIV | P_ANON,
		   NUM);

    if (!*err) {
	if (p->ret->t == MAT) {
	    gretl_matrix *m = p->ret->v.m;

	    if (gretl_matrix_is_scalar(m)) {
		x = p->ret->v.m->val[0];
	    } else if (!gretl_is_null_matrix(m)) {
		fprintf(stderr, "evaluate_if_cond: got %d x %d matrix\n",
			m->rows, m->cols);
		*err = E_TYPES;
	    }
	} else if (p->ret->t == NUM) {
	    x = p->ret->v.xval;
	} else {
	    *err = E_TYPES;
	}
    } else if (*err == 1) {
	*err = E_PARSE;
    }

    gen_cleanup(p);

    return x;
}

double evaluate_if_cond (parser *p, DATASET *dset, PRN *prn, int *err)
{
    double x = evaluate_scalar_genr(p, dset, prn, err);

    return *err ? x : (double) (x != 0.0);
}

/* destroy a previously compiled generator */

void destroy_genr (parser *p)
{
#if GDEBUG
    fprintf(stderr, "\n*** destroy_genr: p = %p\n", (void *) p);
#endif

    if (p != NULL) {
	p->flags = 0;
	gen_cleanup(p);
	free(p);
    }
}

GretlType genr_get_output_type (const parser *p)
{
    int t = GRETL_TYPE_NONE;

    if (!p->err) {
	if (p->targ == NUM) {
	    t = GRETL_TYPE_DOUBLE;
	} else if (p->targ == SERIES) {
	    t = GRETL_TYPE_SERIES;
	} else if (p->targ == MAT) {
	    t = GRETL_TYPE_MATRIX;
	} else if (p->targ == LIST) {
	    t = GRETL_TYPE_LIST;
	} else if (p->targ == BUNDLE) {
	    t = GRETL_TYPE_BUNDLE;
	} else if (p->targ == ARRAY) {
	    t = GRETL_TYPE_ARRAY;
	}
    }

    return t;
}

int genr_get_output_varnum (const parser *p)
{
    return p->lh.vnum;
}

gretl_matrix *genr_get_output_matrix (parser *p)
{
    gretl_matrix *m = p->lh.mret;

    if (p->targ != MAT) {
	/* matrix under bundle or array?
	   nullify pointer in case the type changes
	*/
	p->lh.mret = NULL;
    }

    return m;
}

double genr_get_output_scalar (const parser *p)
{
    if (p->targ == NUM) {
	return gretl_scalar_get_value(p->lh.name, NULL);
    } else {
	return NADBL;
    }
}

int genr_no_assign (const parser *p)
{
    return (p->flags & (P_DISCARD | P_VOID));
}

int genr_is_autoregressive (const parser *p)
{
    return (p->flags & P_AUTOREG);
}

void genr_set_na_check (parser *p)
{
    p->flags |= P_NATEST;
}

void genr_unset_na_check (parser *p)
{
    p->flags &= ~P_NATEST;
}
