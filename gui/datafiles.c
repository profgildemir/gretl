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

/* datafiles.c : for gretl */

#include "gretl.h"
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

extern void open_db_clist (GtkWidget *w, gpointer data);
extern void open_remote_clist (GtkWidget *w, gpointer data);
extern void grab_remote_db (GtkWidget *w, gpointer data);
extern gint populate_dbfilelist (windata_t *ddata);

extern GtkItemFactoryEntry sample_script_items[];
extern GdkColor gray;

char pwtpath[MAXLEN];
char woolpath[MAXLEN];
static int file_sel_open = 0;

static GtkWidget *files_window (windata_t *fdata);
static GtkWidget *files_notebook (windata_t *fdata, 
				  GtkWidget **w1, GtkWidget **w2);
static int populate_notebook_filelists (windata_t *fdata, 
					GtkWidget *lbox1, 
					GtkWidget *lbox2,
					GtkWidget *notebook);
gint populate_filelist (windata_t *fdata);
void browser_open_data (GtkWidget *w, gpointer data);
void browser_open_ps (GtkWidget *w, gpointer data);

extern int jwdata;  /* settings.c */

/* ........................................................... */

static int read_ps_descriptions (windata_t *fdata)
{
    FILE *fp;
    char line[MAXLEN], fname[MAXLEN];
    gchar *row[3];
    gint i;

    if (fdata->role == PWT_PS)
	build_path(pwtpath, "ps_descriptions", fname, NULL);
    else
	build_path(paths.scriptdir, 
		   (fdata->role == GREENE_PS)? "wg_ps_descriptions" :
		   "ps_descriptions",
		   fname, NULL);

    fp = fopen(fname, "r");
    if (fp == NULL) {
	errbox(_("Couldn't open descriptions file"));
	return 1;
    }

    i = 0;
    while (fgets(line, MAXLEN - 1, fp)) {
	if (line[0] == '#') continue;
	line[MAXLEN-1] = 0;
	row[0] = strtok(line, "\"");
	(void) strtok(NULL, "\"");
	row[1] = strtok(NULL, "\"");
	(void) strtok(NULL, "\"");
	row[2] = strtok(NULL, "\"");
	gtk_clist_append(GTK_CLIST(fdata->listbox), row);
	if (i % 2) {
	    gtk_clist_set_background(GTK_CLIST(fdata->listbox), i, &gray);
	}
	i++;
    }
    fclose(fp);

    gtk_clist_select_row(GTK_CLIST (fdata->listbox), 0, 0);
    return 0;
}

/* ........................................................... */

static int read_data_descriptions (windata_t *fdata)
{
    FILE *fp;
    char line[MAXLEN], fname[MAXLEN];
    char descrip[80];
    gchar *row[2];
    gint i;

    if (fdata->role == RAMU_DATA) 
	build_path(paths.datadir, "descriptions", fname, NULL);
    else if (fdata->role == PWT_DATA)
	build_path(pwtpath, "descriptions", fname, NULL);
    else if (fdata->role == JW_DATA) {
	build_path(woolpath, "jw_descriptions", fname, NULL);
    } 
    else if (fdata->role == GREENE_DATA) {
	strcpy(fname, paths.datadir);
	append_dir(fname, "greene");
	strcat(fname, "wg_descriptions"); 
    } 

    fp = fopen(fname, "r");
    if (fp == NULL) {
	sprintf(errtext, _("Couldn't open data descriptions file\n%s"), fname);
	errbox(errtext);
	return 1;
    }

    i = 0;
    while (fgets(line, MAXLEN - 1, fp)) {
        if (line[0] == '#') continue;
        line[MAXLEN-1] = 0;
        *fname = 0;
        *descrip = 0;
        if (sscanf(line, " \"%15[^\"]\",\"%79[^\"]\"", 
                   fname, descrip) == 2) {
	    row[0] = fname;
	    row[1] = descrip;
	    gtk_clist_append(GTK_CLIST(fdata->listbox), row);
	    if (i % 2) {
		gtk_clist_set_background(GTK_CLIST(fdata->listbox), 
					 i, &gray);
	    }
	    i++;
	}
    }

    fclose(fp);
    
    gtk_clist_select_row(GTK_CLIST (fdata->listbox), 0, 0);  
    return 0;
}

/* ........................................................... */

static void get_textbook_file_path (const char *fname, char *fullname,
				    const char *suffix)
{
    FILE *fp;

    build_path(paths.datadir, fname, fullname, suffix);
    fp = fopen(fullname, "r");
    if (fp != NULL) {
	fclose(fp);
    } else {
	build_path(woolpath, fname, fullname, suffix);
    }
}

/* ........................................................... */

static void browse_header (GtkWidget *w, gpointer data)
{
    char hdrname[MAXLEN];
    windata_t *win = (windata_t *) data;
    PRN *prn;
    gchar *fname;

    gtk_clist_get_text(GTK_CLIST(win->listbox), win->active_var, 
		       0, &fname);
    
    if (win->role == PWT_DATA)
	build_path(pwtpath, fname, hdrname, ".gdt"); 
    else if (win->role == RAMU_DATA)
	build_path(paths.datadir, fname, hdrname, ".gdt");
    else if (win->role == JW_DATA)
	build_path(woolpath, fname, hdrname, ".gdt");
    else if (win->role == TEXTBOOK_DATA) 
	get_textbook_file_path(fname, hdrname, ".gdt");
    else if (win->role == GREENE_DATA) {
	strcpy(hdrname, paths.datadir);
	append_dir(hdrname, "greene");
	strcat(hdrname, fname);
	strcat(hdrname, ".gdt");
    }

    prn = gretl_print_new(GRETL_PRINT_NULL, NULL);

    prn->buf = get_xml_description(hdrname);

    if (prn->buf != NULL)
	view_buffer(prn, 80, 300, _("gretl: data header"), INFO, NULL);
    else {
	errbox(_("Failed to retrieve description of data"));
	fprintf(stderr, _("didn't get description from %s\n"), hdrname);
    }
}

/* ........................................................... */

void browser_open_data (GtkWidget *w, gpointer data)
{
    windata_t *win = (windata_t *) data;
    gchar *datname;

    gtk_clist_get_text(GTK_CLIST(win->listbox), win->active_var, 
		       0, &datname);

    if (win->role == PWT_DATA)
	build_path(pwtpath, datname, trydatfile, ".gdt");
    else if (win->role == RAMU_DATA)  
	build_path(paths.datadir, datname, trydatfile, ".gdt");
    else if (win->role == JW_DATA)
	build_path(woolpath, datname, trydatfile, ".gdt");
    else if (win->role == TEXTBOOK_DATA) 
	get_textbook_file_path(datname, trydatfile, ".gdt");
    else if (win->role == GREENE_DATA) {
	strcpy(trydatfile, paths.datadir);
	append_dir(trydatfile, "greene");
	strcat(trydatfile, datname);
	strcat(trydatfile, ".gdt");
    }

    verify_open_data(win, OPEN_DATA);
} 

/* ........................................................... */

void browser_open_ps (GtkWidget *w, gpointer data)
{
    windata_t *mydata = (windata_t *) data;
    gchar *fname;

    gtk_clist_get_text(GTK_CLIST(mydata->listbox), mydata->active_var, 
		       0, &fname);

    if (mydata->role == PWT_PS) {
	build_path(pwtpath, fname, scriptfile, ".inp");
    } else {
	build_path(paths.scriptdir, fname, scriptfile, ".inp");
    }

    gtk_widget_destroy(GTK_WIDGET(mydata->w));

    mkfilelist(3, scriptfile);

    view_file(scriptfile, 0, 0, 78, 370, VIEW_SCRIPT, sample_script_items);
} 

/* ........................................................... */

static void file_sel_ok (GtkWidget *w, gpointer data)
{
    windata_t *mydata = (windata_t *) data;

    file_sel_open = 0;
    free_windata(NULL, mydata);
}

/* ........................................................... */

static void get_local_status (char *fname, char *status, time_t remtime)
{
    char fullname[MAXLEN];
    struct stat fbuf;
    int err;

    build_path(paths.binbase, fname, fullname, NULL);
    if ((err = stat(fullname, &fbuf)) == -1) {
	if (errno == ENOENT)
	    strcpy(status, _("Not installed"));
	else
	    strcpy(status, _("Unknown: access error"));
    }
    if (!err) {
	if (difftime(remtime, fbuf.st_ctime) > 360)
	    strcpy(status, _("Not up to date"));
	else 
	    strcpy(status, _("Up to date"));
    }
}

/* ........................................................... */

static int process_line (char *line, char *fname, time_t *date)
{
    char mon[4], hrs[9];
    int day, yr;
    struct tm mytime;
    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
			    "Jul","Aug","Sep","Oct","Nov","Dec"};
    int i;

    if (sscanf(line, "%*s%*s%3s%2d%8s%4d%16s", 
	       mon, &day, hrs, &yr, fname) != 5)
	return 1;
    hrs[2] = 0;

    mytime.tm_sec = 0;
    mytime.tm_min = 0;   
    mytime.tm_wday = 0;   
    mytime.tm_yday = 0;   
    mytime.tm_isdst = -1; 
    mytime.tm_hour = atoi(hrs);
    mytime.tm_year = yr - 1900;
    mytime.tm_mday = day;
    mytime.tm_mon = 0;
    for (i=0; i<12; i++)
	if (strcmp(mon, months[i]) == 0)
	    mytime.tm_mon = i;

    *date = mktime(&mytime);
    return 0;
}

/* ........................................................... */

void trim_ext (char *fname)
{
    size_t i, n = strlen(fname);

    for (i=n-1; i>0; i--)
	if (fname[i] == '.') fname[i] = 0;
}

/* ........................................................... */

static gint populate_remote_dblist (windata_t *ddata)
{
    int err;
    char *getbuf;
    char fname[16], line[80], errbuf[80], status[20];
    gchar *row[3];
    gint i;
    time_t remtime;

    if ((getbuf = mymalloc(8192)) == NULL)
	return 1;
    clear(getbuf, 8192);

    errbuf[0] = '\0';
    err = retrieve_url(LIST_DBS, NULL, NULL, 0, &getbuf, errbuf);

    if (err) {
        if (strlen(errbuf)) {
	    if (errbuf[strlen(errbuf)-1] == '\n')
		errbuf[strlen(errbuf)-1] = '\0';
	    errbox(errbuf);
	} else 
	    errbox(_("Error retrieving data from server"));
	free(getbuf);
	return err;
    }
    
    gtk_clist_clear(GTK_CLIST(ddata->listbox));
    gtk_clist_freeze(GTK_CLIST(ddata->listbox));

    i = 0;
    getbufline(NULL, NULL, 1);

    while (getbufline(getbuf, line, 0)) {
	if (strstr(line, "idx")) continue;
	if (process_line(line, fname, &remtime))
	    continue;
	get_local_status(fname, status, remtime);
	trim_ext(fname);
	row[0] = fname;
	if (!getbufline(getbuf, line, 0)) 
	    row[1] = NULL;
	else
	    row[1] = line + 2;
	row[2] = status;
	gtk_clist_append(GTK_CLIST(ddata->listbox), row);
	if (i % 2) {
	    gtk_clist_set_background(GTK_CLIST(ddata->listbox), i, &gray);
	}
	i++;
    }

    gtk_clist_thaw(GTK_CLIST(ddata->listbox));

    free(getbuf);
    if (i > 0) {
	gtk_clist_select_row(GTK_CLIST(ddata->listbox), 0, 0);
    } else {
	errbox(_("No database files found"));
    }
    return 0;
}

/* ........................................................... */

static void build_datafiles_popup (windata_t *win)
{
    if (win->popup != NULL) return;

    win->popup = gtk_menu_new();

    add_popup_item(_("Info"), win->popup, 
		   GTK_SIGNAL_FUNC(browse_header), 
		   win);
    add_popup_item(_("Open"), win->popup, 
		   GTK_SIGNAL_FUNC(browser_open_data), 
		   win);
}

/* ........................................................... */

void display_files (gpointer data, guint code, GtkWidget *widget)
{
    GtkWidget *filebox, *openbutton, *midbutton, *closebutton;
    GtkWidget *lbox1, *lbox2;
    GtkWidget *main_vbox, *button_box;
    windata_t *fdata;
    int err = 0;
    void (*browse_func)() = NULL;

    if (file_sel_open) return;
    if ((fdata = mymalloc(sizeof *fdata)) == NULL)
	return;
    windata_init(fdata);

    file_sel_open = 1;
    fdata->w = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_signal_connect (GTK_OBJECT (fdata->w), "destroy",
			GTK_SIGNAL_FUNC (file_sel_ok),
			fdata);

    switch (code) {
    case RAMU_PS:
    case GREENE_PS:
    case PWT_PS:
	gtk_window_set_title(GTK_WINDOW (fdata->w), 
			     _("gretl: practice files"));
	browse_func = browser_open_ps;
	break;
    case RAMU_DATA:
    case GREENE_DATA:
    case JW_DATA:
    case PWT_DATA:
    case TEXTBOOK_DATA:
	gtk_window_set_title(GTK_WINDOW (fdata->w), 
			     _("gretl: data files"));
	browse_func = browser_open_data;
	break;
    case NATIVE_DB:
    case RATS_DB:
	gtk_window_set_title(GTK_WINDOW (fdata->w), 
			     _("gretl: database files"));
	browse_func = open_db_clist;
	break;
    case REMOTE_DB:
	gtk_window_set_title(GTK_WINDOW (fdata->w), 
			     _("gretl: databases on server"));
	browse_func = open_remote_clist;
	break;
    }

    /* set up grids */
    main_vbox = gtk_vbox_new (FALSE, 5);
    gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 10);
    gtk_container_add (GTK_CONTAINER (fdata->w), main_vbox);

    fdata->role = code;

    if (code == TEXTBOOK_DATA) {
	filebox = files_notebook(fdata, &lbox1, &lbox2);
    } else {
	filebox = files_window(fdata);
    }

    gtk_box_pack_start(GTK_BOX(main_vbox), filebox, TRUE, TRUE, 0);

    /* popup menu? */
    if (code == RAMU_DATA || code == GREENE_DATA || code == PWT_DATA
	|| code == JW_DATA || code == TEXTBOOK_DATA) {
	build_datafiles_popup(fdata);
	if (code == TEXTBOOK_DATA) {
	    gtk_signal_connect (GTK_OBJECT(lbox1), "button_press_event",
				GTK_SIGNAL_FUNC(popup_menu_handler), 
				(gpointer) fdata->popup);
	    gtk_signal_connect (GTK_OBJECT(lbox2), "button_press_event",
				GTK_SIGNAL_FUNC(popup_menu_handler), 
				(gpointer) fdata->popup);
	} else {
	    gtk_signal_connect (GTK_OBJECT(fdata->listbox), "button_press_event",
				GTK_SIGNAL_FUNC(popup_menu_handler), 
				(gpointer) fdata->popup);
	}
    }

    if (code == REMOTE_DB) {
	GtkWidget *hbox;

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(main_vbox), hbox, FALSE, FALSE, 0);
	fdata->status = gtk_label_new(_("Network status: OK"));
	gtk_label_set_justify(GTK_LABEL(fdata->status), GTK_JUSTIFY_LEFT);
	gtk_box_pack_start(GTK_BOX(hbox), fdata->status, FALSE, FALSE, 0);
    }

    button_box = gtk_hbox_new (FALSE, 5);
    gtk_box_set_homogeneous (GTK_BOX (button_box), TRUE);
    gtk_box_pack_start (GTK_BOX (main_vbox), button_box, FALSE, FALSE, 0);

    openbutton = gtk_button_new_with_label 
	((code == REMOTE_DB)? _("Get series listing") : _("Open"));
    gtk_box_pack_start (GTK_BOX (button_box), openbutton, FALSE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(openbutton), "clicked",
		       GTK_SIGNAL_FUNC(browse_func), fdata);
    if (code != NATIVE_DB && code != RATS_DB && code != REMOTE_DB) 
       	gtk_signal_connect(GTK_OBJECT(openbutton), "clicked", 
	GTK_SIGNAL_FUNC(delete_widget), fdata->w); 

    if (code == RAMU_DATA || code == GREENE_DATA || code == PWT_DATA
	|| code == JW_DATA || code == TEXTBOOK_DATA || code == REMOTE_DB) {
	midbutton = gtk_button_new_with_label 
	    ((code == REMOTE_DB)? _("Install") : _("Info"));
	gtk_box_pack_start (GTK_BOX (button_box), midbutton, FALSE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(midbutton), "clicked",
			   (code == REMOTE_DB)?
			   GTK_SIGNAL_FUNC(grab_remote_db) :
			   GTK_SIGNAL_FUNC(browse_header), fdata);
    }

    if (code == RAMU_DATA || code == JW_DATA || code == TEXTBOOK_DATA) {
	midbutton = gtk_button_new_with_label(_("Find"));
	gtk_box_pack_start(GTK_BOX (button_box), midbutton, FALSE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(midbutton), "clicked",
			   GTK_SIGNAL_FUNC(datafile_find), fdata);	
    }

    closebutton = gtk_button_new_with_label(_("Close"));
    gtk_box_pack_start (GTK_BOX (button_box), closebutton, FALSE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(closebutton), "clicked",
		       GTK_SIGNAL_FUNC(delete_widget), fdata->w);

    /* put stuff into clist(s) */
    if (code == TEXTBOOK_DATA) {
	err = populate_notebook_filelists(fdata, lbox1, lbox2, filebox);
    } else {
	err = populate_filelist(fdata);
    }

    if (err) {
	gtk_widget_destroy(fdata->w);
	return;
    }

    gtk_widget_show_all(fdata->w); 
}

/* ........................................................... */

gint populate_filelist (windata_t *fdata)
{
    gint a = fdata->role;

    if (a == NATIVE_DB || a == RATS_DB)
	return populate_dbfilelist(fdata);

    if (a == REMOTE_DB)
	return populate_remote_dblist(fdata);

    if (a == RAMU_PS || a == GREENE_PS || a == PWT_PS) 
	return read_ps_descriptions(fdata);

    return read_data_descriptions(fdata);
}

/* ......................................................... */

static GtkWidget *files_window (windata_t *fdata) 
{
    char *data_titles[] = {
	_("File"), 
	_("Summary")
    };
    char *ps_titles[] = {
	_("Script"), 
	_("Topic"), 
	_("Data")
    };
    char *db_titles[] = {
	_("Database"), 
	_("Source")
    };
    char *remote_titles[] = {
	_("Database"), 
	_("Source"), 
	_("Local status")
    };

    char **titles = data_titles;

    int data_col_width[] = {128, 256}; 
    int ps_col_width[] = {68, 180, 160};
    int db_col_width[] = {80, 304};
    int remote_col_width[] = {80, 256, 180};
    int *col_width = data_col_width;
    int full_width = 500, file_height = 260;

    GtkWidget *box, *scroller;
    int i, cols = 2;

    switch (fdata->role) {
    case NATIVE_DB:
	titles = db_titles;
	col_width = db_col_width;
	break;
    case REMOTE_DB:
	titles = remote_titles;
	cols = 3;
	col_width = remote_col_width;
	full_width = 560;
	break;
    case RATS_DB:
	titles = db_titles;
	cols = 1;
	col_width = db_col_width;
	col_width[0] = 200;
	full_width = 240;
	break;
    case RAMU_PS:
    case GREENE_PS:
    case PWT_PS:
	titles = ps_titles;
	cols = 3;
	col_width = ps_col_width;
	full_width = 480;
	break;
    case GREENE_DATA:
    case PWT_DATA:
    case JW_DATA:
	break;
    case RAMU_DATA:
	col_width[0] = 64;
	col_width[1] = 320;
	break;
    }

    fdata->active_var = 1; 

    box = gtk_vbox_new (FALSE, 0);

    full_width *= gui_scale;
    file_height *= gui_scale;

    gtk_widget_set_usize (box, full_width, file_height);
   
    scroller = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
				    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    fdata->listbox = gtk_clist_new_with_titles(cols, titles);
    gtk_clist_column_titles_passive(GTK_CLIST(fdata->listbox));
    gtk_container_add (GTK_CONTAINER (scroller), fdata->listbox);
    gtk_clist_set_selection_mode (GTK_CLIST (fdata->listbox), 
				  GTK_SELECTION_BROWSE);
    for (i=0; i<cols; i++) {
	col_width[i] *= gui_scale;
	gtk_clist_set_column_width (GTK_CLIST (fdata->listbox), i,
				    col_width[i]);
	gtk_clist_set_column_justification (GTK_CLIST (fdata->listbox), i, 
					    GTK_JUSTIFY_LEFT);
    }
    gtk_box_pack_start (GTK_BOX (box), scroller, TRUE, TRUE, TRUE);
    gtk_signal_connect_after (GTK_OBJECT (fdata->listbox), "select_row", 
			      GTK_SIGNAL_FUNC (selectrow), fdata);
    gtk_widget_show (fdata->listbox);
    gtk_widget_show (scroller);

    gtk_widget_show (box);

    return box;
}

/* .................................................................. */

static void really_set_panel_code (GtkWidget *w, dialog_t *d)
{
    DATAINFO *pdinfo = (DATAINFO *) d->data;

    pdinfo->time_series = d->code;
    set_sample_label(pdinfo);
    d->data = NULL;
}

/* .................................................................. */

static void set_panel_code (GtkWidget *w, dialog_t *d)
{
    gint i;

    if (GTK_TOGGLE_BUTTON(w)->active) {
	i = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(w), "action"));
	d->code = i;
    }
}

/* .................................................................. */

void panel_structure_dialog (DATAINFO *pdinfo, GtkWidget *w,
			     void (*cleanfun)(), void (*helpfun)())
{
    dialog_t *d, *cancel_d;
    GtkWidget *button;
    GtkWidget *tempwid;
    GSList *group;

    d = malloc(sizeof *d);
    if (d == NULL) return;
    cancel_d = malloc(sizeof *cancel_d);
    if (cancel_d == NULL) {
	free(d);
	return;
    }
    
    d->data = pdinfo;
    cancel_d->data = NULL;
    cancel_d->all_buttons = d->all_buttons = NULL;

    d->dialog = gtk_dialog_new();
    w = d->dialog;

    d->code = (dataset_is_panel(pdinfo))? pdinfo->time_series : STACKED_TIME_SERIES;

    gtk_window_set_title (GTK_WINDOW (d->dialog), _("gretl: panel structure"));
    gtk_window_set_policy (GTK_WINDOW (d->dialog), FALSE, FALSE, FALSE);
    gtk_container_border_width (GTK_CONTAINER 
				(GTK_DIALOG (d->dialog)->vbox), 10);
    gtk_container_border_width (GTK_CONTAINER 
				(GTK_DIALOG (d->dialog)->action_area), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (d->dialog)->vbox), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (d->dialog)->action_area), 15);
    gtk_box_set_homogeneous (GTK_BOX 
			     (GTK_DIALOG (d->dialog)->action_area), TRUE);
    gtk_window_set_position (GTK_WINDOW (d->dialog), GTK_WIN_POS_MOUSE);

    gtk_signal_connect (GTK_OBJECT (d->dialog), "destroy", 
			GTK_SIGNAL_FUNC (cleanfun), 
			cancel_d);

    button = gtk_radio_button_new_with_label (NULL, _("Stacked time series"));
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->vbox), 
			button, TRUE, TRUE, FALSE);
    if (d->code == STACKED_TIME_SERIES)
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       GTK_SIGNAL_FUNC(set_panel_code), d);
    gtk_object_set_data(GTK_OBJECT(button), "action", 
			GINT_TO_POINTER(STACKED_TIME_SERIES));
    gtk_widget_show (button);

    group = gtk_radio_button_group (GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label(group, _("Stacked cross sections"));
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->vbox), 
			button, TRUE, TRUE, FALSE);
    if (d->code == STACKED_CROSS_SECTION)
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       GTK_SIGNAL_FUNC(set_panel_code), d);
    gtk_object_set_data(GTK_OBJECT(button), "action", 
			GINT_TO_POINTER(STACKED_CROSS_SECTION));
    gtk_widget_show (button);

    /* Create the "OK" button */
    tempwid = gtk_button_new_with_label ("OK");
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->action_area), 
			tempwid, TRUE, TRUE, FALSE);
    gtk_signal_connect(GTK_OBJECT(tempwid), "clicked",
                       GTK_SIGNAL_FUNC(really_set_panel_code), d);
    gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked", 
			       GTK_SIGNAL_FUNC (gtk_widget_destroy), 
			       GTK_OBJECT (d->dialog));
    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

    /* Create the "Cancel" button */
    tempwid = gtk_button_new_with_label (_("Cancel"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->action_area), 
			tempwid, TRUE, TRUE, FALSE);
    gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked", 
			       GTK_SIGNAL_FUNC (gtk_widget_destroy), 
			       GTK_OBJECT (d->dialog));
    gtk_widget_show (tempwid);

    /* Create a "Help" button */
    tempwid = gtk_button_new_with_label (_("Help"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->action_area), 
			tempwid, TRUE, TRUE, FALSE);
    gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			GTK_SIGNAL_FUNC (helpfun), 
			GINT_TO_POINTER (PANEL));
    gtk_widget_show (tempwid);

    gtk_widget_show (d->dialog);
    gtk_main();
}

/* .................................................................. */

void panel_restructure_dialog (gpointer data, guint u, GtkWidget *w)
{
    int resp;
    gchar *msg;

    msg = g_strdup_printf(_("Do you want to restructure the current panel data set\n"
			    "as stacked time series?"));

    resp = yes_no_dialog(_("gretl: panel structure"), msg, 0);
    g_free(msg);

    if (resp == GRETL_YES) {
	void *handle;
	int (*switch_panel_orientation)(double **, DATAINFO *);

	if (gui_open_plugin("panel_data", &handle) == 0) {
	    switch_panel_orientation = 
		get_plugin_function("switch_panel_orientation", handle);
	    if (switch_panel_orientation != NULL) {
		if (switch_panel_orientation(Z, datainfo)) {
		    errbox(_("Failed to change panel structure"));
		} else {
		    msg = g_strdup_printf(_("Panel structure changed to %s"), 
					  _("stacked time series"));
		    infobox(msg);
		    g_free(msg);
		    data_status |= MODIFIED_DATA;
		    set_sample_label(datainfo);
		}
	    }
	}
    }
}

/* .................................................................. */

struct ts_pd {
    int pd;
    const char *label;
};

void time_series_dialog (gpointer data, guint u, GtkWidget *w)
{
    gchar *msg = NULL;
    const char *label = NULL;
    int i;
    struct ts_pd ok_pd[] = {
	{  1, N_("annual data") },
	{  4, N_("quarterly data") },
	{ 12, N_("monthly data") },
	{ 52, N_("weekly data") },
	{  5, N_("daily data") },
	{  7, N_("daily data") },
	{ 24, N_("hourly data") },
	{  0, NULL }
    };
	
    for (i=0; ok_pd[i].pd != 0; i++) { 
	if (datainfo->pd == ok_pd[i].pd) {
	    label = ok_pd[i].label;
	    break;
	}
    }

    if (label != NULL) {
	int resp;

	msg = g_strdup_printf(_("Do you want to register the current data set\n"
		"as %s?"), _(label));
	resp = yes_no_dialog(_("gretl: time series data"), msg, 0);
	if (resp == GRETL_YES) {
	    if (!(datainfo->time_series == TIME_SERIES))
		data_status |= MODIFIED_DATA;
	    datainfo->time_series = TIME_SERIES;
	    set_sample_label(datainfo);
	}
    } else {
	msg = g_strdup_printf(_("The current data frequency, %d, is not "
				"recognized\nas a valid time-series frequency"), 
			      datainfo->pd);
	errbox(msg);
    }
}

static void 
datafiles_page_callback (GtkNotebook *notebook, GtkNotebookPage *page,
			 guint page_num, windata_t *fdata)
{
    if (page_num == 0) {
	fdata->listbox = gtk_object_get_data(GTK_OBJECT(notebook), "w1");
    } else if (page_num == 1) {
	fdata->listbox = gtk_object_get_data(GTK_OBJECT(notebook), "w2");
    }
}

static GtkWidget *files_notebook (windata_t *fdata, 
				  GtkWidget **w1, GtkWidget **w2)
{
    GtkWidget *notebook;
    GtkWidget *listpage;
    GtkWidget *label;
    int role = fdata->role;

    notebook = gtk_notebook_new();

    fdata->role = RAMU_DATA;
    listpage = files_window(fdata);
    label = gtk_label_new("Ramanathan");
    gtk_widget_show(label);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), listpage, label);
    *w1 = fdata->listbox;
    gtk_object_set_data(GTK_OBJECT(notebook), "w1", *w1);

    fdata->role = JW_DATA;
    listpage = files_window(fdata);
    label = gtk_label_new("Wooldridge");
    gtk_widget_show(label);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), listpage, label);
    *w2 = fdata->listbox;
    gtk_object_set_data(GTK_OBJECT(notebook), "w2", *w2);

    fdata->role = role;

    gtk_signal_connect(GTK_OBJECT(GTK_NOTEBOOK(notebook)), "switch-page",
		       GTK_SIGNAL_FUNC(datafiles_page_callback),
		       fdata);

    gtk_widget_show(notebook);

    return notebook;
}

static int populate_notebook_filelists (windata_t *fdata, 
					GtkWidget *lbox1, 
					GtkWidget *lbox2,
					GtkWidget *notebook)
{
    int role = fdata->role;

    fdata->role = RAMU_DATA;
    fdata->listbox = lbox1;
    if (populate_filelist(fdata)) {
	return 1;
    }

    fdata->role = JW_DATA;
    fdata->listbox = lbox2;
    if (populate_filelist(fdata)) {
	return 1;
    }

    if (jwdata) {
	fdata->listbox = lbox2;
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook), 1);
    } else {
	fdata->listbox = lbox1;
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook), 0);
    }

    fdata->role = role;

    return 0;
} 







