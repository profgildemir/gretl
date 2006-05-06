/*
 *  Copyright (c) by Allin Cottrell
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

/* selector.c for gretl */

#include "gretl.h"
#include "selector.h"
#include "dlgutils.h"
#include "menustate.h"
#include "fileselect.h"
#include "fnsave.h"

#include "var.h"
#include "gretl_func.h"

#ifndef OLD_GTK
# include "treeutils.h"
#endif

#define VLDEBUG 0

enum {
    SR_LVARS = 1,
    SR_RLVARS,
    SR_RUVARS,
    SR_DEPVAR,
    SR_EXTRA
};

#define N_EXTRA 6
#define N_RADIOS 2

struct _selector {
    GtkWidget *dlg;
    GtkWidget *vbox;
    GtkWidget *action_area;
    GtkWidget *lvars;
    GtkWidget *depvar;
    GtkWidget *rlvars;
    GtkWidget *ruvars;
    GtkWidget *default_check;
    GtkWidget *add_button;
    GtkWidget *lags_button;
    GtkWidget *extra[N_EXTRA];
    GtkWidget *radios[N_RADIOS];
    int code;
    int active_var;
    int error;
    gretlopt opts;
    char *cmdlist;
    gpointer data;
    int (*callback)();
};

#ifdef ENABLE_GMP
#define MODEL_CODE(c) (c == OLS || c == CORC || c == HILU || c == WLS || \
                       c == POOLED || c == HCCM || c == HSK || c == ARMA || \
                       c == TSLS || c == LOGIT || c == PROBIT || c == GARCH || \
                       c == AR || c == MPOLS || c == LAD || c == LOGISTIC || \
                       c == TOBIT || c == PWE || c == POISSON)
#else
#define MODEL_CODE(c) (c == OLS || c == CORC || c == HILU || c == WLS || \
                       c == POOLED || c == HCCM || c == HSK || c == ARMA || \
                       c == TSLS || c == LOGIT || c == PROBIT || c == GARCH || \
                       c == AR || c == LAD || c == LOGISTIC || \
                       c == TOBIT || c == PWE || c == POISSON)
#endif

#define COINT_CODE(c) (c == COINT || c == COINT2)

#define VEC_CODE(c) (c == COINT || c == COINT2 || c == VAR || \
                     c == VECM || c == VLAGSEL)

#define ADDVAR_CODE(c) (c == LOGS || c == LAGS || c == SQUARE || \
                        c == DIFF || c == LDIFF)

#define GRAPH_CODE(c) (c == GR_PLOT || c == GR_XY || c == GR_IMP || GR_DUMMY)

#define TWO_VARS_CODE(c) (c == SPEARMAN || c == MEANTEST || c == MEANTEST2 || \
                          c == VARTEST || c == ELLIPSE)

#define WANT_TOGGLES(c) (c == ARMA || \
                         c == COINT || \
                         c == COINT2 || \
                         c == GARCH || \
                         c == HILU || \
                         c == LOGIT || \
                         c == OLS || \
                         c == PROBIT || \
                         c == TOBIT || \
                         c == TSLS || \
                         c == VAR || \
                         c == VECM || \
                         c == VLAGSEL || \
                         c == WLS)

#define WANT_RADIOS(c) (c == COINT2 || c == VECM || c == ARMA)

#define USE_VECXLIST(c) (c == VAR || c == VLAGSEL || c == VECM)

#define dataset_lags_ok(d) ((d)->structure == TIME_SERIES || \
			    (d)->structure == SPECIAL_TIME_SERIES || \
                            (d)->structure == STACKED_TIME_SERIES || \
                            (d)->structure == STACKED_CROSS_SECTION)

#define select_lags_upper(c) (c == VAR || c == VECM || c == VLAGSEL || c == TSLS)
#define select_lags_lower(c) (MODEL_CODE(c)) 
#define select_lags_depvar(c) (MODEL_CODE(c) && c != ARMA) 

static int default_var;
static int want_seasonals;
static int default_order;
static int vartrend;
static int lags_hidden;
static int y_x_lags_enabled;
static int y_w_lags_enabled;
static int arma_p = 1;
static int arma_P = 0;
static int arima_d = 0;
static int arima_D = 0;
static int arma_q = 1;
static int arma_Q = 0;
static int garch_p = 1;
static int garch_q = 1;
static int arma_const = 1;
static int arma_x12 = 0;

static int *xlist;
static int *instlist;
static int *veclist;
static int *vecxlist;

static GtkWidget *scatters_label;
static GtkWidget *scatters_menu;
#ifdef OLD_GTK
static GtkWidget *x_axis_item;
#endif

static selector *open_selector;

static gint listvar_special_click (GtkWidget *widget, GdkEventButton *event, 
				   gpointer data);
static gint lvars_right_click (GtkWidget *widget, GdkEventButton *event, 
			       selector *sr);
static gboolean lags_dialog_driver (GtkWidget *w, selector *sr);
static int list_show_var (int v, int code, int show_lags);
static int selector_get_depvar_number(selector *sr);
static int spinner_get_int (GtkWidget *w);
static int functions_list (selector *sr);

#include "lagpref.c"

static int selection_at_max (selector *sr, int nsel)
{
    int ret = 0;

    if (TWO_VARS_CODE(sr->code) && nsel == 2) {
	ret = 1;
    }

    return ret;
}

static int sr_get_lag_context (selector *sr, int locus)
{
    int c = 0;

    if (sr == NULL || !dataset_lags_ok(datainfo)) {
	return 0;
    }

    if (locus == SR_RUVARS && select_lags_upper(sr->code)) {
	c = (sr->code == TSLS)? LAG_W : LAG_X;
    } else if (locus == SR_RLVARS && select_lags_lower(sr->code)) {
	c = LAG_X;
    } 

    return c;
}

static int lag_context_from_widget (GtkWidget *w)
{
    gint locus = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "locus"));
    selector *sr = g_object_get_data(G_OBJECT(w), "sr");
    int context = 0;

    if (sr != NULL && locus) {
	context = sr_get_lag_context(sr, locus);
    }

    return context;
}

static int lags_button_relevant (selector *sr, int locus)
{
    if (sr->lags_button != NULL) {
	if (locus == SR_RUVARS && select_lags_upper(sr->code)) {
	    return 1;
	} else if (locus == SR_RLVARS && select_lags_lower(sr->code)) {
	    return 1;
	}
    }

    return 0;
}

void clear_selector (void)
{
    default_var = 0;
    default_order = 0;
    vartrend = 0;

    arma_p = 1;
    arima_d = 0;
    arma_q = 1;
    arma_P = 0;
    arima_D = 0;
    arma_Q = 0;
    arma_const = 1;

    garch_p = 1;
    garch_q = 1;

    free(xlist);
    xlist = NULL;
    free(instlist);
    instlist = NULL;

    free(veclist);
    veclist = NULL;
    free(vecxlist);
    vecxlist = NULL;

    destroy_lag_preferences();
}

static int selector_get_depvar_number (selector *sr)
{
    int ynum = 0;

    if (sr != NULL && sr->depvar != NULL) {
	ynum = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->depvar), 
						 "data"));
    }

    return ynum;
}

#ifdef OLD_GTK
# include "selector-gtk1.c"
#else

static gint dblclick_lvars_row (GtkWidget *w, GdkEventButton *event, 
				selector *sr); 

/* gtk 2.0 versions of several utility functions */

static void 
real_varlist_set_var (int v, int lag, GtkListStore *store, GtkTreeIter *iter)
{
    if (lag == 0) {
	gtk_list_store_set(store, iter, 0, v, 1, 0, 
			   2, datainfo->varname[v], 
			   -1);
    } else {
	char vstr[VNAMELEN+8];

	sprintf(vstr, "%s(-%d)", datainfo->varname[v], lag);
	gtk_list_store_set(store, iter, 0, v, 1, lag, 
			   2, vstr, -1);
    }
} 

static int 
varlist_remove_var_full (int v, GtkTreeModel *mod, GtkTreeIter *iter)
{
    GtkTreeIter *last = iter;
    int row = -1, ok = 1;
    int tv, i = 0;

#if VLDEBUG
    fprintf(stderr, "\nvarlist_remove_var_full: looking for var %d (%s)\n", v,
	    datainfo->varname[v]);
#endif

    if (gtk_tree_model_get_iter_first(mod, iter)) {
	last = iter;
	while (1) {
	    gtk_tree_model_get(mod, iter, 0, &tv, -1);
#if VLDEBUG
	    fprintf(stderr, "row %d: checking against %d\n", i, tv);
#endif
	    if (tv == v) {
#if (GTK_MINOR_VERSION >= 2) 
		ok = gtk_list_store_remove(GTK_LIST_STORE(mod), iter);
#else
		gtk_list_store_remove(GTK_LIST_STORE(mod), iter);
#endif
#if VLDEBUG
		fprintf(stderr, "removed at row %d, now ok = %d\n", i, ok);
#endif
		if (row < 0) {
		    row = i;
		}
		if (ok) {
		    continue;
		} else {
		    break;
		}
	    }
	    if (!gtk_tree_model_iter_next(mod, iter)) {
		/* iter is now invalid */
		iter = last;
		break;
	    }
	    last = iter;
	    i++;
	} 
    } else {
	iter = last;
    }

    return row;
}   

static void
varlist_insert_var_full (int v, GtkTreeModel *mod, GtkTreeIter *iter, 
			 selector *sr, int locus)
{
    int lcontext = 0;

#if VLDEBUG
    fprintf(stderr, "varlist_insert_var_full: starting var %d\n", v);
#endif

    if (v > 0 && dataset_lags_ok(datainfo)) {
	lcontext = sr_get_lag_context(sr, locus);
    }

    if (lcontext) {
	int *laglist = get_lag_pref_as_list(v, lcontext);

	if (laglist != NULL) {
	    int i, row, append = 0;

	    row = varlist_remove_var_full(v, mod, iter);
	    if (row < 0) {
		append = 1;
	    }

#if VLDEBUG
	    fprintf(stderr, "got laglist, done prior removal, row=%d, append=%d\n",
		    row, append);
#endif
	    for (i=1; i<=laglist[0]; i++) {
		if (append) {
		    gtk_list_store_append(GTK_LIST_STORE(mod), iter);
		} else {
		    gtk_list_store_insert(GTK_LIST_STORE(mod), iter, row++);
		}
#if VLDEBUG
		fprintf(stderr, "adding var %d, lag %d\n", v, laglist[i]);
#endif
		real_varlist_set_var(v, laglist[i], GTK_LIST_STORE(mod), iter);
	    }
	    free(laglist);
	} else {
	    lcontext = 0;
	}
    }

    if (lcontext == 0) {
	gtk_list_store_append(GTK_LIST_STORE(mod), iter);
	real_varlist_set_var(v, 0, GTK_LIST_STORE(mod), iter);
    }
}

static gboolean set_active_var (GtkWidget *widget, GdkEventButton *event,
				selector *sr)
{
    GtkTreeView *view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreePath *path;

    if (gtk_tree_view_get_path_at_pos(view, event->x, event->y, &path, 
				      NULL, NULL, NULL)) { 
	GtkTreeIter iter;
	gint varnum, row;

	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, 0, &varnum, -1);
	if (sr != NULL) {
	    sr->active_var = varnum;
	}
	row = tree_path_get_row_number(path);
	g_object_set_data(G_OBJECT(widget), "active_row",
			  GINT_TO_POINTER(row));
	gtk_tree_path_free(path);
    }

    return FALSE;
}

static void list_append_var_simple (GtkListStore *store, GtkTreeIter *iterp, int v)
{
    const char *vname = datainfo->varname[v];

    gtk_list_store_append(store, iterp);    
    gtk_list_store_set(store, iterp, 0, v, 1, 0, 2, vname, -1);
}

static void list_append_var (GtkListStore *store, GtkTreeIter *iter,
			     int v, selector *sr, int locus)
{
    int i, lcontext = 0;

    if (v > 0 && dataset_lags_ok(datainfo)) {
	lcontext = sr_get_lag_context(sr, locus);
    }

    if (lcontext) {
	int *laglist = get_lag_pref_as_list(v, lcontext);

	if (laglist != NULL) {
	    for (i=1; i<=laglist[0]; i++) {
		gtk_list_store_append(store, iter);
		real_varlist_set_var(v, laglist[i], store, iter);
	    }
	    free(laglist);
	} else {
	    lcontext = 0;
	}
    }

    if (lcontext == 0) {
	gtk_list_store_append(store, iter);
	real_varlist_set_var(v, 0, store, iter);
    }
}

/* build a new liststore and associated tree view, and pack into the
   given box */

static GtkWidget *var_list_box_new (GtkBox *box, selector *sr, int locus) 
{
    GtkListStore *store; 
    GtkWidget *view, *scroller;
    GtkCellRenderer *renderer; 
    GtkTreeViewColumn *column;
    GtkTreeSelection *select;
    int width = 120;
    int height = -1;

    store = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);

    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(G_OBJECT(store));

    g_object_set_data(G_OBJECT(view), "sr", sr);
    g_object_set_data(G_OBJECT(view), "locus", GINT_TO_POINTER(locus));

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ypad", 0, NULL);
    column = gtk_tree_view_column_new_with_attributes(NULL,
						      renderer,
						      "text", 2,
						      NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);	
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    gtk_tree_view_set_reorderable(GTK_TREE_VIEW(view), FALSE);

    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_EXTENDED);

    g_signal_connect(G_OBJECT(view), "motion_notify_event",
		     G_CALLBACK(listbox_drag), NULL);

    if (locus == SR_LVARS) { 
	/* left-hand box with the selectable vars */
	g_signal_connect(G_OBJECT(view), "button_press_event",
			 G_CALLBACK(lvars_right_click),
			 sr);
	g_signal_connect(G_OBJECT(view), "button_press_event",
			 G_CALLBACK(set_active_var),
			 sr);
	g_signal_connect(G_OBJECT(view), "button_press_event",
			 G_CALLBACK(dblclick_lvars_row),
			 sr);
    } else if (locus == SR_RLVARS || locus == SR_RUVARS) { 
	/* lists of selected items */
	g_signal_connect(G_OBJECT(view), "button_press_event",
			 G_CALLBACK(set_active_var),
			 NULL);
	g_signal_connect(G_OBJECT(view), "button_press_event",
			 G_CALLBACK(listvar_special_click),
			 view);
    } 

    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scroller),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW (scroller),
					GTK_SHADOW_IN);    
    gtk_container_add(GTK_CONTAINER(scroller), view);

    gtk_box_pack_start(box, scroller, TRUE, TRUE, 0);

    width *= gui_scale;
    gtk_widget_set_size_request(view, width, height);
    gtk_widget_show(view);
    gtk_widget_show(scroller);

    return view;
}

/* add to "extra" var slot the current selection from sr->lvars */

static void real_set_extra_var (GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter *iter, selector *sr)
{
    gint vnum;
    gchar *vname;
    
    gtk_tree_model_get(model, iter, 0, &vnum, 2, &vname, -1);
    gtk_entry_set_text(GTK_ENTRY(sr->extra[0]), vname);
    g_free(vname);
    g_object_set_data(G_OBJECT(sr->extra[0]), "data",
		      GINT_TO_POINTER(vnum));
}

static void set_extra_var_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sr->lvars));
    gtk_tree_selection_selected_foreach(selection, 
					(GtkTreeSelectionForeachFunc) 
					real_set_extra_var,
					sr);
}

static void real_set_factor (GtkTreeModel *model, GtkTreePath *path,
			     GtkTreeIter *iter, selector *sr)
{
    gint vnum;
    gchar *vname;
    
    gtk_tree_model_get(model, iter, 0, &vnum, 2, &vname, -1);
    gtk_entry_set_text(GTK_ENTRY(sr->rlvars), vname);
    g_free(vname);
    g_object_set_data(G_OBJECT(sr->rlvars), "data",
		      GINT_TO_POINTER(vnum));
}

static void set_factor_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sr->lvars));
    gtk_tree_selection_selected_foreach(selection, 
					(GtkTreeSelectionForeachFunc) 
					real_set_factor,
					sr);
}

static void remove_as_indep_var (selector *sr, gint v)
{
    GtkTreeView *view = GTK_TREE_VIEW(sr->rlvars);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;
    gint xnum;

    if (gtk_tree_model_get_iter_first(model, &iter)) {
	do {
	    gtk_tree_model_get(model, &iter, 0, &xnum, -1);
	    if (xnum == v) {
		gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
	    }
        } while (gtk_tree_model_iter_next(model, &iter));
    }
}

static void 
maybe_insert_or_revise_depvar_lags (selector *sr, int v, int lcontext,
				    int revise)
{
    int *laglist = NULL;
    GtkWidget *w;
    GtkTreeModel *mod;
    GtkTreeIter iter;
    int append = 1;
    int modv, row = 0;
    int jmin = 0, jmax = 1;
    int i, j;

    if (lcontext == LAG_Y_X) {
	jmin = 0;
	jmax = 1;
    } else if (lcontext == LAG_Y_W) {
	/* TSLS */
	jmin = 1;
	jmax = 2;
    } else if (sr->code == TSLS) {
	jmin = 0;
	jmax = 2;
    }

    for (j=0; j<jmax; j++) {

	w = (j > 0)? sr->ruvars: sr->rlvars;
	if (w == NULL) {
	    return;
	}
	
	mod = gtk_tree_view_get_model(GTK_TREE_VIEW(w));

	if (lcontext == 0) {
	    lcontext = (j > 0)? LAG_Y_W : LAG_Y_X;
	}

	laglist = get_lag_pref_as_list(v, lcontext);
	if (laglist == NULL) {
	    if (revise) {
		varlist_remove_var_full(v, mod, &iter);
	    }
	    return;
	}

	varlist_remove_var_full(v, mod, &iter);

	gtk_tree_model_get_iter_first(mod, &iter);
	do {
	    gtk_tree_model_get(mod, &iter, 0, &modv, -1);
	    if (modv > 0) {
		append = 0;
		break;
	    }
	    row++;
	} while (gtk_tree_model_iter_next(mod, &iter));

	for (i=1; i<=laglist[0]; i++) {
	    if (append) {
		gtk_list_store_append(GTK_LIST_STORE(mod), &iter);
	    } else {
		gtk_list_store_insert(GTK_LIST_STORE(mod), &iter, row++);
	    }
#if VLDEBUG
	    fprintf(stderr, "depvar_lags: adding var %d, lag %d\n", v, laglist[i]);
#endif
	    real_varlist_set_var(v, laglist[i], GTK_LIST_STORE(mod), &iter);
	}    

	free(laglist);
    }
}

static void maybe_insert_depvar_lags (selector *sr, int v, int lcontext)
{
    maybe_insert_or_revise_depvar_lags(sr, v, lcontext, 0);
}

static void maybe_revise_depvar_lags (selector *sr, int v, int lcontext)
{
    maybe_insert_or_revise_depvar_lags(sr, v, lcontext, 1);
}

static void dependent_var_cleanup (selector *sr, int newy)
{
    int oldy = selector_get_depvar_number(sr);

    if (oldy > 0) {
	remove_as_indep_var(sr, oldy);
	if (oldy != newy) {
	    y_x_lags_enabled = 0;
	    y_w_lags_enabled = 0;
	}
    }
    remove_as_indep_var(sr, newy);
}

static void set_dependent_var_from_active (selector *sr)
{
    gint v = sr->active_var;

    if (sr->depvar == NULL) return;

    /* models: if we select foo as regressand, remove it from the list
       of regressors if need be; also remove lags associated with the
       previous dependent var, if any.
    */
    if (MODEL_CODE(sr->code)) {
	dependent_var_cleanup(sr, v);
    }

    gtk_entry_set_text(GTK_ENTRY(sr->depvar), datainfo->varname[v]);
    g_object_set_data(G_OBJECT(sr->depvar), "data", GINT_TO_POINTER(v));

    if (select_lags_depvar(sr->code)) {
	maybe_insert_depvar_lags(sr, v, 0);
    }
}

static void real_set_dependent_var (GtkTreeModel *model, GtkTreePath *path,
				    GtkTreeIter *iter, selector *sr)
{
    gchar *vname;
    gint v;

    gtk_tree_model_get(model, iter, 0, &v, 2, &vname, -1);

    if (MODEL_CODE(sr->code)) {
	dependent_var_cleanup(sr, v);
    }

    gtk_entry_set_text(GTK_ENTRY(sr->depvar), vname);
    g_object_set_data(G_OBJECT(sr->depvar), "data", GINT_TO_POINTER(v));

    if (select_lags_depvar(sr->code)) {
	maybe_insert_depvar_lags(sr, v, 0);
    }
}

static void set_dependent_var_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    if (sr->depvar == NULL) return;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(sr->lvars));
    gtk_tree_selection_selected_foreach (selection, 
					 (GtkTreeSelectionForeachFunc) 
					 real_set_dependent_var,
					 sr);
}

static void set_right_var_from_main (GtkTreeModel *model, GtkTreePath *path,
				     GtkTreeIter *iter, selector *sr)
{
    GtkTreeModel *rmod;
    GtkTreeIter r_iter;
    gchar *vnum = NULL;
    int v;

    gtk_tree_model_get(model, iter, 0, &vnum, -1);

    rmod = gtk_tree_view_get_model(GTK_TREE_VIEW(sr->rlvars));
    if (rmod == NULL) {
	g_free(vnum);
	return;
    }

    v = atoi(vnum);

    if (gtk_tree_model_get_iter_first(rmod, &r_iter)) {
	while (gtk_tree_model_iter_next(rmod, &r_iter)) {
	    ;
	}
    }

    gtk_list_store_append(GTK_LIST_STORE(rmod), &r_iter);
    real_varlist_set_var(v, 0, GTK_LIST_STORE(rmod), &r_iter);   

    g_free(vnum);
}

static void set_vars_from_main (selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(mdata->listbox));
    gtk_tree_selection_selected_foreach(selection, 
					(GtkTreeSelectionForeachFunc) 
					set_right_var_from_main,
					sr);
}

/* Append a specified variable in the SR_RLVARS locus: used when
   saving data and there's only one variable to save.
*/

static void select_singleton (selector *sr)
{
    GtkTreeModel *lmod, *rmod;
    GtkTreeIter iter;
    int v;

    lmod = gtk_tree_view_get_model(GTK_TREE_VIEW(sr->lvars));
    if (lmod == NULL) {
	return;
    }    

    rmod = gtk_tree_view_get_model(GTK_TREE_VIEW(sr->rlvars));
    if (rmod == NULL) {
	return;
    }

    gtk_tree_model_get_iter_first(lmod, &iter);
    gtk_tree_model_get(lmod, &iter, 0, &v, -1);

    gtk_tree_model_get_iter_first(rmod, &iter);
    gtk_list_store_append(GTK_LIST_STORE(rmod), &iter);
    gtk_list_store_set(GTK_LIST_STORE(rmod), &iter, 
		       0, v, 1, 0, 2, datainfo->varname[v], -1);
}

static int lvar_present_on_right (gint lnum, GtkWidget *list)
{
    GtkTreeIter iter;
    GtkTreeModel *rmod;
    gint rnum;

    g_return_val_if_fail(GTK_IS_TREE_VIEW(list), 0);

    rmod = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    if (gtk_tree_model_get_iter_first(rmod, &iter)) {
	do {
	    gtk_tree_model_get(rmod, &iter, 0, &rnum, -1);
	    if (rnum == lnum) {
		return 1;
	    }
	} while (gtk_tree_model_iter_next(rmod, &iter));
    }

    return 0;
}

static void real_add_generic (GtkTreeModel *srcmodel, GtkTreeIter *srciter, 
			      selector *sr, int locus)
{
    GtkWidget *list;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *vname = NULL;
    gint v, xnum;
    gint already_there = 0;
    gint at_max = 0;
    gint keep_names = 0;
    int nvars = 0;

    list = (locus == SR_RUVARS)? sr->ruvars : sr->rlvars;

    if (!GTK_IS_TREE_VIEW(list)) {
	return;
    }

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    if (model == NULL) {
	return;
    }

    keep_names = 
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->lvars), 
					  "keep_names"));

    if (keep_names) {
	gtk_tree_model_get(srcmodel, srciter, 0, &v, 2, &vname, -1);
    } else {
	gtk_tree_model_get(srcmodel, srciter, 0, &v, -1);
    }

    if (sr->code == SAVE_FUNCTIONS && 
	lvar_present_on_right(v, sr->rlvars)) {
	return;
    }

    nvars = 0;
    if (gtk_tree_model_get_iter_first(model, &iter)) {
	do {
	    nvars++;
	    if (!at_max && selection_at_max(sr, nvars)) {
		at_max = 1;
	    }
	    if (!already_there) {
		gtk_tree_model_get(model, &iter, 0, &xnum, -1);
		if (xnum == v) {
		    already_there = 1; 
		}
	    }
	} while (gtk_tree_model_iter_next(model, &iter));
    }

    if (!already_there && !at_max) {
	if (vname != NULL) {
	    gtk_list_store_append(GTK_LIST_STORE(model), &iter);
	    gtk_list_store_set(GTK_LIST_STORE(model), &iter, 
			       0, v, 1, 0, 2, vname, -1);
	    g_free(vname);
	} else {
	    varlist_insert_var_full(v, model, &iter, sr, locus);
	}
	nvars++;
    }

    if (sr->add_button != NULL && at_max) {
	gtk_widget_set_sensitive(sr->add_button, FALSE);
    }

    if (nvars > 0 && lags_button_relevant(sr, locus)) {
	gtk_widget_set_sensitive(sr->lags_button, TRUE);
    }
}

static void add_to_rlvars (GtkTreeModel *model, GtkTreePath *path,
			   GtkTreeIter *iter, selector *sr)
{
    /* models: don't add the regressand to the list of regressors */
    if (MODEL_CODE(sr->code)) {
	gint xnum, ynum;
    
	gtk_tree_model_get(model, iter, 0, &xnum, -1);
	ynum = selector_get_depvar_number(sr);
	if (xnum == ynum) {
	    return;
	}
    }

    /* function selector: don't add an interface to both
       rlvars and ruvars */
    if (sr->code == SAVE_FUNCTIONS) {
	gint lnum;

	gtk_tree_model_get(model, iter, 0, &lnum, -1);
	if (lvar_present_on_right(lnum, sr->ruvars)) {
	    return;
	}
    }

    real_add_generic(model, iter, sr, SR_RLVARS);
}

static void add_to_ruvars (GtkTreeModel *model, GtkTreePath *path,
			   GtkTreeIter *iter, selector *sr)
{
    real_add_generic(model, iter, sr, SR_RUVARS);
}

static void add_to_ruvars_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sr->lvars));
    gtk_tree_selection_selected_foreach(selection, 
					(GtkTreeSelectionForeachFunc) 
					add_to_ruvars,
					sr);
}

static void add_all_to_rlvars_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    if (!GTK_IS_TREE_VIEW(sr->lvars)) {
	return;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sr->lvars));
    gtk_tree_selection_select_all(selection);
    gtk_tree_selection_selected_foreach(selection, 
					(GtkTreeSelectionForeachFunc) 
					add_to_rlvars,
					sr);
}

static void add_to_rlvars_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    if (!GTK_IS_TREE_VIEW(sr->lvars)) {
	return;
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sr->lvars));
    gtk_tree_selection_selected_foreach(selection, 
					(GtkTreeSelectionForeachFunc) 
					add_to_rlvars,
					sr);
}

static void remove_from_right_callback (GtkWidget *w, gpointer data)
{
    GtkTreeView *view = GTK_TREE_VIEW(data);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
    GtkTreePath *path;
    GtkTreeIter iter, last;
    selector *sr;
    int context = 0;
    int nsel = 0;

    if (model == NULL || selection == NULL) {
	return;
    }

    /* get to the last row */
    if (gtk_tree_model_get_iter_first(model, &iter)) {
	last = iter;
	nsel = 1;
	while (gtk_tree_model_iter_next(model, &iter)) {
	    last = iter;
	    nsel++;
	}
    } else {
	return;
    }
    
    context = lag_context_from_widget(GTK_WIDGET(view));
    
    /* work back up, deleting selected rows */
    path = gtk_tree_model_get_path(model, &last);
    while (1) {
	if (gtk_tree_model_get_iter(model, &last, path) &&
	    gtk_tree_selection_iter_is_selected(selection, &last)) {
	    if (context) {
		gint v, lag;
		gtk_tree_model_get(model, &last, 0, &v, 1, &lag, -1);
		remove_specific_lag(v, lag, context);
	    }
	    gtk_list_store_remove(GTK_LIST_STORE(model), &last);
	    nsel--;
	}
	if (!gtk_tree_path_prev(path)) {
	    break;
	}
    } 

    sr = g_object_get_data(G_OBJECT(data), "selector");
    if (sr != NULL && sr->add_button != NULL &&
	!GTK_WIDGET_SENSITIVE(sr->add_button) &&
	!selection_at_max(sr, nsel)) {
	gtk_widget_set_sensitive(sr->add_button, TRUE);
    }
}

/* callbacks from button presses in list boxes: double and right
   clicks do special stuff */

static gint 
dblclick_lvars_row (GtkWidget *w, GdkEventButton *event, selector *sr) 
{
    if (event != NULL && event->type == GDK_2BUTTON_PRESS) { 
	if (sr->code == SAVE_FUNCTIONS) {
	    add_to_ruvars_callback(NULL, sr);
	} else {
	    set_dependent_var_from_active(sr);
	    if (sr->default_check != NULL) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sr->default_check),
					     TRUE);
	    }
	}
    }

    return FALSE;
}

static gint listvar_special_click (GtkWidget *widget, GdkEventButton *event, 
				   gpointer data)
{
    GdkWindow *topwin;
    GdkModifierType mods;

    topwin = gtk_widget_get_parent_window(GTK_WIDGET(data));
    gdk_window_get_pointer(topwin, NULL, NULL, &mods); 

    if (mods & GDK_BUTTON2_MASK) {
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(data), TRUE);
    } else {
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(data), FALSE);
    }

    if (mods & GDK_BUTTON3_MASK) {
	remove_from_right_callback(NULL, data);
	return TRUE;
    } 

    return FALSE;
}

#endif /* functions specific to gtk-2.0 */

static gint lvars_right_click (GtkWidget *widget, GdkEventButton *event, 
			       selector *sr)
{
    GdkWindow *topwin;
    GdkModifierType mods;

    topwin = gtk_widget_get_parent_window(sr->lvars);
    gdk_window_get_pointer(topwin, NULL, NULL, &mods); 

    if (mods & GDK_BUTTON3_MASK) {
	add_to_rlvars_callback(NULL, sr);
	return TRUE;
    }

    return FALSE;
}

/* end special click callbacks */

#ifndef OLD_GTK

static void varlist_insert_const (GtkWidget *w)
{
    GtkTreeModel *mod = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
    GtkTreeIter iter;

    gtk_tree_model_get_iter_first(mod, &iter);
    gtk_list_store_append(GTK_LIST_STORE(mod), &iter);
    gtk_list_store_set(GTK_LIST_STORE(mod), &iter, 
		       0, 0, 1, 0, 2, "const", -1);
}

static void clear_vars (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    /* deselect all vars on left */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sr->lvars));
    gtk_tree_selection_unselect_all(selection);

    /* clear dependent var slot */
    if (sr->depvar != NULL) {
	gtk_entry_set_text(GTK_ENTRY(sr->depvar), "");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sr->default_check),
				     FALSE);
	default_var = 0;
    }

    if (sr->code == GR_DUMMY || sr->code == GR_3D) {
	/* clear special slot */
	gtk_entry_set_text(GTK_ENTRY(sr->rlvars), "");
    } else {
	/* empty lower right variable list */
	clear_varlist(sr->rlvars);
	if (sr->add_button != NULL) {
	    gtk_widget_set_sensitive(sr->add_button, TRUE);
	}
    }

    if (MODEL_CODE(sr->code) && sr->code != ARMA) {
	/* insert default const in regressors box */
	varlist_insert_const(sr->rlvars);
    }

    if (sr->ruvars != NULL) {
	/* empty upper right variable list */
	clear_varlist(sr->ruvars);
	if (sr->code == TSLS) {
	    varlist_insert_const(sr->ruvars);
	}
    }

    if (sr->lags_button != NULL) {
	gtk_widget_set_sensitive(sr->lags_button, FALSE);
    }

    clear_selector();
}

static gint varlist_row_count (selector *sr, int locus, int *realrows)
{
    int lcontext = 0;
    GtkWidget *w;
    GtkTreeModel *mod;
    GtkTreeIter iter;
    gint v, lag, n = 0;

    w = (locus == SR_RLVARS)? sr->rlvars : sr->ruvars;

    if (realrows != NULL) {
	lcontext = sr_get_lag_context(sr, locus);
	*realrows = 0;
    }

    if (w != NULL) {
	mod = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
	if (GTK_IS_TREE_MODEL(mod) && 
	    gtk_tree_model_get_iter_first(mod, &iter)) {
	    do {
		n++;
		if (lcontext) {
		    gtk_tree_model_get(mod, &iter, 0, &v, 1, &lag, -1);
		    if (!is_lag_dummy(v, lag, lcontext)) {
			*realrows += 1;
		    }
		}
	    } while (gtk_tree_model_iter_next(mod, &iter));
	}	    
    }

    if (realrows != NULL && lcontext == 0) {
	*realrows = n;
    }

    return n;
}

#endif

static void topslot_empty (int code)
{
    switch (code) {
    case GR_XY:
    case GR_3D:
    case GR_IMP:
	errbox(_("You must select an X-axis variable"));
	break;
    case SCATTERS:
	errbox(_("You must select a Y-axis variable"));
	break;
    default:
	errbox(_("You must select a dependent variable"));
    }
}

static void reverse_list (char *list)
{
    char *tmp, *p;
    char istr[8];

    p = strchr(list, ';');
    if (p == NULL) return;

    tmp = malloc(strlen(list) + 4);
    if (tmp == NULL) return;

    sscanf(list, "%7s", istr);

    strcpy(tmp, p + 2);
    strcat(tmp, " ; ");
    strcat(tmp, istr);

    strcpy(list, tmp);
    
    free(tmp);
}

enum cmdlist_codes {
    ADD_NOW,
    ADD_AT_END
};

static int add_to_cmdlist (selector *sr, const char *add)
{
    int n = strlen(sr->cmdlist);
    char *cmdlist = NULL;
    int err = 0;

    if (n % MAXLEN > MAXLEN - 32) {
	int blocks = 2 + n / MAXLEN;

	cmdlist = realloc(sr->cmdlist, blocks * MAXLEN);
	if (cmdlist == NULL) {
	    err = 1;
	} else {
	    sr->cmdlist = cmdlist;
	}
    }

    if (!err) {
	strcat(sr->cmdlist, add);
    }

    return err;
}

static void add_pdq_vals_to_cmdlist (selector *sr)
{
    char s[32];

    if (sr->code == GARCH) {
	garch_p = spinner_get_int(sr->extra[0]);
	garch_q = spinner_get_int(sr->extra[1]);
	sprintf(s, "%d %d ; ", garch_p, garch_q);
	add_to_cmdlist(sr, s);
	return;
    }

    arma_p = spinner_get_int(sr->extra[0]);
    arima_d = spinner_get_int(sr->extra[1]);
    arma_q = spinner_get_int(sr->extra[2]);

    sprintf(s, "%d %d %d ; ", arma_p, arima_d, arma_q);
    add_to_cmdlist(sr, s);

    if (sr->extra[3] != NULL) {
	arma_P = spinner_get_int(sr->extra[3]);
	arima_D = spinner_get_int(sr->extra[4]);
	arma_Q = spinner_get_int(sr->extra[5]);

	if (arma_P > 0 || arima_D > 0 || arma_Q > 0) {
	    sprintf(s, "%d %d %d ; ", arma_P, arima_D, arma_Q);
	    add_to_cmdlist(sr, s);
	}
    }
}

/* Take the stored preferred laglist for a variable (if any) and
   convert to a string specification for adding to the regression
   command line.
*/

static char *
discrete_lags_string (const char *vname, const int *laglist,
		      char context)
{
    int len = strlen(vname) + 4;
    gchar *tmp;
    char *s;
    int i, li;
    int err = 0;

    s = malloc(len + laglist[0] * 6);

    if (s != NULL) {
	sprintf(s, " %s(", vname);
	for (i=1; i<=laglist[0]; i++) {
	    li = laglist[i];
	    if (li > 999) {
		err = 1;
		break;
	    }
	    tmp = g_strdup_printf("%s%d", (li > 0)? "-" : "", li);
	    strcat(s, tmp);
	    if (i < laglist[0]) {
		strcat(s, ", ");
	    } else {
		strcat(s, ")");
	    }
	    g_free(tmp);
	}

	if (err) {
	    free(s);
	    s = NULL;
	}
    }

    return s;
}  

/* for use in constructing command list, possibly with
   embedded lags */

static char *get_lagpref_string (int v, char context)
{
    const char *vname = datainfo->varname[v];
    const int *laglist;
    int lmin, lmax;
    char *s = NULL;

    if (v == 0) {
	return g_strdup(" 0");
    }

    get_lag_preference(v, &lmin, &lmax, &laglist, context);

    if (laglist != NULL) {
	s = discrete_lags_string(vname, laglist, context);
    } else if (lmin != lmax) {
	s = g_strdup_printf(" %s(%s%d to -%d)", vname, (lmin > 0)? "-" : "",
			    lmin, lmax);
    } else if (lmin != 0) {
	s = g_strdup_printf(" %s(%s%d)", vname, (lmin > 0)? "-" : "",
			    lmin);
    } else if (context != LAG_Y_X && context != LAG_Y_W) {
	s = g_strdup_printf(" %d", v);
    }

#if LDEBUG
    if (s != NULL) {
	fprintf(stderr, "get_lagpref_string (v=%d, context=%d):\n"
		" constructed s = '%s'\n", v, (int) context, s);
    }
#endif

    return s;
} 

static int maybe_resize_recorder_lists (selector *sr, int n)
{
    int err = 0;

    if (MODEL_CODE(sr->code) || VEC_CODE(sr->code)) {
	int *newlist;

	if (MODEL_CODE(sr->code)) {
	    newlist = gretl_list_resize(&xlist, n);
	} else {
	    newlist = gretl_list_resize(&veclist, n);
	}
	if (newlist == NULL) {
	    err = E_ALLOC;
	}
    }

    return err;
}

static int maybe_resize_exog_recorder_lists (selector *sr, int n)
{
    int err = 0;

    if (sr->code == TSLS || USE_VECXLIST(sr->code)) {
	int *newlist;

	if (sr->code == TSLS) {
	    newlist = gretl_list_resize(&instlist, n);
	} else {
	    newlist = gretl_list_resize(&vecxlist, n);
	}
	if (newlist == NULL) {
	    err = E_ALLOC;
	}
    } 

    return err;
}

#ifndef OLD_GTK

static void get_rlvars_data (selector *sr, int rows, int context)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint rvar, lag;
    gchar *rvstr;
    int added = 0;
    int i, j = 1;
	
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(sr->rlvars));
    gtk_tree_model_get_iter_first(model, &iter);

    for (i=0; i<rows; i++) {

	gtk_tree_model_get(model, &iter, 0, &rvar, 1, &lag, -1);

	if (is_lag_dummy(rvar, lag, context)) {
	    gtk_tree_model_iter_next(model, &iter);
	    continue;
	}

	if (context) {
	    rvstr = get_lagpref_string(rvar, context);
	} else {
	    rvstr = g_strdup_printf(" %d", rvar);
	}

	if (rvstr == NULL) {
	    sr->error = E_ALLOC;
	    break;
	} else {
	    add_to_cmdlist(sr, rvstr);
	    g_free(rvstr);
	    added++;
	}

	/* save for future reference */
	if (MODEL_CODE(sr->code) && xlist != NULL) {
	    xlist[j++] = rvar;
	} else if (VEC_CODE(sr->code) && veclist != NULL) {
	    veclist[j++] = rvar;
	}

	gtk_tree_model_iter_next(model, &iter);
    }

    if (sr->code == ARMA && added && !(sr->opts & OPT_N)) {
	/* add const explicitly */
	add_to_cmdlist(sr, " 0");
    }
}

#else

static void get_rlvars_data (selector *sr, int rows, int context)
{
    gchar *rvstr, *lagstr;
    int lag;
    int added = 0;
    int i, j = 1;

    for (i=0; i<rows; i++) {
	lagstr = NULL;
	lag = 0;

	gtk_clist_get_text(GTK_CLIST(sr->rlvars), i, 0, &rvstr);
	gtk_clist_get_text(GTK_CLIST(sr->rlvars), i, 1, &lagstr);
	if (lagstr != NULL) {
	    lag = atoi(lagstr);
	}

	if (is_lag_dummy(atoi(rvstr), lag, context)) {
	    continue;
	}

	if (context) {
	    lagstr = get_lagpref_string(atoi(rvstr), context);
	    add_to_cmdlist(sr, lagstr);
	    free(lagstr);
	    added++;
	} else {
	    add_to_cmdlist(sr, " ");
	    add_to_cmdlist(sr, rvstr);
	    added++;
	}

	if (MODEL_CODE(sr->code) && xlist != NULL) { 
	    xlist[j++] = atoi(rvstr);
	} else if (VEC_CODE(sr->code) && veclist != NULL) {
	    veclist[j++] = atoi(rvstr);
	}
    }

    if (sr->code == ARMA && added && !(sr->opts & OPT_N)) {
	/* add const explicitly */
	add_to_cmdlist(sr, " 0");
    }
}

#endif

#ifndef OLD_GTK

static int get_ruvars_data (selector *sr, int rows, int context)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint exog, lag, ynum;
    int *reclist = NULL;
    gchar *tmp;
    int i, j = 1;
    int err = 0;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(sr->ruvars));
    gtk_tree_model_get_iter_first(model, &iter);

    if (sr->code == TSLS) {
	reclist = instlist;
    } else if (USE_VECXLIST(sr->code)) {
	reclist = vecxlist;
    }

    ynum = selector_get_depvar_number(sr);

    for (i=0; i<rows; i++) {

	gtk_tree_model_get(model, &iter, 0, &exog, 1, &lag, -1);

	if (sr->code == TSLS && exog == ynum && lag == 0) {
	    errbox("You can't use the dependent variable as an instrument");
	    err = 1;
	    break;
	}
		
	if (is_lag_dummy(exog, lag, context)) {
	    gtk_tree_model_iter_next(model, &iter);
	    continue;
	}

	if (context) {
	    tmp = get_lagpref_string(exog, context);
	} else {
	    tmp = g_strdup_printf(" %d", exog);
	}

	add_to_cmdlist(sr, tmp);
	g_free(tmp);

	if (reclist != NULL) {
	    reclist[j++] = exog;
	}

	gtk_tree_model_iter_next(model, &iter);
    }

    return err;
} 

#else

static int get_ruvars_data (selector *sr, int rows, int context)
{
    gchar *tmp;
    gchar *exog;
    gchar *lstr;
    int *reclist = NULL;
    int lag, ynum, xnum;
    int i, j = 1;
    int err = 0;

    if (sr->code == TSLS) {
	reclist = instlist;
    } else if (USE_VECXLIST(sr->code)) {
	reclist = vecxlist;
    }

    ynum = selector_get_depvar_number(sr);

    for (i=0; i<rows; i++) {
	lstr = NULL;
	lag = 0;

	gtk_clist_get_text(GTK_CLIST(sr->ruvars), i, 0, &exog);
	gtk_clist_get_text(GTK_CLIST(sr->ruvars), i, 1, &lstr);

	if (lstr != NULL) {
	    lag = atoi(lstr);
	}

	xnum = atoi(exog);
	if (sr->code == TSLS && xnum == ynum && lag == 0) {
	    errbox("You can't use the dependent variable as an instrument");
	    err = 1;
	    break;
	}	    

	if (is_lag_dummy(xnum, lag, context)) {
	    continue;
	}

	if (context) {
	    tmp = get_lagpref_string(xnum, context);
	    add_to_cmdlist(sr, tmp);
	    free(tmp);
	} else {
	    add_to_cmdlist(sr, " ");
	    add_to_cmdlist(sr, exog);
	}

	if (reclist != NULL) {
	    reclist[j++] = xnum;
	}
    }

    return err;
}

#endif

static void parse_extra_widgets (selector *sr, char *endbit)
{
    const gchar *txt = NULL;
    char numstr[8];
    int k;

    if (sr->code == WLS || sr->code == POISSON || sr->code == AR ||
	sr->code == GR_DUMMY || sr->code == GR_3D) {
	txt = gtk_entry_get_text(GTK_ENTRY(sr->extra[0]));
	if (txt == NULL || *txt == '\0') {
	    if (sr->code == WLS) {
		errbox(_("You must select a weight variable"));
		sr->error = 1;
	    } else if (sr->code == AR) {
		errbox(_("You must specify a list of lags"));
		sr->error = 1;
	    } else if (sr->code == GR_DUMMY || sr->code == GR_3D) {
		errbox(("You must select a Y-axis variable"));
		sr->error = 1;
	    }
	}
    }

    if (sr->error) {
	return;
    }

    if (sr->code == WLS || sr->code == GR_DUMMY || sr->code == GR_3D) {
	k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->extra[0]), "data"));
	sprintf(numstr, "%d ", k);
	add_to_cmdlist(sr, numstr);
    } else if (sr->code == POISSON) {
	if (txt != NULL && *txt != '\0') {
	    k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->extra[0]), "data"));
	    sprintf(endbit, " ; %d", k);
	}
    } else if (sr->code == AR) {
	add_to_cmdlist(sr, txt);
	add_to_cmdlist(sr, " ; ");
    } 
}

static void vec_get_spinner_data (selector *sr, int *order)
{
    GtkAdjustment *adj;
    char numstr[8];
    int k;

    /* lag order */
    adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(sr->extra[0]));
    *order = (int) adj->value;
    sprintf(numstr, "%d ", *order);
    add_to_cmdlist(sr, numstr);

    if (sr->code == VECM) {
	/* cointegration rank */
	adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(sr->extra[1]));
	k = (int) adj->value;
	sprintf(numstr, "%d", k);
	add_to_cmdlist(sr, numstr);
    }
}

static void parse_depvar_widget (selector *sr, char *endbit, char **dvlags,
				 char **idvlags)
{
    const gchar *txt = gtk_entry_get_text(GTK_ENTRY(sr->depvar));

    txt = gtk_entry_get_text(GTK_ENTRY(sr->depvar));

    if (txt == NULL || *txt == 0) {
	topslot_empty(sr->code);
	sr->error = 1;
    } else {
	int ynum = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->depvar), "data"));
	char numstr[8];

	if (sr->code == GR_XY || sr->code == GR_IMP) {
	    sprintf(endbit, " %d", ynum);
	} else {
	    sprintf(numstr, "%d", ynum);
	    add_to_cmdlist(sr, numstr);
	}
	if (dataset_lags_ok(datainfo) && 
	    select_lags_depvar(sr->code)) {
	    *dvlags = get_lagpref_string(ynum, LAG_Y_X);
	    if (sr->code == TSLS) {
		*idvlags = get_lagpref_string(ynum, LAG_Y_W);
	    }
	}
	if (sr->default_check != NULL && 
	    GTK_TOGGLE_BUTTON(sr->default_check)->active) {
	    default_var = ynum;
	}
    }
}

static void parse_special_graph_data (selector *sr)
{
    const gchar *txt = gtk_entry_get_text(GTK_ENTRY(sr->rlvars));
    char numstr[8];

    if (txt == NULL || !*txt) {
	if (sr->code == GR_3D) {
	    errbox(_("You must select a Z-axis variable"));
	} else {
	    errbox(_("You must select a factor variable"));
	}
	sr->error = 1;
    } else {
	int v = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->rlvars), 
						  "data"));
	sprintf(numstr, " %d", v);
	add_to_cmdlist(sr, numstr);
    }
}

static void construct_cmdlist (selector *sr)
{
    gint rows = 0, realrows = 0;
    gchar endbit[12] = {0};
    char *dvlags = NULL;
    char *idvlags = NULL;
    int context = 0;
    int order = 0;

    sr->error = 0;
    sr->cmdlist = mymalloc(MAXLEN); 
    if (sr->cmdlist == NULL) {
	return;
    }
    sr->cmdlist[0] = 0;

    /* deal with content of "extra" widgets */
    if (sr->code == ARMA || sr->code == GARCH) {
	add_pdq_vals_to_cmdlist(sr);
    } else if (VEC_CODE(sr->code)) {
	vec_get_spinner_data(sr, &order);
    } else {
	parse_extra_widgets(sr, endbit);
    }

    /* deal with the "depvar" widget */
    if (!sr->error && sr->depvar != NULL) {
	parse_depvar_widget(sr, endbit, &dvlags, &idvlags);
    }

    /* bail out if things have gone wrong already */
    if (sr->error) {
	return;
    }

    if (sr->code == SCATTERS) {
	add_to_cmdlist(sr, ";");
    }

    if (sr->code == GR_DUMMY || sr->code == GR_3D) { 
	parse_special_graph_data(sr);
	return;
    } 

    rows = varlist_row_count(sr, SR_RLVARS, &realrows);

    if (VEC_CODE(sr->code) && rows < 2) {
	errbox(_("You must select two or more endogenous variables"));
	sr->error = 1;
	return;
    }

    if (realrows > 0) {
	maybe_resize_recorder_lists(sr, realrows);
    }

    /* now for the varlist on the lower right, which usually contains
       the independent variables in a regression context */
    context = sr_get_lag_context(sr, SR_RLVARS);
    get_rlvars_data(sr, rows, context);

    /* and the "extra" list box on the upper right? */
    if (sr->code == TSLS || sr->code == SAVE_FUNCTIONS || 
	USE_VECXLIST(sr->code)) {
	rows = varlist_row_count(sr, SR_RUVARS, &realrows);
	if (rows > 0) {
	    if (realrows > 0) {
		maybe_resize_exog_recorder_lists(sr, realrows);
	    }
	    context = sr_get_lag_context(sr, SR_RUVARS);

	    if (sr->code == TSLS && dvlags != NULL) {
		add_to_cmdlist(sr, dvlags);
		free(dvlags);
		dvlags = NULL;
	    }

	    if (*sr->cmdlist != '\0') {
		add_to_cmdlist(sr, " ;");
	    }

	    sr->error = get_ruvars_data(sr, rows, context);
	} else if (sr->code == TSLS) {
	    errbox(_("You must specify a set of instrumental variables"));
	    sr->error = 1;
	} else if (sr->code == SAVE_FUNCTIONS) {
	    errbox(_("You must specify a public interface"));
	    sr->error = 1;
	}	    
    }

    /* deal with any trailing strings */
    if (!sr->error) {
	if (endbit[0] != '\0') {
	    add_to_cmdlist(sr, endbit);
	} else if (dvlags != NULL) {
	    add_to_cmdlist(sr, dvlags);
	    free(dvlags);
	} else if (idvlags != NULL) {
	    add_to_cmdlist(sr, idvlags);
	    free(idvlags);
	}
    }

    if (sr->code == SCATTERS && !sr->error) {
	int xstate;

#ifndef OLD_GTK	
	xstate = gtk_option_menu_get_history(GTK_OPTION_MENU(scatters_menu));
#else
	xstate = GTK_OPTION_MENU(scatters_menu)->menu_item == x_axis_item;
#endif
	if (xstate) {
	    reverse_list(sr->cmdlist);
	}
    }

    if (!sr->error) {
	/* record some choices as defaults */
	if ((sr->code == VECM || sr->code == VAR || sr->code == VLAGSEL) 
	    && (sr->opts & OPT_D)) {
	    want_seasonals = 1;
	}
	if (sr->code == VECM || sr->code == VAR) {
	    default_order = order;
	}
	if ((sr->code == VAR || sr->code == VLAGSEL) && (sr->opts & OPT_T)) {
	    vartrend = 1;
	}
	if (sr->code == ARMA) {
	    if (sr->opts & OPT_N) {
		arma_const = 0;
	    } 
	    if (sr->opts & OPT_X) {
		arma_x12 = 1;
	    }
	}
    }
}

static void cancel_selector (GtkWidget *widget, selector *sr)
{
    if (open_selector != NULL) {
	gtk_widget_destroy(sr->dlg);
    }
}

static void destroy_selector (GtkWidget *w, selector *sr) 
{
#ifndef OLD_GTK
    if (SAVE_DATA_ACTION(sr->code) || sr->code == SAVE_FUNCTIONS) {
	gtk_main_quit();
    }
#endif

    free(sr->cmdlist);
    free(sr);

    open_selector = NULL;
}

static char *est_str (int cmdnum)
{
    switch (cmdnum) {
    case OLS:
	return N_("OLS");
    case HCCM:
	return N_("HCCM");
    case HSK:
	return N_("Heteroskedasticity corrected");
    case CORC:
	return N_("Cochrane-Orcutt");
    case HILU:
	return N_("Hildreth-Lu");
    case PWE:
	return N_("Prais-Winsten");
    case LOGIT:
	return N_("Logit");
    case PROBIT:
	return N_("Probit");
    case TOBIT:
	return N_("Tobit");
    case LOGISTIC:
	return N_("Logistic");
    case POISSON:
	return N_("Poisson");
    case POOLED:
	return N_("Pooled OLS");
    case WLS:
	return N_("Weighted least squares");
    case TSLS:
	return N_("Two-stage least squares");
    case AR:
	return N_("Autoregressive");
    case ARMA:
	return N_("ARIMA");
    case GARCH:
	return N_("GARCH");
    case VAR:
	return N_("VAR");
    case VLAGSEL:
	return N_("VAR lag selection");
    case VECM:
	return N_("VECM");
    case LAD:
	return N_("LAD");
    case COINT:
    case COINT2:
	return N_("Cointegration");
#ifdef ENABLE_GMP
    case MPOLS:
	return N_("High precision OLS");
#endif
    default:
	return "";
    }
}

static char *extra_string (int cmdnum)
{
    switch (cmdnum) {
    case WLS:
	return N_("Weight variable");
    case POISSON:
	return N_("Offset variable");
    case TSLS:
	return N_("Instruments");
    case AR:
	return N_("List of AR lags");
    case GR_DUMMY:
    case GR_3D:
	return N_("Y-axis variable");
    case SAVE_FUNCTIONS:
	return N_("Public interfaces");
    default:
	return NULL;
    }
}

static gint flip_scatters_axis (GtkMenuItem *m, GtkOptionMenu *popdown)
{
#ifdef OLD_GTK
    gint xstate = (popdown->menu_item == x_axis_item);
#else
    gint xstate = gtk_option_menu_get_history(popdown);
#endif

    if (xstate) {
	gtk_label_set_text(GTK_LABEL(scatters_label), _("Y-axis variables"));
    } else {
	gtk_label_set_text(GTK_LABEL(scatters_label), _("X-axis variables"));
    }

    return FALSE;
}

static GtkWidget *
scatters_popdown (void)
{
    GtkWidget *popdown;
    GtkWidget *menu;
    GtkWidget *child;
    const char *popstrings[] = {
        N_("Y-axis variable"),
        N_("X-axis variable")
    };
    int i;

    popdown = gtk_option_menu_new();
    menu = gtk_menu_new();

    for (i=0; i<2; i++) {
        child = gtk_menu_item_new_with_label(_(popstrings[i]));
	g_signal_connect(G_OBJECT(child), "activate",
			 G_CALLBACK(flip_scatters_axis), popdown);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), child);
#ifdef OLD_GTK
	if (i == 1) x_axis_item = child;
#endif
    }

    gtk_option_menu_set_menu(GTK_OPTION_MENU(popdown), menu);
    scatters_menu = popdown;

    return popdown;
}

static GtkWidget *
entry_with_label_and_chooser (selector *sr, GtkWidget *vbox,
			      gchar *label_string,
			      int label_active,
			      void (*clickfunc)())
{
    GtkWidget *tmp, *x_hbox;
    GtkWidget *entry;

    if (label_active) {
	tmp = scatters_popdown();
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show_all(tmp);
    } else if (label_string != NULL) {
	tmp = gtk_label_new(label_string);
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    }

    x_hbox = gtk_hbox_new(FALSE, 5); 

    tmp = gtk_button_new_with_label (_("Choose->"));
    gtk_box_pack_start(GTK_BOX(x_hbox), tmp, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT(tmp), "clicked", 
		      G_CALLBACK(clickfunc), sr);
    gtk_widget_show(tmp); 

#ifdef OLD_GTK
    entry = gtk_entry_new_with_max_length(VNAMELEN-1);
#else
    entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), VNAMELEN-1);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), VNAMELEN+3);
#endif

    gtk_box_pack_start(GTK_BOX(x_hbox), entry, FALSE, FALSE, 0);
    gtk_widget_show(entry); 

    gtk_box_pack_start(GTK_BOX(vbox), x_hbox, FALSE, FALSE, 0);
    gtk_widget_show(x_hbox); 

    if (label_active || label_string != NULL) {
	tmp = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    }

    return entry;
}

static void build_x_axis_section (selector *sr, GtkWidget *right_vbox)
{
    if (sr->code == SCATTERS) {
	sr->depvar = entry_with_label_and_chooser(sr, right_vbox,
						  NULL, 1,
						  set_dependent_var_callback);
    } else {
	sr->depvar = entry_with_label_and_chooser(sr, right_vbox,
						  _("X-axis variable"), 0,
						  set_dependent_var_callback);
    }
}

static void maybe_activate_depvar_lags (GtkWidget *w, selector *sr)
{
    if (select_lags_depvar(sr->code) && sr->lags_button != NULL) {
	const gchar *txt = gtk_entry_get_text(GTK_ENTRY(w));

	if (txt != NULL && *txt != 0) {
	    gtk_widget_set_sensitive(sr->lags_button, TRUE);
	}
    }
}

/* returns 1 if a variable is pre-inserted as dependent, 0 otherwise */

static int build_depvar_section (selector *sr, GtkWidget *right_vbox,
				 int preselect)
{
    GtkWidget *tmp, *depvar_hbox;
    int yvar = (preselect)? preselect : default_var;

    tmp = gtk_label_new(_("Dependent variable"));
    gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);

    depvar_hbox = gtk_hbox_new(FALSE, 5); 

    tmp = gtk_button_new_with_label (_("Choose ->"));
    gtk_box_pack_start(GTK_BOX(depvar_hbox), tmp, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT(tmp), "clicked", 
                      G_CALLBACK(set_dependent_var_callback), sr);
    gtk_widget_show(tmp); 

#ifdef OLD_GTK
    sr->depvar = gtk_entry_new_with_max_length(VNAMELEN-1);
#else
    sr->depvar = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(sr->depvar), VNAMELEN-1);
    gtk_entry_set_width_chars(GTK_ENTRY(sr->depvar), VNAMELEN+3);
#endif

    g_signal_connect(G_OBJECT(sr->depvar), "changed",
		     G_CALLBACK(maybe_activate_depvar_lags), sr);

    if (yvar) {
        gtk_entry_set_text(GTK_ENTRY(sr->depvar), datainfo->varname[yvar]);
        g_object_set_data(G_OBJECT(sr->depvar), "data",
                          GINT_TO_POINTER(yvar));
    }

    gtk_box_pack_start(GTK_BOX(depvar_hbox), sr->depvar, FALSE, FALSE, 0);
    gtk_widget_show(sr->depvar); 

    gtk_box_pack_start(GTK_BOX(right_vbox), depvar_hbox, FALSE, FALSE, 0);
    gtk_widget_show(depvar_hbox); 

    sr->default_check = gtk_check_button_new_with_label(_("Set as default"));
    gtk_box_pack_start(GTK_BOX(right_vbox), sr->default_check, FALSE, FALSE, 0);
    gtk_widget_show(sr->default_check); 

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);

    return yvar;
}

enum {
    LAG_ONLY,
    LAG_AND_RANK
};

static void lag_order_spin (selector *sr, GtkWidget *vbox, int code)
{
    GtkWidget *tmp, *hbox;
    GtkObject *adj;
#ifdef OLD_GTK
    gfloat lag; 
    gfloat minlag;
    gfloat maxlag;
#else
    gdouble lag; 
    gdouble minlag;
    gdouble maxlag;
#endif
    const char *labels[] = {
	N_("lag order:"),
	N_("cointegration rank:")
    };
    int i, nspin = (code == LAG_AND_RANK)? 2 : 1;

    maxlag = (datainfo->n < 72)? (datainfo->n / 2) : 36;
    minlag = 1;

    if (default_order > 0 && default_order <= maxlag) {
	lag = default_order;
    } else {
	lag = (datainfo->pd > 12)? 12 : datainfo->pd;
    }

    if (sr->code == VLAGSEL) {
	minlag = 2;
	lag *= 2;
	if (lag > maxlag) {
	    lag = maxlag;
	}
    }

    for (i=0; i<nspin; i++) {
	hbox = gtk_hbox_new(FALSE, 5);
	if (sr->code == VLAGSEL) {
	    tmp = gtk_label_new(_("maximum lag:"));
	} else {
	    tmp = gtk_label_new(_(labels[i]));
	}
	gtk_box_pack_start(GTK_BOX(hbox), tmp, TRUE, TRUE, 5);
	gtk_widget_show(tmp);
	gtk_misc_set_alignment(GTK_MISC(tmp), 0.0, 0.5);
	
	if (i == 0) {
	    adj = gtk_adjustment_new(lag, minlag, maxlag, 1, 1, 1);
	} else {
	    adj = gtk_adjustment_new(1, 1, 10, 1, 1, 1);
	}
	sr->extra[i] = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), sr->extra[i], FALSE, FALSE, 5);
	gtk_widget_show(sr->extra[i]);

	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox); 
    }
}

static void dummy_box (selector *sr, GtkWidget *vbox)
{
    sr->rlvars = entry_with_label_and_chooser(sr, vbox,
					      _("Factor (dummy)"), 0,
					      set_factor_callback);
}

static void zvar_box (selector *sr, GtkWidget *vbox)
{
    sr->rlvars = entry_with_label_and_chooser(sr, vbox,
					      _("Z-axis variable"), 0,
					      set_factor_callback);
}

static void extra_var_box (selector *sr, GtkWidget *vbox)
{
    sr->extra[0] = entry_with_label_and_chooser(sr, vbox,
						NULL, 0,
						set_extra_var_callback);
}

static void auxiliary_varlist_box (selector *sr, GtkWidget *right_vbox)
{
#ifndef OLD_GTK
    GtkListStore *store;
    GtkTreeIter iter;
#else
    GtkWidget *store;
    gint iter = 0;
#endif
    int *reclist = NULL;
    
    GtkWidget *tmp, *remove, *midhbox, *button_vbox;

    midhbox = gtk_hbox_new(FALSE, 5);
    button_vbox = gtk_vbox_new(TRUE, 0);

    tmp = gtk_button_new_with_label(_("Add ->"));
    gtk_box_pack_start(GTK_BOX(button_vbox), tmp, TRUE, FALSE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(add_to_ruvars_callback), sr);
    
    remove = gtk_button_new_with_label(_("<- Remove"));
    gtk_box_pack_start(GTK_BOX(button_vbox), remove, TRUE, FALSE, 0);

    if (sr->code == VAR || sr->code == VLAGSEL || sr->code == VECM) {
	sr->lags_button = gtk_button_new_with_label(_("lags..."));
	gtk_box_pack_start(GTK_BOX(button_vbox), sr->lags_button, TRUE, FALSE, 0);
	g_signal_connect(G_OBJECT(sr->lags_button), "clicked", 
			 G_CALLBACK(lags_dialog_driver), sr);
	gtk_widget_set_sensitive(sr->lags_button, FALSE);
    }

    gtk_box_pack_start(GTK_BOX(midhbox), button_vbox, TRUE, TRUE, 0);
    gtk_widget_show_all(button_vbox);

    /* then the listing */
    sr->ruvars = var_list_box_new(GTK_BOX(midhbox), sr, SR_RUVARS);
#ifndef OLD_GTK
    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->ruvars)));
    gtk_list_store_clear(store);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
#else
    store = sr->ruvars;
#endif

    if (sr->code == TSLS) {
	reclist = instlist;
    } else if (sr->code == VAR || sr->code == VLAGSEL || sr->code == VECM) {
	reclist = vecxlist;
    }

    if (reclist != NULL) {
	int i;

	for (i=1; i<=reclist[0]; i++) {
	    list_append_var(store, &iter, reclist[i], sr, SR_RUVARS);
	}
	if (reclist[0] > 0 && (sr->code == VAR || sr->code == VLAGSEL ||
			       sr->code == VECM)) {
	    gtk_widget_set_sensitive(sr->lags_button, TRUE);
	}
    } else if (!VEC_CODE(sr->code) && sr->code != SAVE_FUNCTIONS) {
	list_append_var(store, &iter, 0, sr, SR_RUVARS);
    }

    /* hook up remove button to list box */
    g_signal_connect (G_OBJECT(remove), "clicked", 
		      G_CALLBACK(remove_from_right_callback), 
		      sr->ruvars);

    gtk_box_pack_start(GTK_BOX(right_vbox), midhbox, TRUE, TRUE, 0);
    gtk_widget_show(midhbox); 
}

static void build_mid_section (selector *sr, GtkWidget *right_vbox)
{
    GtkWidget *tmp;
    const char *str = _(extra_string(sr->code));

    if (str != NULL) {
	tmp = gtk_label_new(str);
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    }	

    if (sr->code == WLS || sr->code == POISSON ||
	sr->code == GR_DUMMY || sr->code == GR_3D) { 
	extra_var_box(sr, right_vbox);
    } else if (sr->code == TSLS || sr->code == SAVE_FUNCTIONS) {
	auxiliary_varlist_box(sr, right_vbox);
    } else if (sr->code == AR) {
	sr->extra[0] = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(right_vbox), sr->extra[0], 
			   FALSE, TRUE, 0);
	gtk_widget_show(sr->extra[0]); 
    } else if (sr->code == VAR || sr->code == VLAGSEL) {
	lag_order_spin(sr, right_vbox, LAG_ONLY);
	tmp = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
	tmp = gtk_label_new(_("Exogenous variables"));
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
	auxiliary_varlist_box(sr, right_vbox);
    } else if (sr->code == VECM) {
	lag_order_spin(sr, right_vbox, LAG_AND_RANK);
	tmp = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
	tmp = gtk_label_new(_("Exogenous variables"));
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
	auxiliary_varlist_box(sr, right_vbox);	
    } else if (VEC_CODE(sr->code)) {
	lag_order_spin(sr, right_vbox, LAG_ONLY);
    }

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);
}

static int screen_scalar (int i, int c)
{
    if ((MODEL_CODE(c) || VEC_CODE(c) || GRAPH_CODE(c) || 
	 c == LAGS || c == DIFF || c == LDIFF)
	&& datainfo->vector[i] == 0) {
	return 1;
    } else {
	return 0;
    }
}

static void selector_init (selector *sr, guint code, const char *title,
			   gpointer p, int (*callback)())
{
    GtkWidget *base, *hsep;
    int i, dlgheight = 340;
    double hx;
    
    if (MODEL_CODE(code)) {
	if (datainfo->v > 9) {
	    dlgheight += 80;
	} else if (code == ARMA) {
	    dlgheight += 40;
	}
    } 

    if (code == WLS || code == POISSON || code == AR) {
	dlgheight += 30;
    } else if (code == TSLS) {
	dlgheight += 40;
    } else if (VEC_CODE(code)) {
	dlgheight = 450;
	if (code == VAR || code == VECM) {
	    dlgheight += 90;
	} else if (code == VLAGSEL) {
	    dlgheight += 40;
	}
    } 

    if (WANT_TOGGLES(code)) {
	dlgheight += 40;
    }

    if (WANT_RADIOS(code)) {
	dlgheight += 40;
    }

    if (code == ARMA && datainfo->pd > 1) {
	/* seasonal spinners */
	dlgheight += 60;
    }

    if (dataset_lags_ok(datainfo)) {
	if (MODEL_CODE(code) && code != ARMA) {
	    /* lag selector button at foot */
	    dlgheight += 30;
	}
    }

    sr->lvars = NULL;
    sr->depvar = NULL;
    sr->rlvars = NULL;
    sr->ruvars = NULL;
    sr->default_check = NULL;
    sr->add_button = NULL;
    sr->lags_button = NULL;

    for (i=0; i<N_EXTRA; i++) {
	sr->extra[i] = NULL;
    }

    for (i=0; i<N_RADIOS; i++) {
	sr->radios[i] = NULL;
    }    

    sr->cmdlist = NULL;
    sr->data = p;
    sr->callback = callback;

    sr->active_var = 0;
    sr->error = 0;
    sr->opts = OPT_NONE;

    sr->code = code;
    sr->dlg = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    open_selector = sr;

    gtk_window_set_title(GTK_WINDOW(sr->dlg), title);

    hx = (double) dlgheight * gui_scale;
    dlgheight = hx;
    
    gtk_window_set_default_size(GTK_WINDOW(sr->dlg), -1, dlgheight); 

    g_signal_connect(G_OBJECT(sr->dlg), "destroy", 
		     G_CALLBACK(destroy_selector), 
		     sr); 

    /* create equivalent of gtkdialog structure */
    base = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(sr->dlg), base);
    gtk_widget_show(base);

    sr->vbox = gtk_vbox_new(FALSE, 0);
    gtk_widget_show(sr->vbox);

    /* make (upper) vbox expansible */
    gtk_box_pack_start(GTK_BOX(base), sr->vbox, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(sr->vbox), 5);
    gtk_box_set_spacing(GTK_BOX(sr->vbox), 5);

    hsep = gtk_hseparator_new();
    gtk_widget_show(hsep);
    gtk_box_pack_start(GTK_BOX(base), hsep, FALSE, FALSE, 0);

    sr->action_area = gtk_hbox_new(FALSE, 0);
    gtk_widget_show(sr->action_area);

    /* hbox for buttons is not expansible */
    gtk_box_pack_start(GTK_BOX(base), sr->action_area, 
		       FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(sr->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(sr->action_area), 5);
    gtk_box_set_homogeneous(GTK_BOX(sr->action_area), TRUE);
} 

static void option_callback (GtkWidget *w, selector *sr)
{
    gint i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "opt"));
    gretlopt opt = i;

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	sr->opts |= opt;
    } else {
	sr->opts &= ~opt;
    }  
}

static void reverse_option_callback (GtkWidget *w, selector *sr)
{
    gint i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "opt"));
    gretlopt opt = i;

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	sr->opts &= ~opt;
    } else {
	sr->opts |= opt;
    }    
}

static void robust_config_button (GtkWidget *w, GtkWidget *b)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	gtk_widget_set_sensitive(b, TRUE);
    } else {
	gtk_widget_set_sensitive(b, FALSE);
    }
}

static GtkWidget *spinner_aux_label (int i)
{
    GtkWidget *hbox;
    GtkWidget *lbl;

    hbox = gtk_hbox_new(FALSE, 5);

    if (i == 0) {
	lbl = gtk_label_new(_("Non-seasonal"));
    } else {
	lbl = gtk_label_new(_("Seasonal"));
    }

    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 5);
    gtk_widget_show(lbl);

    return hbox;
}

static GtkWidget *spinner_label (int i, int code)
{
    const char *arma_strs[] = {
	N_("AR order:"),
	N_("Difference:"),
	N_("MA order:")
    };
    const char *arch_strs[] = {
	N_("ARCH p:"),
	N_("ARCH q:")
    };
    GtkWidget *lbl = NULL;

    if (code == ARMA) {
	lbl = gtk_label_new(_(arma_strs[i % 3]));
    } else {
	lbl = gtk_label_new(_(arch_strs[i]));
    }

    return lbl;
}

static void build_pdq_spinners (selector *sr)
{
    GtkWidget *tmp, *hbox;
    GtkObject *adj;
    gdouble vmax, val;
    int i;

    if (sr->code == GARCH) {
	hbox = gtk_hbox_new(FALSE, 5);
	for (i=0; i<2; i++) {
	    tmp = spinner_label(i, GARCH);
	    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	    val = (i==0)? garch_p : garch_q;
	    adj = gtk_adjustment_new(val, 0, 4, 1, 1, 1);
	    sr->extra[i] = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
	    gtk_box_pack_start(GTK_BOX(hbox), sr->extra[i], FALSE, FALSE, 5);
	}
	gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 5);
	gtk_widget_show_all(hbox);
	return;
    }

    if (datainfo->pd > 1) {
	tmp = spinner_aux_label(0);
	gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    }

    hbox = gtk_hbox_new(FALSE, 5);

    for (i=0; i<3; i++) {
	tmp = spinner_label(i, ARMA);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 0);
	val = (i==0)? arma_p : (i==1)? arima_d : arma_q;
	vmax = (i == 1)? 2 : 4;
	adj = gtk_adjustment_new(val, 0, vmax, 1, 1, 1);
	sr->extra[i] = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), sr->extra[i], FALSE, FALSE, 5);
    }

    gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 5);
    gtk_widget_show_all(hbox);

    if (datainfo->pd > 1) {
	tmp = spinner_aux_label(1);
	gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);

	hbox = gtk_hbox_new(FALSE, 5);
	for (i=0; i<3; i++) {
	    tmp = spinner_label(i, ARMA);
	    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 0);
	    val = (i==0)? arma_P : (i==1)? arima_D : arma_Q;
	    vmax = (i == 1)? 2 : 4;
	    adj = gtk_adjustment_new(val, 0, vmax, 1, 1, 1);
	    sr->extra[i+3] = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
	    gtk_box_pack_start(GTK_BOX(hbox), sr->extra[i+3], FALSE, FALSE, 5);
	}

	gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 5);
	gtk_widget_show_all(hbox);    
    }
}

static void hc_config (GtkWidget *w, gpointer p)
{
    options_dialog(4);
}

static void pack_switch (GtkWidget *b, selector *sr,
			 gboolean checked, gboolean reversed, 
			 gretlopt opt, int child)
{
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
    gint offset = (child)? 15 : 0;
    gint i = opt;

    g_object_set_data(G_OBJECT(b), "opt", GINT_TO_POINTER(i));

    if (reversed) {
	g_signal_connect(G_OBJECT(b), "toggled", 
			 G_CALLBACK(reverse_option_callback), sr);
	if (checked) {
	    sr->opts &= ~opt;
	} else {
	    sr->opts |= opt;
	}
    } else {
	g_signal_connect(G_OBJECT(b), "toggled", 
			 G_CALLBACK(option_callback), sr);
	if (checked) {
	    sr->opts |= opt;
	} else {
	    sr->opts &= ~opt;
	}
    }

    gtk_box_pack_start(GTK_BOX(hbox), b, TRUE, TRUE, offset);
    gtk_widget_show(b);

    gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), checked);
}

#define robust_conf(c) (c != LOGIT && c != PROBIT)

static void build_selector_switches (selector *sr) 
{
    GtkWidget *hbox, *tmp;

    if (sr->code == OLS || sr->code == WLS ||  
	sr->code == GARCH || sr->code == TSLS || sr->code == VAR || 
	sr->code == LOGIT || sr->code == PROBIT) {
	GtkWidget *b1;

	tmp = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);

	b1 = gtk_check_button_new_with_label(_("Robust standard errors"));
	g_object_set_data(G_OBJECT(b1), "opt", GINT_TO_POINTER(OPT_R));
	g_signal_connect(G_OBJECT(b1), "toggled",
			 G_CALLBACK(option_callback), sr);

	if (robust_conf(sr->code) && using_hc_by_default()) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b1), TRUE);
	}

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), b1, FALSE, FALSE, 0);
	gtk_widget_show(b1);

	if (robust_conf(sr->code)) {
	    GtkWidget *b2;

	    b2 = gtk_button_new_with_label(_("configure"));
	    g_signal_connect(G_OBJECT(b2), "clicked",
			     G_CALLBACK(hc_config), sr);
	    gtk_widget_set_sensitive(b2, using_hc_by_default());

	    g_signal_connect(G_OBJECT(b1), "toggled",
			     G_CALLBACK(robust_config_button), b2);	

	    gtk_box_pack_start(GTK_BOX(hbox), b2, FALSE, FALSE, 0);
	    gtk_widget_show(b2);
	}

	gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
    }

    if (sr->code == TOBIT || sr->code == ARMA || sr->code == GARCH) {
	if (sr->code == ARMA) {
	    tmp = gtk_check_button_new_with_label(_("Include a constant"));
	    pack_switch(tmp, sr, arma_const, TRUE, OPT_N, 0);
	}
	tmp = gtk_check_button_new_with_label(_("Show details of iterations"));
	pack_switch(tmp, sr, FALSE, FALSE, OPT_V, 0);
    } else if (sr->code == COINT2 || sr->code == VECM || 
	       sr->code == VAR || sr->code == VLAGSEL) {
	if (sr->code == VAR || sr->code == VLAGSEL) {
	    tmp = gtk_check_button_new_with_label(_("Include a constant"));
	    pack_switch(tmp, sr, TRUE, TRUE, OPT_N, 0);
	    tmp = gtk_check_button_new_with_label(_("Include a trend"));
	    pack_switch(tmp, sr, vartrend, FALSE, OPT_T, 0);
	} else {
	    tmp = gtk_check_button_new_with_label(_("Show details of regressions"));
	    pack_switch(tmp, sr, FALSE, FALSE, OPT_V, 0);
	}
	tmp = gtk_check_button_new_with_label(_("Include seasonal dummies"));
	pack_switch(tmp, sr, 
		    want_seasonals && (datainfo->pd == 4 || datainfo->pd == 12),
		    FALSE, OPT_D, 0);
	if (datainfo->pd != 4 && datainfo->pd != 12) {
	    gtk_widget_set_sensitive(tmp, FALSE);
	}
    } else if (sr->code == HILU) {
	tmp = gtk_check_button_new_with_label(_("Fine-tune using Cochrane-Orcutt"));
	pack_switch(tmp, sr, TRUE, TRUE, OPT_B, 0);
    } else if (sr->code == COINT) {
	tmp = gtk_check_button_new_with_label
	    (_("Cointegrating regression includes a constant"));
	pack_switch(tmp, sr, TRUE, TRUE, OPT_N, 0);
    }

#ifdef HAVE_X12A    
    if (sr->code == ARMA) {
	tmp = gtk_check_button_new_with_label(_("Use X-12-ARIMA"));
	pack_switch(tmp, sr, arma_x12, FALSE, OPT_X, 0);
    }	
#endif
} 

static void unhide_lags_callback (GtkWidget *w, selector *sr)
{
    int i, show_lags = GTK_TOGGLE_BUTTON(w)->active;
#ifndef OLD_GTK
    GtkListStore *store;
    GtkTreeIter iter;

    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->lvars)));
    gtk_list_store_clear(store);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
#else
    GtkWidget *store = sr->lvars;
    gint iter = 0;

    gtk_clist_clear(GTK_CLIST(sr->lvars));
#endif

    for (i=1; i<datainfo->v; i++) {
	if (list_show_var(i, sr->code, show_lags)) {
	    list_append_var_simple(store, &iter, i);
	}
    }
}

static void unhide_lags_switch (selector *sr) 
{
    GtkWidget *hbox, *tmp;
    GtkWidget *button;

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);

    button = gtk_check_button_new_with_label(_("Show lagged variables"));
    g_signal_connect(G_OBJECT(button), "toggled",
		     G_CALLBACK(unhide_lags_callback), sr);

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show_all(hbox);
} 

static void build_arma_radios (selector *sr)
{
    GtkWidget *b1, *b2;
    GSList *group;

    b1 = gtk_radio_button_new_with_label(NULL, _("Exact Maximum Likelihood"));
    pack_switch(b1, sr, TRUE, FALSE, OPT_NONE, 1);

    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(b1));
    b2 = gtk_radio_button_new_with_label(group, _("Conditional Maximum Likelihood"));
    pack_switch(b2, sr, FALSE, FALSE, OPT_C, 1);

    sr->radios[0] = b1;
    sr->radios[1] = b2;
}

static void build_vec_radios (selector *sr)
{
    GtkWidget *tmp;
    GtkWidget *button = NULL;
    GSList *group = NULL;
    const char *opt_strs[] = {
	N_("No constant"),
	N_("Restricted constant"),
	N_("Unrestricted constant"),
	N_("Restricted trend"),
	N_("Unrestricted trend"),
	NULL
    };
    gretlopt opts[] = { 
	OPT_N, 
	OPT_R, 
	OPT_NONE, 
	OPT_A, 
	OPT_T 
    };
    int i, deflt = 0;

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);

    for (i=0; opt_strs[i] != NULL; i++) {
	if (button != NULL) {
	    group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
	} else {
	    group = NULL;
	}
	button = gtk_radio_button_new_with_label(group, _(opt_strs[i]));
	pack_switch(button, sr, (opts[i] == deflt), FALSE, opts[i], 0);
    }
}

static void build_selector_radios (selector *sr)
{
    if (sr->code == ARMA) {
	build_arma_radios(sr);
    } else {
	build_vec_radios(sr);
    }
}

static void lag_selector_button (selector *sr)
{
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);

    sr->lags_button = gtk_button_new_with_label(_("lags..."));

    g_signal_connect(G_OBJECT(sr->lags_button), "clicked", 
		     G_CALLBACK(lags_dialog_driver), sr);
    if (varlist_row_count(sr, SR_RLVARS, NULL) < 2) {
	gtk_widget_set_sensitive(sr->lags_button, FALSE);
    }

    gtk_box_pack_start(GTK_BOX(hbox), sr->lags_button, FALSE, FALSE, 0);
    gtk_widget_show(sr->lags_button);

    gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox);
}    

static void selector_doit (GtkWidget *w, selector *sr)
{
    construct_cmdlist(sr);

    if (sr->error == 0) {
	int err = sr->callback(sr);

	if (!err && open_selector != NULL) {
	    gtk_widget_destroy(sr->dlg);
	}
    } 
}

static void 
build_selector_buttons (selector *sr)
{
    GtkWidget *tmp;

    tmp = standard_button(GTK_STOCK_OK);
    GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(sr->action_area), tmp, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(selector_doit), sr);
    gtk_widget_show(tmp);
    gtk_widget_grab_default(tmp);

    tmp = standard_button(GTK_STOCK_CLEAR);
    GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(sr->action_area), tmp, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(clear_vars), sr);
    gtk_widget_show(tmp);

    tmp = standard_button(GTK_STOCK_CANCEL);
    GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(sr->action_area), tmp, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(cancel_selector), sr);
    gtk_widget_show(tmp);

    if (sr->code != PRINT && sr->code != SAVE_FUNCTIONS &&
	sr->code != DEFINE_LIST && !SAVE_DATA_ACTION(sr->code)) {
	tmp = standard_button(GTK_STOCK_HELP);
	GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(sr->action_area), tmp, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(tmp), "clicked", 
			 G_CALLBACK(context_help), 
			 GINT_TO_POINTER(sr->code));
	gtk_widget_show(tmp);
    }
}

static int list_show_var (int v, int code, int show_lags)
{
    int ret = 1;

    lags_hidden = 0;

    if (v == 0 && code == DEFINE_LIST) {
	;
    } else if (v == 0 && (!MODEL_CODE(code) || code == ARMA)) {
	ret = 0;
    } else if (is_hidden_variable(v, datainfo)) {
	ret = 0;
    } else if (screen_scalar(v, code)) {
	ret = 0;
    } else if (!show_lags && is_standard_lag(v, datainfo)) {
	lags_hidden = 1;
	ret = 0;
    }

    return ret;
}

static GtkWidget *selection_dialog_top_label (int ci)
{
    gchar *s;

    if (MODEL_CODE(ci) || VEC_CODE(ci))
	s = _(est_str(ci));
    else if (ci == GR_XY)
	s = _("XY scatterplot");
    else if (ci == GR_IMP)
	s = _("plot with impulses");
    else if (ci == GR_3D)
	s = _("3D plot");
    else if (ci == SCATTERS)
	s = _("multiple scatterplots");
    else if (ci == GR_DUMMY)
	s = _("factorized plot");
    else if (ci == SAVE_FUNCTIONS) 
	s = _("functions to package");
    else
	s = "fixme need string";

    return gtk_label_new(s);
}

void selection_dialog (const char *title, int (*callback)(), guint ci,
		       int preselect) 
{
#ifndef OLD_GTK
    GtkListStore *store;
    GtkTreeIter iter;
#else
    GtkWidget *store;
    gint iter = 0;
#endif
    GtkWidget *right_vbox, *tmp;
    GtkWidget *big_hbox;
    GtkWidget *button_vbox;
    selector *sr;
    int yvar = 0;
    int i;

    if (open_selector != NULL) {
	gdk_window_raise(open_selector->dlg->window);
	return;
    }

    sr = mymalloc(sizeof *sr);
    if (sr == NULL) return;

    selector_init(sr, ci, title, NULL, callback);

    tmp = selection_dialog_top_label(ci);
    gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    /* the following encloses LHS lvars, depvar and indepvar stuff */
    big_hbox = gtk_hbox_new(FALSE, 5); 

    /* LHS: list of elements to choose from */
    sr->lvars = var_list_box_new(GTK_BOX(big_hbox), sr, SR_LVARS);
#ifndef OLD_GTK
    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->lvars)));
    gtk_list_store_clear(store);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
#else
    store = sr->lvars;
#endif

    if (ci == SAVE_FUNCTIONS) {
	functions_list(sr);
    } else {
	for (i=0; i<datainfo->v; i++) {
	    if (list_show_var(i, ci, 0)) {
		list_append_var_simple(store, &iter, i);
	    }
	}
    }

    /* RHS: vertical holder */
    right_vbox = gtk_vbox_new(FALSE, 5);

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);

    if (MODEL_CODE(ci)) { 
	/* models: top right -> dependent variable */
	yvar = build_depvar_section(sr, right_vbox, preselect);
    } else if (ci == GR_XY || ci == GR_IMP || ci == GR_DUMMY
	       || ci == SCATTERS || ci == GR_3D) {
	/* graphs: top right -> x-axis variable */
	build_x_axis_section(sr, right_vbox);
    }

    /* middle right: used for some estimators and factored plot */
    if (ci == WLS || ci == AR || ci == TSLS || 
	VEC_CODE(ci) || ci == POISSON || ci == SAVE_FUNCTIONS ||
	ci == GR_DUMMY || ci == GR_3D) {
	build_mid_section(sr, right_vbox);
    }
    
    if (ci == GR_DUMMY) {
	/* special case: choose dummy var for factorized plot */
	dummy_box(sr, right_vbox);
    } else if (ci == GR_3D) {
	/* special case: choose Z axis variable */
	zvar_box(sr, right_vbox);
    } else { 
	/* all other uses: scrollable list of vars */
	GtkWidget *remove;
	GtkWidget *indepvar_hbox;

	if (COINT_CODE(ci)) {
	    tmp = gtk_label_new(_("Variables to test"));
	} else if (VEC_CODE(ci)) {
	    tmp = gtk_label_new(_("Endogenous variables"));
	} else if (MODEL_CODE(ci)) {
	    tmp = gtk_label_new(_("Independent variables"));
	} else if (ci == GR_XY || ci == GR_IMP) {
	    tmp = gtk_label_new(_("Y-axis variables"));
	} else if (ci == SCATTERS) {
	    scatters_label = tmp = gtk_label_new(_("X-axis variables"));
	} else if (ci == SAVE_FUNCTIONS) {
	    tmp = gtk_label_new(_("Helper functions"));
	}
    
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);

	indepvar_hbox = gtk_hbox_new(FALSE, 5);

	/* push/pull buttons first, in their own little vbox */
	button_vbox = gtk_vbox_new(TRUE, 0);

	tmp = gtk_button_new_with_label (_("Add ->"));
	gtk_box_pack_start(GTK_BOX(button_vbox), tmp, TRUE, FALSE, 0);
	g_signal_connect (G_OBJECT(tmp), "clicked", 
			  G_CALLBACK(add_to_rlvars_callback), sr);
    
	remove = gtk_button_new_with_label (_("<- Remove"));
	gtk_box_pack_start(GTK_BOX(button_vbox), remove, TRUE, FALSE, 0);

	if (sr->code == ARMA) {
	    sr->lags_button = gtk_button_new_with_label(_("lags..."));
	    gtk_box_pack_start(GTK_BOX(button_vbox), sr->lags_button, TRUE, FALSE, 0);
	    g_signal_connect(G_OBJECT(sr->lags_button), "clicked", 
			     G_CALLBACK(lags_dialog_driver), sr);
	    gtk_widget_set_sensitive(sr->lags_button, FALSE);
	    /* FIXME sensitize if vars go in here */
	}

	gtk_box_pack_start(GTK_BOX(indepvar_hbox), button_vbox, TRUE, TRUE, 0);
	gtk_widget_show_all(button_vbox);

	/* then the listing */
	sr->rlvars = var_list_box_new(GTK_BOX(indepvar_hbox), sr, SR_RLVARS);
#ifndef OLD_GTK
	store = 
	    GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->rlvars)));
	gtk_list_store_clear (store);
	gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
#else
	store = sr->rlvars;
#endif

	if (MODEL_CODE(ci)) {
	    if (ci != ARMA) {
		/* stick the constant in by default */
		list_append_var(store, &iter, 0, sr, SR_RLVARS);
	    }
	    if (xlist != NULL) {
		int nx = 0;

		/* we have a saved list of regressors */
		for (i=1; i<=xlist[0]; i++) {
		    if (xlist[i] != 0) {
			list_append_var(store, &iter, xlist[i], sr, SR_RLVARS);
			nx++;
		    }
		}
		if (nx > 0 && sr->code == ARMA) {
		    gtk_widget_set_sensitive(sr->lags_button, TRUE);
		}
	    }
	} else if (VEC_CODE(ci) && veclist != NULL) {
	    for (i=1; i<=veclist[0]; i++) {
		list_append_var(store, &iter, veclist[i], sr, SR_RLVARS);
	    }
	}

	/* hook remove button to listing */
	g_signal_connect (G_OBJECT(remove), "clicked", 
			  G_CALLBACK(remove_from_right_callback), 
			  sr->rlvars);

	/* pack the lower right stuff into the RHS vbox */
	gtk_box_pack_start(GTK_BOX(right_vbox), indepvar_hbox, TRUE, TRUE, 0);
	gtk_widget_show(indepvar_hbox);
    }

    /* pack the whole RHS to the right of the LHS lvars */
    gtk_box_pack_start(GTK_BOX(big_hbox), right_vbox, TRUE, TRUE, 0);
    gtk_widget_show(right_vbox);

    /* pack the whole central section into the dialog's vbox */
    gtk_box_pack_start(GTK_BOX(sr->vbox), big_hbox, TRUE, TRUE, 0);
    gtk_widget_show(big_hbox);

    /* AR, D, MA spinners for ARIMA; or P and Q for GARCH */
    if (sr->code == ARMA || sr->code == GARCH) {
	build_pdq_spinners(sr);
    }

    /* toggle switches for some cases */
    if (WANT_TOGGLES(sr->code)) {
	build_selector_switches(sr);
    }

    /* and radio buttons for some */
    if (WANT_RADIOS(sr->code)) {
	build_selector_radios(sr);
    }

    /* and lag selection stuff, if relevant */
    if (dataset_lags_ok(datainfo)) {
	if (MODEL_CODE(sr->code) && sr->code != ARMA) {
	    lag_selector_button(sr);
	} 
	if (select_lags_depvar(sr->code) && yvar > 0) {
	    maybe_activate_depvar_lags(sr->depvar, sr);
	    maybe_insert_depvar_lags(sr, yvar, 0);
	}
    } 

    /* buttons: OK, Clear, Cancel, Help */
    build_selector_buttons(sr);

    gtk_widget_show(sr->dlg);
}

static char *get_topstr (int cmdnum)
{
    switch (cmdnum) {    
    case LOGS:
	return N_("Select variables for logging");
    case LAGS:
	return N_("Select variables for lagging");
    case SQUARE:
	return N_("Select variables to square");
    case DIFF:
	return N_("Select variables to difference");
    case LDIFF:
	return N_("Select variables to log-difference");
    case ADD:
	return N_("Select variables to add");
    case OMIT:
    case VAROMIT:
	return N_("Select variables to omit");
    case COEFFSUM:
	return N_("Select coefficients to sum");
    case SPEARMAN:
    case MEANTEST:
    case MEANTEST2:
    case VARTEST:
    case ELLIPSE:
	return N_("Select two variables");
    case PRINT:
	return N_("Select variables to display");
    case GR_PLOT: 
    case GR_BOX: 
    case GR_NBOX:
	return N_("Select variables to plot");
    case SAVE_DATA:
    case SAVE_DATA_AS:
    case SAVE_GZDATA:
    case EXPORT_CSV:
    case EXPORT_R:
    case EXPORT_OCTAVE:
	return N_("Select variables to save");
    case COPY_CSV:
	return N_("Select variables to copy");
    case DEFINE_LIST:
	return N_("Define named list");
    default:
	return "";
    }
}

#ifdef OLD_GTK

static int add_omit_list (gpointer p, selector *sr)
{
    windata_t *vwin = (windata_t *) p;
    GRETL_VAR *var = NULL;
    MODEL *pmod = NULL;
    gchar *row[3] = { NULL, NULL, NULL };
    gchar id[5];
    int i, nvars = 0;

    if (sr->code == VAROMIT) {
	var = (GRETL_VAR *) vwin->data;
    } else {
	pmod = (MODEL *) vwin->data;
    }

    if (sr->code == ELLIPSE) {
	char pname[VNAMELEN];

	for (i=0; i<pmod->ncoeff; i++) {
	    gretl_model_get_param_name(pmod, datainfo, i, pname);
	    sprintf(id, "%d", i);
	    row[0] = id;
	    row[2] = pname;
	    gtk_clist_append(GTK_CLIST(sr->lvars), row);
	    nvars++;
	}
	g_object_set_data(G_OBJECT(sr->lvars), "keep_names",
			  GINT_TO_POINTER(1));
    } else if (sr->code == OMIT || sr->code == COEFFSUM) {
	for (i=2; i<=pmod->list[0]; i++) {
	    if (pmod->list[i] == 0) {
		continue;
	    }
	    if (pmod->list[i] == LISTSEP) {
		break;
	    }
	    sprintf(id, "%d", pmod->list[i]);
	    row[0] = id;
	    row[2] = datainfo->varname[pmod->list[i]];
	    gtk_clist_append(GTK_CLIST(sr->lvars), row);
	    nvars++;
	} 
    } else if (sr->code == ADD) {
	for (i=1; i<datainfo->v; i++) {
	    int j, match = 0;

	    for (j=1; j<=pmod->list[0]; j++) {
		if (i == pmod->list[j]) {
		    match = 1;
		    break;
		}
	    }
	    if (match) continue;
	    sprintf(id, "%d", i);
	    row[0] = id;
	    row[2] = datainfo->varname[i];
	    gtk_clist_append(GTK_CLIST(sr->lvars), row);
	    nvars++;
	}
    } else if (sr->code == VAROMIT) {
	int err, vi, *exolist;

	exolist = gretl_VAR_get_exo_list(var, &err);
	if (exolist != NULL) {
	    for (i=1; i<=exolist[0]; i++) {
		vi = exolist[i];
		sprintf(id, "%d", vi);
		row[0] = id;
		row[2] = datainfo->varname[vi];
		gtk_clist_append(GTK_CLIST(sr->lvars), row);
		nvars++;
	    }	    
	} 
    } 

    return nvars;
}

static int functions_list (selector *sr)
{
    const char *fnname;
    gchar *row[3] = { NULL, NULL, NULL };
    gchar id[5];
    int i, nfn;

    nfn = n_user_functions();

    for (i=0; i<nfn; i++) {
	fnname = user_function_name_by_index(i);
	sprintf(id, "%d", i);
	row[0] = id;
	row[2] = (char *) fnname;
	gtk_clist_append(GTK_CLIST(sr->lvars), row);
    }
    g_object_set_data(G_OBJECT(sr->lvars), "keep_names",
		      GINT_TO_POINTER(1));

    return nfn;
}

#else

static int add_omit_list (gpointer p, selector *sr)
{
    windata_t *vwin = (windata_t *) p;
    GRETL_VAR *var = NULL;
    MODEL *pmod = NULL;
    GtkListStore *store;
    GtkTreeIter iter;
    int i, nvars = 0;

    if (sr->code == VAROMIT) {
	var = (GRETL_VAR *) vwin->data;
    } else {
	pmod = (MODEL *) vwin->data;
    }

    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->lvars)));
    gtk_list_store_clear(store);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

    if (sr->code == ELLIPSE) {
	char pname[VNAMELEN];

	for (i=0; i<pmod->ncoeff; i++) {
	    gretl_model_get_param_name(pmod, datainfo, i, pname);
	    gtk_list_store_append(store, &iter);
	    gtk_list_store_set(store, &iter, 
			       0, i, 1, 0,
			       2, pname,
			       -1);
	    nvars++;
	}
	g_object_set_data(G_OBJECT(sr->lvars), "keep_names",
			  GINT_TO_POINTER(1));
    } else if (sr->code == OMIT || sr->code == COEFFSUM) {
	for (i=2; i<=pmod->list[0]; i++) {
	    if (pmod->list[i] == 0) {
		continue;
	    }
	    if (pmod->list[i] == LISTSEP) {
		break;
	    }
	    gtk_list_store_append(store, &iter);
	    gtk_list_store_set(store, &iter, 
			       0, pmod->list[i], 1, 0,
			       2, datainfo->varname[pmod->list[i]],
			       -1);
	    nvars++;
	} 
    } else if (sr->code == ADD) {
	for (i=1; i<datainfo->v; i++) {
	    int j, match = 0;

	    for (j=1; j<=pmod->list[0]; j++) {
		if (i == pmod->list[j]) {
		    match = 1;
		    break;
		}
	    }
	    if (!match) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 
				   0, i, 1, 0, 
				   2, datainfo->varname[i],
				   -1);
		nvars++;
	    }
	}
    } else if (sr->code == VAROMIT) {
	int err, vi, *exolist;

	exolist = gretl_VAR_get_exo_list(var, &err);
	if (exolist != NULL) {
	    for (i=1; i<=exolist[0]; i++) {
		vi = exolist[i];
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 
				   0, vi, 1, 0,
				   2, datainfo->varname[vi],
				   -1);
		nvars++;
	    }	    
	}
    } 

    return nvars;
}

static int functions_list (selector *sr)
{
    GtkListStore *store;
    GtkTreeIter iter;
    const char *fnname;
    int i, nfn;

    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->lvars)));
    gtk_list_store_clear(store);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

    nfn = n_user_functions();

    for (i=0; i<nfn; i++) {
	fnname = user_function_name_by_index(i);
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter,
			   0, i, 1, 0,
			   2, fnname, -1);
    }
    g_object_set_data(G_OBJECT(sr->lvars), "keep_names",
		      GINT_TO_POINTER(1));

    return nfn;
}

#endif

static GtkWidget *simple_selection_top_label (int code)
{
    GtkWidget *label = NULL;
    const char *str = get_topstr(code);

    if (*str != '\0') {
	label = gtk_label_new(_(str));
    } 

    return label;
}

static gboolean remove_busy_signal (GtkWidget *w, windata_t *vwin)
{
    if (vwin != NULL) {
	unset_window_busy(vwin);
    }
    return FALSE;
}

static void selector_add_top_entry (selector *sr)
{
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *entry;

    hbox = gtk_hbox_new(FALSE, 0);
    label = gtk_label_new("Name for list:");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
    entry = gtk_entry_new_with_max_length(31);
    gtk_box_pack_start(GTK_BOX(hbox), entry, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 0);

    sr->extra[0] = entry;

    gtk_widget_show_all(hbox);
}

void simple_selection (const char *title, int (*callback)(), guint ci,
		       gpointer p) 
{
#ifndef OLD_GTK
    GtkListStore *store;
    GtkTreeIter iter;
#else
    GtkWidget *store;
    gint iter = 0;
#endif
    GtkWidget *left_vbox, *button_vbox, *right_vbox, *tmp;
    GtkWidget *top_hbox, *big_hbox, *remove_button;
    selector *sr;
    int nleft = 0;
    int i;

    if (open_selector != NULL) {
	gdk_window_raise(open_selector->dlg->window);
	return;
    }

    sr = mymalloc(sizeof *sr);
    if (sr == NULL) {
	return;
    }

    selector_init(sr, ci, title, p, callback);

    tmp = simple_selection_top_label(ci);
    if (tmp != NULL) {
	gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    } 

    /* entry field for some uses */
    if (ci == DEFINE_LIST) {
	selector_add_top_entry(sr);
    }

    /* for titles */
    top_hbox = gtk_hbox_new(FALSE, 0); 
    gtk_box_set_homogeneous(GTK_BOX(top_hbox), TRUE);

    tmp = gtk_label_new(_("Available vars"));
    gtk_box_pack_start(GTK_BOX(top_hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    tmp = gtk_label_new(" ");
    gtk_box_pack_start(GTK_BOX(top_hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    tmp = gtk_label_new(_("Selected vars"));
    gtk_box_pack_start(GTK_BOX(top_hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    gtk_box_pack_start(GTK_BOX(sr->vbox), top_hbox, FALSE, FALSE, 5);
    gtk_widget_show(top_hbox);

    /* the following will enclose 3 or more vboxes */
    big_hbox = gtk_hbox_new(FALSE, 5); 

    /* holds list of elements available for selection */
    left_vbox = gtk_vbox_new(FALSE, 5);

    sr->lvars = var_list_box_new(GTK_BOX(left_vbox), sr, SR_LVARS);
#ifndef OLD_GTK
    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->lvars)));
    gtk_list_store_clear(store);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
#else
    store = sr->lvars;
#endif

    if (ci == OMIT || ci == ADD || ci == COEFFSUM ||
	ci == ELLIPSE || ci == VAROMIT) {
        nleft = add_omit_list(p, sr);
	g_signal_connect(G_OBJECT(sr->dlg), "destroy", 
			 G_CALLBACK(remove_busy_signal), 
			 p);
    } else {
	int start = (ci == DEFINE_LIST)? 0 : 1;

	nleft = 0;
	for (i=start; i<datainfo->v; i++) {
	    if (list_show_var(i, ci, 0)) {
		list_append_var_simple(store, &iter, i);
		nleft++;
	    }
	}
    }

    gtk_box_pack_start(GTK_BOX(big_hbox), left_vbox, TRUE, TRUE, 0);
    gtk_widget_show(left_vbox);
    
    /* middle: vertical holder for push/pull buttons */
    button_vbox = gtk_vbox_new(TRUE, 0);

    sr->add_button = gtk_button_new_with_label(_("Select ->"));
    gtk_box_pack_start(GTK_BOX(button_vbox), sr->add_button, TRUE, FALSE, 0);
    g_signal_connect(G_OBJECT(sr->add_button), "clicked", 
		     G_CALLBACK(add_to_rlvars_callback), sr);

    if (p == NULL && !TWO_VARS_CODE(sr->code)) {
	/* data save action */
	tmp = gtk_button_new_with_label (_("All ->"));
	gtk_box_pack_start(GTK_BOX(button_vbox), tmp, TRUE, FALSE, 0);
	g_signal_connect (G_OBJECT(tmp), "clicked", 
			  G_CALLBACK(add_all_to_rlvars_callback), sr);
    }
    
    remove_button = gtk_button_new_with_label(_("<- Remove"));
    gtk_box_pack_start(GTK_BOX(button_vbox), remove_button, TRUE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(big_hbox), button_vbox, TRUE, TRUE, 0);
    gtk_widget_show_all(button_vbox);

    /* RHS: vertical holder for selected vars */
    right_vbox = gtk_vbox_new(FALSE, 5);

    sr->rlvars = var_list_box_new(GTK_BOX(right_vbox), sr, SR_RLVARS);
    g_object_set_data(G_OBJECT(sr->rlvars), "selector", sr);

    gtk_box_pack_start(GTK_BOX(big_hbox), right_vbox, TRUE, TRUE, 0);
    gtk_widget_show(right_vbox);

    /* connect removal from right signal */
    g_signal_connect(G_OBJECT(remove_button), "clicked", 
		     G_CALLBACK(remove_from_right_callback), 
		     sr->rlvars);

    /* pack the whole central section into the dialog's vbox */
    gtk_box_pack_start(GTK_BOX(sr->vbox), big_hbox, TRUE, TRUE, 0);
    gtk_widget_show(big_hbox);

    /* unhide lags check box? */
    if (SAVE_DATA_ACTION(sr->code) && lags_hidden) {
	unhide_lags_switch(sr);
    }

    /* buttons: "OK", Clear, Cancel, Help */
    build_selector_buttons(sr);

    if (TWO_VARS_CODE(sr->code) && sr->code != ELLIPSE &&
	mdata_selection_count() == 2) {
	set_vars_from_main(sr);
    } else if (nleft == 1) {
	select_singleton(sr);
    }

    gtk_widget_show(sr->dlg);

    if (SAVE_DATA_ACTION(sr->code)) {
	gretl_set_window_modal(sr->dlg);
    }
}

struct list_maker {
    char *liststr;
    int n_items;
    size_t len;
    int overflow;
};

#ifdef OLD_GTK

static void selection_add_item (gint i, struct list_maker *lmkr)
{
    gchar *varnum = NULL;

    if (lmkr->len > MAXLEN - 12) {
	lmkr->overflow = 1;
	return;
    }

    if (gtk_clist_get_text(GTK_CLIST(mdata->listbox), i, 0, &varnum)) {   
	strcat(lmkr->liststr, " ");
	strcat(lmkr->liststr, varnum);
	lmkr->len += strlen(varnum) + 1;
	lmkr->n_items += 1;
    }
}

#else

static void selection_add_item (GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter *iter, struct list_maker *lmkr)
{
    gchar *varnum = NULL;

    if (lmkr->len > MAXLEN - 12) {
	lmkr->overflow = 1;
	return;
    }

    gtk_tree_model_get (model, iter, 0, &varnum, -1);
    strcat(lmkr->liststr, " ");
    strcat(lmkr->liststr, varnum);
    lmkr->len += strlen(varnum) + 1;
    g_free(varnum);
    lmkr->n_items += 1;
}

#endif

char *main_window_selection_as_string (void) 
{
#ifdef OLD_GTK
    GList *select;
#else
    GtkTreeSelection *select;
#endif
    struct list_maker lmkr;

    lmkr.liststr = mymalloc(MAXLEN);
    if (lmkr.liststr == NULL) {
	return NULL;
    }

    lmkr.liststr[0] = 0;
    lmkr.n_items = lmkr.overflow = 0;
    lmkr.len = 0;

#ifdef OLD_GTK
    select = GTK_CLIST(mdata->listbox)->selection;
    if (select != NULL) {
	g_list_foreach(select, (GFunc) selection_add_item, 
		       &lmkr);
    }
#else
    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(mdata->listbox));

    gtk_tree_selection_selected_foreach(select, 
					(GtkTreeSelectionForeachFunc) 
					selection_add_item,
					&lmkr); 
#endif

    if (lmkr.overflow) {
	errbox(_("Too many items were selected"));
	lmkr.liststr[0] = 0;
    }

    return lmkr.liststr;
}

static const char *data_save_title (int code)
{
    switch (code) {
    case EXPORT_CSV:
	return _("Save CSV data file");
    case EXPORT_R:
	return _("Save R data file");
    case EXPORT_OCTAVE:
	return _("Save octave data file");
    default:
	return _("Save data file");
    }
    return "";
}

static int data_save_selection_callback (selector *sr)
{
    gpointer data = sr->data;
    int code = sr->code;

    if (sr->cmdlist == NULL || *sr->cmdlist == 0) {
	return 0;
    }

    if (storelist != NULL) {
	free(storelist);
	storelist = NULL;
    }

    storelist = g_strdup(sr->cmdlist);
    gtk_widget_destroy(sr->dlg);

    if (code == SAVE_FUNCTIONS) {
	prepare_functions_save();
    } else if (code != COPY_CSV) {
	file_selector(data_save_title(code), code, FSEL_DATA_MISC, data);
    }

    return 0;
}

void data_save_selection_wrapper (int file_code, gpointer p)
{
    if (file_code == SAVE_FUNCTIONS) {
	if (n_user_functions() == 0) {
	    errbox("No user-defined functions are currently loaded.\n"
		   "Please load or define some functions first.");
	    return;
	}
	selection_dialog(_("Save functions"), 
			 data_save_selection_callback, file_code, 0);
    } else {
	simple_selection((file_code == COPY_CSV)? 
			 _("Copy data") : _("Save data"), 
			 data_save_selection_callback, file_code, p);
    }

#ifndef OLD_GTK
    gtk_main(); /* the corresponding gtk_main_quit() is in
		   the function destroy_selector() */
#endif
}

/* accessor functions */

int selector_code (const selector *sr)
{
    return sr->code;
}

const char *selector_list (const selector *sr)
{
    const char *ret = NULL;

    if (sr->cmdlist != NULL && *sr->cmdlist != '\0') {
	ret = sr->cmdlist;
    }

    return ret;
}

int selector_list_hasconst (const selector *sr)
{
    int hc = sr->cmdlist != NULL && 
	strstr(sr->cmdlist, " 0") != NULL;

    return hc;
}

gpointer selector_get_data (const selector *sr)
{
    return sr->data;
}

gretlopt selector_get_opts (const selector *sr)
{
    return sr->opts;
}

const char *selector_entry_text (const selector *sr)
{
    g_return_val_if_fail(GTK_IS_ENTRY(sr->extra[0]), NULL);

    return gtk_entry_get_text(GTK_ENTRY(sr->extra[0]));
}

int selector_error (const selector *sr)
{
    return sr->error;
}

void maybe_clear_selector (const int *dlist)
{
    int i, j;

    if (xlist != NULL) {
	for (i=1; i<=xlist[0]; i++) {
	    for (j=1; j<=dlist[0]; j++) {
		if (xlist[i] >= dlist[j]) {
		    clear_selector();
		    return;
		}
	    }
	}
    }
}

/* ------------- lag selection apparatus -------------- */

#define NOT_LAG 6666
#define VDEFLT -1

typedef struct var_lag_info_ var_lag_info;

struct var_lag_info_ {
    int v;
    int pos;
    int nvl;
    int lmin;
    int lmax;
    char context;
    char *lspec;
    GtkWidget *spin1;
    GtkWidget *spin2;
    GtkWidget *entry;
    GtkWidget *toggle;
    var_lag_info *vlp;
};

enum {
    LAGS_APPLY = 1,
    LAGS_OK,
    LAGS_CANCEL
};

#define depvar_row(c) (c == LAG_Y_X || c == LAG_Y_W)

static void lag_entry_callback (GtkWidget *w, gpointer p)
{
    const gchar *s = gtk_entry_get_text(GTK_ENTRY(w));
    var_lag_info *vlinfo = (var_lag_info *) p;

    free(vlinfo->lspec);
    vlinfo->lspec = g_strdup(s);

    if (vlinfo->v == VDEFLT) {
	/* set the default for this context */
	var_lag_info *vlset = vlinfo->vlp;
	GtkWidget *e;
	int i;

	for (i=vlinfo->pos+1; i<vlinfo->nvl; i++) {
	    if (vlset[i].context == vlinfo->context) {
		e = vlset[i].entry;
		if (e != NULL) {
		    gtk_entry_set_text(GTK_ENTRY(vlset[i].entry), s);
		}
	    }
	}
    }
}

#ifdef OLD_GTK

static int spinner_get_int (GtkWidget *w)
{
    GtkAdjustment *adj = 
	gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(w));

    return (int) adj->value;
}

#else

static int spinner_get_int (GtkWidget *w)
{
    return (int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
}

#endif

static void lag_set_callback (GtkWidget *w, gpointer p)
{
    var_lag_info *vlinfo;
    double dlag;
    int *lag;

#ifdef OLD_GTK
    /* inputs are swapped */
    w = GTK_WIDGET(p);
#endif

    vlinfo = (var_lag_info *) g_object_get_data(G_OBJECT(w), "vlinfo");

    lag = (w == vlinfo->spin1)? &vlinfo->lmin : &vlinfo->lmax;
    *lag = spinner_get_int(w);
    dlag = *lag;

    /* fix-ups, if need be */
    if (w == vlinfo->spin1 && vlinfo->spin2 != NULL) {
	int lmax = spinner_get_int(vlinfo->spin2);

	if (lmax < *lag) {
	    gtk_spin_button_set_value(GTK_SPIN_BUTTON(vlinfo->spin2), dlag);
	}
    } else if (vlinfo->spin1 != NULL) {
	int lmin = spinner_get_int(vlinfo->spin1);

	if (lmin > *lag) {
	    gtk_spin_button_set_value(GTK_SPIN_BUTTON(vlinfo->spin1), dlag);
	}
    }	

    if (vlinfo->v == VDEFLT) {
	/* set the default for this context */
	var_lag_info *vlset = vlinfo->vlp;
	GtkWidget *s;
	int i;

	for (i=vlinfo->pos+1; i<vlinfo->nvl; i++) {
	    if (vlset[i].context == vlinfo->context) {
		s = (w == vlinfo->spin1)? vlset[i].spin1 : vlset[i].spin2;
		if (s != NULL) {
		    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), dlag);
		}
	    }
	}
    }
}

static void activate_specific_lags (GtkWidget *w, var_lag_info *vlinfo)
{
    gboolean active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));

    if (active) {
	gtk_widget_set_sensitive(vlinfo->entry, TRUE);
	gtk_widget_set_sensitive(vlinfo->spin1, FALSE);
	gtk_widget_set_sensitive(vlinfo->spin2, FALSE);
    } else {
	gtk_widget_set_sensitive(vlinfo->entry, FALSE);
	gtk_widget_set_sensitive(vlinfo->spin1, TRUE);
	gtk_widget_set_sensitive(vlinfo->spin2, TRUE);
    } 

    if (vlinfo->v == VDEFLT) {
	/* set the default per context */
	var_lag_info *vlset = vlinfo->vlp;
	int i;

	for (i=vlinfo->pos+1; i<vlinfo->nvl; i++) {
	    if (vlset[i].context == vlinfo->context) {
		if (vlset[i].toggle != NULL) {
		    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vlset[i].toggle), 
						 active);
		}
	    }
	}
    } 
}

static void lag_toggle_register (GtkWidget *w, var_lag_info *vlinfo)
{
    var_lag_info *vlset = vlinfo->vlp;
    gboolean active;
    int i;

    if (vlset == NULL) return;

    for (i=0; i<vlinfo->nvl; i++) {
	if (depvar_row(vlset[i].context)) {
	    if (!GTK_WIDGET_SENSITIVE(vlset[i].spin1) &&
		!GTK_WIDGET_SENSITIVE(vlset[i].entry)) {
		/* dependent var lags are disabled */
		vlset[i].lmin = vlset[i].lmax = NOT_LAG; /* ?? */
		free(vlset[i].lspec);
		vlset[i].lspec = NULL;
		continue;
	    }
	}
	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vlset[i].toggle));
	if (active) {
	    vlset[i].lmin = vlset[i].lmax = NOT_LAG; 
	} else {
	    free(vlset[i].lspec);
	    vlset[i].lspec = NULL;
	}
    }
}

static void activate_y_lags (GtkWidget *w, var_lag_info *vlinfo)
{
    gboolean active = 
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));

    gtk_widget_set_sensitive(vlinfo->spin1, active);
    gtk_widget_set_sensitive(vlinfo->spin2, active);
    gtk_widget_set_sensitive(vlinfo->toggle, active);

    if (active) {
	vlinfo->lmin = spinner_get_int(vlinfo->spin1);
	vlinfo->lmax = spinner_get_int(vlinfo->spin2);
    }

    if (vlinfo->context == LAG_Y_X) {
	y_x_lags_enabled = active;
    } else {
	y_w_lags_enabled = active;
    }
}

static void lagsel_spin_connect (GtkWidget *button)
{
#ifdef OLD_GTK
    GtkAdjustment *adj;

    adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(button));
    gtk_signal_connect(GTK_OBJECT(adj), "value-changed", 
		       GTK_SIGNAL_FUNC(lag_set_callback), button);
#else
    g_signal_connect(G_OBJECT(button), "value-changed", 
		     G_CALLBACK(lag_set_callback), NULL);
#endif
}

static void lags_set_ok (GtkWidget *w, gpointer p)
{
    int *resp = (int *) p;

    *resp = LAGS_OK;
}

static void lags_set_cancel (GtkWidget *w, gpointer p)
{
    int *resp = (int *) p;

    *resp = LAGS_CANCEL;
}

static void resensitive_selector (GtkWidget *w, gpointer p)
{
    if (open_selector != NULL) {
	gtk_widget_set_sensitive(open_selector->dlg, TRUE);
    }
}

/* Below: we provide spinners for a lag range and also a free-form
   entry field for non-contiguous lags.  In some circumstances we
   allow specification of lags for the dependent variable as well
   as the independent vars.
*/

static int 
lags_dialog (const int *list, var_lag_info *vlinfo, selector *sr) 
{
    GtkWidget *lbl, *dialog, *myvbox;
    GtkWidget *tbl, *tmp, *hbox;
    GtkWidget *y_check = NULL;
    gint tbl_len;
    double lmin, lmax, ldef;
    int i, j;
    int ret = LAGS_APPLY;

    dialog = gretl_dialog_new(_("lag order"), sr->dlg, GRETL_DLG_BLOCK);
    g_signal_connect(G_OBJECT(dialog), "destroy",
		     G_CALLBACK(resensitive_selector), NULL);

    myvbox = gtk_vbox_new(FALSE, 5);

    /* allow for additional label row */
    tbl_len = list[0] + 1;

    lmax = (datainfo->t2 - datainfo->t1) / list[0];
    ldef = datainfo->pd;

    tbl = gtk_table_new(tbl_len, 7, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(tbl), 5);
    gtk_table_set_col_spacings(GTK_TABLE(tbl), 5);
    gtk_box_pack_start(GTK_BOX(myvbox), tbl, FALSE, FALSE, 0);

    /* row 0 of table: headings */
    lbl = gtk_label_new(_("Variable"));
    gtk_table_attach_defaults(GTK_TABLE(tbl), lbl, 0, 1, 0, 1);
    lbl = gtk_label_new(_("lags"));
    gtk_table_attach_defaults(GTK_TABLE(tbl), lbl, 1, 4, 0, 1);
    lbl = gtk_label_new("  ");
    gtk_table_attach_defaults(GTK_TABLE(tbl), lbl, 4, 5, 0, 1);
    lbl = gtk_label_new(_("or"));
    gtk_table_attach_defaults(GTK_TABLE(tbl), lbl, 5, 6, 0, 1);
    lbl = gtk_label_new(_("specific lags"));
    gtk_table_attach_defaults(GTK_TABLE(tbl), lbl, 6, 7, 0, 1);

    j = 0;
    for (i=1; i<=list[0]; i++) {

	if (list[i] >= LISTSEP) {
	    if (list[i] - LISTSEP == LAG_W) {
		tmp = gtk_label_new(_("Instruments"));
		gtk_table_attach_defaults(GTK_TABLE(tbl), tmp, 0, 7, i, i+1);
	    } else if (depvar_row(list[i] - LISTSEP)) { 
		y_check = gtk_check_button_new_with_label(_("Lags of dependent variable"));
		gtk_table_attach_defaults(GTK_TABLE(tbl), y_check, 0, 7, i, i+1);
	    } else {
		tmp = gtk_hseparator_new();
		gtk_table_attach_defaults(GTK_TABLE(tbl), tmp, 0, 7, i, i+1);
	    }
	    continue;
	}

	if (list[i] == VDEFLT) {
	    lbl = gtk_label_new("default");
	} else {
	    lbl = gtk_label_new(datainfo->varname[list[i]]);
	}
	gtk_table_attach_defaults(GTK_TABLE(tbl), lbl, 0, 1, i, i+1);

	lmin = (depvar_row(vlinfo[j].context))? 1 : 0;

	/* min. lag spinner */
	vlinfo[j].spin1 = gtk_spin_button_new_with_range(lmin, lmax, 1);
	gtk_table_attach_defaults(GTK_TABLE(tbl), vlinfo[j].spin1, 1, 2, i, i+1);
	g_object_set_data(G_OBJECT(vlinfo[j].spin1), "vlinfo", &vlinfo[j]);
	lagsel_spin_connect(vlinfo[j].spin1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(vlinfo[j].spin1), 
				  vlinfo[j].lmin);

	lbl = gtk_label_new("to");
	gtk_table_attach_defaults(GTK_TABLE(tbl), lbl, 2, 3, i, i+1);

	/* max. lag spinner */
	vlinfo[j].spin2 = gtk_spin_button_new_with_range(lmin, lmax, 1);
	gtk_table_attach_defaults(GTK_TABLE(tbl), vlinfo[j].spin2, 3, 4, i, i+1);
	g_object_set_data(G_OBJECT(vlinfo[j].spin2), "vlinfo", &vlinfo[j]);
	lagsel_spin_connect(vlinfo[j].spin2);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(vlinfo[j].spin2), 
				  vlinfo[j].lmax);

	/* spacer column */
	lbl = gtk_label_new("  ");
	gtk_table_attach_defaults(GTK_TABLE(tbl), lbl, 4, 5, 0, 1);

	/* toggle button for activating entry of specific lags */
	vlinfo[j].toggle = gtk_check_button_new();
	gtk_table_attach_defaults(GTK_TABLE(tbl), vlinfo[j].toggle, 5, 6, i, i+1);
	g_signal_connect(G_OBJECT(vlinfo[j].toggle), "toggled", 
			 G_CALLBACK(activate_specific_lags), &vlinfo[j]);

	/* entry widget for specific lags */
	vlinfo[j].entry = gtk_entry_new();
#ifndef OLD_GTK
	gtk_entry_set_width_chars(GTK_ENTRY(vlinfo[j].entry), 16);
#endif
	gtk_table_attach_defaults(GTK_TABLE(tbl), vlinfo[j].entry, 6, 7, i, i+1);
	g_signal_connect(G_OBJECT(vlinfo[j].entry), "changed", 
			 G_CALLBACK(lag_entry_callback), &vlinfo[j]);
	if (vlinfo[j].lspec != NULL && vlinfo[j].lspec[0] != 0) {
	    /* got a saved non-contiguous lag spec */
	    gtk_entry_set_text(GTK_ENTRY(vlinfo[j].entry), vlinfo[j].lspec);
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vlinfo[j].toggle),
					 TRUE);
	} else {
	    gtk_widget_set_sensitive(vlinfo[j].entry, FALSE);
	}

	/* set sensitivity of dependent variable lags apparatus */
	if (depvar_row(vlinfo[j].context)) {
	    g_signal_connect(G_OBJECT(y_check), "toggled", 
			     G_CALLBACK(activate_y_lags), &vlinfo[j]);
	    if ((vlinfo[j].context == LAG_Y_X && y_x_lags_enabled) ||
		(vlinfo[j].context == LAG_Y_W && y_w_lags_enabled)) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(y_check),
					     TRUE);
	    } else {
		gtk_widget_set_sensitive(vlinfo[j].spin1, FALSE);
		gtk_widget_set_sensitive(vlinfo[j].spin2, FALSE);
		gtk_widget_set_sensitive(vlinfo[j].toggle, FALSE);
		gtk_widget_set_sensitive(vlinfo[j].entry, FALSE);
	    }
	}

	j++;
    }

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), myvbox, TRUE, TRUE, 5);

    if (list[0] > 10) {
	GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
				       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroller), 
					      hbox);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), scroller, TRUE, TRUE, 5);
	gtk_widget_show_all(scroller);
	gtk_widget_set_size_request(scroller, -1, 360);
    } else {
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 5);
	gtk_widget_show_all(hbox);
    }
	
    /* Create the "OK" button */
    tmp = ok_button(GTK_DIALOG(dialog)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(lags_set_ok), &ret);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(lag_toggle_register), &vlinfo[0]);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(delete_widget), dialog);
    gtk_widget_grab_default(tmp);
    gtk_widget_show(tmp);

    /* Create the "Apply" button */
    tmp = apply_button(GTK_DIALOG(dialog)->action_area);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(lag_toggle_register), &vlinfo[0]);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(delete_widget), dialog);
    gtk_widget_show(tmp);

    /* "Cancel" button */
    tmp = cancel_delete_button(GTK_DIALOG(dialog)->action_area, dialog);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(lags_set_cancel), &ret);

    /* "Help" button */
    context_help_button(GTK_DIALOG(dialog)->action_area, LAGS_DIALOG);

    gtk_widget_set_sensitive(sr->dlg, FALSE);
    gtk_widget_show(dialog);   

    return ret;
}

static int laggable_var (int v, int lag, int context)
{
    if (v == 0 || !strcmp(datainfo->varname[v], "time") ||
	!strcmp(datainfo->varname[v], "timesq")) {
	return 0;
    } 

    if (is_lag_dummy(v, lag, context)) {
	return 0;
    }

    return 1;
}

#ifdef OLD_GTK

static void 
revise_var_string (var_lag_info *vlinfo, selector *sr, int locus)
{
    GtkWidget *w;
    char *vnum;
    int modv;
    int k, rows;

    w = (locus == SR_RUVARS)? sr->ruvars : sr->rlvars;
    rows = GTK_CLIST(w)->rows;

    for (k=0; k<rows; k++) {
	gtk_clist_get_text(GTK_CLIST(w), k, 0, &vnum);
	modv = atoi(vnum);
	if (modv == vlinfo->v) {
	    varlist_insert_var_full(vlinfo->v, w, k, sr, locus);
	    break;
	}
    }
}

static int get_laggable_vars (GtkWidget *w, int context, int *list, int *i)
{
    gint rows, xnum, lag;
    int k, n = 0;

    if (!GTK_IS_CLIST(w)) {
	return 0;
    }  

    rows = GTK_CLIST(w)->rows;

    for (k=0; k<rows; k++) {
	clist_row_get_v_and_lag(GTK_CLIST(w), k, &xnum, &lag);
	if (laggable_var(xnum, lag, context)) {
	    if (list != NULL) {
		list[*i] = xnum;
		*i += 1;
	    }
	    n++;
	}
    }

    return n;
}

#else

static void 
revise_var_string (var_lag_info *vlinfo, selector *sr, int locus)
{
    GtkWidget *w;
    GtkTreeModel *mod;
    GtkTreeIter iter;
    int modv;

    w = (locus == SR_RUVARS)? sr->ruvars : sr->rlvars;
    mod = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
    gtk_tree_model_get_iter_first(mod, &iter);

    do {
	gtk_tree_model_get(mod, &iter, 0, &modv, -1);
	if (modv == vlinfo->v) {
	    varlist_insert_var_full(vlinfo->v, mod, &iter, sr, locus);
	    break;
	}
    } while (gtk_tree_model_iter_next(mod, &iter));
}

/* go through a list box and find the vars that are candidates for
   lag selection: this excludes constant and trend, and also
   list entries that are themselves just dummy placeholders
   for already selected lags of a variable */

static int get_laggable_vars (GtkWidget *w, int context, int *list, int *i)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gint xnum, lag;
    int n = 0;

    if (!GTK_IS_TREE_VIEW(w)) {
	return 0;
    }

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
    if (model == NULL) {
	return 0;
    }

    if (gtk_tree_model_get_iter_first(model, &iter)) {
	do {
	    gtk_tree_model_get(model, &iter, 0, &xnum, 1, &lag, -1);
	    if (laggable_var(xnum, lag, context)) { 
		if (list != NULL) {
		    list[*i] = xnum;
		    *i += 1;
		}
		n++;
	    }
	} while (gtk_tree_model_iter_next(model, &iter));
    }

    return n;
}

#endif

static int *sr_get_stoch_list (selector *sr, int *nset, int *pcontext)
{
    GtkWidget *list[2] = { NULL, NULL };
    gint ynum = 0;
    int nv[2] = {0};
    int llen, sep;
    int context;
    int i, j;
    int *slist = NULL;

    if (sr->code != ARMA && sr->code != VAR &&
	sr->code != VECM && sr->code != VLAGSEL) { 
	ynum = selector_get_depvar_number(sr);
    }

    list[0] = (select_lags_lower(sr->code))? sr->rlvars : sr->ruvars;
    if (sr->code == TSLS) {
	list[1] = sr->ruvars;
    } 

    for (j=0; list[j] != NULL && j<2; j++) {
	context = (j == 1)? LAG_W : LAG_X;
	nv[j] = get_laggable_vars(list[j], context, NULL, NULL);
    }

    if (nv[0] == 0) {
	list[0] = NULL;
    }
    if (nv[1] == 0) {
	list[1] = NULL;
    }

    llen = sep = 0;

    if (ynum == 0 && nv[0] == 0 && nv[1] == 0) {
	/* no vars to deal with */
	errbox("Please add some variables to the model first");
    } else {
	if (nv[0] > 0) {
	    *pcontext = LAG_X;
	    llen += nv[0] + 1; /* xvars plus their defaults */
	    if (ynum > 0) {
		llen++; /* separator */
		sep++;
		llen++; /* dep var row */
	    }
	}
	if (nv[1] > 0) {
	    if (nv[0] > 0) {
		llen++; /* separator from X's */
		sep++;
	    } else {
		*pcontext = LAG_W;
	    }
	    llen++; /* "instruments" heading */
	    sep++;
	    llen += nv[1] + 1; /* instruments plus their defaults */
	    if (ynum > 0) {
		llen++; /* separator */
		sep++;
		llen++; /* dep var row */
	    }	    
	}
	if (nv[0] == 0 && nv[1] == 0) {
	    /* only the dependent variable is present */
	    *pcontext = LAG_Y_X;
	    llen++; /* "separator" */
	    sep++;
	    llen++; /* dep var row */
	}

	slist = gretl_list_new(llen);

	if (slist != NULL) {
	    i = 1;
	    for (j=0; j<2; j++) {
		if (list[j] == NULL) {
		    continue;
		}
		context = (j == 1)? LAG_W : LAG_X;
		if (j == 1) {
		    if (nv[0] > 0) {
			slist[i++] = LISTSEP;
		    }
		    slist[i++] = LISTSEP + LAG_W;
		}
		slist[i++] = VDEFLT;
		get_laggable_vars(list[j], context, slist, &i);
		if (ynum > 0) {
		    slist[i++] = LISTSEP + ((j > 0)? LAG_Y_W : LAG_Y_X);
		    slist[i++] = ynum;
		}
	    }
	    if (nv[0] == 0 && nv[1] == 0) {
		slist[i++] = LISTSEP + LAG_Y_X;
		slist[i++] = ynum;
	    }
	}
	*nset = llen - sep;
    }

    return slist;
}

static void maybe_revise_var_string (var_lag_info *vlinfo, selector *sr)
{
    int locus = 0;

#if VLDEBUG
    fprintf(stderr, "maybe_revise_var_string: v = %d\n", vlinfo->v);
#endif

    if (vlinfo->context == LAG_X) {
	locus = (sr->code == VAR || 
		 sr->code == VECM ||
		 sr->code == VLAGSEL)? SR_RUVARS : SR_RLVARS;
    } else if (vlinfo->context == LAG_W) {
	locus = SR_RUVARS;
    } else if (depvar_row(vlinfo->context)) { 
#if VLDEBUG
	fprintf(stderr, " calling maybe_revise_depvar_lags\n");
#endif
	maybe_revise_depvar_lags(sr, vlinfo->v, vlinfo->context);
    }

    if (locus > 0) {
	revise_var_string(vlinfo, sr, locus);
    } 
}

/* return 1 if lags changed, else 0 */

static int set_lags_for_var (var_lag_info *vlinfo, int yxlags, int ywlags)
{
    int *llist = NULL;
    int changed = 0;
    int err = 0;

#if LDEBUG
    fprintf(stderr, "set_lags_for_var: v=%d, lmin=%d, lmax=%d, lspec=%p\n",
	    vlinfo->v, vlinfo->lmin, vlinfo->lmax, (void *) vlinfo->lspec);
#endif

    if (vlinfo->lspec != NULL && *vlinfo->lspec != 0) {
	charsub(vlinfo->lspec, ',', ' ');
	llist = gretl_list_from_string(vlinfo->lspec);
	err = set_lag_prefs_from_list(vlinfo->v, llist, vlinfo->context,
				      &changed);
	if (err) {
	    free(llist);
	}
    } else if (vlinfo->lmin != NOT_LAG && vlinfo->lmax != NOT_LAG) {
	set_lag_prefs_from_minmax(vlinfo->v, vlinfo->lmin, vlinfo->lmax, 
				  vlinfo->context, &changed);
    } else {
	set_null_lagpref(vlinfo->v, vlinfo->context, &changed);
    }

    if (vlinfo->context == LAG_Y_X && yxlags != y_x_lags_enabled) {
	changed = 1;
    } else if (vlinfo->context == LAG_Y_W && ywlags != y_w_lags_enabled) {
	changed = 1;
    }

    return changed;
}

#if LDEBUG > 2
static void print_vlinfo (var_lag_info *vlinfo)
{
    fprintf(stderr, "\nCreated vlinfo struct:\n");
    fprintf(stderr, " v = %d\n", vlinfo->v);
    fprintf(stderr, " pos = %d\n", vlinfo->pos);
    fprintf(stderr, " nvl = %d\n", vlinfo->nvl);
    fprintf(stderr, " context = %d\n", (int) vlinfo->context);
    fprintf(stderr, " vlp = %p\n", (void *) vlinfo->vlp);
    
    fprintf(stderr, " lmin = %d\n", vlinfo->lmin);
    fprintf(stderr, " lmax = %d\n", vlinfo->lmax);

    if (vlinfo->lspec != NULL) {
	fprintf(stderr, " lspec = '%s'\n", vlinfo->lspec);
    } else {
	fprintf(stderr, " lspec = NULL\n");
    }
}
#endif

static gboolean lags_dialog_driver (GtkWidget *w, selector *sr)
{
    var_lag_info *vlinfo;
    int yxlags = y_x_lags_enabled;
    int ywlags = y_w_lags_enabled;
    int context = 0;
    int i, j, resp, nvl;
    int *list;

    list = sr_get_stoch_list(sr, &nvl, &context);
    if (list == NULL) {
	return FALSE;
    }

#if LDEBUG
    printlist(list, "stochastic vars list");
    fprintf(stderr, "number of setters = %d\n", nvl);
#endif

    vlinfo = mymalloc(nvl * sizeof *vlinfo);
    if (vlinfo == NULL) {
	free(list);
	return FALSE;
    }

    j = 0;
    for (i=1; i<=list[0]; i++) {
	const int *laglist = NULL;
	int v = list[i];

	if (v >= LISTSEP) {
	    context = v - LISTSEP;
#if LDEBUG
	    fprintf(stderr, "list[%d] = %d: set context = %d\n",
		    i, v, context);
#endif
	    continue;
	}

	/* pick up any saved preferences (including saved defaults) */
	get_lag_preference(v, &vlinfo[j].lmin, &vlinfo[j].lmax, 
			   &laglist, context);

	if (laglist != NULL) {
	    vlinfo[j].lspec = gretl_list_to_string(laglist);
	} else {
	    vlinfo[j].lspec = NULL;
	}

	vlinfo[j].v = v;
	vlinfo[j].pos = j;
	vlinfo[j].nvl = nvl;
	vlinfo[j].context = context;
	vlinfo[j].spin1 = NULL;
	vlinfo[j].spin2 = NULL;
	vlinfo[j].entry = NULL;
	vlinfo[j].toggle = NULL;
	vlinfo[j].vlp = vlinfo;
#if LDEBUG > 2
	print_vlinfo(&vlinfo[j]);
#endif
	j++;
    }

    resp = lags_dialog(list, vlinfo, sr);

    for (j=0; j<nvl; j++) {
	if (resp != LAGS_CANCEL) {
	    int changed = set_lags_for_var(&vlinfo[j], yxlags, ywlags);

	    if (vlinfo[j].v != VDEFLT && changed) {
		maybe_revise_var_string(&vlinfo[j], sr);
	    }
	}
	if (vlinfo[j].lspec != NULL) {
	    free(vlinfo[j].lspec);
	}
    }

    free(list);
    free(vlinfo);

    if (resp == LAGS_OK) {
	selector_doit(NULL, sr);
    }

    return FALSE;
}

