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

/* fileselect.c for gretl -- use the gtk file chooser under X11,
   the native MS file selector under MS Windows */

#include "gretl.h"
#include "gpt_control.h"
#include "session.h"
#include "textbuf.h"
#include "textutil.h"
#include "filelists.h"
#include "fileselect.h"
#include "fnsave.h"
#include "database.h"
#include "bootstrap.h"

#include <sys/stat.h>
#include <unistd.h>

#ifdef G_OS_WIN32
# include <gdk/gdkwin32.h>
#endif

#define IS_DAT_ACTION(i) (i == SAVE_DATA || \
                          i == SAVE_DATA_AS || \
                          i == OPEN_DATA)

#define OPEN_DATA_ACTION(i)  (i == OPEN_DATA || \
                              i == OPEN_CSV || \
                              i == OPEN_ASCII || \
                              i == OPEN_OCTAVE || \
                              i == OPEN_GNUMERIC || \
	                      i == OPEN_XLS || \
                              i == OPEN_WF1 || \
                              i == OPEN_DTA || \
                              i == OPEN_JMULTI || \
                              i == OPEN_ODS)

#define APPEND_DATA_ACTION(i) (i == APPEND_DATA || \
                               i == APPEND_CSV || \
                               i == APPEND_OCTAVE || \
                               i == APPEND_GNUMERIC || \
                               i == APPEND_XLS || \
                               i == APPEND_ASCII || \
                               i == APPEND_WF1 || \
                               i == APPEND_DTA || \
                               i == APPEND_JMULTI || \
                               i == APPEND_ODS)

#define SAVE_GRAPH_ACTION(i) (i == SAVE_GNUPLOT)

#define EXPORT_ACTION(a,s) ((a == EXPORT_OCTAVE || \
                             a == EXPORT_R || \
                             a == EXPORT_CSV || \
                             a == EXPORT_DAT || \
                             a == EXPORT_JM) && s != FSEL_DATA_PRN)

#define GDT_ACTION(i) (i == SAVE_DATA || \
                       i == SAVE_DATA_AS || \
                       i == SAVE_BOOT_DATA || \
                       i == OPEN_DATA || \
                       i == APPEND_DATA)

#define SET_DIR_ACTION(i) (i == SET_DIR || i == SET_WDIR)

struct extmap {
    int action;
    char *ext;
};

static struct extmap action_map[] = {
    { SAVE_DBDATA,       ".bin" },
    { SAVE_SCRIPT,       ".inp" },
    { SAVE_FUNCTIONS_AS, ".inp" },
    { SAVE_CONSOLE,      ".inp" },
    { SAVE_SESSION,      ".gretl" },
    { SAVE_GP_CMDS,      ".plt" },
    { SAVE_R_CMDS,       ".R" },
    { SAVE_FUNCTIONS,    ".gfn" },
    { EXPORT_CSV,        ".csv" },
    { EXPORT_R,          ".R" },
    { OPEN_OCTAVE,       ".m" },
    { APPEND_OCTAVE,     ".m" },
    { EXPORT_OCTAVE,     ".m" },
    { EXPORT_DAT,        ".dat" },
    { SAVE_OUTPUT,       ".txt" },
    { SAVE_TEX,          ".tex" },
    { SAVE_RTF,          ".rtf" },
    { SAVE_TEXT,         ".txt" },
    { OPEN_SCRIPT,       ".inp" },
    { OPEN_SESSION,      ".gretl" },
    { OPEN_CSV,          ".csv" },
    { APPEND_CSV,        ".csv" },
    { OPEN_ASCII,        ".txt" },
    { APPEND_ASCII,      ".txt" },
    { OPEN_GNUMERIC,     ".gnumeric" },
    { APPEND_GNUMERIC,   ".gnumeric" },
    { OPEN_XLS,          ".xls" },
    { APPEND_XLS,        ".xls" },
    { OPEN_WF1,          ".wf1" },
    { APPEND_WF1,        ".wf1" },
    { OPEN_DTA,          ".dta" },
    { APPEND_DTA,        ".dta" },
    { OPEN_JMULTI,       ".dat" },
    { APPEND_JMULTI,     ".dat" },
    { OPEN_ODS,          ".ods" },
    { APPEND_ODS,        ".ods" },
    { OPEN_RATS_DB,      ".rat" },
    { OPEN_PCGIVE_DB,    ".bn7" },
    { FILE_OP_MAX,       NULL }
};

static gretlopt save_action_to_opt (int action, gpointer p)
{
    gretlopt opt = OPT_NONE;

    switch (action) {
    case SAVE_DATA:
    case SAVE_DATA_AS:
    case SAVE_BOOT_DATA: opt = OPT_Z; break;
    case SAVE_DBDATA:    opt = OPT_D; break;
    case EXPORT_OCTAVE:  opt = OPT_M; break;
    case EXPORT_R:       opt = OPT_R; break;
    case EXPORT_CSV:     opt = OPT_C; break;
    case EXPORT_DAT:     opt = OPT_G; break; /* PcGive */
    case EXPORT_JM:      opt = OPT_J; break; /* JMulti */
    default: break;
    }

    if (p != NULL) {
	opt |= GPOINTER_TO_INT(p);
    }

    return opt;
}

static const char *get_gp_ext (int ttype)
{
    if (ttype == GP_TERM_EPS)      return ".eps";
    else if (ttype == GP_TERM_PDF) return ".pdf";
    else if (ttype == GP_TERM_FIG) return ".fig";
    else if (ttype == GP_TERM_TEX) return ".tex";
    else if (ttype == GP_TERM_PNG) return ".png";
    else if (ttype == GP_TERM_EMF) return ".emf";
    else if (ttype == GP_TERM_SVG) return ".svg";
    else if (ttype == GP_TERM_PLT) return ".plt";
    else return "*";
}

static int dat_ext (const char *str, int showerr)
{
    const char *suff;
    int err = 0;

    if (str == NULL) {
	return 0;
    }

    suff = strrchr(str, '.');

    if (suff != NULL && !strcmp(suff, ".gdt")) {
	if (showerr) {
	    errbox(_("The suffix you selected should be used\n"
		   "only for gretl datafiles"));
	}
	err = 1;
    }

    return err;
}

static const char *get_ext (int action, gpointer data)
{
    const char *s = NULL;

    if (GDT_ACTION(action)) {
	return ".gdt";
    } else if (action == SAVE_GNUPLOT) {
	int ttype = gp_term_code(data);

	s = get_gp_ext(ttype);
    } else {
	size_t i;

	for (i=0; i < sizeof action_map / sizeof *action_map; i++) {
	    if (action == action_map[i].action) {
		s = action_map[i].ext;
		break;
	    }
	}
    }

    return s;
}

static int isdir (const char *path)
{
    struct stat buf;

    return (stat(path, &buf) == 0 && S_ISDIR(buf.st_mode)); 
}

static int check_maybe_add_ext (char *fname, int action, gpointer data)
{
    const char *ext = NULL;

    if (fname == NULL) {
	return 1;
    }

    /* don't mess if the fname is really a dir */
    if (isdir(fname)) {
	return !SET_DIR_ACTION(action);
    }

    /* don't mess with a filename that already has an extension */
    if (dotpos(fname) != strlen(fname)) {
	return 0;
    }
    
    /* otherwise add an appropriate extension */
    ext = get_ext(action, data);
    if (ext != NULL && strlen(ext) > 1) {
	strcat(fname, ext);
    }

    return 0;
}

static void script_window_update (windata_t *vwin, const char *fname)
{
    gchar *trfname, *title;
    const char *p = strrchr(fname, SLASH);

    /* ensure UTF-8 filename for display */
    if (p != NULL) {
	trfname = my_filename_to_utf8(p + 1);
    } else {
	trfname = my_filename_to_utf8(fname);
    }

    /* update the window title */
    title = g_strdup_printf("gretl: %s", trfname);
    gtk_window_set_title(GTK_WINDOW(vwin->main), title);
    g_free(trfname);
    g_free(title);

    /* and update internal filename record */
    strcpy(vwin->fname, fname);

    if (vwin->role == VIEW_LOG || vwin->role == VIEW_SCRIPT ||
	vwin->role == VIEW_FUNC_CODE) {
	/* change role of window for editing */
	vwin->role = EDIT_SCRIPT;
    }

    mark_vwin_content_saved(vwin);

    /* make the window editable */
    if (!gtk_text_view_get_editable(GTK_TEXT_VIEW(vwin->text))) {
	view_window_set_editable(vwin);
    }
}

static void 
save_editable_content (int action, const char *fname, windata_t *vwin)
{
    FILE *fp;
    gchar *buf;
#ifdef ENABLE_NLS
    gchar *trbuf;
#endif

    buf = textview_get_text(vwin->text);
    if (buf == NULL) {
	errbox("Couldn't retrieve buffer");
	return;
    }

    fp = gretl_fopen(fname, "w");
    if (fp == NULL) {
	file_write_errbox(fname);
	g_free(buf);
	return;
    }

#ifdef ENABLE_NLS
    trbuf = my_locale_from_utf8(buf);
    if (trbuf != NULL) {
	system_print_buf(trbuf, fp);
	g_free(trbuf);
    }
#else
    system_print_buf(buf, fp);
#endif

    g_free(buf);
    fclose(fp);
    
    if (action == SAVE_SCRIPT) {
	strcpy(scriptfile, fname);
	mkfilelist(FILE_LIST_SCRIPT, scriptfile);
	script_window_update(vwin, fname);
    } else if (action == SAVE_GP_CMDS || action == SAVE_R_CMDS) {
	script_window_update(vwin, fname);
    } 
}

static void set_startdir (char *startdir, int action)
{
    get_default_dir(startdir, action);

#ifndef G_OS_WIN32
    if (startdir[strlen(startdir) - 1] != '/') {
	strcat(startdir, "/");
    }
#endif
}

static void filesel_save_prn_buffer (PRN *prn, const char *fname)
{
    FILE *fp = gretl_fopen(fname, "w");

    if (fp == NULL) {
	file_write_errbox(fname);
    } else {
	const char *buf = gretl_print_get_buffer(prn);

	fputs(buf, fp);
	fclose(fp);
    }
}

static void filesel_open_script (const char *fname)
{
    if (has_suffix(fname, ".R")) {
	view_file(fname, 1, 0, 78, 370, EDIT_R);
    } else if (has_suffix(fname, ".plt")) {
	view_file(fname, 1, 0, 78, 370, EDIT_GP);	
    } else {
	strcpy(tryfile, fname);
	if (view_file(tryfile, 1, 0, 78, 370, EDIT_SCRIPT) != NULL) {
	    strcpy(scriptfile, tryfile);
	    mkfilelist(FILE_LIST_SCRIPT, scriptfile);
	    set_currdir_from_filename(scriptfile);
	}
    }
}

static void filesel_open_session (const char *fname)
{
    strcpy(tryfile, fname);

    if (is_session_file(fname)) {
	verify_open_session();
    } else {
	windata_t *vwin;

	if (has_system_prefix(tryfile, &paths, SCRIPT_SEARCH)) {
	    vwin = view_file(tryfile, 0, 0, 78, 370, VIEW_SCRIPT);
	} else {
	    vwin = view_file(tryfile, 1, 0, 78, 370, EDIT_SCRIPT);
	}

	if (vwin != NULL) {
	    strcpy(scriptfile, tryfile);
	}
    }
}

static char *suggested_savename (const char *fname)
{
    const char *ss = strrchr(fname, SLASH);
    char *s, *sfx;

    if (ss == NULL) {
	s = g_strdup(fname);
    } else {
	s = g_strdup(ss + 1);
    }

    sfx = strrchr(s, '.');

    if (sfx != NULL && (strlen(sfx) == 4 || !strcmp(sfx, ".gnumeric"))) {
	if (strcmp(sfx, ".gdt")) {
	    strcpy(sfx, ".gdt");
	}
    }

    return s;
}

static char *suggested_exportname (const char *fname, int action)
{
    const char *ss = strrchr(fname, SLASH);
    char *s, *sfx;

    if (ss == NULL) {
	s = g_strdup(fname);
    } else {
	s = g_strdup(ss + 1);
    }

    sfx = strrchr(s, '.');

    if (sfx != NULL && (strlen(sfx) == 4 || !strcmp(sfx, ".gnumeric"))) {
	const char *test;

	switch(action) {
	case EXPORT_OCTAVE:
	    test = ".m";
	    break;
	case EXPORT_R:
	    test = ".R";
	    break;
	case EXPORT_CSV:
	    test = ".csv";
	    break;
	case EXPORT_DAT:
	case EXPORT_JM:
	    test = ".dat";
	    break;
	default:
	    test = NULL;
	    break;
	}

	if (test != NULL && strcmp(test, sfx)) {
	    strcpy(sfx, test);
	}
    }

    return s;
}

static void bootstrap_save_callback (const char *fname)
{
    int err = bootstrap_save_data(fname);

    if (err) {
	gui_errmsg(err);
    } 
}

static void
file_selector_process_result (const char *in_fname, int action, FselDataSrc src,
			      gpointer data)
{
    char fname[FILENAME_MAX];

    *fname = 0;
    strncat(fname, in_fname, FILENAME_MAX - 1);

    if (action < END_OPEN) {
	FILE *fp = gretl_fopen(fname, "r");

	if (fp == NULL) {
	    file_read_errbox(fname);
	    return;
	} else {
	    fclose(fp);
	}
    } 

    if (OPEN_DATA_ACTION(action)) {
	strcpy(tryfile, fname);
	verify_open_data(NULL, action);
    } else if (APPEND_DATA_ACTION(action)) {
	strcpy(tryfile, fname);
	do_open_data(NULL, action);
    } else if (action == OPEN_SCRIPT) {
	if (src == FSEL_DATA_FNPKG) {
	    fnsave_set_script(fname, data);
	} else {
	    filesel_open_script(fname);
	}
    } else if (action == OPEN_SESSION) {
	filesel_open_session(fname);
    } else if (action == OPEN_MARKERS) {
	do_add_markers(fname);
    } else if (action == OPEN_RATS_DB) {
	open_rats_window(fname);
    } else if (action == OPEN_PCGIVE_DB) {
	open_bn7_window(fname);
    }

    if (action < END_OPEN) {
	return;
    }

    /* now for the save/export options */

    if (action > SAVE_DBDATA && action != EXPORT_DAT && dat_ext(fname, 1)) { 
	return;
    }

    if (check_maybe_add_ext(fname, action, data)) {
	return;
    }

    if (action == SAVE_TEX) {
	if (src == FSEL_DATA_PRN) {
	    save_latex((PRN *) data, fname);
	} else {
	    save_latex(NULL, fname);
	}
    } else if (src == FSEL_DATA_PRN) {
	filesel_save_prn_buffer((PRN *) data, fname);
    } else if (SAVE_DATA_ACTION(action)) {
	do_store(fname, save_action_to_opt(action, data));
    } else if (action == SAVE_GNUPLOT) {
	save_graph_to_file(data, fname);
    } else if (action == SAVE_SESSION) {
	save_session(fname);
    } else if (action == SAVE_FUNCTIONS) {
	save_user_functions(fname, data);
    } else if (action == SAVE_FUNCTIONS_AS) {
	save_user_functions_as_script(fname, data);
    } else if (action == SAVE_BOOT_DATA) {
	bootstrap_save_callback(fname);
    } else if (action == SET_PROG || action == SET_DIR) {
	set_path_callback(data, fname);
    } else if (action == SET_WDIR) {
	set_working_dir_callback(data, fname);
    } else {
	windata_t *vwin = (windata_t *) data;

	save_editable_content(action, fname, vwin);
    }
}

#ifdef G_OS_WIN32 

/* ........................................................... */

          /* MS Windows version of file selection code */

/* ........................................................... */


#include <windows.h>
#include <shlobj.h>

struct winfilter {
    const char *descrip;
    const char *pat;
} winfilter;

struct win32_filtermap {
    int action;
    struct winfilter filter;
};

static struct winfilter get_gp_filter (int ttype)
{
    static struct winfilter gpfilt[] = {
	{ N_("postscript files (*.eps)"), "*.eps" },
	{ N_("PDF files (*.pdf)"), "*.pdf" },
	{ N_("xfig files (*.fig)"), "*.fig" },
	{ N_("LaTeX files (*.tex)"), "*.tex" },
	{ N_("PNG files (*.png)"), "*.png" },
	{ N_("Windows metafiles (*.emf)"), "*.emf" },
	{ N_("gnuplot files (*.plt)"), "*.plt" },
	{ N_("all files (*.*)"), "*" }
    };

    if (ttype == GP_TERM_EPS) 
	return gpfilt[0];
    else if (ttype == GP_TERM_PDF) 
	return gpfilt[1];
    else if (ttype == GP_TERM_FIG) 
	return gpfilt[2];
    else if (ttype == GP_TERM_TEX) 
	return gpfilt[3];
    else if (ttype == GP_TERM_PNG) 
	return gpfilt[4];
    else if (ttype == GP_TERM_EMF) 
	return gpfilt[5];
    else if (ttype == GP_TERM_PLT)
	return gpfilt[6];
    else
	return gpfilt[7];
}

static struct winfilter get_filter (int action, gpointer data)
{
    static struct win32_filtermap map[] = {
	{ SAVE_DATA,        { N_("gretl data files (*.gdt)"), "*.gdt" }},
	{ SAVE_DBDATA,      { N_("gretl database files (*.bin)"), "*.bin" }},
	{ SAVE_SCRIPT,      { N_("gretl script files (*.inp)"), "*.inp" }},
	{ SAVE_FUNCTIONS_AS,{ N_("gretl script files (*.inp)"), "*.inp" }},
	{ SAVE_CONSOLE,     { N_("gretl command files (*.inp)"), "*.inp" }},
	{ SAVE_SESSION,     { N_("session files (*.gretl)"), "*.gretl" }},
	{ SAVE_GP_CMDS,     { N_("gnuplot files (*.plt)"), "*.plt" }},
	{ SAVE_R_CMDS,      { N_("GNU R files (*.R)"), "*.R" }},
	{ SAVE_FUNCTIONS,   { N_("gretl function files (*.gfn)"), "*.gfn" }},
	{ EXPORT_CSV,       { N_("CSV files (*.csv)"), "*.csv" }},
	{ EXPORT_R,         { N_("GNU R files (*.R)"), "*.R" }},
	{ EXPORT_OCTAVE,    { N_("GNU Octave files (*.m)"), "*.m" }},
	{ OPEN_OCTAVE,      { N_("GNU Octave files (*.m)"), "*.m" }},
	{ APPEND_OCTAVE,    { N_("GNU Octave files (*.m)"), "*.m" }},
	{ EXPORT_DAT,       { N_("PcGive files (*.dat)"), "*.dat" }},
	{ EXPORT_JM,        { N_("JMulti files (*.dat)"), "*.dat" }},
	{ SAVE_OUTPUT,      { N_("text files (*.txt)"), "*.txt" }},
	{ SAVE_TEX,         { N_("TeX files (*.tex)"), "*.tex" }},
	{ SAVE_RTF,         { N_("RTF files (*.rtf)"), "*.rtf" }},
	{ SAVE_TEXT,        { N_("ASCII files (*.txt)"), "*.txt" }},
	{ OPEN_SCRIPT,      { N_("gretl script files (*.inp)"), "*.inp" }},
	{ OPEN_SESSION,     { N_("session files (*.gretl)"), "*.gretl" }},
	{ OPEN_CSV,         { N_("CSV files (*.csv)"), "*.csv" }},
	{ APPEND_CSV,       { N_("CSV files (*.csv)"), "*.csv" }},
	{ OPEN_ASCII,       { N_("ASCII files (*.txt)"), "*.txt" }},
	{ APPEND_ASCII,     { N_("ASCII files (*.txt)"), "*.txt" }},
	{ OPEN_GNUMERIC,    { N_("Gnumeric files (*.gnumeric)"), "*.gnumeric" }},
	{ APPEND_GNUMERIC,  { N_("Gnumeric files (*.gnumeric)"), "*.gnumeric" }},
	{ OPEN_XLS,         { N_("Excel files (*.xls)"), "*.xls" }},
	{ APPEND_XLS,       { N_("Excel files (*.xls)"), "*.xls" }},
	{ OPEN_WF1,         { N_("Eviews workfiles (*.wf1)"), "*.wf1" }},
	{ APPEND_WF1,       { N_("Eviews workfiles (*.wf1)"), "*.wf1" }},
	{ OPEN_DTA,         { N_("Stata files (*.dta)"), "*.dta" }},
	{ APPEND_DTA,       { N_("Stata files (*.dta)"), "*.dta" }},
	{ OPEN_JMULTI,      { N_("JMulTi files (*.dat)"), "*.dat" }},
	{ APPEND_JMULTI,    { N_("JMulTi files (*.dat)"), "*.dat" }},
	{ OPEN_ODS,         { N_("Open Document Spreadsheet (*.ods)"), "*.ods" }},
	{ APPEND_ODS,       { N_("Open Document Spreadsheet (*.ods)"), "*.ods" }},
	{ OPEN_RATS_DB,     { N_("RATS databases (*.rat)"), "*.rat" }},
	{ OPEN_PCGIVE_DB,   { N_("PcGive data files (*.bn7)"), "*.bn7" }},
	{ SET_PROG,         { N_("program files (*.exe)"), "*.exe" }}
    };
    static struct winfilter default_filter = {
	N_("all files (*.*)"), "*" 
    };
    struct winfilter filt;
    int i;

    if (action == SAVE_GNUPLOT) {
	int ttype = gp_term_code(data);

	filt = get_gp_filter(ttype);
    } else {
	filt = default_filter;

	for (i=0; i< sizeof map / sizeof *map; i++) {
	    if (action == map[i].action) {
		filt = map[i].filter;
		break;
	    }
	}
    }

    return filt;
}

static char *make_winfilter (int action, FselDataSrc src, gpointer data)
{
    struct winfilter filter;
    char *ret, *p;

    ret = calloc(256, 1);
    if (ret == NULL) {
	return NULL;
    }

    p = ret;

    if (GDT_ACTION(action)) {
	filter = get_filter(SAVE_DATA, data);
    } else {
	filter = get_filter(action, data);
    }

    strcpy(p, I_(filter.descrip));
    p += strlen(p) + 1;
    strcpy(p, filter.pat);

    if (action == OPEN_SCRIPT && src != FSEL_DATA_FNPKG) {
	p += strlen(p) + 1;
	strcpy(p, I_("gnuplot files (*.plt)"));
	p += strlen(p) + 1;
	strcpy(p, "*.plt");
	p += strlen(p) + 1;
	strcpy(p, I_("GNU R files (*.*)"));
	p += strlen(p) + 1;
	strcpy(p, "*.R");
    }	

    if (strncmp(filter.descrip, "all", 3)) {
	p += strlen(p) + 1;
	strcpy(p, I_("all files (*.*)"));
	p += strlen(p) + 1;
	strcpy(p, "*");
    }

    return ret;
}

static int select_dirname (char *fname, char *trmsg)
{
    BROWSEINFO bi;
    LPITEMIDLIST pidl;
    char dirname[MAX_PATH];
    int ret = 1;

    CoInitialize(NULL);

    bi.hwndOwner = NULL;
    bi.pidlRoot = NULL; /* FIXME? */
    bi.pszDisplayName = dirname;
    bi.lpszTitle = trmsg;
    bi.ulFlags = BIF_USENEWUI;
    bi.lpfn = NULL;
    bi.lParam = 0;
    bi.iImage = 0;   

    pidl = SHBrowseForFolder(&bi);
    if (pidl == NULL) {
	ret = 0;
    } else {
	SHGetPathFromIDList(pidl, fname);
	CoTaskMemFree(pidl);
    }

    return ret;
}

static void win32_file_selector (const char *msg, int action, FselDataSrc src, 
				 gpointer data, GtkWidget *parent) 
{
    OPENFILENAME of;
    int retval;
    char fname[MAXLEN], endname[64], startdir[MAXLEN];
    char *filter = NULL;
    gchar *trmsg = NULL;

    *fname = '\0';
    *endname = '\0';

    set_startdir(startdir, action);

    /* special cases */
    if (action == SAVE_DATA && *paths.datfile != '\0') {
	char *savename = suggested_savename(paths.datfile);

	strcpy(fname, savename);
	g_free(savename);
	if (!(data_status & BOOK_DATA)) {
	    get_base(startdir, paths.datfile, SLASH);
	}
    } else if (EXPORT_ACTION(action, src) && *paths.datfile != '\0') {
	char *savename = suggested_exportname(paths.datfile, action);

	strcpy(fname, savename);
	g_free(savename);
	get_base(startdir, paths.datfile, SLASH);
    } else if (action == SET_PROG) {
	char *strvar = (char *) data;

	if (strvar != NULL && *strvar != '\0') {
	    if (get_base(startdir, strvar, SLASH)) {
		strcpy(fname, strvar + slashpos(strvar) + 1);
	    } 
	}
    } else if (action == SAVE_FUNCTIONS || action == SAVE_FUNCTIONS_AS) {
	get_default_package_name(fname, data, action);
    }

    if (doing_nls()) {
	trmsg = my_locale_from_utf8(msg);
    } else {
	trmsg = g_strdup(msg);
    }

    if (SET_DIR_ACTION(action)) {
	retval = select_dirname(fname, trmsg);
    } else {
	/* initialize file dialog info struct */
	memset(&of, 0, sizeof of);
#ifdef OPENFILENAME_SIZE_VERSION_400
	of.lStructSize = OPENFILENAME_SIZE_VERSION_400;
#else
	of.lStructSize = sizeof of;
#endif
	if (parent != NULL) {
	    of.hwndOwner = GDK_WINDOW_HWND(parent->window);
	} else {
	    of.hwndOwner = NULL;
	}
	filter = make_winfilter(action, src, data);
	of.lpstrFilter = filter;
	of.lpstrCustomFilter = NULL;
	of.nFilterIndex = 1;
	of.lpstrFile = fname;
	of.nMaxFile = sizeof fname;
	of.lpstrFileTitle = endname;
	of.nMaxFileTitle = sizeof endname;
	of.lpstrInitialDir = startdir;
	of.lpstrTitle = trmsg;
	of.lpstrDefExt = NULL;
	of.Flags = OFN_HIDEREADONLY;

	if (action < END_OPEN || action == SET_PROG) {
	    retval = GetOpenFileName(&of);
	} else {
	    /* a file save action */
	    retval = GetSaveFileName(&of);
	}
    }

    free(filter);
    g_free(trmsg);

    if (!retval) {
	if (CommDlgExtendedError()) {
	    errbox(_("File dialog box error"));
	}
	return;
    }

    file_selector_process_result(fname, action, src, data);
}

#else /* End of MS Windows file selection code, start GTK */

static char *get_filter_suffix (int action, gpointer data, char *suffix)
{
    
    const char *ext = get_ext(action, data);

    if (ext == NULL) { 
	strcpy(suffix, "*");
    } else {
	sprintf(suffix, "*%s", ext);
    }

    return suffix;
}

static GtkFileFilter *get_file_filter (int action, gpointer data)
{
    GtkFileFilter *filter;
    char suffix[16];
    
    filter = gtk_file_filter_new();
    get_filter_suffix(action, data, suffix);
    gtk_file_filter_add_pattern(filter, suffix);

    return filter;
}

static void gtk_file_selector (const char *msg, int action, FselDataSrc src, 
			       gpointer data, GtkWidget *parent) 
{
    GtkWidget *filesel;
    char startdir[MAXLEN];
    GtkFileFilter *filter;
    GtkFileChooserAction fa;
    const gchar *okstr;
    gint response;

    set_startdir(startdir, action);

    if (SET_DIR_ACTION(action)) {
	fa = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
	okstr = GTK_STOCK_OK;
    } else if (action == SET_PROG) {
	fa = GTK_FILE_CHOOSER_ACTION_OPEN;
	okstr = GTK_STOCK_OK;
    } else if (action < END_OPEN) {
	fa = GTK_FILE_CHOOSER_ACTION_OPEN;
	okstr = GTK_STOCK_OPEN;
    } else {
	fa = GTK_FILE_CHOOSER_ACTION_SAVE;
	okstr = GTK_STOCK_SAVE;
    }

    if (parent == NULL) {
	parent = mdata->main;
    }

    filesel = gtk_file_chooser_dialog_new(msg, GTK_WINDOW(parent), fa,
					  GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					  okstr, GTK_RESPONSE_ACCEPT,
					  NULL);

    gtk_dialog_set_default_response(GTK_DIALOG(filesel), GTK_RESPONSE_ACCEPT);

    filter = get_file_filter(action, data);

    if (action == OPEN_ASCII || action == APPEND_ASCII) {
	gtk_file_filter_set_name(filter, _("ASCII files (*.txt)"));
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(filesel), filter);
    } else if (action == OPEN_SCRIPT) {
	gtk_file_filter_set_name(filter, _("gretl script files (*.inp)"));
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(filesel), filter);
    }	

    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(filesel), filter);

    if (action == OPEN_ASCII || action == APPEND_ASCII) {
	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("all files (*.*)"));
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(filesel), filter);
    } else if (action == OPEN_SCRIPT && src != FSEL_DATA_FNPKG) {
	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("GNU R files (*.R)"));
	gtk_file_filter_add_pattern(filter, "*.R");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(filesel), filter);
	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, _("gnuplot files (*.plt)"));
	gtk_file_filter_add_pattern(filter, "*.plt");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(filesel), filter);
    } 

    /* FIXME session dir */
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(filesel), startdir);

    /* special cases */

    if (action == SAVE_DATA && *paths.datfile != '\0') {
	char *savename = suggested_savename(paths.datfile);

	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(filesel), 
					  savename);
	g_free(savename);
    } else if (EXPORT_ACTION(action, src) && *paths.datfile != '\0') {
	char *savename = suggested_exportname(paths.datfile, action);

	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(filesel), 
					  savename);
	g_free(savename);
    } else if (action == SET_PROG) {
	char *strvar = (char *) data;

	if (strvar != NULL && g_path_is_absolute(strvar)) {
	    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(filesel), 
					  strvar);
	} else {
	    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(filesel), 
						"/usr/bin");
	}	    
    } else if (action == SET_DIR) {
	char *strvar = (char *) data;

	if (strvar != NULL && g_path_is_absolute(strvar)) {
	    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(filesel), 
					  strvar);
	} 
    } else if (action == SAVE_FUNCTIONS || action == SAVE_FUNCTIONS_AS) {
	char fname[MAXLEN];

	*fname = '\0';
	get_default_package_name(fname, data, action);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(filesel), 
					  fname);
    } 

    response = gtk_dialog_run(GTK_DIALOG(filesel));

    if (response == GTK_RESPONSE_ACCEPT) {
	gchar *fname;
	
	fname = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(filesel));
	file_selector_process_result(fname, action, src, data);
	g_free(fname);
    } 

    gtk_widget_destroy(filesel);
}

#endif /* end of non-MS Windows code */

void file_selector (const char *msg, int action, FselDataSrc src, gpointer data)
{
    GtkWidget *w = NULL;

    if (src == FSEL_DATA_VWIN) {
	windata_t *vwin = (windata_t *) data;

	w = vwin->main;
    }

#ifdef G_OS_WIN32
    win32_file_selector(msg, action, src, data, w);
#else
    gtk_file_selector(msg, action, src, data, w);
#endif
}

void file_selector_with_parent (const char *msg, int action, FselDataSrc src, 
				gpointer data, GtkWidget *w)
{
#ifdef G_OS_WIN32
    win32_file_selector(msg, action, src, data, w);
#else
    gtk_file_selector(msg, action, src, data, w);
#endif
}
