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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/* gui_utils.c for gretl */

#include "gretl.h"
#include <sys/stat.h>
#include <unistd.h>
#include "htmlprint.h"
#include "guiprint.h"

#ifdef G_OS_WIN32
# include <windows.h>
#endif

#if !defined(G_OS_WIN32) && !defined(USE_GNOME)
char rcfile[MAXLEN];
#endif

char *storelist = NULL;

extern GtkTooltips *gretl_tips;
extern int session_saved;
extern GtkWidget *mysheet;
extern GtkWidget *toolbar_box;
extern GdkColor red, blue;
extern char *space_to_score (char *str);

extern int want_toolbar;
extern char calculator[MAXSTR];
extern char editor[MAXSTR];
extern char Rcommand[MAXSTR];
extern char dbproxy[21];
int use_proxy;

/* filelist stuff */
#define MAXRECENT 4

#define SCRIPT_CHANGED(w) w->active_var = 1
#define SCRIPT_SAVED(w) w->active_var = 0
#define SCRIPT_IS_CHANGED(w) w->active_var == 1

static void printfilelist (int filetype, FILE *fp);

static char datalist[MAXRECENT][MAXSTR], *datap[MAXRECENT];
static char sessionlist[MAXRECENT][MAXSTR], *sessionp[MAXRECENT];
static char scriptlist[MAXRECENT][MAXSTR], *scriptp[MAXRECENT];

/* helpfile stuff */
struct help_head_t {
    char *name;
    int *topics;
    int *pos;
    int ntopics;
};
static int gui_help_length, script_help_length;
static struct help_head_t **cli_heads, **gui_heads;

/* searching stuff */
static int look_for_string (char *haystack, char *needle, int nStart);
static void close_find_dialog (GtkWidget *widget, gpointer data);
static void find_in_help (GtkWidget *widget, gpointer data);
static void find_in_text (GtkWidget *widget, gpointer data);
static void find_in_clist (GtkWidget *widget, gpointer data);
static void cancel_find (GtkWidget *widget, gpointer data);
static void find_string_dialog (void (*YesFunc)(), void (*NoFunc)(),
				gpointer data);
static void set_up_viewer_menu (GtkWidget *window, windata_t *vwin, 
				GtkItemFactoryEntry items[]);
static GtkWidget *find_window = NULL;
static GtkWidget *find_entry;
static char *needle;

static void edit_script_help (GtkWidget *widget, GdkEventButton *b,
			      windata_t *vwin);
static void file_viewer_save (GtkWidget *widget, windata_t *vwin);
static void make_prefs_tab (GtkWidget *notebook, int tab);
static void apply_changes (GtkWidget *widget, gpointer data);
static gint query_save_script (GtkWidget *w, GdkEvent *event, windata_t *vwin);
#ifndef G_OS_WIN32
static void read_rc (void);
#endif

extern void do_coeff_intervals (gpointer data, guint i, GtkWidget *w);
extern void save_plot (char *fname, GPT_SPEC *plot);
extern gboolean console_handler (GtkWidget *w, GdkEventKey *key, 
				 gpointer user_data);
extern void do_panel_diagnostics (gpointer data, guint u, GtkWidget *w);

/* font handling */
static char fontspec[MAXLEN] = 
"-b&h-lucidatypewriter-medium-r-normal-sans-12-*-*-*-*-*-*-*";
GdkFont *fixed_font;

static int usecwd;
int olddat;

typedef struct {
    char *key;         /* config file variable name */
    char *description; /* How the field will show up in the options dialog */
    char *link;        /* in case of radio button pair, alternate string */
    void *var;         /* pointer to variable */
    char type;         /* 'U' (user) or 'R' (root) for string, 'B' for boolean */
    int len;           /* storage size for string variable (also see Note) */
    short tab;         /* which tab (if any) does the item fall under? */
    GtkWidget *widget;
} RCVARS;

/* Note: actually "len" above is overloaded: if an rc_var is of type 'B'
   (boolean) and not part of a radio group, then a non-zero value for
   len will link the var's toggle button with the sensitivity of the
   preceding rc_var's entry field.  For example, the "use_proxy" button
   controls the sensitivity of the "dbproxy" entry widget. */

RCVARS rc_vars[] = {
    {"gretldir", _("Main gretl directory"), NULL, paths.gretldir, 
     'R', MAXLEN, 1, NULL},
    {"userdir", _("User's gretl directory"), NULL, paths.userdir, 
     'U', MAXLEN, 1, NULL},
    {"gnuplot", _("Command to launch gnuplot"), NULL, paths.gnuplot, 
     'R', MAXLEN, 1, NULL},
    {"Rcommand", _("Command to launch GNU R"), NULL, Rcommand, 
     'R', MAXSTR, 1, NULL},
    {"viewdvi", _("Command to view DVI files"), NULL, viewdvi, 
     'R', MAXSTR, 1, NULL},
    {"expert", _("Expert mode (no warnings)"), NULL, &expert, 
     'B', 0, 1, NULL},
    {"updater", _("Tell me about gretl updates"), NULL, &updater, 
     'B', 0, 1, NULL},
    {"binbase", _("gretl database directory"), NULL, paths.binbase, 
     'U', MAXLEN, 2, NULL},
    {"ratsbase", _("RATS data directory"), NULL, paths.ratsbase, 
     'U', MAXLEN, 2, NULL},
    {"dbhost_ip", _("Database server IP"), NULL, paths.dbhost_ip, 
     'U', 16, 2, NULL},
    {"dbproxy", _("HTTP proxy (ipnumber:port)"), NULL, dbproxy, 
     'U', 21, 2, NULL},
    {"useproxy", _("Use HTTP proxy"), NULL, &use_proxy, 
     'B', 1, 2, NULL},
    {"calculator", _("Calculator"), NULL, calculator, 
     'U', MAXSTR, 3, NULL},
    {"editor", _("Editor"), NULL, editor, 
     'U', MAXSTR, 3, NULL},
    {"toolbar", _("Show gretl toolbar"), NULL, &want_toolbar, 
     'B', 0, 3, NULL},
    {"usecwd", _("Use current working directory as default"), 
     _("Use gretl user directory as default"), &usecwd, 'B', 0, 4, NULL},
    {"olddat", _("Use \".dat\" as default datafile suffix"), 
     _("Use \".gdt\" as default suffix"), &olddat, 'B', 0, 5, NULL},
    {"fontspec", _("Fixed font"), NULL, fontspec, 'U', MAXLEN, 0, NULL},
    {NULL, NULL, NULL, NULL, 0, 0, 0, NULL}   
};

GtkItemFactoryEntry model_items[] = {
    { _("/_File"), NULL, NULL, 0, "<Branch>" },
    { _("/File/_Save as text..."), NULL, file_save, SAVE_MODEL, NULL },
    { _("/File/Save to session as icon"), NULL, remember_model, 0, NULL },
    { _("/File/Save as icon and close"), NULL, remember_model, 1, NULL },
#if defined(G_OS_WIN32) || defined(USE_GNOME)
    { _("/File/_Print..."), NULL, window_print, 0, NULL },
#endif
    { _("/_Edit"), NULL, NULL, 0, "<Branch>" },
    { _("/Edit/_Copy selection"), NULL, text_copy, COPY_SELECTION, NULL },
    { _("/Edit/Copy _all"), NULL, NULL, 0, "<Branch>" },
    { _("/Edit/Copy _all/as plain _text"), NULL, text_copy, COPY_TEXT, NULL },
    { _("/Edit/Copy _all/as _HTML"), NULL, text_copy, COPY_HTML, NULL },
    { _("/Edit/Copy _all/as _LaTeX"), NULL, text_copy, COPY_LATEX, NULL },
    { _("/Edit/Copy _all/as _RTF"), NULL, text_copy, COPY_RTF, NULL },
    { _("/_Tests"), NULL, NULL, 0, "<Branch>" },    
    { _("/Tests/omit variables"), NULL, model_test_callback, OMIT, NULL },
    { _("/Tests/add variables"), NULL, model_test_callback, ADD, NULL },
    { _("/Tests/sep1"), NULL, NULL, 0, "<Separator>" },
    { _("/Tests/non-linearity (squares)"), NULL, do_lmtest, AUX_SQ, NULL },
    { _("/Tests/non-linearity (logs)"), NULL, do_lmtest, AUX_LOG, NULL },
    { _("/Tests/sep2"), NULL, NULL, 0, "<Separator>" },
    { _("/Tests/autocorrelation"), NULL, model_test_callback, LMTEST, NULL },
    { _("/Tests/heteroskedasticity"), NULL, do_lmtest, AUX_WHITE, NULL },
    { _("/Tests/Chow test"), NULL, model_test_callback, CHOW, NULL },
    { _("/Tests/CUSUM test"), NULL, do_cusum, 0, NULL },
    { _("/Tests/ARCH"), NULL, model_test_callback, ARCH, NULL },
    { _("/Tests/normality of residual"), NULL, do_resid_freq, 0, NULL },
    { _("/Tests/panel diagnostics"), NULL, do_panel_diagnostics, 0, NULL },
    { _("/_Graphs"), NULL, NULL, 0, "<Branch>" }, 
    { _("/Graphs/residual plot"), NULL, NULL, 0, "<Branch>" },
    { _("/Graphs/fitted, actual plot"), NULL, NULL, 0, "<Branch>" },
    { _("/_Model data"), NULL, NULL, 0, "<Branch>" },
    { _("/_Model data/Display actual, fitted, residual"), NULL, 
      display_fit_resid, 0, NULL },
    { _("/_Model data/Forecasts with standard errors"), NULL, 
      model_test_callback, FCAST, NULL },
    { _("/_Model data/Confidence intervals for coefficients"), NULL, 
      do_coeff_intervals, 0, NULL },
    { _("/_Model data/Add to data set/fitted values"), NULL, 
      fit_resid_callback, 1, NULL },
    { _("/_Model data/Add to data set/residuals"), NULL, 
      fit_resid_callback, 0, NULL },
    { _("/_Model data/Add to data set/squared residuals"), NULL, 
      fit_resid_callback, 2, NULL },
    { _("/_Model data/Add to data set/error sum of squares"), NULL, 
      model_stat_callback, ESS, NULL },
    { _("/_Model data/Add to data set/standard error of residuals"), NULL, 
      model_stat_callback, SIGMA, NULL },
    { _("/_Model data/Add to data set/R-squared"), NULL, 
      model_stat_callback, R2, NULL },
    { _("/_Model data/Add to data set/T*R-squared"), NULL, 
      model_stat_callback, TR2, NULL },
    { _("/_Model data/Add to data set/log likelihood"), NULL, 
      model_stat_callback, LNL, NULL },
    { _("/_Model data/Add to data set/degrees of freedom"), NULL, 
      model_stat_callback, DF, NULL },
    { _("/_Model data/coefficient covariance matrix"), NULL, 
      do_outcovmx, 0, NULL },
    { _("/_Model data/sep1"), NULL, NULL, 0, "<Separator>" },
    { _("/_Model data/Define new variable..."), NULL, model_test_callback, 
      MODEL_GENR, NULL },
    { _("/_LaTeX"), NULL, NULL, 0, "<Branch>" },
    { _("/LaTeX/_View"), NULL, NULL, 0, "<Branch>" },
    { _("/LaTeX/View/_Tabular"), NULL, view_latex, 0, NULL },
    { _("/LaTeX/View/_Equation"), NULL, view_latex, 1, NULL },
    { _("/LaTeX/_Save"), NULL, NULL, 0, "<Branch>" },
    { _("/LaTeX/Save/_Tabular"), NULL, file_save, SAVE_TEX_TAB, NULL },
    { _("/LaTeX/Save/_Equation"), NULL, file_save, SAVE_TEX_EQ, NULL },
    { _("/LaTeX/_Copy"), NULL, NULL, 0, "<Branch>" },
    { _("/LaTeX/Copy/_Tabular"), NULL, text_copy, COPY_LATEX, NULL },
    { _("/LaTeX/Copy/_Equation"), NULL, text_copy, COPY_LATEX_EQUATION, NULL },
    { NULL, NULL, NULL, 0, NULL}
};

GtkItemFactoryEntry help_items[] = {
    { _("/_Topics"), NULL, NULL, 0, "<Branch>" },    
    { _("/_Find"), NULL, menu_find, 0, NULL },
    { NULL, NULL, NULL, 0, NULL}
};

GtkItemFactoryEntry edit_items[] = {
#if defined(G_OS_WIN32) || defined(USE_GNOME)
    { _("/File/_Print..."), NULL, window_print, 0, NULL },
#endif    
    { _("/_Edit"), NULL, NULL, 0, "<Branch>" },
    { _("/Edit/_Copy selection"), NULL, text_copy, COPY_SELECTION, NULL },
    { _("/Edit/Copy _all"), NULL, text_copy, COPY_TEXT, NULL },
    { _("/Edit/_Paste"), NULL, text_paste, 0, NULL },
    { _("/Edit/_Replace..."), NULL, text_replace, 0, NULL },
    { _("/Edit/_Undo"), NULL, text_undo, 0, NULL },
    { NULL, NULL, NULL, 0, NULL }
};

/* ........................................................... */

void load_fixed_font (void)
{
    /* get a monospaced font for various windows */
    fixed_font = gdk_font_load(fontspec);
}

/* ........................................................... */

int copyfile (const char *src, const char *dest) 
{
    FILE *srcfd, *destfd;
    char buf[8192];
    size_t n;
   
    if ((srcfd = fopen(src, "rb")) == NULL) {
	return 1; 
    }
    if ((destfd = fopen(dest, "wb")) == NULL) {
	fclose(srcfd);
	return 1;
    }
    while ((n = fread(buf, 1, sizeof buf, srcfd)) > 0) {
	fwrite(buf, 1, n, destfd);
    }
    fclose(srcfd);
    fclose(destfd);
    return 0;
}

/* ........................................................... */

int isdir (const char *path)
{
    struct stat buf;

    if (stat(path, &buf) == 0 && S_ISDIR(buf.st_mode)) 
	return 1;
    else 
	return 0;
}

/* ........................................................... */

int getbufline (char *buf, char *line, int init)
{
    static int pos;
    int i = 0;

    if (init) pos = 0;
    else {
	while (buf[i+pos] != '\n') {
	    line[i] = buf[i+pos];
	    if (buf[i+pos] == 0)
		return 0;
	    i++;
	}
	pos += i + 1;
	line[i] = 0;
    }
    return i;
}

/* ........................................................... */

void append_dir (char *fname, const char *dir)
{
    if (dir != NULL) strcat(fname, dir);
    strcat(fname, SLASHSTR);
}

/* ........................................................... */

#if !defined(G_OS_WIN32) && !defined(USE_GNOME)
void set_rcfile (void) 
{
    char *tmp;

    tmp = getenv("HOME");
    strcpy(rcfile, tmp);
    strcat(rcfile, "/.gretlrc");
    read_rc(); 
}
#endif

#ifdef USE_GNOME
void set_rcfile (void)
{
    read_rc();
}
#endif

/* ........................................................... */

/* Below: Keep a record of (most) windows that are open, so they 
   can be destroyed en masse when a new data file is opened, to
   prevent weirdness that could arise if (e.g.) a model window
   that pertains to a previously opened data file remains open
   after the data set has been changed.  Script windows are
   exempt, otherwise they are likely to disappear when their
   "run" control is activated, which we don't want.
*/

enum winstack_codes {
    STACK_INIT,
    STACK_ADD,
    STACK_REMOVE,
    STACK_DESTROY
};

static void winstack (int code, GtkWidget *w)
{
    static int n_windows;
    static GtkWidget **wstack;
    int i;

    switch (code) {
    case STACK_DESTROY:	
	for (i=0; i<n_windows; i++) 
	    if (wstack[i] != NULL) 
		gtk_widget_destroy(wstack[i]);
	free(wstack);
    case STACK_INIT:
	wstack = NULL;
	n_windows = 0;
	break;
    case STACK_ADD:
	for (i=0; i<n_windows; i++) {
	    if (wstack[i] == NULL) {
		wstack[i] = w;
		break;
	    }
	}
	if (i == n_windows) {
	    n_windows++;
	    wstack = myrealloc(wstack, n_windows * sizeof *wstack);
	    if (wstack != NULL) 
		wstack[n_windows-1] = w;
	}
	break;
    case STACK_REMOVE:
	for (i=0; i<n_windows; i++) {
	    if (wstack[i] == w) {
		wstack[i] = NULL;
		break;
	    }
	}
	break;
    default:
	break;
    }
}

void winstack_init (void)
{
    winstack(STACK_INIT, NULL);
}
    
void winstack_destroy (void)
{
    winstack(STACK_DESTROY, NULL);
}

static void winstack_add (GtkWidget *w)
{
    winstack(STACK_ADD, w);
}

static void winstack_remove (GtkWidget *w)
{
    winstack(STACK_REMOVE, w);
}

/* ........................................................... */

static void delete_file (GtkWidget *widget, char *fle) 
{
    remove(fle);
    g_free(fle);
}

/* ........................................................... */

static void delete_file_viewer (GtkWidget *widget, gpointer data) 
{
    windata_t *vwin = (windata_t *) data;

    if (vwin->role == EDIT_SCRIPT && SCRIPT_IS_CHANGED(vwin)) {
	gint resp;

	resp = query_save_script(NULL, NULL, vwin);
	if (!resp) gtk_widget_destroy(vwin->dialog);
    } else 
	gtk_widget_destroy(vwin->dialog);
}

/* ........................................................... */

void delete_model (GtkWidget *widget, gpointer data) 
{
    MODEL *pmod = (MODEL *) data;
    if (pmod->name == NULL) {
	free_model(pmod);
	pmod = NULL;
    }
}

/* ........................................................... */

void delete_widget (GtkWidget *widget, gpointer data)
{
    gtk_widget_destroy(data);
}

/* ........................................................... */

void catch_key (GtkWidget *w, GdkEventKey *key)
{
    
    if (key->keyval == GDK_q) { 
        gtk_widget_destroy(w);
    }
    else if (key->keyval == GDK_s) {
	windata_t *vwin = gtk_object_get_data(GTK_OBJECT(w), "ddata");

	if (Z != NULL && vwin != NULL && vwin->role == VIEW_MODEL)
	    remember_model(vwin, 1, NULL);
    }
}

/* ........................................................... */

void catch_edit_key (GtkWidget *w, GdkEventKey *key, windata_t *vwin)
{
    GdkModifierType mods;

    gdk_window_get_pointer(w->window, NULL, NULL, &mods);

    if (key->keyval == GDK_F1 && vwin->role == EDIT_SCRIPT) { 
	vwin->help_active = 1;
	edit_script_help(NULL, NULL, vwin);
    }

    else if (mods & GDK_CONTROL_MASK) {
	if (gdk_keyval_to_upper(key->keyval) == GDK_S) 
	    file_viewer_save(NULL, vwin);
	else if (gdk_keyval_to_upper(key->keyval) == GDK_Q) {
	    if (vwin->role == EDIT_SCRIPT && SCRIPT_IS_CHANGED(vwin)) {
		gint resp;

		resp = query_save_script(NULL, NULL, vwin);
		if (!resp) gtk_widget_destroy(vwin->dialog);
	    } else 
		gtk_widget_destroy(w);
	}
    }

#ifdef notyet
    /* pick out some stuff that shouldn't be captured */
    if (mods > GDK_SHIFT_MASK || 
	key->keyval < GDK_space || 
	key->keyval > GDK_asciitilde) {
	return;
    } else {  /* colorize comments */
	int cw = gdk_char_width(fixed_font, 'x'); 
	int currpos, xpos;
	gchar *starter, out[2];

	currpos = GTK_EDITABLE(vwin->w)->current_pos;
	xpos = GTK_TEXT(vwin->w)->cursor_pos_x / cw; 
	starter = gtk_editable_get_chars(GTK_EDITABLE(vwin->w),
					 currpos - xpos, currpos - xpos + 1);
	sprintf(out, "%c", key->keyval);
	key->keyval = GDK_VoidSymbol;

	if ((starter != NULL && starter[0] == '#') || out[0] == '#') {
	    gtk_text_insert(GTK_TEXT(vwin->w), fixed_font, 
			    &blue, NULL, out, 1);
	} else {
	    gtk_text_insert(GTK_TEXT(vwin->w), fixed_font, 
			    NULL, NULL, out, 1);
	}
	gtk_signal_emit_stop_by_name(GTK_OBJECT(w), "key-press-event");
	if (starter != NULL) g_free(starter);
    }
#endif
}

/* ........................................................... */

void *mymalloc (size_t size) 
{
    void *mem;
   
    if((mem = malloc(size)) == NULL) 
	errbox(_("Out of memory!"));
    return mem;
}

/* ........................................................... */

void *myrealloc (void *ptr, size_t size) 
{
    void *mem;
   
    if ((mem = realloc(ptr, size)) == NULL) 
	errbox(_("Out of memory!"));
    return mem;
}

/* ........................................................... */

void register_data (const char *fname, int record)
{    
    char datacmd[MAXLEN];

    /* basic accounting */
    data_status |= HAVE_DATA;
    orig_vars = datainfo->v;

    /* set appropriate data_status bits */
    if (fname == NULL)
	data_status |= (GUI_DATA|MODIFIED_DATA);
    else if (!(data_status & IMPORT_DATA)) {
	if (strstr(paths.datfile, paths.datadir) != NULL) 
	    data_status |= BOOK_DATA;
	else
	    data_status |= USER_DATA; 
    }

    /* sync main window with datafile */
    populate_clist(mdata->listbox, datainfo);
    set_sample_label(datainfo);
    menubar_state(TRUE);
    session_state(TRUE);

    /* record opening of data file in command log */
    if (record && fname != NULL) {
	mkfilelist(1, fname);
	sprintf(datacmd, "open %s", fname);
	check_cmd(datacmd);
	cmd_init(datacmd); 
    } 
}

#define APPENDING(action) (action == APPEND_CSV || \
                           action == APPEND_GNUMERIC || \
                           action == APPEND_EXCEL)

/* ........................................................... */

static void get_worksheet_data (const char *fname, int datatype,
				int append)
{
    int err;
    void *handle;
    int (*sheet_get_data)(const char*, double ***, DATAINFO *, char *);

    if (datatype == GRETL_GNUMERIC) {
	if (gui_open_plugin("gnumeric_import", &handle)) return;
	sheet_get_data = get_plugin_function("gbook_get_data", handle);
    }
    else if (datatype == GRETL_EXCEL) {
	if (gui_open_plugin("excel_import", &handle)) return;
	sheet_get_data = get_plugin_function("excel_get_data", handle);
    }
    else {
	errbox("Unrecognized data type");
	return;
    }

    if (sheet_get_data == NULL) {
        errbox(_("Couldn't load plugin function"));
        close_plugin(handle);
        return;
    }

    err = (*sheet_get_data)(fname, &Z, datainfo, errtext);
    close_plugin(handle);

    if (err == -1) /* the user canceled the import */
	return;

    if (err) {
	if (strlen(errtext)) errbox(errtext);
	else errbox("Failed to import spreadsheet data");
	return;
    }

    if (append) {
	infobox("Data appended OK");
	data_status |= MODIFIED_DATA;
	register_data(fname, 0);
    } else {
	data_status |= IMPORT_DATA;
	strcpy(paths.datfile, fname);
	register_data(fname, 1);
    }
}

/* ........................................................... */

void do_open_data (GtkWidget *w, gpointer data, int code)
     /* cases: 
	- called from dialog: user has said Yes to opening data file,
	although a data file is already open
	- reached without dialog, in expert mode or when no datafile
	is open yet
     */
{
    gint datatype, err;
    dialog_t *d = NULL;
    windata_t *fwin = NULL;
    int append = APPENDING(code);

    if (data != NULL) {    
	if (w == NULL) { /* not coming from edit_dialog */
	    fwin = (windata_t *) data;
	} else {
	    d = (dialog_t *) data;
	    fwin = (windata_t *) d->data;
	}
    }

    if (code == OPEN_CSV || code == APPEND_CSV)
	datatype = GRETL_CSV_DATA;
    if (code == OPEN_GNUMERIC || code == APPEND_GNUMERIC)
	datatype = GRETL_GNUMERIC;
    else if (code == OPEN_EXCEL || code == APPEND_EXCEL)
	datatype = GRETL_EXCEL;
    else if (code == OPEN_BOX)
	datatype = GRETL_BOX_DATA;
    else {
	PRN *prn;	

	if (bufopen(&prn)) return;
	datatype = detect_filetype(trydatfile, &paths, prn);
	gretl_print_destroy(prn);
    }

    /* destroy the current data set, etc., unless we're explicitly appending */
    if (!append) close_session();

    if (datatype == GRETL_GNUMERIC || datatype == GRETL_EXCEL) {
	get_worksheet_data(trydatfile, datatype, append);
	return;
    }
    else if (datatype == GRETL_CSV_DATA) {
	do_open_csv_box(trydatfile, OPEN_CSV, append);
	return;
    }
    else if (datatype == GRETL_BOX_DATA) {
	do_open_csv_box(trydatfile, OPEN_BOX, 0);
	return;
    }
    else { /* native data */
	PRN prn;
	prn.buf = NULL; prn.fp = stderr;
	if (datatype == GRETL_XML_DATA)
	    err = get_xmldata(&Z, datainfo, trydatfile, &paths, 
			      data_status, &prn, 1);
	else
	    err = get_data(&Z, datainfo, trydatfile, &paths, data_status, &prn);
    }

    if (err) {
	gui_errmsg(err);
	delete_from_filelist(1, trydatfile);
	return;
    }	

    /* trash the practice files window that launched the query? */
    if (fwin != NULL) gtk_widget_destroy(fwin->w); 

    strcpy(paths.datfile, trydatfile);

    register_data(paths.datfile, 1);
}

/* ........................................................... */

void verify_open_data (gpointer userdata, int code)
     /* give user choice of not opening selected datafile,
	if there's already a datafile open and we're not
	in "expert" mode */
{
    if (data_status && !expert && 
	yes_no_dialog (_("gretl: open data"), 
		       _("Opening a new data file will automatically\n"
		       "close the current one.  Any unsaved work\n"
		       "will be lost.  Proceed to open data file?"), 0))
	return;
    else 
	do_open_data(NULL, userdata, code);
}

/* ........................................................... */

void verify_open_session (gpointer userdata)
     /* give user choice of not opening session file,
	if there's already a datafile open and we're not
	in "expert" mode */
{
    if (data_status && !expert &&
	yes_no_dialog (_("gretl: open session"), 
		       _("Opening a new session file will automatically\n"
		       "close the current session.  Any unsaved work\n"
		       "will be lost.  Proceed to open session file?"), 0))
	return;
    else 
	do_open_session(NULL, userdata);
}

/* ........................................................... */

static void set_data_from_filelist (gpointer data, guint i, 
				    GtkWidget *widget)
{
    strcpy(trydatfile, datap[i]); 
    verify_open_data(NULL, 0);
}

/* ........................................................... */

static void set_session_from_filelist (gpointer data, guint i, 
				       GtkWidget *widget)
{
    strcpy(tryscript, sessionp[i]);
    verify_open_session(NULL);
}

/* ........................................................... */

static void set_script_from_filelist (gpointer data, guint i, 
				      GtkWidget *widget)
{
    strcpy(tryscript, scriptp[i]);
    do_open_script(NULL, NULL);
}

/* ........................................................... */

void save_session (char *fname) 
{
    int i, spos;
    char msg[MAXLEN], savedir[MAXLEN], fname2[MAXLEN];
    char session_base[MAXLEN], tmp[MAXLEN], grftmp[64];
    FILE *fp;
    PRN *prn;

    spos = slashpos(fname);
    if (spos) 
	safecpy(savedir, fname, spos);
    else *savedir = 0;

#ifdef CMD_DEBUG
    dump_cmd_stack("stderr");
#endif

    /* save commands, by dumping the command stack */
    if (haschar('.', fname) < 0)
	strcat(fname, ".gretl");
    if (dump_cmd_stack(fname)) return;

    get_base(session_base, fname, '.');

    /* get ready to save "session" */
    fp = fopen(fname, "a");
    if (fp == NULL) {
	sprintf(errtext, _("Couldn't open session file %s"), fname);
	errbox(errtext);
	return;
    }
    fprintf(fp, "(* saved objects:\n");

    /* save session models */
    for (i=0; i<session.nmodels; i++) {
	fprintf(fp, "model %d \"%s\"\n", 
		(session.models[i])->ID, 
		(session.models[i])->name);
    }

    /* save session graphs */
    for (i=0; i<session.ngraphs; i++) {
	/* formulate save name for graph */
	strcpy(grftmp, (session.graphs[i])->name);
	sprintf(tmp, "%s%s", session_base, space_to_score(grftmp));
	/* does the constructed filename differ from the
	   current one? */
	if (strcmp((session.graphs[i])->fname, tmp)) {
	    if (copyfile((session.graphs[i])->fname, tmp)) {
		errbox(_("Couldn't copy graph file"));
		continue;
	    } else {
		remove((session.graphs[i])->fname);
		strcpy((session.graphs[i])->fname, tmp);
	    }
	}
	fprintf(fp, "%s %d \"%s\" %s\n", 
		((session.graphs[i])->name[0] == 'G')? "graph" : "plot",
		(session.graphs[i])->ID, 
		(session.graphs[i])->name, 
		(session.graphs[i])->fname);
    }

    fprintf(fp, "*)\n");
    fclose(fp);

    /* save session notes, if any */
    if (session.notes != NULL && strlen(session.notes)) {
	switch_ext(fname2, fname, "Notes");
	fp = fopen(fname2, "w");
	if (fp != NULL) {
	    fprintf(fp, "%s", session.notes);
	    fclose(fp);
	} else
	    errbox(_("Couldn't write session notes file"));
    }

    /* save output */
    switch_ext(fname2, fname, "txt");
    prn = gretl_print_new(GRETL_PRINT_FILE, fname2);
    if (prn == NULL) {
	errbox(_("Couldn't open output file for writing"));
	return;
    }

    gui_logo(prn->fp);
    session_time(prn->fp);
    pprintf(prn, _("Output from %s\n"), fname);
    execute_script(fname, NULL, NULL, NULL, prn, SAVE_SESSION_EXEC); 
    gretl_print_destroy(prn);

    sprintf(msg, _("session saved to %s -\n"), savedir);
    strcat(msg, _("commands: "));
    strcat(msg, (spos)? fname + spos + 1 : fname);
    strcat(msg, _("\noutput: "));
    spos = slashpos(fname2);
    strcat(msg, (spos)? fname2 + spos + 1 : fname2);
    infobox(msg);

    mkfilelist(2, fname);
    session_saved = 1;
    session_changed(0);

    return;
}

/* ......................................................... */

struct gui_help_item {
    int code;
    char *string;
};

static struct gui_help_item gui_help_items[] = {
    { GR_PLOT,    "graphing" },
    { GR_XY,      "graphing" },
    { GR_DUMMY,   "factorized" },
    { GR_BOX,     "boxplots" },
    { GR_NBOX,    "boxplots" },
    { ONLINE,     "online" },
    { MARKERS,    "markers" },
    { EXPORT,     "export" },
    { SMPLBOOL,   "sampling" },
    { SMPLDUM,    "sampling" },
    { COMPACT,    "compact" },
    { VSETMISS,   "missing" },
    { GSETMISS,   "missing" },
    { 0,          NULL },
};

/* ......................................................... */

static int extra_command_number (const char *s)
{
    int i;

    for (i=0; gui_help_items[i].code; i++)
	if (!strcmp(s, gui_help_items[i].string))
	    return gui_help_items[i].code;
    return 0;
}

/* ......................................................... */

static char *help_string_from_cmd (int cmd)
{
    int i;

    for (i=0; gui_help_items[i].code; i++)
	if (cmd == gui_help_items[i].code)
	    return gui_help_items[i].string;
    return NULL;    
}

/* ......................................................... */

static int real_helpfile_init (int cli)
{
    FILE *fp;
    char *helpfile;
    struct help_head_t **heads = NULL;
    char testline[MAXLEN], topicword[32];
    int i, g, pos, match, nheads = 0, topic = 0;
    int length = 0, memfail = 0;

    helpfile = (cli)? paths.cmd_helpfile : paths.helpfile;

    /* first pass: find length and number of topics */
    fp = fopen(helpfile, "r");
    if (fp == NULL) {
	fprintf(stderr, _("help file %s is not accessible\n"), helpfile);
	return -1;
    }

    while (!memfail && fgets(testline, MAXLEN-1, fp)) {
	if (*testline == '@') {
	    chopstr(testline);
	    match = 0;
	    for (i=0; i<nheads; i++) {
		if (!strcmp(testline + 1, (heads[i])->name)) {
		    match = 1;
		    (heads[i])->ntopics += 1;
		    break;
		}
	    }
	    if (!match) {
		heads = realloc(heads, (nheads + 2) * sizeof *heads);
		if (heads != NULL) { 
		    heads[nheads] = malloc(sizeof **heads);
		    if (heads[nheads] != NULL) {
			(heads[nheads])->name = malloc(strlen(testline));
			if ((heads[nheads])->name != NULL) {
			    strcpy((heads[nheads])->name, testline + 1);
			    (heads[nheads])->ntopics = 1;
			    nheads++;
			} else memfail = 1;
		    } else memfail = 1;
		} else memfail = 1;
	    }
	} else length++;
    }
    fclose(fp);

    if (memfail) return -1;

    for (i=0; i<nheads; i++) {
	(heads[i])->topics = malloc((heads[i])->ntopics * sizeof(int));
	if ((heads[i])->topics == NULL) memfail = 1;
	(heads[i])->pos = malloc((heads[i])->ntopics * sizeof(int));
	if ((heads[i])->pos == NULL) memfail = 1; 
	(heads[i])->ntopics = 0;
    }
    heads[i] = NULL;

    if (memfail) return -1;

    /* second pass, assemble the topic list */
    fp = fopen(helpfile, "r");
    i = 0;
    pos = 0;
    g = 0;
    while (!memfail && fgets(testline, MAXLEN-1, fp)) {
	if (topic == 1) 
	    sscanf(testline, "%31s", topicword);
	if (*testline == '@') {
	    chopstr(testline);
	    match = -1;
	    for (i=0; i<nheads; i++) {
		if (!strcmp(testline + 1, (heads[i])->name)) {
		    match = i;
		    break;
		}
	    }
	    if (match >= 0) {
		int t, m = (heads[match])->ntopics;

		t = command_number(topicword);
		if (t) (heads[match])->topics[m] = t;
		else (heads[match])->topics[m] = 
			 extra_command_number(topicword);
		(heads[match])->pos[m] = pos - 1;
		(heads[match])->ntopics += 1;
	    }		
	} else pos++;
	if (*testline == '#') topic = 1;
	else topic = 0;
    }
    fclose(fp);

    if (cli) cli_heads = heads;
    else gui_heads = heads;

    return length;
}

/* ......................................................... */

void helpfile_init (void)
{
    gui_help_length = real_helpfile_init(0);
    script_help_length = real_helpfile_init(1);
}

/* ......................................................... */

static char *get_gui_help_string (int pos)
{
    int i, j;

    for (i=0; gui_heads[i] != NULL; i++) 
	for (j=0; j<(gui_heads[i])->ntopics; j++)
	    if (pos == (gui_heads[i])->pos[j])
		return help_string_from_cmd((gui_heads[i])->topics[j]);
    return NULL;
}

/* ........................................................... */

static void add_help_topics (windata_t *hwin, int script)
{
    int i, j;
    GtkItemFactoryEntry helpitem;
    gchar *mpath = _("/_Topics");
    struct help_head_t **heads = (script)? cli_heads : gui_heads;

    helpitem.path = NULL;

    /* See if there are any topics to add */
    if (heads == NULL) return;

    /* put the topics under the menu heading */
    for (i=0; heads[i] != NULL; i++) {
	if (helpitem.path == NULL)
	    helpitem.path = mymalloc(80);
	helpitem.accelerator = NULL;
	helpitem.callback_action = 0; 
	helpitem.item_type = "<Branch>";
	sprintf(helpitem.path, "%s/%s", mpath, (heads[i])->name);
	helpitem.callback = NULL; 
	gtk_item_factory_create_item(hwin->ifac, &helpitem, NULL, 1);
	for (j=0; j<(heads[i])->ntopics; j++) {
	    helpitem.accelerator = NULL;
	    helpitem.callback_action = (heads[i])->pos[j]; 
	    helpitem.item_type = NULL;
	    if ((heads[i])->topics[j] < NC) {
		sprintf(helpitem.path, "%s/%s/%s", 
			mpath, (heads[i])->name, 
			commands[(heads[i])->topics[j]]);
	    } else {
		sprintf(helpitem.path, "%s/%s/%s", 
			mpath, (heads[i])->name, 
			get_gui_help_string((heads[i])->pos[j]));
	    }
	    helpitem.callback = (script)? do_script_help : do_gui_help; 
	    gtk_item_factory_create_item(hwin->ifac, &helpitem, NULL, 1);
	}
    }
    free(helpitem.path);
}

/* ........................................................... */

static windata_t *helpwin (int script) 
{
    windata_t *vwin = NULL;

    if (script) {
	vwin = view_file(paths.cmd_helpfile, 0, 0, 77, 400, 
			 CLI_HELP, help_items);
	add_help_topics(vwin, 1);
    } else {
	vwin = view_file(paths.helpfile, 0, 0, 77, 400, 
			 HELP, help_items);
	add_help_topics(vwin, 0);
    }
    return vwin;
}

/* ........................................................... */

void menu_find (gpointer data, guint db, GtkWidget *widget)
{
    if (db) 
	find_string_dialog(find_in_clist, cancel_find, data);
    else 
	find_string_dialog(find_in_help, cancel_find, data);
}

/* ........................................................... */

void datafile_find (GtkWidget *widget, gpointer data)
{
    find_string_dialog(find_in_clist, cancel_find, data);
}

/* ........................................................... */

void find_var (gpointer p, guint u, GtkWidget *w)
{
    find_string_dialog(find_in_clist, cancel_find, mdata);
}

/* ........................................................... */

void context_help (GtkWidget *widget, gpointer data)
{
    int i, j, help_code = GPOINTER_TO_INT(data);
    int pos = 0;

    for (i=0; gui_heads[i] != NULL; i++) {
	for (j=0; j<(gui_heads[i])->ntopics; j++)
	    if (help_code == (gui_heads[i])->topics[j])
		pos = (gui_heads[i])->pos[j];
    }
    /* fallback */
    if (!pos) {
	char *helpstr = help_string_from_cmd(help_code);
	int altcode;

	if (helpstr != NULL) {
	    altcode = extra_command_number(helpstr);
	    for (i=0; gui_heads[i] != NULL; i++)
		for (j=0; j<(gui_heads[i])->ntopics; j++)
		    if (altcode == (gui_heads[i])->topics[j])
			pos = (gui_heads[i])->pos[j];
	}
    }
    do_gui_help(NULL, pos, NULL);
}

/* ........................................................... */

static void real_do_help (guint pos, int cli)
{
    double frac;
    gfloat adj;
    static GtkWidget *gui_help_view;
    static GtkWidget *script_help_view;
    GtkWidget *w = (cli)? script_help_view : gui_help_view;

    if (w == NULL) {
	windata_t *hwin = helpwin(cli);

	if (hwin != NULL) {
	    if (cli) w = script_help_view = hwin->w;
	    else w = gui_help_view = hwin->w;
	}
	gtk_signal_connect(GTK_OBJECT(w), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed),
			   (cli)? &script_help_view : &gui_help_view);	
    } else {
	gdk_window_show(w->parent->window);
	gdk_window_raise(w->parent->window);
    }
    
    frac = (double) pos * (double) GTK_TEXT(w)->vadj->upper;
    frac /= (double) (cli)? script_help_length : gui_help_length;
    adj = 0.999 * frac;

    gtk_adjustment_set_value(GTK_TEXT(w)->vadj, adj);
}

/* ........................................................... */

void do_gui_help (gpointer data, guint pos, GtkWidget *widget) 
{
    real_do_help(pos, 0);
}

/* ........................................................... */

void do_script_help (gpointer data, guint pos, GtkWidget *widget) 
{
    real_do_help(pos, 1);
}

/* ........................................................... */

static int pos_from_cmd (int cmd)
{
    int i, j;

    for (i=0; cli_heads[i] != NULL; i++)
	for (j=0; j<(cli_heads[i])->ntopics; j++)
	    if (cmd == (cli_heads[i])->topics[j])
		return (cli_heads[i])->pos[j];
    return 0;
}

/* ........................................................... */

static void activate_script_help (GtkWidget *widget, windata_t *vwin)
{
    GdkCursor *cursor = gdk_cursor_new(GDK_QUESTION_ARROW);

    gdk_window_set_cursor(GTK_TEXT(vwin->w)->text_area, cursor);
    gdk_cursor_destroy(cursor);
    vwin->help_active = 1;
}

/* ........................................................... */

static void edit_script_help (GtkWidget *widget, GdkEventButton *b,
			      windata_t *vwin)
{
    if (!vwin->help_active) { /* command help not activated */
	return;
    } else {
	gchar *text;
	guint pt = GTK_EDITABLE(vwin->w)->current_pos;
	int len = gtk_text_get_length(GTK_TEXT(vwin->w));
	int pos = 0;

	text = gtk_editable_get_chars(GTK_EDITABLE(vwin->w), 
				      0, (pt + 9 > len)? -1 : pt + 8);

	if (text != NULL && strlen(text) > 0) {
	    char *p, *q;
	    char word[9];

	    p = q = text + pt;
	    if (pt > 0)
		while (p - text && !isspace(*(p-1))) p--;
	    if (pt < strlen(text))
		while (*q && !isspace(*q)) q++;
	    *word = '\0';
	    strncat(word, p, (q - p > 8)? 8 : q - p);
	    pos = pos_from_cmd(command_number(word));
	} 
	
	real_do_help(pos, 1);
	g_free(text);
	gdk_window_set_cursor(GTK_TEXT(vwin->w)->text_area, NULL);
	vwin->help_active = 0;
    }
}

/* ........................................................... */

static void buf_edit_save (GtkWidget *widget, gpointer data)
{
    windata_t *vwin = (windata_t *) data;
    gchar *text;
    char **pbuf = (char **) vwin->data;

    text = gtk_editable_get_chars(GTK_EDITABLE(vwin->w), 0, -1);
    if (text == NULL || !strlen(text)) {
	errbox(_("Buffer is empty"));
	g_free(text);
	return;
    }

    /* swap the edited text into the buffer */
    free(*pbuf); 
    *pbuf = text;

    if (vwin->role == EDIT_HEADER) {
	infobox(_("Data info saved"));
	data_status |= MODIFIED_DATA;
    } 
    else if (vwin->role == EDIT_NOTES) {
	infobox(_("Notes saved"));
	session_changed(1);
    }
}

/* ........................................................... */

static void file_viewer_save (GtkWidget *widget, windata_t *vwin)
{
    /* special case: a newly created script */
    if (strstr(vwin->fname, "script_tmp") || !strlen(vwin->fname)) {
	file_save(vwin, SAVE_SCRIPT, NULL);
	strcpy(vwin->fname, scriptfile);
    } else {
	char buf[MAXLEN];
	FILE *fp;
	gchar *text;

	if ((fp = fopen(vwin->fname, "w")) == NULL) {
	    errbox(_("Can't open file for writing"));
	    return;
	} else {
	    text = gtk_editable_get_chars(GTK_EDITABLE(vwin->w), 0, -1);
	    fprintf(fp, "%s", text);
	    fclose(fp);
	    g_free(text);
	    sprintf(buf, _("Saved %s\n"), vwin->fname);
	    infobox(buf);
	    if (vwin->role == EDIT_SCRIPT) 
		SCRIPT_SAVED(vwin);
	}
    }
} 

/* .................................................................. */

void windata_init (windata_t *vwin)
{
    vwin->dialog = NULL;
    vwin->listbox = NULL;
    vwin->mbar = NULL;
    vwin->w = NULL;
    vwin->status = NULL;
    vwin->popup = NULL;
    vwin->ifac = NULL;
    vwin->data = NULL;
    vwin->fname[0] = '\0';
    vwin->role = -1;
    vwin->active_var = 0;
    vwin->help_active = 0;
}

/* .................................................................. */

void free_windata (GtkWidget *w, gpointer data)
{
    windata_t *vwin = (windata_t *) data;

    if (vwin) {
	if (vwin->w) {
	    gchar *undo = 
		gtk_object_get_data(GTK_OBJECT(vwin->w), "undo");
	    
	    if (undo) g_free(undo);
	}
	if (vwin->listbox) 
	    gtk_widget_destroy(GTK_WIDGET(vwin->listbox));
	if (vwin->mbar) 
	    gtk_widget_destroy(GTK_WIDGET(vwin->mbar));
	if (vwin->status) 
	    gtk_widget_destroy(GTK_WIDGET(vwin->status));
	if (vwin->ifac) 
	    gtk_object_unref(GTK_OBJECT(vwin->ifac));  
	if (vwin->popup) 
	    gtk_object_unref(GTK_OBJECT(vwin->popup));
	if (vwin->role == SUMMARY || vwin->role == VAR_SUMMARY)
	    free_summary(vwin->data); 
	if (vwin->role == CORR)
	    free_corrmat(vwin->data);
	if (vwin->dialog)
	    winstack_remove(vwin->dialog);
	free(vwin);
	vwin = NULL;
    }
}

#if defined(G_OS_WIN32) || defined(USE_GNOME) 
static void window_print_callback (GtkWidget *w, windata_t *vwin)
{
    window_print(vwin, 0, w);
}
#endif

/* ........................................................... */

static void text_find_callback (GtkWidget *w, gpointer data)
{
    find_string_dialog(find_in_text, cancel_find, data);
}

/* ........................................................... */

#include "pixmaps/save.xpm"
#include "pixmaps/saveas.xpm"
#if defined(G_OS_WIN32) || defined(USE_GNOME)
# include "pixmaps/print.xpm"
#endif
#include "pixmaps/exec_small.xpm"
#include "pixmaps/copy.xpm"
#include "pixmaps/paste.xpm"
#include "pixmaps/search.xpm"
#include "pixmaps/replace.xpm"
#include "pixmaps/undo.xpm"
#include "pixmaps/question.xpm"
#include "pixmaps/close.xpm"

static void make_viewbar (windata_t *vwin)
{
    GtkWidget *iconw, *button, *viewbar;
    GdkPixmap *icon;
    GdkBitmap *mask;
    GdkColormap *colormap;
    int i;
    static char *viewstrings[] = {_("Save"),
				  _("Save as..."),
				  _("Print..."),
				  _("Run"),
				  _("Copy selection"), 
				  _("Paste"),
				  _("Find..."),
				  _("Replace..."),
				  _("Undo"),
				  _("Help on command"),
				  _("Close"),
				  NULL};
    gchar **toolxpm = NULL;
    void (*toolfunc)() = NULL;

    int run_ok = (vwin->role == EDIT_SCRIPT ||
		  vwin->role == VIEW_SCRIPT ||
		  vwin->role == VIEW_LOG);

    int edit_ok = (vwin->role == EDIT_SCRIPT ||
		   vwin->role == EDIT_HEADER ||
		   vwin->role == EDIT_NOTES ||
		   vwin->role == SCRIPT_OUT);

    int save_as_ok = (vwin->role != EDIT_HEADER && 
		      vwin->role != EDIT_NOTES);

#if defined(G_OS_WIN32) || defined(USE_GNOME)
    int print_ok = 1;
#else
    int print_ok = 0;
#endif

    colormap = gdk_colormap_get_system();
    viewbar = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(vwin->dialog)->action_area), 
		      viewbar);

    colorize_tooltips(GTK_TOOLBAR(viewbar)->tooltips);

    for (i=0; viewstrings[i] != NULL; i++) {
	switch (i) {
	case 0:
	    if (edit_ok && vwin->role != SCRIPT_OUT) {
		toolxpm = save_xpm;	    
		if (vwin->role == EDIT_HEADER || vwin->role == EDIT_NOTES) 
		    toolfunc = buf_edit_save;
		else
		    toolfunc = file_viewer_save;
	    } else
		toolfunc = NULL;
	    break;
	case 1:
	    if (save_as_ok) {
		toolxpm = save_as_xpm;
		toolfunc = file_save_callback;
	    } else
		toolfunc = NULL;
	    break;
	case 2:
	    if (print_ok) {
#if defined(G_OS_WIN32) || defined(USE_GNOME)
		toolxpm = print_xpm;
		toolfunc = window_print_callback;
#endif
	    } else
		toolfunc = NULL;
	    break;
	case 3:
	    if (run_ok) {
		toolxpm = exec_xpm;
		toolfunc = run_script_callback;
	    } else
		toolfunc = NULL;
	    break;
	case 4:
	    toolxpm = copy_xpm;
	    toolfunc = text_copy_callback;
	    break;
	case 5:
	    if (edit_ok) {
		toolxpm = paste_xpm;
		toolfunc = text_paste_callback;
	    } else
		toolfunc = NULL;
	    break;
	case 6:
	    toolxpm = search_xpm;
	    toolfunc = text_find_callback;
	    break;
	case 7:
	    if (edit_ok) {
		toolxpm = replace_xpm;
		toolfunc = text_replace_callback;
	    } else
		toolfunc = NULL;
	    break;
	case 8:
	    if (edit_ok) {
		toolxpm = undo_xpm;
		toolfunc = text_undo_callback;
	    } else
		toolfunc = NULL;
	    break;
	case 9:
	    if (run_ok) {
		toolxpm = question_xpm;
		toolfunc = activate_script_help;
	    } else
		toolfunc = NULL;
	    break;
	case 10:
	    toolxpm = close_xpm;
	    toolfunc = delete_file_viewer;
	    break;
	default:
	    break;
	}

	if (toolfunc == NULL) continue;

	icon = gdk_pixmap_colormap_create_from_xpm_d(NULL, colormap, &mask, NULL, 
						     toolxpm);
	iconw = gtk_pixmap_new(icon, mask);
	button = gtk_toolbar_append_item(GTK_TOOLBAR(viewbar),
					 NULL, viewstrings[i], NULL,
					 iconw,
					 toolfunc, vwin);
    }
    gtk_widget_show(viewbar);
}

/* ........................................................... */

windata_t *view_buffer (PRN *prn, int hsize, int vsize, 
			char *title, int role,
			GtkItemFactoryEntry menu_items[]) 
{
    GtkWidget *dialog, *close, *table;
    GtkWidget *vscrollbar; 
    windata_t *vwin;

    if ((vwin = mymalloc(sizeof *vwin)) == NULL) return NULL;
    windata_init(vwin);
    vwin->role = role;

    hsize *= gdk_char_width(fixed_font, 'W');
    hsize += 48;

    dialog = gtk_dialog_new();
    vwin->dialog = dialog;
    winstack_add(dialog);
    gtk_widget_set_usize (dialog, hsize, vsize);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_container_border_width (GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), 5);
    gtk_container_border_width 
	(GTK_CONTAINER(GTK_DIALOG(dialog)->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 5);
    gtk_box_set_homogeneous(GTK_BOX(GTK_DIALOG(dialog)->action_area), TRUE);
#ifndef G_OS_WIN32
    gtk_signal_connect_after(GTK_OBJECT(dialog), "realize", 
			     GTK_SIGNAL_FUNC(set_wm_icon), 
			     NULL);
#endif

    if (menu_items != NULL) {
	set_up_viewer_menu(dialog, vwin, menu_items);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			   vwin->mbar, FALSE, TRUE, 0);
	gtk_widget_show(vwin->mbar);
    }

    table = gtk_table_new(1, 2, FALSE);
    gtk_widget_set_usize(table, 500, 400);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       table, TRUE, TRUE, FALSE);

    vwin->w = gtk_text_new(NULL, NULL);

    gtk_text_set_editable(GTK_TEXT(vwin->w), FALSE);

    gtk_text_set_word_wrap(GTK_TEXT(vwin->w), TRUE);
    gtk_table_attach(GTK_TABLE(table), vwin->w, 0, 1, 0, 1,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND | 
		     GTK_SHRINK, 0, 0);
    gtk_widget_show(vwin->w);

    vscrollbar = gtk_vscrollbar_new(GTK_TEXT (vwin->w)->vadj);
    gtk_table_attach (GTK_TABLE (table), 
		      vscrollbar, 1, 2, 0, 1,
		      GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_widget_show (vscrollbar);

    gtk_widget_show(table);

    /* close button */
    close = gtk_button_new_with_label(_("Close"));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), 
		       close, FALSE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(close), "clicked", 
		       GTK_SIGNAL_FUNC(delete_file_viewer), vwin);
    gtk_widget_show(close);

    /* insert and then free the text buffer */
    gtk_text_insert(GTK_TEXT(vwin->w), fixed_font, 
		    NULL, NULL, prn->buf, 
		    strlen(prn->buf));
    gretl_print_destroy(prn);
    
    gtk_signal_connect(GTK_OBJECT(dialog), "key_press_event", 
			   GTK_SIGNAL_FUNC(catch_key), dialog);

    /* clean up when dialog is destroyed */
    gtk_signal_connect(GTK_OBJECT(dialog), "destroy", 
		       GTK_SIGNAL_FUNC(free_windata), vwin);

    gtk_widget_show(dialog);
    return vwin;
}

/* ........................................................... */

static gchar *make_viewer_title (int role, const char *fname)
{
    gchar *title = NULL;

    switch (role) {
    case HELP: 
	title = g_strdup(_("gretl: help")); break;
    case CLI_HELP:
	title = g_strdup(_("gretl: command syntax")); break;
    case VIEW_LOG:
	title = g_strdup(_("gretl: command log")); break;
    case CONSOLE:
	title = g_strdup(_("gretl console")); break;
    case EDIT_SCRIPT:
    case VIEW_SCRIPT:	
	if (strstr(fname, "script_tmp") || strstr(fname, "session.inp"))
	    title = g_strdup(_("gretl: command script"));
	else {
	    gchar *p = strrchr(fname, SLASH);
	    title = g_strdup_printf("gretl: %s", p? p + 1 : fname);
	} 
	break;
    case EDIT_NOTES:
	title = g_strdup(_("gretl: session notes")); break;
    case GR_PLOT:
	title = g_strdup(_("gretl: edit plot commands")); break;
    case SCRIPT_OUT:
	title = g_strdup(_("gretl: script output")); break;
    default:
	break;
    }
    return title;
}

/* ........................................................... */

static void script_changed (GtkWidget *w, windata_t *vwin)
{
    SCRIPT_CHANGED(vwin);
}

/* ........................................................... */

static void auto_save_script (windata_t *vwin)
{
    FILE *fp;
    char msg[MAXLEN];
    gchar *savestuff;

    if (strstr(vwin->fname, "script_tmp") || !strlen(vwin->fname)) {
	file_save(vwin, SAVE_SCRIPT, NULL);
	strcpy(vwin->fname, scriptfile);
    }

    if ((fp = fopen(vwin->fname, "w")) == NULL) {
	sprintf(msg, _("Couldn't write to %s"), vwin->fname);
	errbox(msg); 
	return;
    }
    savestuff = 
	gtk_editable_get_chars(GTK_EDITABLE(vwin->w), 0, -1);
    fprintf(fp, "%s", savestuff);
    g_free(savestuff); 
    fclose(fp);
    infobox(_("script saved"));
    SCRIPT_SAVED(vwin);
}

/* ........................................................... */

static gint query_save_script (GtkWidget *w, GdkEvent *event, windata_t *vwin)
{
    if (SCRIPT_IS_CHANGED(vwin)) {
	int button;

	button = yes_no_dialog(_("gretl: script"), 
			       _("Save changes?"), 1);

	if (button == CANCEL_BUTTON)
	    return TRUE;
	if (button == YES_BUTTON)
	    auto_save_script(vwin);
    }
    return FALSE;
}

/* ........................................................... */

windata_t *view_file (char *filename, int editable, int del_file, 
		      int hsize, int vsize, int role, 
		      GtkItemFactoryEntry menu_items[]) 
{
    GtkWidget *dialog, *table, *vscrollbar; 
    void *colptr = NULL, *nextcolor = NULL;
    char tempstr[MAXSTR], *fle = NULL;
    FILE *fd = NULL;
    windata_t *vwin;
    gchar *title;
    static GtkStyle *style;
    int show_viewbar = (role != CONSOLE &&
			role != HELP &&
			role != CLI_HELP);
    int doing_script = (role == EDIT_SCRIPT ||
			role == VIEW_SCRIPT ||
			role == VIEW_LOG);

    fd = fopen(filename, "r");
    if (fd == NULL) {
	sprintf(errtext, _("Can't open %s for reading"), filename);
	errbox(errtext);
	return NULL;
    }

    if ((vwin = mymalloc(sizeof *vwin)) == NULL)
	return NULL;
    windata_init(vwin);
    strcpy(vwin->fname, filename);
    vwin->role = role;

    hsize *= gdk_char_width(fixed_font, 'W');
    hsize += 48;

    dialog = gtk_dialog_new();
    vwin->dialog = dialog;
    if (!doing_script) winstack_add(dialog);
    gtk_widget_set_usize (dialog, hsize, vsize);

    title = make_viewer_title(role, filename);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    g_free(title);

    gtk_container_border_width (GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), 5);
    gtk_container_border_width 
        (GTK_CONTAINER(GTK_DIALOG(dialog)->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 5);
    gtk_box_set_homogeneous(GTK_BOX(GTK_DIALOG(dialog)->action_area), TRUE);
#ifndef G_OS_WIN32
    gtk_signal_connect_after(GTK_OBJECT(dialog), "realize", 
			     GTK_SIGNAL_FUNC(set_wm_icon), 
			     NULL);
#endif

    if (menu_items != NULL) {
	set_up_viewer_menu(dialog, vwin, menu_items);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
			   vwin->mbar, FALSE, TRUE, 0);
	gtk_widget_show(vwin->mbar);
    }

    table = gtk_table_new(1, 2, FALSE);
    gtk_widget_set_usize(table, 500, 400);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       table, TRUE, TRUE, FALSE);

    vwin->w = gtk_text_new(NULL, NULL);

    if (style == NULL) {
	style = gtk_style_new();
	gdk_font_unref(style->font);
	style->font = fixed_font;
    }
    gtk_widget_set_style(GTK_WIDGET(vwin->w), style);

    gtk_text_set_editable(GTK_TEXT(vwin->w), editable);

    /* special case: the gretl console */
    if (role == CONSOLE) {
	gtk_signal_connect(GTK_OBJECT(vwin->w), "key_press_event",
			   (GtkSignalFunc) console_handler, NULL);
    } 

    if (doing_script) {
	gtk_signal_connect_after(GTK_OBJECT(vwin->w), "button_press_event",
				 (GtkSignalFunc) edit_script_help, vwin);
    } 

    gtk_text_set_word_wrap(GTK_TEXT(vwin->w), TRUE);
    gtk_table_attach(GTK_TABLE(table), vwin->w, 0, 1, 0, 1,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND | 
		     GTK_SHRINK, 0, 0);
    gtk_widget_show(vwin->w);

    vscrollbar = gtk_vscrollbar_new (GTK_TEXT (vwin->w)->vadj);
    gtk_table_attach (GTK_TABLE (table), 
		      vscrollbar, 1, 2, 0, 1,
		      GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_widget_show (vscrollbar);

    gtk_widget_show(table);

    /* is the file to be deleted after viewing? */
    if (del_file) {
	if ((fle = mymalloc(strlen(filename) + 1)) == NULL)
	    return NULL;
	strcpy(fle, filename);
    }

    /* should we show a toolbar? */
    if (show_viewbar) { 
	make_viewbar(vwin);
    } else { /* make a simple Close button instead */
	GtkWidget *close = 
	    gtk_button_new_with_label(_("Close"));

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), 
			   close, FALSE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(close), "clicked", 
			   GTK_SIGNAL_FUNC(delete_file_viewer), vwin);
	gtk_widget_show(close);
    }

    /* insert the file text */
    memset(tempstr, 0, sizeof tempstr);
    while (fgets(tempstr, sizeof tempstr - 1, fd)) {
	if (tempstr[0] == '@') continue;
	if (tempstr[0] == '?') 
	    colptr = (role == CONSOLE)? &red : &blue;
	if (tempstr[0] == '#') {
	    if (role == HELP || role == CLI_HELP) {
		tempstr[0] = ' ';
		nextcolor = &red;
	    } else
		colptr = &blue;
	} else
	    nextcolor = NULL;
	gtk_text_insert(GTK_TEXT(vwin->w), fixed_font, 
			colptr, NULL, tempstr, 
			strlen(tempstr));
	colptr = nextcolor;
	memset(tempstr, 0, sizeof tempstr);
    }
    fclose(fd);

    /* grab the "changed" signal when editing a script */
    if (role == EDIT_SCRIPT) {
	gtk_signal_connect(GTK_OBJECT(vwin->w), "changed", 
			   GTK_SIGNAL_FUNC(script_changed), vwin);
    }

    /* catch some keystrokes */
    if (!editable) {
	gtk_signal_connect(GTK_OBJECT(dialog), "key_press_event", 
			   GTK_SIGNAL_FUNC(catch_key), dialog);
    } else {
	gtk_object_set_data(GTK_OBJECT(dialog), "vwin", vwin);
	gtk_signal_connect(GTK_OBJECT(dialog), "key_press_event", 
			   GTK_SIGNAL_FUNC(catch_edit_key), vwin);	
    }  

    /* offer chance to save script on exit */
    if (role == EDIT_SCRIPT)
	gtk_signal_connect(GTK_OBJECT(dialog), "delete_event", 
			   GTK_SIGNAL_FUNC(query_save_script), vwin);

    /* clean up when dialog is destroyed */
    if (del_file) {
	gtk_signal_connect(GTK_OBJECT(dialog), "destroy", 
			   GTK_SIGNAL_FUNC(delete_file), (gpointer) fle);
    }
    gtk_signal_connect(GTK_OBJECT(dialog), "destroy", 
		       GTK_SIGNAL_FUNC(free_windata), vwin);

    gtk_widget_show(dialog);

    return vwin;
}

/* ........................................................... */

windata_t *edit_buffer (char **pbuf, int hsize, int vsize, 
			char *title, int role) 
{
    GtkWidget *dialog, *table;
    GtkWidget *vscrollbar; 
    windata_t *vwin;

    if ((vwin = mymalloc(sizeof *vwin)) == NULL)
	return NULL;
    windata_init(vwin);
    vwin->data = pbuf;
    vwin->role = role;

    hsize *= gdk_char_width(fixed_font, 'W');
    hsize += 48;

    dialog = gtk_dialog_new();
    vwin->dialog = dialog;
    winstack_add(dialog);
    gtk_widget_set_usize (dialog, hsize, vsize);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_container_border_width (GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), 5);
    gtk_container_border_width 
        (GTK_CONTAINER(GTK_DIALOG(dialog)->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 5);
    gtk_box_set_homogeneous(GTK_BOX(GTK_DIALOG(dialog)->action_area), TRUE);
#ifndef G_OS_WIN32
    gtk_signal_connect_after(GTK_OBJECT(dialog), "realize", 
			     GTK_SIGNAL_FUNC(set_wm_icon), 
			     NULL);
#endif

    /* add a menu bar */
    set_up_viewer_menu(dialog, vwin, edit_items);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       vwin->mbar, FALSE, TRUE, 0);
    gtk_widget_show(vwin->mbar);

    table = gtk_table_new(1, 2, FALSE);
    gtk_widget_set_usize(table, 500, 400);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       table, TRUE, TRUE, FALSE);

    vwin->w = gtk_text_new(NULL, NULL);

    gtk_text_set_editable(GTK_TEXT(vwin->w), TRUE);
    gtk_text_set_word_wrap(GTK_TEXT(vwin->w), TRUE);

    gtk_table_attach(GTK_TABLE(table), vwin->w, 0, 1, 0, 1,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND | 
		     GTK_SHRINK, 0, 0);
    gtk_widget_show(vwin->w);

    vscrollbar = gtk_vscrollbar_new (GTK_TEXT (vwin->w)->vadj);
    gtk_table_attach (GTK_TABLE (table), 
		      vscrollbar, 1, 2, 0, 1,
		      GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_widget_show (vscrollbar);

    gtk_widget_show(table);

    /* add an editing bar */
    make_viewbar(vwin);    

    /* insert the buffer text */
    if (*pbuf)
	gtk_text_insert(GTK_TEXT(vwin->w), fixed_font, 
			NULL, NULL, *pbuf, strlen(*pbuf));
    else {
	gtk_text_insert(GTK_TEXT(vwin->w), fixed_font, 
			NULL, NULL, "A", strlen("A"));
	gtk_editable_delete_text(GTK_EDITABLE(vwin->w), 0, -1);
    }

    /* clean up when dialog is destroyed */
    gtk_signal_connect(GTK_OBJECT(dialog), "destroy", 
		       GTK_SIGNAL_FUNC(free_windata), vwin);

    gtk_widget_show(dialog);

    return vwin;
}

/* ........................................................... */

void flip (GtkItemFactory *ifac, char *path, gboolean s)
{
    if (ifac != NULL) {
	GtkWidget *w = gtk_item_factory_get_item(ifac, path);

	if (w != NULL) 
	    gtk_widget_set_sensitive(w, s);
	else
	    fprintf(stderr, _("Failed to flip state of \"%s\"\n"), path);
    }
}

/* ........................................................... */

static void model_rtf_copy_state (GtkItemFactory *ifac, gboolean s)
{
    flip(ifac, "/Edit/Copy all/as RTF", s);
}

/* ........................................................... */

static void model_latex_copy_state (GtkItemFactory *ifac, gboolean s)
{
    flip(ifac, "/Edit/Copy all/as HTML", s);
    flip(ifac, "/Edit/Copy all/as LaTeX", s);
}

/* ........................................................... */

static void model_panel_menu_state (GtkItemFactory *ifac, gboolean s)
{
    flip(ifac, _("/Tests/panel diagnostics"), s);
}

/* ........................................................... */

static void model_ml_menu_state (GtkItemFactory *ifac, gboolean s)
{
    flip(ifac, _("/Model data/Add to data set/log likelihood"), s);
}

/* ........................................................... */

static void model_menu_state (GtkItemFactory *ifac, gboolean s)
{
    flip(ifac, _("/Tests/non-linearity (squares)"), s);
    flip(ifac, _("/Tests/non-linearity (logs)"), s);
    flip(ifac, _("/Tests/autocorrelation"), s);
    flip(ifac, _("/Tests/heteroskedasticity"), s);
    flip(ifac, _("/Tests/Chow test"), s);
    flip(ifac, _("/Tests/CUSUM test"), s);
    flip(ifac, _("/Tests/ARCH"), s);
    flip(ifac, _("/Tests/normality of residual"), s);
    flip(ifac, _("/Graphs"), s);
    flip(ifac, _("/Model data/Display actual, fitted, residual"), s);
    flip(ifac, _("/Model data/Forecasts with standard errors"), s);
    flip(ifac, _("/Model data/Add to data set/residuals"), s);
    flip(ifac, _("/Model data/Add to data set/squared residuals"), s);
    flip(ifac, _("/Model data/Add to data set/error sum of squares"), s);
    flip(ifac, _("/Model data/Add to data set/standard error of residuals"), s);
    flip(ifac, _("/Model data/Add to data set/R-squared"), s);
    flip(ifac, _("/Model data/Add to data set/T*R-squared"), s);    
}

/* ........................................................... */

static void lmmenu_state (GtkItemFactory *ifac, gboolean s)
{
    flip(ifac, _("/Tests/non-linearity (squares)"), s);
    flip(ifac, _("/Tests/non-linearity (logs)"), s);
    flip(ifac, _("/Tests/autocorrelation"), s);
    flip(ifac, _("/Tests/heteroskedasticity"), s);
    flip(ifac, _("/Tests/Chow test"), s);
    flip(ifac, _("/Tests/CUSUM test"), s);
    flip(ifac, _("/Tests/ARCH"), s);
}

/* ........................................................... */

static void latex_menu_state (GtkItemFactory *ifac, gboolean s)
{
    flip(ifac, _("/LaTeX"), s);
}

/* ........................................................... */

static void model_save_state (GtkItemFactory *ifac, gboolean s)
{
    flip(ifac, _("/File/Save to session as icon"), s);
    flip(ifac, _("/File/Save as icon and close"), s);
}

/* ........................................................... */

static void set_up_viewer_menu (GtkWidget *window, windata_t *vwin, 
				GtkItemFactoryEntry items[])
{
    GtkAccelGroup *accel;
    gint n_items = 0;

    while (items[n_items].path != NULL) n_items++;

    accel = gtk_accel_group_new();
    vwin->ifac = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>", 
				      accel);
    gtk_item_factory_create_items(vwin->ifac, n_items, items, vwin);
    vwin->mbar = gtk_item_factory_get_widget(vwin->ifac, "<main>");
    gtk_accel_group_attach(accel, GTK_OBJECT (window));

    if (vwin->role == SUMMARY || vwin->role == VAR_SUMMARY
	|| vwin->role == CORR) {
	augment_copy_menu(vwin);
	return;
    }

    if (vwin->role == VIEW_MODEL && vwin->data != NULL) { 
	MODEL *pmod = (MODEL *) vwin->data;

	model_rtf_copy_state(vwin->ifac, pmod->ci == OLS || 
			     pmod->ci == CORC || pmod->ci == HILU ||
			     pmod->ci == WLS || pmod->ci == HSK ||
			     pmod->ci == HCCM);
			     
	model_latex_copy_state(vwin->ifac, 
			       pmod->ci == OLS || pmod->ci == CORC ||
			       pmod->ci == HILU);

	model_panel_menu_state(vwin->ifac, pmod->ci == POOLED);

	latex_menu_state(vwin->ifac, !pmod->errcode && (pmod->ci == OLS || 
					    pmod->ci == POOLED ||
					    pmod->ci == CORC || 
					    pmod->ci == HILU));

	lmmenu_state(vwin->ifac, pmod->ci == OLS || pmod->ci == POOLED);

	if (pmod->ci == LOGIT || pmod->ci == PROBIT) {
	    model_menu_state(vwin->ifac, FALSE);
	    model_ml_menu_state(vwin->ifac, TRUE);
	} else
	    model_ml_menu_state(vwin->ifac, FALSE);
	if (pmod->name)
	    model_save_state(vwin->ifac, FALSE);
    }
}

/* .................................................................. */

static void add_vars_to_plot_menu (windata_t *vwin)
{
    int i, j;
    GtkItemFactoryEntry varitem;
    gchar *mpath[] = {_("/Graphs/residual plot"), 
		      _("/Graphs/fitted, actual plot")};
    MODEL *pmod = vwin->data;

    varitem.path = NULL;

    for (i=0; i<2; i++) {
	varitem.path = mymalloc(64);
	varitem.accelerator = NULL;
	varitem.callback_action = 0; 
	varitem.item_type = NULL;
	if (dataset_is_time_series(datainfo))
	    sprintf(varitem.path, _("%s/against time"), mpath[i]);
	else
	    sprintf(varitem.path, _("%s/by observation number"), mpath[i]);
	if (i == 0)
	    varitem.callback = resid_plot; 
	else
	    varitem.callback = fit_actual_plot;
	gtk_item_factory_create_item(vwin->ifac, &varitem, vwin, 1);

	/* put the indep vars on the menu list */
	for (j=2; j<pmod->list[0]; j++) {
	    if (pmod->list[j] == 0) continue;
	    if (varitem.path == NULL)
		varitem.path = mymalloc(64);
	    varitem.accelerator = NULL;
	    varitem.callback_action = pmod->list[j]; 
	    varitem.item_type = NULL;
	    sprintf(varitem.path, _("%s/against %s"), mpath[i], 
		    datainfo->varname[pmod->list[j]]);
	    if (i == 0)
		varitem.callback = resid_plot; 
	    else
		varitem.callback = fit_actual_plot;
	    gtk_item_factory_create_item(vwin->ifac, &varitem, vwin, 1);
	}
    }
    free(varitem.path);
}

/* .................................................................. */

static void plot_dummy_call (gpointer data, guint v, GtkWidget *widget)
{
    GtkCheckMenuItem *item = GTK_CHECK_MENU_ITEM(widget);
    windata_t *vwin = (windata_t *) data;

    if (item->active) vwin->active_var = v; 
}

/* .................................................................. */

static void add_dummies_to_plot_menu (windata_t *vwin)
{
    int i, dums = 0;
    GtkItemFactoryEntry dumitem;
    MODEL *pmod = vwin->data;

    dumitem.path = NULL;

    /* put the dummy independent vars on the menu list */
    for (i=2; i<pmod->list[0]; i++) {
	if (pmod->list[i] == 0) continue;
	if (!isdummy(pmod->list[i], datainfo->t1, datainfo->t2, Z))
	    continue;
	if (!dums) { /* add separator, branch and "none" */
	    dumitem.path = mymalloc(64);
	    sprintf(dumitem.path, _("/Graphs/dumsep"));
	    dumitem.callback = NULL;
	    dumitem.callback_action = 0;
	    dumitem.item_type = "<Separator>";
	    dumitem.accelerator = NULL;
	    gtk_item_factory_create_item(vwin->ifac, &dumitem, vwin, 1);
	    /* menu branch */
	    sprintf(dumitem.path, _("/Graphs/Separation"));
	    dumitem.callback = NULL;
	    dumitem.callback_action = 0;
	    dumitem.item_type = "<Branch>";
	    dumitem.accelerator = NULL;
	    gtk_item_factory_create_item(vwin->ifac, &dumitem, vwin, 1);
	    /* "none" option */
	    sprintf(dumitem.path, _("/Graphs/Separation/none"));
	    dumitem.callback = plot_dummy_call;
	    dumitem.callback_action = 0;
	    dumitem.item_type = "<RadioItem>";
	    dumitem.accelerator = NULL;
	    gtk_item_factory_create_item(vwin->ifac, &dumitem, vwin, 1);
	    dums = 1;
	} 
	dumitem.callback_action = pmod->list[i]; 
	sprintf(dumitem.path, _("/Graphs/Separation/by %s"),  
		datainfo->varname[pmod->list[i]]);
	dumitem.callback = plot_dummy_call;	    
	dumitem.accelerator = NULL;
	dumitem.item_type = _("/Graphs/Separation/none");
	gtk_item_factory_create_item(vwin->ifac, &dumitem, vwin, 1);
    }
    free(dumitem.path);
}

/* ........................................................... */

static void check_model_menu (GtkWidget *w, GdkEventButton *eb, 
			      gpointer data)
{
    windata_t *mwin = (windata_t *) data;
    MODEL *pmod = mwin->data;
    extern int quiet_sample_check (MODEL *pmod);
    int s, ok = 1;

    if (Z == NULL) {
	flip(mwin->ifac, _("/File/Save to sesssion as icon"), FALSE);
	flip(mwin->ifac, _("/File/Save as icon and close"), FALSE);
	flip(mwin->ifac, _("/Edit/Copy all"), FALSE);
	flip(mwin->ifac, _("/Model data"), FALSE);
	flip(mwin->ifac, _("/Tests"), FALSE);
	flip(mwin->ifac, _("/Graphs"), FALSE);
	flip(mwin->ifac, _("/Model data"), FALSE);
	flip(mwin->ifac, _("/LaTeX"), FALSE);
	return;
    }

    if (quiet_sample_check(pmod)) ok = 0;
    s = GTK_WIDGET_IS_SENSITIVE
	(gtk_item_factory_get_item(mwin->ifac, _("/Tests/omit variables")));
    if ((s && ok) || (!s && !ok)) return;
    s = !s;

    flip(mwin->ifac, _("/Tests/omit variables"), s);
    flip(mwin->ifac, _("/Tests/add variables"), s);
    flip(mwin->ifac, _("/Tests/non-linearity (squares)"), s);
    flip(mwin->ifac, _("/Tests/non-linearity (logs)"), s);
    flip(mwin->ifac, _("/Tests/autocorrelation"), s);
    flip(mwin->ifac, _("/Tests/heteroskedasticity"), s);
    flip(mwin->ifac, _("/Tests/Chow test"), s);
    flip(mwin->ifac, _("/Tests/CUSUM test"), s);
    flip(mwin->ifac, _("/Tests/ARCH"), s);
    flip(mwin->ifac, _("/Graphs"), s);
    flip(mwin->ifac, _("/Model data/Display actual, fitted, residual"), s);
    flip(mwin->ifac, _("/Model data/Forecasts with standard errors"), s);
    flip(mwin->ifac, _("/Model data/Confidence intervals for coefficients"), s);
    flip(mwin->ifac, _("/Model data/Add to data set/fitted values"), s);
    flip(mwin->ifac, _("/Model data/Add to data set/residuals"), s);
    flip(mwin->ifac, _("/Model data/Add to data set/squared residuals"), s);
    flip(mwin->ifac, _("/Model data/Define new variable..."), s);
}

/* ........................................................... */

int view_model (PRN *prn, MODEL *pmod, int hsize, int vsize, 
		char *title) 
{
    windata_t *vwin;
    GtkWidget *dialog, *close, *table, *scroller;

    if ((vwin = mymalloc(sizeof *vwin)) == NULL) return 1;
    windata_init(vwin);

    hsize *= gdk_char_width (fixed_font, 'W');
    hsize += 48;

    vwin->data = pmod;
    vwin->role = VIEW_MODEL;

    dialog = gtk_dialog_new();
    vwin->dialog = dialog;
    winstack_add(dialog);
    gtk_widget_set_usize (dialog, hsize, vsize);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_container_border_width (GTK_CONTAINER 
				(GTK_DIALOG(dialog)->vbox), 5);
    gtk_container_border_width 
	(GTK_CONTAINER(GTK_DIALOG(dialog)->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 5);
    gtk_box_set_homogeneous(GTK_BOX 
			    (GTK_DIALOG(dialog)->action_area), TRUE);
#ifndef G_OS_WIN32
    gtk_signal_connect_after(GTK_OBJECT(dialog), "realize", 
			     GTK_SIGNAL_FUNC(set_wm_icon), 
			     NULL);
#endif

    set_up_viewer_menu(dialog, vwin, model_items);

    /* add menu of indep vars, against which to plot resid */
    add_vars_to_plot_menu(vwin);
    add_dummies_to_plot_menu(vwin);
    gtk_signal_connect(GTK_OBJECT(vwin->mbar), "button_press_event", 
		       GTK_SIGNAL_FUNC(check_model_menu), vwin);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       vwin->mbar, FALSE, TRUE, 0);
    gtk_widget_show(vwin->mbar);

    table = gtk_table_new(1, 2, FALSE);
    gtk_widget_set_usize(table, hsize, vsize); 
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
		       table, TRUE, TRUE, FALSE);

    vwin->w = gtk_text_new(NULL, NULL);
    gtk_text_set_editable(GTK_TEXT(vwin->w), FALSE);
    gtk_text_set_word_wrap(GTK_TEXT(vwin->w), TRUE);
    gtk_table_attach(GTK_TABLE(table), vwin->w, 0, 1, 0, 1,
		     GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND | 
		     GTK_SHRINK, 0, 0);
    gtk_widget_show(vwin->w);
    scroller = gtk_vscrollbar_new(GTK_TEXT(vwin->w)->vadj);
    gtk_table_attach (GTK_TABLE(table), 
		      scroller, 1, 2, 0, 1,
		      GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
    gtk_widget_show(scroller);

    gtk_widget_show(table);

    /* close button */
    close = gtk_button_new_with_label(_("Close"));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), 
		       close, FALSE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(close), "clicked", 
		       GTK_SIGNAL_FUNC(delete_file_viewer), vwin);
    gtk_widget_show(close);

    /* insert and then free the model buffer */
    gtk_text_insert(GTK_TEXT(vwin->w), fixed_font, 
		    NULL, NULL, prn->buf, 
		    strlen(prn->buf));
    gretl_print_destroy(prn);

    copylist(&default_list, pmod->list);

    /* attach shortcuts */
    gtk_object_set_data(GTK_OBJECT(dialog), "ddata", vwin);
    gtk_signal_connect(GTK_OBJECT(dialog), "key_press_event", 
		       GTK_SIGNAL_FUNC(catch_key), 
		       dialog);

    /* clean up when dialog is destroyed */
    gtk_signal_connect(GTK_OBJECT(dialog), "destroy", 
		       GTK_SIGNAL_FUNC(delete_model), 
		       vwin->data);
    gtk_signal_connect(GTK_OBJECT(dialog), "destroy", 
		       GTK_SIGNAL_FUNC(free_windata), 
		       vwin);

    gtk_widget_show_all(dialog);
    return 0;
}

/* ......................................................... */

void setup_column (GtkWidget *listbox, int column, int width) 
{
    if (width == 0) 
	gtk_clist_set_column_auto_resize (GTK_CLIST (listbox), column, TRUE);
    else if (width == -1) 
	gtk_clist_set_column_visibility (GTK_CLIST (listbox), column, FALSE);
    else 
	gtk_clist_set_column_width (GTK_CLIST (listbox), column, width);
}

/* ........................................................... */

#if defined(USE_GNOME)

static void msgbox (const char *msg, int err)
{
    if (err) gnome_app_error(GNOME_APP(mdata->w), msg);
    else gnome_app_message(GNOME_APP(mdata->w), msg);
}

#elif defined(G_OS_WIN32)

static void msgbox (const char *msg, int err)
{
    if (err) 
	MessageBox(NULL, msg, "gretl", MB_OK | MB_ICONERROR);
    else
	MessageBox(NULL, msg, "gretl", MB_OK | MB_ICONINFORMATION);
}

#else /* plain GTK */

static void msgbox (const char *msg, int err) 
{
    GtkWidget *w, *label, *button, *table;
    char labeltext[MAXLEN];

    if (err)
	sprintf(labeltext, _("Error:\n%s\n"), msg);
    else
	sprintf(labeltext, _("Info:\n%s\n"), msg);
    w = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_container_border_width(GTK_CONTAINER(w), 5);
    gtk_window_position (GTK_WINDOW(w), GTK_WIN_POS_MOUSE);
    gtk_window_set_title (GTK_WINDOW (w), (err)? _("gretl error") : 
			  _("gretl info"));  
  
    table = gtk_table_new(2, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(w), table);
  
    label = gtk_label_new(labeltext);
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 3, 0, 1);

    if (err)
	button = gtk_button_new_with_label(_("Close"));
    else
	button = gtk_button_new_with_label("OK");
    gtk_table_attach_defaults(GTK_TABLE(table), button, 1, 2, 1, 2);
  
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(delete_widget), w);
    gtk_widget_show(button);
    gtk_widget_show(label);
    gtk_widget_show(table);
    gtk_widget_show(w);  
}

#endif

/* ........................................................... */

void errbox (const char *msg) 
{
    msgbox(msg, 1);
}

/* ........................................................... */

void infobox (const char *msg) 
{
    msgbox(msg, 0);
}

/* ........................................................... */

int validate_varname (const char *varname)
{
    int i, n = strlen(varname);
    char namebit[9];
    
    if (n > 8) {
	safecpy(namebit, varname, 8);
	sprintf(errtext, _("Variable name %s... is too long\n"
	       "(the max is 8 characters)"), namebit);
	errbox(errtext);
	return 1;
    }
    if (!(isalpha(varname[0]))) {
	sprintf(errtext, _("First char of name ('%c') is bad\n"
	       "(first must be alphabetical)"), varname[0]);
	errbox(errtext);
	return 1;
    }
    for (i=1; i<n; i++) {
	if (!(isalpha(varname[i]))  
	    && !(isdigit(varname[i]))
	    && varname[i] != '_') {
	    sprintf(errtext, _("Name contains an illegal char (in place %d)\n"
		    "Use only letters, digits and underscore"), i + 1);
	    errbox(errtext);
	    return 1;
	}
    }
    return 0;
}	

/* .................................................................. */

void options_dialog (gpointer data) 
{
    GtkWidget *tempwid, *dialog, *notebook;

    dialog = gtk_dialog_new ();
    gtk_window_set_title (GTK_WINDOW (dialog), _("gretl: options"));
    gtk_container_border_width 
	(GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 10);
    gtk_container_border_width 
	(GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 15);
    gtk_box_set_homogeneous (GTK_BOX (GTK_DIALOG (dialog)->action_area), TRUE);
    gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
    gtk_signal_connect_object 
	(GTK_OBJECT (dialog), "delete_event", GTK_SIGNAL_FUNC 
	 (gtk_widget_destroy), GTK_OBJECT (dialog));
   
    notebook = gtk_notebook_new ();
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			notebook, TRUE, TRUE, 0);
    gtk_widget_show (notebook);

    make_prefs_tab (notebook, 1);
    make_prefs_tab (notebook, 2);
    make_prefs_tab (notebook, 3);
    make_prefs_tab (notebook, 4);
    make_prefs_tab (notebook, 5);
   
    tempwid = gtk_button_new_with_label ("OK");
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG 
				 (dialog)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			GTK_SIGNAL_FUNC (apply_changes), NULL);
    gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked", 
			       GTK_SIGNAL_FUNC (gtk_widget_destroy), 
			       GTK_OBJECT (dialog));
    gtk_widget_show (tempwid);

    tempwid = gtk_button_new_with_label (_("  Cancel  "));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG 
				 (dialog)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked", 
			       GTK_SIGNAL_FUNC (gtk_widget_destroy), 
			       GTK_OBJECT (dialog));
    gtk_widget_show (tempwid);

    tempwid = gtk_button_new_with_label (_("Apply"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG 
				 (dialog)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			GTK_SIGNAL_FUNC (apply_changes), NULL);
    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

    gtk_widget_show (dialog);
}

/* .................................................................. */

static void flip_sensitive (GtkWidget *w, gpointer data)
{
    GtkWidget *entry = GTK_WIDGET(data);
    
    gtk_widget_set_sensitive(entry, GTK_TOGGLE_BUTTON(w)->active);
}

/* .................................................................. */

static void make_prefs_tab (GtkWidget *notebook, int tab) 
{
    GtkWidget *box, *inttbl, *chartbl, *tempwid = NULL;
    int i, tbl_len, tbl_num, tbl_col;
    RCVARS *rc = NULL;
   
    box = gtk_vbox_new (FALSE, 0);
    gtk_container_border_width (GTK_CONTAINER (box), 10);
    gtk_widget_show (box);

    if (tab == 1)
	tempwid = gtk_label_new (_("General"));
    else if (tab == 2)
	tempwid = gtk_label_new (_("Databases"));
    else if (tab == 3)
	tempwid = gtk_label_new (_("Toolbar"));
    else if (tab == 4)
	tempwid = gtk_label_new (_("Open/Save path"));
    else if (tab == 5)
	tempwid = gtk_label_new (_("Data files"));
    
    gtk_widget_show (tempwid);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), box, tempwid);   

    tbl_len = 1;
    chartbl = gtk_table_new (tbl_len, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (chartbl), 5);
    gtk_table_set_col_spacings (GTK_TABLE (chartbl), 5);
    gtk_box_pack_start (GTK_BOX (box), chartbl, FALSE, FALSE, 0);
    gtk_widget_show (chartbl);
   
    tbl_num = tbl_col = 0;
    inttbl = gtk_table_new (1, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (inttbl), 2);
    gtk_table_set_col_spacings (GTK_TABLE (inttbl), 5);
    gtk_box_pack_start (GTK_BOX (box), inttbl, FALSE, FALSE, 0);
    gtk_widget_show (inttbl);

    i = 0;
    while (rc_vars[i].key != NULL) {
	rc = &rc_vars[i];
	if (rc->tab == tab) {
	    if (rc->type == 'B' 
		&& rc->link == NULL) { /* simple boolean variable */
		tempwid = gtk_check_button_new_with_label 
		    (rc->description);
		gtk_table_attach_defaults 
		    (GTK_TABLE (inttbl), tempwid, tbl_col, tbl_col + 1, 
		     tbl_num, tbl_num + 1);
		if (*(int *)(rc->var))
		    gtk_toggle_button_set_active 
			(GTK_TOGGLE_BUTTON (tempwid), TRUE);
		else
		    gtk_toggle_button_set_active 
			(GTK_TOGGLE_BUTTON (tempwid), FALSE);
		/* special case: link between toggle and preceding entry */
		if (rc->len) {
		    gtk_widget_set_sensitive(rc_vars[i-1].widget,
					     GTK_TOGGLE_BUTTON(tempwid)->active);
		    gtk_signal_connect(GTK_OBJECT(tempwid), "clicked",
				       GTK_SIGNAL_FUNC(flip_sensitive),
				       rc_vars[i-1].widget);
		}
		/* end link to entry */
		gtk_widget_show (tempwid);
		rc->widget = tempwid;
		tbl_col++;
		if (tbl_col == 2) {
		    tbl_col = 0;
		    tbl_num++;
		    gtk_table_resize (GTK_TABLE (inttbl), tbl_num + 1, 2);
		}
	    } 
	    else if (rc->type == 'B') { /* radio-button dichotomy */
		int val = *(int *)(rc->var);
		GSList *group;

		tbl_num += 2;
		gtk_table_resize (GTK_TABLE(inttbl), tbl_num + 1, 2);

		tempwid = gtk_radio_button_new_with_label(NULL, 
							  rc->description);
		gtk_table_attach_defaults 
		    (GTK_TABLE (inttbl), tempwid, tbl_col, tbl_col + 1, 
		     tbl_num - 2, tbl_num - 1);    
		if (val) 
		    gtk_toggle_button_set_active 
			(GTK_TOGGLE_BUTTON(tempwid), TRUE);
		gtk_widget_show (tempwid);
		rc->widget = tempwid;
		group = gtk_radio_button_group(GTK_RADIO_BUTTON(tempwid));
		tempwid = gtk_radio_button_new_with_label(group, rc->link);
		gtk_table_attach_defaults 
		    (GTK_TABLE (inttbl), tempwid, tbl_col, tbl_col + 1, 
		     tbl_num - 1, tbl_num);  
		if (!val)
		    gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON(tempwid), TRUE);
		gtk_widget_show (tempwid);
	    } else { /* string variable */
		tbl_len++;
		gtk_table_resize (GTK_TABLE (chartbl), tbl_len, 2);
		tempwid = gtk_label_new (rc->description);
		gtk_misc_set_alignment (GTK_MISC (tempwid), 1, 0.5);
		gtk_table_attach_defaults (GTK_TABLE (chartbl), 
					   tempwid, 0, 1, tbl_len-1, tbl_len);
		gtk_widget_show (tempwid);

		tempwid = gtk_entry_new ();
		gtk_table_attach_defaults (GTK_TABLE (chartbl), 
					   tempwid, 1, 2, tbl_len-1, tbl_len);
		gtk_entry_set_text (GTK_ENTRY (tempwid), rc->var);
		gtk_widget_show (tempwid);
		rc->widget = tempwid;
	    } 
	}
	i++;
    }
}

/* .................................................................. */

static void apply_changes (GtkWidget *widget, gpointer data) 
{
    gchar *tempstr;
    extern void show_toolbar (void);
    int i = 0;

    while (rc_vars[i].key != NULL) {
	if (rc_vars[i].widget != NULL) {
	    if (rc_vars[i].type == 'B') {
		if (GTK_TOGGLE_BUTTON(rc_vars[i].widget)->active)
		    *(int *)(rc_vars[i].var) = TRUE;
		else *(int *)(rc_vars[i].var) = FALSE;
	    } 
	    if (rc_vars[i].type == 'U' || rc_vars[i].type == 'R') {
		tempstr = gtk_entry_get_text
		    (GTK_ENTRY(rc_vars[i].widget));
		if (tempstr != NULL && strlen(tempstr)) 
		    strncpy(rc_vars[i].var, tempstr, rc_vars[i].len - 1);
	    }
	}
	i++;
    }
    write_rc();
    if (toolbar_box == NULL && want_toolbar)
	show_toolbar();
    else if (toolbar_box != NULL && !want_toolbar) {
	gtk_widget_destroy(toolbar_box);
	toolbar_box = NULL;
    }
    proxy_init(dbproxy);
}

/* .................................................................. */

static void str_to_boolvar (char *s, void *b)
{
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0)
	*(int *)b = TRUE;
    else
	*(int *)b = FALSE;	
}

/* .................................................................. */

static void boolvar_to_str (void *b, char *s)
{
    if (*(int *)b) strcpy(s, "true");
    else strcpy(s, "false");
}

/* .................................................................. */

#if defined(USE_GNOME)

void write_rc (void) 
{
    char gpath[MAXSTR];
    char val[6];
    int i = 0;

    while (rc_vars[i].key != NULL) {
	sprintf(gpath, "/gretl/%s/%s", rc_vars[i].description, rc_vars[i].key);
	if (rc_vars[i].type == 'B') {
	    boolvar_to_str(rc_vars[i].var, val);
	    gnome_config_set_string(gpath, val);
	} else
	    gnome_config_set_string(gpath, rc_vars[i].var);
	i++;
    }
    printfilelist(1, NULL); /* data files */
    printfilelist(2, NULL); /* session files */
    printfilelist(3, NULL); /* script files */    
    gnome_config_sync();
    set_paths(&paths, 0, 1);
}

static void read_rc (void) 
{
    int i = 0;
    gchar *value = NULL;
    char gpath[MAXSTR];

    while (rc_vars[i].key != NULL) {
	sprintf(gpath, "/gretl/%s/%s", 
		rc_vars[i].description, 
		rc_vars[i].key);
	if ((value = gnome_config_get_string(gpath)) != NULL) {
	    if (rc_vars[i].type == 'B')
		str_to_boolvar(value, rc_vars[i].var);
	    else
		strncpy(rc_vars[i].var, value, rc_vars[i].len - 1);
	    g_free(value);
	}
	i++;
    }

    /* initialize lists of recently opened files */
    for (i=0; i<MAXRECENT; i++) { 
	datalist[i][0] = 0;
	sessionlist[i][0] = 0;
	scriptlist[i][0] = 0;
    }
    /* get recent file lists */
    for (i=0; i<MAXRECENT; i++) {
	sprintf(gpath, "/gretl/recent data files/%d", i);
	if ((value = gnome_config_get_string(gpath)) != NULL) { 
	    strcpy(datalist[i], value);
	    g_free(value);
	}
	else break;
    }    
    for (i=0; i<MAXRECENT; i++) {
	sprintf(gpath, "/gretl/recent session files/%d", i);
	if ((value = gnome_config_get_string(gpath)) != NULL) { 
	    strcpy(sessionlist[i], value);
	    g_free(value);
	}
	else break;
    } 
    for (i=0; i<MAXRECENT; i++) {
	sprintf(gpath, "/gretl/recent script files/%d", i);
	if ((value = gnome_config_get_string(gpath)) != NULL) { 
	    strcpy(scriptlist[i], value);
	    g_free(value);
	}
	else break;
    }
    set_paths(&paths, 0, 1); /* 0 = not defaults, 1 = gui */
}

/* end of gnome versions, now win32 */
#elif defined(G_OS_WIN32)

void write_rc (void) 
{
    int i = 0;
    char val[6];

    while (rc_vars[i].key != NULL) {
	if (rc_vars[i].type == 'B') {
	    boolvar_to_str(rc_vars[i].var, val);
	    write_reg_val(HKEY_CURRENT_USER, rc_vars[i].key, val);
	} else
	    write_reg_val((rc_vars[i].type == 'R')? 
			  HKEY_CLASSES_ROOT : HKEY_CURRENT_USER, 
			  rc_vars[i].key, rc_vars[i].var);
	i++;
    }
    printfilelist(1, NULL); /* data files */
    printfilelist(2, NULL); /* session files */
    printfilelist(3, NULL); /* script files */
    set_paths(&paths, 0, 1);
}

void read_rc (void) 
{
    int i = 0;
    char rpath[MAXSTR], value[MAXSTR];

    while (rc_vars[i].key != NULL) {
	if (read_reg_val((rc_vars[i].type == 'R')? 
			 HKEY_CLASSES_ROOT : HKEY_CURRENT_USER, 
			 rc_vars[i].key, value) == 0) {
	    if (rc_vars[i].type == 'B') {
		str_to_boolvar(value, rc_vars[i].var);
	    } else
		strncpy(rc_vars[i].var, value, rc_vars[i].len - 1);
	}
	i++;
    }

    /* initialize lists of recently opened files */
    for (i=0; i<MAXRECENT; i++) { 
	datalist[i][0] = 0;
	sessionlist[i][0] = 0;
	scriptlist[i][0] = 0;
    }
    /* get recent file lists */
    for (i=0; i<MAXRECENT; i++) {
	sprintf(rpath, "recent data files\\%d", i);
	if (read_reg_val(HKEY_CURRENT_USER, rpath, value) == 0) 
	    strcpy(datalist[i], value);
	else break;
    }    
    for (i=0; i<MAXRECENT; i++) {
	sprintf(rpath, "recent session files\\%d", i);
	if (read_reg_val(HKEY_CURRENT_USER, rpath, value) == 0) 
	    strcpy(sessionlist[i], value);
	else break;
    } 
    for (i=0; i<MAXRECENT; i++) {
	sprintf(rpath, "recent script files\\%d", i);
	if (read_reg_val(HKEY_CURRENT_USER, rpath, value) == 0) 
	    strcpy(scriptlist[i], value);
	else break;
    }
    set_paths(&paths, 0, 1);
}

#else /* end of win32 versions, now plain GTK */

void write_rc (void) 
{
    FILE *rc;
    int i;
    char val[6];

    rc = fopen(rcfile, "w");
    if (rc == NULL) {
	errbox(_("Couldn't open config file for writing"));
	return;
    }
    fprintf(rc, "# config file written by gretl: do not edit\n");
    i = 0;
    while (rc_vars[i].var != NULL) {
	fprintf(rc, "# %s\n", rc_vars[i].description);
	if (rc_vars[i].type == 'B') {
	    boolvar_to_str(rc_vars[i].var, val);
	    fprintf(rc, "%s = %s\n", rc_vars[i].key, val);
	} else
	    fprintf(rc, "%s = %s\n", rc_vars[i].key, rc_vars[i].var);
	i++;
    }
    printfilelist(1, rc); /* data files */
    printfilelist(2, rc); /* session files */
    printfilelist(3, rc); /* script files */
    fclose(rc);
    set_paths(&paths, 0, 1);
}

static void read_rc (void) 
{
    FILE *rc;
    int i, j;
    char line[MAXLEN], key[32], linevar[MAXLEN];
    int gotrecent = 0;

    if ((rc = fopen(rcfile, "r")) == NULL) return;

    i = 0;
    while (rc_vars[i].var != NULL) {
	if (fgets(line, MAXLEN, rc) == NULL) 
	    break;
	if (line[0] == '#') 
	    continue;
	if (!strncmp(line, "recent ", 7)) {
	    gotrecent = 1;
	    break;
	}
	if (sscanf(line, "%s", key) == 1) {
	    strcpy(linevar, line + strlen(key) + 3); 
	    chopstr(linevar); 
	    for (j=0; rc_vars[j].key != NULL; j++) {
		if (!strcmp(key, rc_vars[j].key)) {
		    if (rc_vars[j].type == 'B')
			str_to_boolvar(linevar, rc_vars[j].var);
		    else
			strcpy(rc_vars[j].var, linevar);
		}
	    }
	}
	i++;
    }

    /* get lists of recently opened files */
    for (i=0; i<MAXRECENT; i++) { 
	datalist[i][0] = 0;
	sessionlist[i][0] = 0;
	scriptlist[i][0] = 0;
    }
    if (gotrecent || (fgets(line, MAXLEN, rc) != NULL && 
	strncmp(line, "recent data files:", 18) == 0)) {
	i = 0;
	while (fgets(line, MAXLEN, rc) && i<MAXRECENT) {
	    if (strncmp(line, "recent session files:", 21) == 0)
		break;
	    chopstr(line);
	    if (strlen(line)) 
		strcpy(datalist[i++], line);
	}
    }
    if (strncmp(line, "recent session files:", 21) == 0) {
	i = 0;
	while (fgets(line, MAXLEN, rc) && i<MAXRECENT) {
	    if (strncmp(line, "recent script files:", 20) == 0)
		break;
	    chopstr(line);
	    if (strlen(line)) 
		strcpy(sessionlist[i++], line);
	}
    }
    if (strncmp(line, "recent script files:", 20) == 0) {
	i = 0;
	while (fgets(line, MAXLEN, rc) && i<MAXRECENT) {
	    chopstr(line);
	    if (strlen(line)) 
		strcpy(scriptlist[i++], line);
	}
    }
    fclose(rc);
    set_paths(&paths, 0, 1);
}

#endif /* end of "plain gtk" versions of read_rc, write_rc */

/* .................................................................. */

static void font_selection_ok (GtkWidget *w, GtkFontSelectionDialog *fs)
{
    gchar *fstring = gtk_font_selection_dialog_get_font_name(fs);

    if (strlen(fstring)) {
	strcpy(fontspec, fstring);
	gdk_font_unref(fixed_font);
	fixed_font = gdk_font_load(fontspec);
	write_rc();
    }
    g_free(fstring);
    gtk_widget_destroy(GTK_WIDGET (fs));
}

/* .................................................................. */

void font_selector (void)
{
    static GtkWidget *fontsel = NULL;
    gchar *spacings[] = { "c", "m", NULL };

    if (!fontsel) {
	fontsel = gtk_font_selection_dialog_new 
	    (_("Font for gretl output windows"));
	gtk_window_set_position (GTK_WINDOW (fontsel), GTK_WIN_POS_MOUSE);
	gtk_font_selection_dialog_set_filter 
	    (GTK_FONT_SELECTION_DIALOG (fontsel),
                                       GTK_FONT_FILTER_BASE, GTK_FONT_ALL,
                                       NULL, NULL, NULL, NULL, spacings, NULL);
	gtk_font_selection_dialog_set_font_name 
	    (GTK_FONT_SELECTION_DIALOG (fontsel), fontspec);

	gtk_signal_connect (GTK_OBJECT(fontsel), "destroy",
			    GTK_SIGNAL_FUNC(gtk_widget_destroyed),
			    &fontsel);

	gtk_signal_connect (GTK_OBJECT 
			    (GTK_FONT_SELECTION_DIALOG 
			     (fontsel)->ok_button),
			    "clicked", GTK_SIGNAL_FUNC(font_selection_ok),
			    GTK_FONT_SELECTION_DIALOG (fontsel));

	gtk_signal_connect_object (GTK_OBJECT 
				   (GTK_FONT_SELECTION_DIALOG 
				    (fontsel)->cancel_button),
				   "clicked", 
				   GTK_SIGNAL_FUNC(gtk_widget_destroy),
				   GTK_OBJECT (fontsel));
    }
    if (!GTK_WIDGET_VISIBLE (fontsel)) gtk_widget_show (fontsel);
    else gtk_widget_destroy (fontsel);
}

/* .................................................................. */

static void close_find_dialog (GtkWidget *widget, gpointer data)
{
    gtk_widget_destroy (widget);
    find_window = NULL;
}

/* .................................................................. */

static void find_in_text (GtkWidget *widget, gpointer data)
{
    int found = 0;
    char *haystack;
    windata_t *vwin = 
	(windata_t *) gtk_object_get_data(GTK_OBJECT(data), "windat");

    haystack = gtk_editable_get_chars(GTK_EDITABLE(vwin->w), 0,
	gtk_text_get_length(GTK_TEXT(vwin->w)));

    if (needle) g_free(needle);

    needle = gtk_editable_get_chars(GTK_EDITABLE(find_entry), 0, -1);
    found = GTK_EDITABLE(vwin->w)->selection_end_pos;

    found = look_for_string(haystack, needle, found);

    if (found >= 0) {
	gtk_text_set_point(GTK_TEXT(vwin->w), found);
	gtk_editable_set_position(GTK_EDITABLE(vwin->w), found);
        gtk_editable_select_region(GTK_EDITABLE(vwin->w), 
				   found, found + strlen(needle));
	find_window = NULL;
    } else infobox(_("String was not found."));

    g_free(haystack);
}

/* .................................................................. */

static void find_in_help (GtkWidget *widget, gpointer data)
{
    int found = 0, i, linecount = 0;
    int help_length;
    char *haystack;
    windata_t *vwin = 
	(windata_t *) gtk_object_get_data(GTK_OBJECT(data), "windat");

    haystack = gtk_editable_get_chars(GTK_EDITABLE(vwin->w), 0,
	gtk_text_get_length(GTK_TEXT(vwin->w)));

    if (vwin->role == CLI_HELP) help_length = script_help_length;
    else help_length = gui_help_length;

    if (needle) g_free(needle);

    needle = gtk_editable_get_chars(GTK_EDITABLE (find_entry), 0, -1);
    found = GTK_EDITABLE(vwin->w)->selection_end_pos;

    found = look_for_string(haystack, needle, found);

    if (found >= 0) {
	gtk_text_freeze(GTK_TEXT(vwin->w));
        gtk_text_set_point (GTK_TEXT(vwin->w), found);
        gtk_text_insert (GTK_TEXT(vwin->w), NULL, NULL, NULL, " ", 1);
        gtk_text_backward_delete (GTK_TEXT(vwin->w), 1);
	gtk_text_thaw(GTK_TEXT(vwin->w));
        gtk_editable_select_region (GTK_EDITABLE(vwin->w), 
				    found, found + strlen(needle));
	for (i=0; i<found; i++) 
	    if (haystack[i] == '\n') linecount++;
	gtk_adjustment_set_value(GTK_TEXT(vwin->w)->vadj, 
				 (gfloat) (linecount - 2) *
				 GTK_TEXT(vwin->w)->vadj->upper / help_length);
	find_window = NULL;
    } else infobox(_("String was not found."));

    g_free(haystack);
}

/* .................................................................. */

static void find_in_clist (GtkWidget *w, gpointer data)
{
    int start, found = 0, n, i;
    gchar *tmp; 
    char haystack[MAXLEN];
    windata_t *dbdat;

    dbdat = (windata_t *) gtk_object_get_data(GTK_OBJECT(data), "windat");

    if (needle) g_free(needle);
    needle = gtk_editable_get_chars(GTK_EDITABLE (find_entry), 0, -1);
    lower(needle);

    start = dbdat->active_var + 1;
    n = GTK_CLIST(dbdat->listbox)->rows;

    for (i=start; i<n; i++) {  
	/* try looking in column 1 first */
	gtk_clist_get_text(GTK_CLIST(dbdat->listbox), i, 1, &tmp);
	strcpy(haystack, tmp);
	lower(haystack);
	found = look_for_string(haystack, needle, 0);
	if (found >= 0) break;
	else { /* try column 0? */
	    gtk_clist_get_text(GTK_CLIST(dbdat->listbox), i, 0, &tmp);
	    strcpy(haystack, tmp);
	    lower(haystack);
	    found = look_for_string(haystack, needle, 0);
	}
    }
    if (found >= 0) {
	gtk_clist_moveto(GTK_CLIST(dbdat->listbox), i, 0, 0, .1);
	gtk_clist_select_row(GTK_CLIST(dbdat->listbox), i, 0);
	dbdat->active_var = i;
	find_window = NULL;    
    } else {
	gtk_clist_select_row(GTK_CLIST(dbdat->listbox), 0, 0);
	dbdat->active_var = 0;
	infobox(_("String was not found."));
    }
}

/* .................................................................. */

static int look_for_string (char *haystack, char *needle, int start)
{
    int pos;
    int HaystackLength = strlen(haystack);
    int NeedleLength = strlen(needle);

    for (pos = start; pos < HaystackLength; pos++) {
        if (strncmp(&haystack[pos], needle, NeedleLength) == 0) 
             return pos;
    }
    return -1;
}

/* .................................................................. */
 
static void cancel_find (GtkWidget *widget, gpointer data)
{
    gtk_widget_destroy(GTK_WIDGET(data));
    find_window = NULL;
}

/* .................................................................. */

static void find_string_dialog (void (*YesFunc)(), void (*NoFunc)(),
				gpointer data)
{
    GtkWidget *label;
    GtkWidget *button;
    GtkWidget *hbox;
    windata_t *mydat = (windata_t *) data;

    if (find_window) {
	gtk_object_set_data(GTK_OBJECT(find_window), "windat", mydat); 
	return;
    }

    find_window = gtk_dialog_new();
    gtk_object_set_data(GTK_OBJECT(find_window), "windat", mydat);

    gtk_signal_connect (GTK_OBJECT (find_window), "destroy",
	                GTK_SIGNAL_FUNC (close_find_dialog),
	                find_window);
    gtk_window_set_title (GTK_WINDOW (find_window), _("gretl: find"));
    gtk_container_border_width (GTK_CONTAINER (find_window), 5);

    hbox = gtk_hbox_new(TRUE, TRUE);
    label = gtk_label_new(_(" Find what:"));
    gtk_widget_show (label);
    find_entry = gtk_entry_new();

    if (needle) {
	gtk_entry_set_text(GTK_ENTRY (find_entry), needle);
	gtk_entry_select_region (GTK_ENTRY (find_entry), 0, 
				 strlen (needle));
    }
    gtk_signal_connect(GTK_OBJECT (find_entry), 
			"activate", 
			GTK_SIGNAL_FUNC (YesFunc),
	                find_window);
    gtk_widget_show (find_entry);

    gtk_box_pack_start (GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX(hbox), find_entry, TRUE, TRUE, 0);
    gtk_widget_show (hbox);

    gtk_box_pack_start(GTK_BOX (GTK_DIALOG (find_window)->vbox), 
                        hbox, TRUE, TRUE, 0);

    gtk_box_set_spacing(GTK_BOX (GTK_DIALOG (find_window)->action_area), 15);
    gtk_box_set_homogeneous(GTK_BOX 
			     (GTK_DIALOG (find_window)->action_area), TRUE);
    gtk_window_set_position(GTK_WINDOW (find_window), GTK_WIN_POS_MOUSE);

    /* find button -- make this the default */
    button = gtk_button_new_with_label (_("Find next"));
    GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX (GTK_DIALOG (find_window)->action_area), 
		       button, TRUE, TRUE, FALSE);
    gtk_signal_connect(GTK_OBJECT (button), "clicked",
		       GTK_SIGNAL_FUNC (YesFunc), find_window);
    gtk_widget_grab_default(button);
    gtk_widget_show(button);

    /* cancel button */
    button = gtk_button_new_with_label (_("Cancel"));
    GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX (GTK_DIALOG (find_window)->action_area), 
		       button, TRUE, TRUE, FALSE);
    gtk_signal_connect(GTK_OBJECT (button), "clicked",
		       GTK_SIGNAL_FUNC (NoFunc), find_window);
    gtk_widget_show(button);

    gtk_widget_grab_focus(find_entry);
    gtk_widget_show (find_window);
}

/* .................................................................. */

void prn_to_clipboard (PRN *prn)
{
    size_t len;

    if (prn->buf == NULL) return;
    len = strlen(prn->buf);

    if (clipboard_buf) g_free(clipboard_buf);
    clipboard_buf = mymalloc(len + 1);

    memcpy(clipboard_buf, prn->buf, len + 1);
    gtk_selection_owner_set(mdata->w,
			    GDK_SELECTION_PRIMARY,
			    GDK_CURRENT_TIME);
}

/* .................................................................. */

void text_copy (gpointer data, guint how, GtkWidget *widget) 
{
    windata_t *vwin = (windata_t *) data;
    PRN *prn;

    /* descriptive statistics */
    if ((vwin->role == SUMMARY || vwin->role == VAR_SUMMARY)
	&& (how == COPY_LATEX || how == COPY_RTF)) {
	GRETLSUMMARY *summ = (GRETLSUMMARY *) vwin->data;
	
	if (bufopen(&prn)) return;
	if (how == COPY_LATEX) {
	    texprint_summary(summ, datainfo, prn);
	    prn_to_clipboard(prn);
	} else {
	    rtfprint_summary(summ, datainfo, prn);
#ifdef G_OS_WIN32
	    win_copy_rtf(prn);
#else
	    prn_to_clipboard(prn);
#endif
	}
	gretl_print_destroy(prn);
	return;
    }

    /* correlation matrix */
    if (vwin->role == CORR 
	&& (how == COPY_LATEX || how == COPY_RTF)) {
	CORRMAT *corr = (CORRMAT *) vwin->data;

	if (bufopen(&prn)) return;
	if (how == COPY_LATEX) {
	    texprint_corrmat(corr, datainfo, prn);
	    prn_to_clipboard(prn);
	} else {
	    rtfprint_corrmat(corr, datainfo, prn);
#ifdef G_OS_WIN32
	    win_copy_rtf(prn);
#else
	    prn_to_clipboard(prn);
#endif
	}
	gretl_print_destroy(prn);
	return;
    }

    /* or it's a model window we're copying from? */
    if (how == COPY_RTF) {
	MODEL *pmod = (MODEL *) vwin->data;

	if (pmod->errcode) 
	    errbox("Couldn't format model");
	else
	    model_to_rtf(pmod);	
	return;
    }
    else if (how == COPY_LATEX || how == COPY_HTML) {
	MODEL *pmod = (MODEL *) vwin->data;

	if (bufopen(&prn)) return;
	if (how == COPY_LATEX)
	    tex_print_model(pmod, datainfo, 0, prn);
	else
	    h_printmodel(pmod, datainfo, prn);
	prn_to_clipboard(prn);
	gretl_print_destroy(prn);
	return;
    }
    else if (how == COPY_LATEX_EQUATION) {
	MODEL *pmod = (MODEL *) vwin->data;

	if (bufopen(&prn)) return;
	tex_print_equation(pmod, datainfo, 0, prn);
	prn_to_clipboard(prn);
	gretl_print_destroy(prn);
	return;
    }

    /* otherwise just copying plain text from plain text window */
    else if (how == COPY_TEXT) {
	gtk_editable_select_region(GTK_EDITABLE(vwin->w), 0, -1);
	gtk_editable_copy_clipboard(GTK_EDITABLE(vwin->w));
    }
    else if (how == COPY_SELECTION) {
	gtk_editable_copy_clipboard(GTK_EDITABLE(vwin->w));
    }
}

/* .................................................................. */

#if defined(G_OS_WIN32) || defined (USE_GNOME)

void window_print (windata_t *vwin, guint u, GtkWidget *widget) 
{
    char *buf, *selbuf = NULL;
    GtkEditable *gedit = GTK_EDITABLE(vwin->w);

    buf = gtk_editable_get_chars(gedit, 0, -1);
    if (gedit->has_selection)
	selbuf = gtk_editable_get_chars(gedit, 
					gedit->selection_start_pos,
					gedit->selection_end_pos);
    winprint(buf, selbuf);
}

#endif

/* .................................................................. */

void text_undo (windata_t *vwin, guint u, GtkWidget *widget)
{
    gchar *old =
	gtk_object_get_data(GTK_OBJECT(vwin->w), "undo");
    
    if (old == NULL) {
	errbox(_("No undo information available"));
    } else {
	guint len = 
	    gtk_text_get_length(GTK_TEXT(vwin->w));
	guint pt = gtk_text_get_point(GTK_TEXT(vwin->w));

	gtk_text_freeze(GTK_TEXT(vwin->w));
	gtk_editable_delete_text(GTK_EDITABLE(vwin->w), 0, len);
	len = 0;
	gtk_editable_insert_text(GTK_EDITABLE(vwin->w), 
				 old, strlen(old), &len);
	gtk_text_set_point(GTK_TEXT(vwin->w), 
			   (pt > len - 1)? len - 1 : pt);
	gtk_text_thaw(GTK_TEXT(vwin->w));
	g_free(old);
	gtk_object_remove_data(GTK_OBJECT(vwin->w), "undo");
    }
}

/* .................................................................. */

void text_paste (windata_t *vwin, guint u, GtkWidget *widget)
{
    gchar *old;
    gchar *undo_buf =
	gtk_editable_get_chars(GTK_EDITABLE(vwin->w), 0, -1);

    old = gtk_object_get_data(GTK_OBJECT(vwin->w), "undo");
    g_free(old);

    gtk_object_set_data(GTK_OBJECT(vwin->w), "undo", undo_buf);

    gtk_editable_paste_clipboard(GTK_EDITABLE(vwin->w));
}

/* .................................................................. */

void make_menu_item (gchar *label, GtkWidget *menu,
		     GtkSignalFunc func, gpointer data)
{
    GtkWidget *item;

    item = gtk_menu_item_new_with_label(label);
    gtk_menu_append(GTK_MENU(menu), item);
    gtk_signal_connect_object(GTK_OBJECT(item), "activate",
			      GTK_SIGNAL_FUNC(func), data);
    gtk_widget_show(item);
}

/* .................................................................. */

void allocate_fileptrs (void)
{
    int i;
    
    for (i=0; i<MAXRECENT; i++) {
	datap[i] = datalist[i];
	sessionp[i] = sessionlist[i];
	scriptp[i] = scriptlist[i];
    }
}

/* .................................................................. */

static void clear_files_list (int filetype, char **filep)
{
    GtkWidget *w;
    char tmpname[MAXSTR];
    gchar itempath[80];
    int i;
    gchar *pathstart[] = {_("/File/Open data"), 
			  _("/Session/Open"),
			  _("/File/Open command file")};

    for (i=0; i<MAXRECENT; i++) {
	sprintf(itempath, "%s/%d. %s", pathstart[filetype - 1],
		i+1, endbit(tmpname, filep[i], -1));
	w = gtk_item_factory_get_widget(mdata->ifac, itempath);
	if (w != NULL) 
	    gtk_item_factory_delete_item(mdata->ifac, itempath);
    }
}

/* .................................................................. */

void mkfilelist (int filetype, const char *fname)
{
    char *tmp[MAXRECENT-1];
    char **filep;
    int i, match = -1;

    if (filetype == 1) filep = datap;
    else if (filetype == 2) filep = sessionp;
    else if (filetype == 3) filep = scriptp;
    else return;

    /* see if this file is already on the list */
    for (i=0; i<MAXRECENT; i++) {
        if (strcmp(filep[i], fname) == 0) {
            match = i;
            break;
        }
    }
    if (match == 0) 
	return; /* file is on top: no change in list */

    /* clear menu files list before rebuilding */
    clear_files_list(filetype, filep);
    
    /* save pointers to current order */
    for (i=0; i<MAXRECENT-1; i++) tmp[i] = filep[i];

    /* copy fname into array, if not already present */
    if (match == -1) {
        for (i=1; i<MAXRECENT; i++) {
            if (filep[i][0] == '\0') {
                strcpy(filep[i], fname);
                match = i;
                break;
	    }
	    if (match == -1) {
		match = MAXRECENT - 1;
		strcpy(filep[match], fname);
	    }
	}
    } 

    /* set first pointer to new file */
    filep[0] = filep[match];

    /* rearrange other pointers */
    for (i=1; i<=match; i++) filep[i] = tmp[i-1];

    add_files_to_menu(filetype);
}

/* .................................................................. */

void delete_from_filelist (int filetype, const char *fname)
{
    char *tmp[MAXRECENT];
    char **filep;
    int i, match = -1;

    if (filetype == 1) filep = datap;
    else if (filetype == 2) filep = sessionp;
    else if (filetype == 3) filep = scriptp;
    else return;

    /* save pointers to current order */
    for (i=0; i<MAXRECENT; i++) {
	tmp[i] = filep[i];
	if (!strcmp(filep[i], fname)) match = i;
    }

    if (match == -1) return;

    /* clear menu files list before rebuilding */
    clear_files_list(filetype, filep);

    for (i=match; i<MAXRECENT-1; i++) {
	filep[i] = tmp[i+1];
    }

    filep[MAXRECENT-1] = tmp[match];
    filep[MAXRECENT-1][0] = '\0';

    add_files_to_menu(filetype);
    /* need to save to file at this point? */
}

/* .................................................................. */

char *endbit (char *dest, char *src, int addscore)
{
    /* take last part of src filename */
    if (strrchr(src, SLASH))
	strcpy(dest, strrchr(src, SLASH) + 1);
    else
	strcpy(dest, src);

    if (addscore != 0) {
	/* then either double (1) or delete (-1) any underscores */
	char mod[MAXSTR];
	size_t i, j, n;

	n = strlen(dest);
	j = 0;
	for (i=0; i<=n; i++) {
	    if (dest[i] != '_')
		mod[j++] = dest[i];
	    else {
		if (addscore == 1) {
		    mod[j++] = '_';
		    mod[j++] = dest[i];
		} 
	    }
	}
	strcpy(dest, mod);
    }
    return dest;
}

/* .................................................................. */

#if defined(USE_GNOME)

static void printfilelist (int filetype, FILE *fp)
     /* fp is ignored */
{
    int i;
    char **filep;
    char gpath[MAXLEN];
    static char *section[] = {"recent data files",
			      "recent session files",
			      "recent script files"};

    switch (filetype) {
    case 1: filep = datap; break;
    case 2: filep = sessionp; break;
    case 3: filep = scriptp; break;
    default: return;
    }

    for (i=0; i<MAXRECENT; i++) {
	sprintf(gpath, "/gretl/%s/%d", section[filetype - 1], i);
	gnome_config_set_string(gpath, filep[i]);
    }
}

#elif defined(G_OS_WIN32)

static void printfilelist (int filetype, FILE *fp)
     /* fp is ignored */
{
    int i;
    char **filep;
    char rpath[MAXLEN];
    static char *section[] = {"recent data files",
			      "recent session files",
			      "recent script files"};

    switch (filetype) {
    case 1: filep = datap; break;
    case 2: filep = sessionp; break;
    case 3: filep = scriptp; break;
    default: return;
    }

    for (i=0; i<MAXRECENT; i++) {
	sprintf(rpath, "%s\\%d", section[filetype - 1], i);
	write_reg_val(HKEY_CURRENT_USER, rpath, filep[i]);
    }
}

#else /* "plain" version follows */

static void printfilelist (int filetype, FILE *fp)
{
    int i;
    char **filep;

    if (filetype == 1) {
	fprintf(fp, "recent data files:\n");
	filep = datap;
    } else if (filetype == 2) {
	fprintf(fp, "recent session files:\n");
	filep = sessionp;
    } else if (filetype == 3) {
	fprintf(fp, "recent script files:\n");
	filep = scriptp;
    } else 
	return;

    for (i=0; i<MAXRECENT; i++) {
	if (filep[i][0]) 
	    fprintf(fp, "%s\n", filep[i]);
	else break;
    }
}

#endif 

/* .................................................................. */

void add_files_to_menu (int filetype)
{
    int i;
    char **filep, tmp[MAXSTR];
    void (*callfunc)();
    GtkItemFactoryEntry fileitem;
    GtkWidget *w;
    gchar *msep[] = {_("/File/Open data/sep"),
		     _("/Session/sep"),
		     _("/File/Open command file/sep")};
    gchar *mpath[] = {_("/File/_Open data"),
		     _("/Session"),
		     _("/File/Open command file")};

    fileitem.path = NULL;

    if (filetype == 1) {
	callfunc = set_data_from_filelist;
	filep = datap;
    } else if (filetype == 2) {
	callfunc = set_session_from_filelist;
	filep = sessionp;
    } else if (filetype == 3) {
	callfunc = set_script_from_filelist;
	filep = scriptp;
    }
    else
	return;

    /* See if there are any files to add */
    if (filep[0][0] == '\0') return;
    else {
	gchar *itemtype = "<Separator>";
	GtkWidget *w;

	/* is a separator already in place? */
	w = gtk_item_factory_get_widget(mdata->ifac, msep[filetype - 1]);
	if (w == NULL) {
	    fileitem.path = mymalloc(80);
	    strcpy(fileitem.path, mpath[filetype - 1]);
	    strcat(fileitem.path, "/sep");
	    fileitem.accelerator = NULL;
	    fileitem.callback = NULL;
	    fileitem.callback_action = 0;
	    fileitem.item_type = itemtype;
	    gtk_item_factory_create_item(mdata->ifac, &fileitem, NULL, 1);
	}
    }

    /* put the files under the menu separator */
    for (i=0; i<MAXRECENT; i++) {
	if (filep[i][0]) {
	    if (fileitem.path == NULL)
		fileitem.path = mymalloc(80);
	    fileitem.accelerator = NULL;
	    fileitem.callback_action = i; 
	    fileitem.item_type = NULL;
	    sprintf(fileitem.path, "%s/%d. %s", mpath[filetype - 1],
		    i+1, endbit(tmp, filep[i], 1));
	    fileitem.callback = callfunc; 
	    gtk_item_factory_create_item(mdata->ifac, &fileitem, NULL, 1);
	    w = gtk_item_factory_get_widget_by_action(mdata->ifac, i);
	    if (w != NULL)
		gtk_tooltips_set_tip(gretl_tips, w, filep[i], NULL);
	} else break;
    }
    free(fileitem.path);
}

/* .................................................................. */

#ifndef G_OS_WIN32
# include <dlfcn.h>
#endif

int gui_open_plugin (const char *plugin, void **handle)
{
    char pluginpath[MAXLEN];

#ifdef G_OS_WIN32
    sprintf(pluginpath, "%s\\%s.dll", paths.gretldir, plugin);
    *handle = LoadLibrary(pluginpath);
    if (*handle == NULL) {
	sprintf(errtext, _("Couldn't load plugin %s"), pluginpath);
	errbox(errtext);
	return 1;
    }
#else
    sprintf(pluginpath, "%splugins/%s.so", paths.gretldir, plugin);
    *handle = dlopen(pluginpath, RTLD_LAZY);
    if (*handle == NULL) {
	sprintf(errtext, _("Failed to load plugin: %s\n"), pluginpath);
	errbox(errtext);
	return 1;
    } 
#endif 
    return 0;
}

/* .................................................................. */

void get_default_dir (char *s)
{
    char *test = NULL;

    if (usecwd) {
	test = getcwd(s, MAXLEN);
	if (test == NULL) 
	    strcpy(s, paths.userdir);
	else
	    strcat(s, SLASHSTR);
    }
    else
	strcpy(s, paths.userdir);    
}
