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

/* dialogs.c for gretl */

#include "gretl.h"
#include "session.h"
#include "selector.h"
#include "obsbutton.h"

extern const char *version_string;

GtkWidget *active_edit_id;
GtkWidget *active_edit_name;
GtkWidget *active_edit_text;

extern int work_done (void); /* library.c */

GtkWidget *open_dialog;
int session_saved;

/* ........................................................... */

void destroy_dialog_data (GtkWidget *w, gpointer data) 
{
    dialog_t *ddata = (dialog_t *) data;

    gtk_main_quit();

    g_free (ddata);
    open_dialog = NULL;
    if (active_edit_id) active_edit_id = NULL;
    if (active_edit_name) active_edit_name = NULL;
    if (active_edit_text) active_edit_text = NULL;
}

/* ........................................................... */

static void dialog_table_setup (dialog_t *dlg, int hsize)
{
    GtkWidget *sw;

    sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_set_usize(sw, hsize, 200);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg->dialog)->vbox), 
		       sw, TRUE, TRUE, FALSE);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
				    GTK_POLICY_AUTOMATIC,
				    GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), dlg->edit); 
    gtk_widget_show(dlg->edit);
    gtk_widget_show(sw);
}

/* ........................................................... */

static GtkWidget *text_edit_new (int *hsize)
{
    GtkWidget *tbuf;

    tbuf = gtk_text_new(NULL, NULL);

    gtk_text_set_editable(GTK_TEXT(tbuf), TRUE);
    gtk_text_set_word_wrap(GTK_TEXT(tbuf), FALSE);
    *hsize *= gdk_char_width(fixed_font, 'W');
    *hsize += 48;

    return tbuf;
}

/* ........................................................... */

void edit_dialog (const char *diagtxt, const char *infotxt, const char *deftext, 
		  void (*okfunc)(), void *okptr,
		  guint cmdcode, guint varclick)
{
    dialog_t *d;
    GtkWidget *tempwid;

    if (open_dialog != NULL) {
	gdk_window_raise(open_dialog->window);
	return;
    }

    d = mymalloc(sizeof *d);
    if (d == NULL) return;

    d->data = okptr;
    d->code = cmdcode;

    d->dialog = gtk_dialog_new();
    open_dialog = d->dialog;    

    gtk_window_set_title (GTK_WINDOW (d->dialog), diagtxt);
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
			GTK_SIGNAL_FUNC (destroy_dialog_data), 
			d);

    if (cmdcode == NLS) {
	int hsize = 64;
	gchar *lbl;

	lbl = g_strdup_printf("%s\n%s", infotxt,
			      _("(Please refer to Help for guidance)"));
	tempwid = gtk_label_new (lbl);
	gtk_label_set_justify(GTK_LABEL(tempwid), GTK_JUSTIFY_CENTER);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->vbox), 
			    tempwid, TRUE, TRUE, 10);
	gtk_widget_show (tempwid);
	g_free(lbl);

	d->edit = text_edit_new (&hsize);
	dialog_table_setup(d, hsize);	
    } else {
	tempwid = gtk_label_new (infotxt);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->vbox), 
			    tempwid, TRUE, TRUE, FALSE);
	gtk_widget_show (tempwid);

	d->edit = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->vbox), 
			    d->edit, TRUE, TRUE, FALSE);

	/* make the Enter key do the business */
	if (okfunc) { 
	    gtk_signal_connect (GTK_OBJECT (d->edit), "activate", 
				GTK_SIGNAL_FUNC (okfunc), (gpointer) d);
	}

	gtk_entry_set_visibility (GTK_ENTRY (d->edit), TRUE);
	if (deftext) {
	    gtk_entry_set_text (GTK_ENTRY (d->edit), deftext);
	    gtk_entry_select_region (GTK_ENTRY (d->edit), 0, strlen (deftext));
	}
	gtk_widget_show (d->edit);
    }

    if (varclick == VARCLICK_INSERT_ID)
	active_edit_id = d->edit; 
    else if (varclick == VARCLICK_INSERT_NAME)
	active_edit_name = d->edit;
    else if (varclick == VARCLICK_INSERT_TEXT)
	active_edit_text = d->edit;

    gtk_widget_grab_focus (d->edit);

    /* Create the "OK" button */
    tempwid = gtk_button_new_with_label (_("OK"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->action_area), 
			tempwid, TRUE, TRUE, FALSE);
    if (okfunc) {
	gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			    GTK_SIGNAL_FUNC (okfunc), (gpointer) d);
    }

    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

    /* Create a "Cancel" button */
    if (cmdcode != CREATE_USERDIR) {
	tempwid = gtk_button_new_with_label (_("Cancel"));
	GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->action_area), 
			    tempwid, TRUE, TRUE, FALSE);
	gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked", 
				   GTK_SIGNAL_FUNC (gtk_widget_destroy), 
				   GTK_OBJECT (d->dialog));
	gtk_widget_show (tempwid);
    }

    /* Create a "Help" button if wanted */
    if (cmdcode && cmdcode != PRINT && cmdcode != CREATE_USERDIR) {
	tempwid = gtk_button_new_with_label (_("Help"));
	GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (d->dialog)->action_area), 
			    tempwid, TRUE, TRUE, FALSE);
	gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			    GTK_SIGNAL_FUNC (context_help), 
			    GINT_TO_POINTER (cmdcode));
	gtk_widget_show (tempwid);
    }

    gtk_widget_show (d->dialog); 

    gtk_main();
} 

#ifdef USE_GNOME

void about_dialog (gpointer data) 
{
    GtkWidget* dlg;
    char const *authors[] = {
	"Allin Cottrell",
	NULL
    };
    const gchar *blurb = N_("An econometrics program for the gnome desktop "
			    "issued under the GNU General Public License.  "
			    "http://gretl.sourceforge.net/");
    gchar *comment = NULL;

#ifdef ENABLE_NLS
    if (strcmp(_("translator_credits"), "translator_credits")) {
	comment = g_strconcat(_(blurb), " ", _("translator_credits"),
			      NULL);
    }
#endif 

    dlg = gnome_about_new("gretl", version_string,
			  "(c) 2000-2003 Allin Cottrell", 
			  authors, (comment != NULL)? comment : _(blurb),
			  gnome_pixmap_file("gretl-logo.xpm") 
			  );

    if (comment != NULL) g_free(comment);

    gnome_dialog_set_parent(GNOME_DIALOG(dlg), GTK_WINDOW(mdata->w));
    gtk_widget_show(dlg);
}

#else /* plain GTK version of About dialog follows */

static int open_xpm (char *filename, GtkWidget *parent, GdkPixmap **pixmap, 
		     GdkBitmap **mask) 
{
    char exfile[MAXLEN];
    GtkStyle *style;

    if (*filename == '\0') return 1;
    strcpy(exfile, paths.gretldir);
    if (exfile[strlen(exfile) - 2] != SLASH)
	strcat(exfile, SLASHSTR);
    strcat(exfile, filename);

    style = gtk_widget_get_style (parent);
    *pixmap = gdk_pixmap_create_from_xpm (parent->window, 
					  mask, 
					  &style->bg[GTK_STATE_NORMAL], 
					  exfile);
    if (*pixmap == NULL) return 0;
    else return 1;
}

void about_dialog (gpointer data) 
{
    GtkWidget *tempwid, *notebook, *box, *label, *view, *vscroll;
    GdkPixmap *logo_pixmap;
    GdkBitmap *logo_mask;
    char *tempstr, *no_gpl, buf[MAXSTR];
    const gchar *tr_credit = "";
    GtkWidget *dialog;
    FILE *fd;

    no_gpl = 
	g_strdup_printf (_("Cannot find the license agreement file COPYING. "
			 "Please make sure it's in %s"), 
			 paths.gretldir);
    dialog = gtk_dialog_new ();
    gtk_window_set_title (GTK_WINDOW (dialog), _("About gretl"));
    gtk_container_border_width (GTK_CONTAINER 
				(GTK_DIALOG (dialog)->vbox), 10);
    gtk_container_border_width (GTK_CONTAINER 
				(GTK_DIALOG (dialog)->action_area), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 5);
    gtk_box_set_homogeneous (GTK_BOX (GTK_DIALOG (dialog)->action_area), TRUE);
    gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
    gtk_signal_connect_object (GTK_OBJECT (dialog), 
			       "delete_event", GTK_SIGNAL_FUNC 
			       (gtk_widget_destroy), GTK_OBJECT (dialog));
    gtk_widget_realize (dialog);
      
    notebook = gtk_notebook_new ();
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			notebook, TRUE, TRUE, 0);
    gtk_widget_show (notebook);
   
    box = gtk_vbox_new (TRUE, 5);
    gtk_container_border_width (GTK_CONTAINER (box), 10);
    gtk_widget_show (box);
   
    if (open_xpm ("gretl-logo.xpm", mdata->w, &logo_pixmap, &logo_mask)) {
	tempwid = gtk_pixmap_new (logo_pixmap, logo_mask);
	gtk_box_pack_start (GTK_BOX (box), tempwid, FALSE, FALSE, 0);
	gtk_widget_show (tempwid);
    }

#ifdef ENABLE_NLS
    if (strcmp(_("translator_credits"), "translator_credits")) {
	tr_credit = _("translator_credits");
    }
#endif  

    tempstr = g_strdup_printf ("gretl, version %s\n"
			       "Copyright (C) 2000-2001 Allin Cottrell "
			       "<cottrell@wfu.edu>\nHomepage: "
			       "http://gretl.sourceforge.net/\n%s",
			       version_string, tr_credit);
    tempwid = gtk_label_new (tempstr);
    g_free (tempstr);
    gtk_box_pack_start (GTK_BOX (box), tempwid, FALSE, FALSE, 0);
    gtk_widget_show (tempwid);
   
    label = gtk_label_new (_("About"));
    gtk_widget_show (label);
   
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), box, label);

    box = gtk_vbox_new (FALSE, 5);
    gtk_container_border_width (GTK_CONTAINER (box), 10);
    gtk_widget_show (box);

    tempwid = gtk_table_new (1, 2, FALSE);
    gtk_box_pack_start (GTK_BOX (box), tempwid, TRUE, TRUE, 0);
    gtk_widget_show (tempwid);

    view = gtk_text_new (NULL, NULL);
    gtk_text_set_editable (GTK_TEXT (view), FALSE);
    gtk_text_set_word_wrap (GTK_TEXT (view), TRUE);
    gtk_table_attach (GTK_TABLE (tempwid), view, 0, 1, 0, 1,
		      GTK_FILL | GTK_EXPAND, GTK_FILL | 
		      GTK_EXPAND | GTK_SHRINK, 0, 0);
    gtk_widget_show (view);

    vscroll = gtk_vscrollbar_new (GTK_TEXT (view)->vadj);
    gtk_table_attach (GTK_TABLE (tempwid), vscroll, 1, 2, 0, 1,
		      GTK_FILL, GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 0);
    gtk_widget_show (vscroll);

    label = gtk_label_new (_("License Agreement"));
    gtk_widget_show (label);
   
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), box, label);

    tempwid = gtk_button_new_with_label (_("  Close  "));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), 
			tempwid, FALSE, FALSE, 0);
    gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked", 
			       GTK_SIGNAL_FUNC (gtk_widget_destroy), 
			       GTK_OBJECT (dialog));
    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

    tempstr = g_strdup_printf("%s/COPYING", paths.gretldir);
    if ((fd = fopen (tempstr, "r")) == NULL) {
	gtk_text_insert (GTK_TEXT (view), NULL, NULL, NULL, 
			 no_gpl, strlen (no_gpl));
	gtk_widget_show (dialog);
	g_free (tempstr);
	return;
    }
    g_free(tempstr);
   
    memset (buf, 0, sizeof (buf));
    while (fread (buf, 1, sizeof (buf) - 1, fd)) {
	gtk_text_insert (GTK_TEXT (view), 
			 fixed_font, NULL, NULL, buf, strlen (buf));
	memset (buf, 0, sizeof (buf));
    }
    fclose (fd);
    gtk_widget_show(dialog);
    g_free(no_gpl);
}         
#endif /* not GNOME */

/* ........................................................... */

void menu_exit_check (GtkWidget *w, gpointer data)
{
    int ret = exit_check(w, NULL, data);

    if (ret == FALSE) gtk_main_quit();
}

/* ......................................................... */

static void save_data_callback (void)
{
    file_save(NULL, SAVE_DATA, NULL);
    if (data_status & MODIFIED_DATA)
	data_status ^= MODIFIED_DATA;
    /* FIXME: need to do more here */
}

#ifdef USE_GNOME

int yes_no_dialog (char *title, char *message, int cancel)
{
    GtkWidget *dialog, *label;
    int button;

    if (cancel)
	dialog = gnome_dialog_new (title,
				   GNOME_STOCK_BUTTON_YES,
				   GNOME_STOCK_BUTTON_NO,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
    else
	dialog = gnome_dialog_new (title,
				   GNOME_STOCK_BUTTON_YES,
				   GNOME_STOCK_BUTTON_NO,
				   NULL);

    gnome_dialog_set_parent (GNOME_DIALOG (dialog), 
			     GTK_WINDOW(mdata->w));

    label = gtk_label_new (message);
    gtk_widget_show (label);
    gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), label, 
			TRUE, TRUE, 0);

    button = gnome_dialog_run_and_close (GNOME_DIALOG (dialog));

    if (button == 0) return GRETL_YES;
    if (button == 1) return GRETL_NO;
    if (button == 2) return GRETL_CANCEL;

    return GRETL_CANCEL;
}

#else /* not USE_GNOME */

struct yes_no_data {
    GtkWidget *dialog;
    int *ret;
    int button;
};

static void yes_no_callback (GtkWidget *w, gpointer data)
{
    struct yes_no_data *mydata = data;

    *(mydata->ret) = mydata->button;
    gtk_main_quit();
    gtk_widget_destroy(mydata->dialog);
}

/* ......................................................... */

gint yes_no_dialog (char *title, char *msg, int cancel)
{
   GtkWidget *tempwid, *dialog;
   int ret;
   struct yes_no_data yesdata, nodata, canceldata;

   dialog = gtk_dialog_new();

   yesdata.dialog = nodata.dialog = canceldata.dialog 
       = dialog;
   yesdata.ret = nodata.ret = canceldata.ret = &ret; 
   yesdata.button = GRETL_YES;
   nodata.button = GRETL_NO;
   canceldata.button = GRETL_CANCEL;
   
   gtk_grab_add (dialog);
   gtk_window_set_title (GTK_WINDOW (dialog), title);
   gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, FALSE, FALSE);
   gtk_container_border_width 
       (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 10);
   gtk_container_border_width 
       (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
   gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 5);
   gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 15);
   gtk_box_set_homogeneous (GTK_BOX (GTK_DIALOG (dialog)->action_area), TRUE);
   gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

   tempwid = gtk_label_new (msg);
   gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), tempwid, 
		       TRUE, TRUE, FALSE);
   gtk_widget_show(tempwid);

   /* "Yes" button */
   tempwid = gtk_button_new_with_label (_("Yes"));
   GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
   gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), 
		       tempwid, TRUE, TRUE, TRUE);  
   gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
		       GTK_SIGNAL_FUNC (yes_no_callback), &yesdata);
   gtk_widget_grab_default (tempwid);
   gtk_widget_show (tempwid);

   /* "No" button */
   tempwid = gtk_button_new_with_label (_("No"));
   gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), 
		       tempwid, TRUE, TRUE, TRUE); 
   gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
		       GTK_SIGNAL_FUNC (yes_no_callback), &nodata);
   gtk_widget_show (tempwid);

   /* Cancel button -- if wanted */
   if (cancel) {
       tempwid = gtk_button_new_with_label (_("Cancel"));
       gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), 
			   tempwid, TRUE, TRUE, TRUE); 
       gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			   GTK_SIGNAL_FUNC (yes_no_callback), &canceldata);
       gtk_widget_show (tempwid);
   }

   gtk_widget_show (dialog);
   gtk_main();
   return ret;
}

#endif /* plain GTK */

/* ........................................................... */

gint exit_check (GtkWidget *widget, GdkEvent *event, gpointer data) 
{
    int button;

#ifdef ALWAYS_SAVE_SESSION
    char fname[MAXLEN];

    strcpy(fname, paths.userdir);
    strcat(fname, "session.inp");
    dump_cmd_stack(fname, 0);
#endif

    /* FIXME: should make both save_session_callback() and
       save_data_callback() blocking functions */

    if (!expert && !replaying() && 
	(session_changed(0) || (work_done() && !session_saved))) {
	button = yes_no_dialog ("gretl", 		      
				_("Do you want to save the commands and\n"
				"output from this gretl session?"), 1);
	if (button == GRETL_YES) {
	    save_session_callback(NULL, 1, NULL);
	    return TRUE; /* bodge */
	}
	/* button -1 = wm close */
	else if (button == GRETL_CANCEL || button == -1) return TRUE;
	/* else button = GRETL_NO: so fall through */
    }

    if (data_status & MODIFIED_DATA) {
	button = yes_no_dialog ("gretl", 
				_("Do you want to save changes you have\n"
				"made to the current data set?"), 1);
	if (button == GRETL_YES) {
	    save_data_callback();
	    return TRUE; 
	}
	else if (button == GRETL_CANCEL || button == -1) return TRUE;
    }    

    write_rc();
    return FALSE;
}

typedef struct {
    GtkWidget *space_button;
    GtkWidget *point_button;
    gint delim;
    gint decpoint;
} csv_stuff;

#ifdef ENABLE_NLS
static void set_dec (GtkWidget *w, gpointer p)
{
    gint i;
    csv_stuff *csv = (csv_stuff *) p;

    if (GTK_TOGGLE_BUTTON(w)->active) {
	i = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(w), "action"));
	csv->decpoint = i;
	if (csv->decpoint == ',' && csv->delim == ',') {
	    csv->delim = ' ';
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (csv->space_button), 
					  TRUE);
	}
    }
}
#endif

static void set_delim (GtkWidget *w, gpointer p)
{
    gint i;
    csv_stuff *csv = (csv_stuff *) p;

    if (GTK_TOGGLE_BUTTON(w)->active) {
	i = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(w), "action"));
	csv->delim = i;
	if (csv->point_button != NULL && 
	    csv->delim == ',' && csv->decpoint == ',') {
	    csv->decpoint = '.';
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (csv->point_button), 
					  TRUE);
	}
    }
}

static void really_set_csv_stuff (GtkWidget *w, gpointer p)
{
    csv_stuff *stuff = (csv_stuff *) p;

    datainfo->delim = stuff->delim;
    datainfo->decpoint = stuff->decpoint;
}

static void destroy_delim_dialog (GtkWidget *w, gint *p)
{
    free(p);
    gtk_main_quit();
}

void delimiter_dialog (void)
{
    GtkWidget *dialog, *tempwid, *button;
    GSList *group;
    csv_stuff *csvptr = NULL;

    csvptr = mymalloc(sizeof *csvptr);
    if (csvptr == NULL) return;
    csvptr->delim = datainfo->delim;
    csvptr->decpoint = '.';
    csvptr->point_button = NULL;

    dialog = gtk_dialog_new();

    gtk_window_set_title (GTK_WINDOW (dialog), _("gretl: data delimiter"));
    gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, FALSE, FALSE);
    gtk_container_border_width (GTK_CONTAINER 
				(GTK_DIALOG (dialog)->vbox), 10);
    gtk_container_border_width (GTK_CONTAINER 
				(GTK_DIALOG (dialog)->action_area), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 15);
    gtk_box_set_homogeneous (GTK_BOX 
			     (GTK_DIALOG (dialog)->action_area), TRUE);
    gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

    gtk_signal_connect (GTK_OBJECT (dialog), "destroy", 
			destroy_delim_dialog, csvptr);

    tempwid = gtk_label_new (_("separator for data columns:"));
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			tempwid, TRUE, TRUE, FALSE);
    gtk_widget_show(tempwid);

    /* comma separator */
    button = gtk_radio_button_new_with_label (NULL, _("comma (,)"));
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			button, TRUE, TRUE, FALSE);
    if (csvptr->delim == ',')
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       GTK_SIGNAL_FUNC(set_delim), csvptr);
    gtk_object_set_data(GTK_OBJECT(button), "action", 
			GINT_TO_POINTER(','));
    gtk_widget_show (button);

    /* space separator */
    group = gtk_radio_button_group (GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label(group, _("space"));
    csvptr->space_button = button;
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			button, TRUE, TRUE, FALSE);
    if (csvptr->delim == ' ')
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       GTK_SIGNAL_FUNC(set_delim), csvptr);
    gtk_object_set_data(GTK_OBJECT(button), "action", 
			GINT_TO_POINTER(' '));    
    gtk_widget_show (button);

    /* tab separator */
    group = gtk_radio_button_group (GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label(group, _("tab"));
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			button, TRUE, TRUE, FALSE);
    if (csvptr->delim == '\t')
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
                       GTK_SIGNAL_FUNC(set_delim), csvptr);
    gtk_object_set_data(GTK_OBJECT(button), "action", 
			GINT_TO_POINTER('\t'));    
    gtk_widget_show (button);

#ifdef ENABLE_NLS
    if (',' == get_local_decpoint()) {
	GSList *decgroup;

	tempwid = gtk_hseparator_new();
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			    tempwid, TRUE, TRUE, FALSE);
	gtk_widget_show(tempwid);
	
	tempwid = gtk_label_new (_("decimal point character:"));
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			    tempwid, TRUE, TRUE, FALSE);
	gtk_widget_show(tempwid);
 
	/* period decpoint */
	button = gtk_radio_button_new_with_label (NULL, _("period (.)"));
	csvptr->point_button = button;
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			    button, TRUE, TRUE, FALSE);
	if (csvptr->decpoint == '.')
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   GTK_SIGNAL_FUNC(set_dec), csvptr);
	gtk_object_set_data(GTK_OBJECT(button), "action", 
			    GINT_TO_POINTER('.'));
	gtk_widget_show (button);

	/* comma decpoint */
	decgroup = gtk_radio_button_group (GTK_RADIO_BUTTON (button));
	button = gtk_radio_button_new_with_label(decgroup, _("comma (,)"));
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			    button, TRUE, TRUE, FALSE);
	if (csvptr->decpoint == ',')
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   GTK_SIGNAL_FUNC(set_dec), csvptr);
	gtk_object_set_data(GTK_OBJECT(button), "action", 
			    GINT_TO_POINTER(','));    
	gtk_widget_show (button);
    }
#endif

    /* Create the "OK" button */
    tempwid = gtk_button_new_with_label (_("OK"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), 
			tempwid, TRUE, TRUE, FALSE);
    gtk_signal_connect(GTK_OBJECT(tempwid), "clicked",
                       GTK_SIGNAL_FUNC(really_set_csv_stuff), csvptr);
    gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked", 
			       GTK_SIGNAL_FUNC (gtk_widget_destroy), 
			       GTK_OBJECT (dialog));
    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

    gtk_widget_show (dialog);

    gtk_main();
}

struct format_info {
    GtkWidget *dialog;
    windata_t *vwin;
    int format;
};

static void destroy_format_dialog (GtkWidget *w, struct format_info *finfo)
{
    free(finfo);
    gtk_main_quit();
}

static void copy_with_format_callback (GtkWidget *w, struct format_info *finfo)
{
    gtk_widget_hide(finfo->dialog);
    text_copy(finfo->vwin, finfo->format, NULL);
    gtk_widget_destroy(finfo->dialog);
}

static void set_copy_format (GtkWidget *w, struct format_info *finfo)
{
    gpointer p = gtk_object_get_data(GTK_OBJECT(w), "format");

    if (p != NULL) {
	finfo->format = GPOINTER_TO_INT(p);
    }
}

void copy_format_dialog (windata_t *vwin, int unused)
{
    GtkWidget *dialog, *tempwid, *button, *hbox;
    GtkWidget *internal_vbox;
    GSList *group;
    struct format_info *finfo;

    finfo = mymalloc(sizeof *finfo);
    if (finfo == NULL) return;

    dialog = gtk_dialog_new();
    
    finfo->vwin = vwin;
    finfo->dialog = dialog;
    finfo->format = COPY_LATEX;

    gtk_window_set_title (GTK_WINDOW (dialog), _("gretl: copy formats"));
    /* gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE); */
    gtk_container_set_border_width (GTK_CONTAINER 
				    (GTK_DIALOG (dialog)->vbox), 10);
    gtk_container_set_border_width (GTK_CONTAINER 
				    (GTK_DIALOG (dialog)->action_area), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 5);

    gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

    gtk_signal_connect (GTK_OBJECT(dialog), "destroy", 
			GTK_SIGNAL_FUNC(destroy_format_dialog), finfo);

    internal_vbox = gtk_vbox_new (FALSE, 5);

    hbox = gtk_hbox_new(FALSE, 5);
    tempwid = gtk_label_new (_("Copy as:"));
    gtk_box_pack_start (GTK_BOX(hbox), tempwid, TRUE, TRUE, 5);
    gtk_widget_show(tempwid);
    gtk_box_pack_start (GTK_BOX(internal_vbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show(hbox); 

    /* LaTeX option */
    button = gtk_radio_button_new_with_label(NULL, "LaTeX");
    gtk_box_pack_start (GTK_BOX(internal_vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(set_copy_format), finfo);
    gtk_object_set_data(GTK_OBJECT(button), "format", GINT_TO_POINTER(COPY_LATEX));    
    gtk_widget_show (button);   

    /* RTF option */
    group = gtk_radio_button_group (GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label(group, "RTF");
    gtk_box_pack_start (GTK_BOX(internal_vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(set_copy_format), finfo);
    gtk_object_set_data(GTK_OBJECT(button), "format", GINT_TO_POINTER(COPY_RTF));    
    gtk_widget_show (button);

    /* plain text option */
    group = gtk_radio_button_group (GTK_RADIO_BUTTON (button));
    button = gtk_radio_button_new_with_label (group, _("plain text"));
    gtk_box_pack_start (GTK_BOX(internal_vbox), button, TRUE, TRUE, 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(set_copy_format), finfo);
    gtk_object_set_data(GTK_OBJECT(button), "format", GINT_TO_POINTER(COPY_TEXT));
    gtk_widget_show (button);

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), internal_vbox, TRUE, TRUE, 5);
    gtk_widget_show (hbox);

    gtk_widget_show (internal_vbox);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 5);
    gtk_widget_show (hbox);

    /* Create the "OK" button */
    tempwid = gtk_button_new_with_label(_("OK"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG (dialog)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(tempwid), "clicked",
		       GTK_SIGNAL_FUNC(copy_with_format_callback), finfo);
    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

    /* "Cancel" button */
    tempwid = gtk_button_new_with_label(_("Cancel"));
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG (dialog)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(tempwid), "clicked",
		       GTK_SIGNAL_FUNC(delete_widget), dialog);
    gtk_widget_show (tempwid);

    gtk_widget_show(dialog);

    gtk_main();
}

struct varinfo_settings {
    GtkWidget *dlg;
    GtkWidget *name_entry;
    GtkWidget *label_entry;
    GtkWidget *display_name_entry;
    GtkWidget *compaction_menu;
    int varnum;
    int full;
};

static void show_varinfo_changes (int v) 
{
    gchar *idstr;
    int i, row = 0;

    for (i=1; i<datainfo->v; i++) {
	gtk_clist_get_text(GTK_CLIST(mdata->listbox), i, 0, &idstr);
	if (atoi(idstr) == v) {
	    row = i;
	    break;
	}
    }

    if (row == 0) return;

    gtk_clist_set_text (GTK_CLIST(mdata->listbox), row,
			1, datainfo->varname[v]);
    gtk_clist_set_text (GTK_CLIST(mdata->listbox), row,
			2, VARLABEL(datainfo, v));
}

static char *trim_text (const char *s)
{
    char *ret = NULL;
    int i;

    while (isspace(*s)) s++;
    if (*s == '\0') return NULL;

    ret = g_strdup(s);
    for (i=strlen(ret)-1; i>0; i--) {
	if (!isspace(ret[i])) break;
	ret[i] = '\0';
    }

    return ret;
}

static void really_set_variable_info (GtkWidget *w, 
				      struct varinfo_settings *vset)
{
    const char *edttext;
    char *newstr = NULL;
    int v = vset->varnum;
    int changed = 0, gui_changed = 0, comp_changed = 0;
    int comp_method;

    edttext = gtk_entry_get_text(GTK_ENTRY(vset->name_entry));
    newstr = trim_text(edttext);
    if (newstr != NULL && strcmp(datainfo->varname[v], newstr)) {
	int err;

	sprintf(line, "rename %d %s", v, newstr);
	if (vset->full) {
	    err = verify_and_record_command(line);
	} else {
	    err = check_cmd(line);
	}
	if (err) {
	    return;
	} else {
	    strcpy(datainfo->varname[v], newstr);
	    gui_changed = 1;
	}
    }
    free(newstr);

    edttext = gtk_entry_get_text(GTK_ENTRY(vset->label_entry));
    newstr = trim_text(edttext);
    if (newstr != NULL && strcmp(VARLABEL(datainfo, v), newstr)) {
	*VARLABEL(datainfo, v) = 0;
	strncat(VARLABEL(datainfo, v), newstr, MAXLABEL - 1);
	changed = 1;
	gui_changed = 1;
    }
    free(newstr);

    if (vset->display_name_entry != NULL) {
	edttext = gtk_entry_get_text(GTK_ENTRY(vset->display_name_entry));
	newstr = trim_text(edttext);
	if (newstr != NULL && strcmp(DISPLAYNAME(datainfo, v), newstr)) {
	    *DISPLAYNAME(datainfo, v) = 0;
	    strncat(DISPLAYNAME(datainfo, v), newstr, MAXDISP - 1);
	    changed = 1;
	}
	free(newstr);
    }

    if (vset->compaction_menu != NULL) { 
	GtkWidget *active_item;

	active_item = GTK_OPTION_MENU(vset->compaction_menu)->menu_item;
	comp_method = GPOINTER_TO_INT(gtk_object_get_data
				      (GTK_OBJECT(active_item), "option"));
	if (comp_method != COMPACT_METHOD(datainfo, v)) {
	    COMPACT_METHOD(datainfo, v) = comp_method;
	    comp_changed = 1;
	}
    }

    if (vset->full) {
	if (changed) {
	    sprintf(line, "label %s -d \"%s\" -n \"%s\"", datainfo->varname[v],
		    VARLABEL(datainfo, v), DISPLAYNAME(datainfo, v));
	    verify_and_record_command(line);
	}

	if (gui_changed) { 
	    show_varinfo_changes(v);
	}

	if (changed || comp_changed || gui_changed) {
	    data_status |= MODIFIED_DATA;
	    set_sample_label(datainfo);
	}
    }

    gtk_widget_destroy(vset->dlg);
}

static void varinfo_cancel (GtkWidget *w, struct varinfo_settings *vset)
{
    if (!vset->full) {
	*datainfo->varname[vset->varnum] = '\0';
    }

    gtk_widget_destroy(vset->dlg);
}

static void free_vsettings (GtkWidget *w, 
			    struct varinfo_settings *vset)
{
    if (!vset->full) gtk_main_quit();
    free(vset);
}

static const char *comp_int_to_string (int i)
{
    if (i == COMPACT_NONE) return N_("not set");
    else if (i == COMPACT_AVG) return N_("average of observations");
    else if (i == COMPACT_SUM) return N_("sum of observations");
    else if (i == COMPACT_SOP) return N_("first observation");
    else if (i == COMPACT_EOP) return N_("last observation");
    else return N_("not set");
}

void varinfo_dialog (int varnum, int full)
{
    GtkWidget *tempwid, *hbox;
    struct varinfo_settings *vset;

    vset = mymalloc(sizeof *vset);
    if (vset == NULL) return;

    vset->varnum = varnum;
    vset->dlg = gtk_dialog_new();
    vset->display_name_entry = NULL;
    vset->compaction_menu = NULL;
    vset->full = full;

    gtk_signal_connect (GTK_OBJECT(vset->dlg), "destroy", 
			GTK_SIGNAL_FUNC(free_vsettings), vset);

    gtk_window_set_title(GTK_WINDOW(vset->dlg), _("gretl: variable attributes"));
    gtk_container_set_border_width (GTK_CONTAINER 
				    (GTK_DIALOG (vset->dlg)->vbox), 10);
    gtk_container_set_border_width (GTK_CONTAINER 
				    (GTK_DIALOG (vset->dlg)->action_area), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (vset->dlg)->vbox), 5);
    gtk_window_set_position (GTK_WINDOW (vset->dlg), GTK_WIN_POS_MOUSE);

    /* read/set name of variable */
    hbox = gtk_hbox_new(FALSE, 5);
    tempwid = gtk_label_new (_("name of variable:"));
    gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 0);
    gtk_widget_show(tempwid);

    vset->name_entry = gtk_entry_new_with_max_length(8);
    gtk_entry_set_text(GTK_ENTRY(vset->name_entry), 
		       datainfo->varname[varnum]);
    gtk_box_pack_start(GTK_BOX(hbox), 
		       vset->name_entry, FALSE, FALSE, 0);
    gtk_widget_show(vset->name_entry); 
    gtk_signal_connect(GTK_OBJECT(vset->name_entry), "activate", 
		       GTK_SIGNAL_FUNC(really_set_variable_info), vset);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
		       hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox); 

    /* read/set descriptive string */
    hbox = gtk_hbox_new(FALSE, 0);
    tempwid = gtk_label_new (_("description:"));
    gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 0);
    gtk_widget_show(tempwid);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
		       hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox);

    hbox = gtk_hbox_new(FALSE, 0);
    vset->label_entry = gtk_entry_new_with_max_length(MAXLABEL-1);
    gtk_entry_set_text(GTK_ENTRY(vset->label_entry), 
		       VARLABEL(datainfo, varnum));
    gtk_box_pack_start(GTK_BOX(hbox), vset->label_entry, TRUE, TRUE, 0);
    gtk_widget_show(vset->label_entry);
    gtk_signal_connect(GTK_OBJECT(vset->label_entry), "activate", 
		       GTK_SIGNAL_FUNC(really_set_variable_info), vset);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
		       hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox); 

    /* read/set display name? */
    if (full) {
	hbox = gtk_hbox_new(FALSE, 5);
	tempwid = gtk_label_new (_("display name (shown in graphs):"));
	gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 0);
	gtk_widget_show(tempwid);

	vset->display_name_entry = gtk_entry_new_with_max_length(MAXDISP-1);
	gtk_entry_set_text(GTK_ENTRY(vset->display_name_entry), 
			   DISPLAYNAME(datainfo, varnum));
	gtk_box_pack_start(GTK_BOX(hbox), 
			   vset->display_name_entry, FALSE, FALSE, 0);
	gtk_widget_show(vset->display_name_entry);
	gtk_signal_connect(GTK_OBJECT(vset->display_name_entry), "activate", 
			   GTK_SIGNAL_FUNC(really_set_variable_info), vset);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
	gtk_widget_show(hbox); 
    } 

    /* read/set compaction method? */
    if (full && dataset_is_time_series(datainfo)) {  
	GtkWidget *menu;
	GtkWidget *child;
	int i;

	hbox = gtk_hbox_new(FALSE, 0);
	tempwid = gtk_label_new (_("compaction method (for reducing frequency):"));
	gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 0);
	gtk_widget_show(tempwid);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	vset->compaction_menu = gtk_option_menu_new();
	menu = gtk_menu_new();
	for (i=COMPACT_NONE; i<COMPACT_MAX; i++) {
	    child = gtk_menu_item_new_with_label(_(comp_int_to_string(i)));
	    gtk_menu_shell_append(GTK_MENU_SHELL(menu), child);
	    gtk_object_set_data(GTK_OBJECT(child), "option",
				GINT_TO_POINTER(i));
	}
	gtk_option_menu_set_menu(GTK_OPTION_MENU(vset->compaction_menu), menu);
	gtk_option_menu_set_history(GTK_OPTION_MENU(vset->compaction_menu),
				    COMPACT_METHOD(datainfo, varnum));    

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(hbox), vset->compaction_menu);
	gtk_widget_show_all(vset->compaction_menu); 

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(vset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox); 
    }

    /* Create the "OK" button */
    tempwid = gtk_button_new_with_label (_("OK"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG (vset->dlg)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(tempwid), "clicked",
		       GTK_SIGNAL_FUNC(really_set_variable_info), vset);
    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

    /* And a Cancel button */
    tempwid = gtk_button_new_with_label (_("Cancel"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG(vset->dlg)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			GTK_SIGNAL_FUNC(varinfo_cancel), vset);
    gtk_widget_show (tempwid);

    /* And a Help button? */
    if (full) {
	tempwid = gtk_button_new_with_label (_("Help"));
	GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
	gtk_box_pack_start (GTK_BOX(GTK_DIALOG(vset->dlg)->action_area), 
			    tempwid, TRUE, TRUE, 0);
	gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			    GTK_SIGNAL_FUNC(context_help), 
			    GINT_TO_POINTER(LABEL));
	gtk_widget_show (tempwid);
    }

    gtk_widget_show (vset->dlg);

    if (!full) gtk_main();
}

/* apparatus for setting sample range */

struct range_setting {
    GtkWidget *dlg;
    GtkWidget *obslabel;
    GtkWidget *startspin;
    GtkWidget *endspin;
    GtkWidget *combo;
};

static void free_rsetting (GtkWidget *w, struct range_setting *rset)
{
    free(rset);
}

static gboolean destroy_rset (GtkWidget *w, GtkWidget *dlg)
{
    gtk_widget_destroy(dlg);
    return TRUE;
}

static gboolean
set_sample_from_dialog (GtkWidget *w, struct range_setting *rset)
{
    int err;

    if (rset->combo != NULL) {
	/* setting from dummy variable */
	const gchar *buf;
	char dumv[VNAMELEN];

	buf = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(rset->combo)->entry));

	if (sscanf(buf, "%8s", dumv) != 1) return TRUE;

	sprintf(line, "smpl %s --dummy", dumv);
	if (verify_and_record_command(line)) return TRUE;
	err = bool_subsample(OPT_O);
	if (!err) {
	    gtk_widget_destroy(rset->dlg);
	} 
    } else {
	ObsButton *button;
	const gchar *s1, *s2;
	int t1, t2;	

	button = OBS_BUTTON(rset->startspin);
	s1 = gtk_entry_get_text(GTK_ENTRY(button));
	t1 = (int) obs_button_get_value(button);

	button = OBS_BUTTON(rset->endspin);
	s2 = gtk_entry_get_text(GTK_ENTRY(button));
	t2 = (int) obs_button_get_value(button); 

	if (t1 != datainfo->t1 || t2 != datainfo->t2) {
	    sprintf(line, "smpl %s %s", s1, s2);
	    if (verify_and_record_command(line)) {
		return TRUE;
	    }
	    err = set_sample(line, datainfo);
	    if (err) gui_errmsg(err);
	    else {
		gtk_widget_destroy(rset->dlg);
		set_sample_label(datainfo);
		restore_sample_state(TRUE);
	    }
	} else {
	    /* no change */
	    gtk_widget_destroy(rset->dlg);
	}
    }

    return TRUE;
}

static GList *get_dummy_list (int *thisdum)
{
    GList *dumlist = NULL;
    int i;

    for (i=1; i<datainfo->v; i++) {
	if (isdummy(Z[i], datainfo->t1, datainfo->t2)) {
	    dumlist = g_list_append(dumlist, datainfo->varname[i]);
	    if (i == mdata->active_var) *thisdum = 1;
	}
    }

    return dumlist;
}

gboolean update_obs_label (GtkEditable *entry, gpointer data)
{
    struct range_setting *rset = (struct range_setting *) data;
    char obstext[32];
    int nobs = 0;

    if (entry != NULL) {
	const gchar *vname = gtk_entry_get_text(GTK_ENTRY(entry));

	if (*vname != '\0') {
	    int v = varindex(datainfo, vname);

	    nobs = isdummy(Z[v], 0, datainfo->n - 1);
	}
    } else {
	int t1 = (int) obs_button_get_value(OBS_BUTTON(rset->startspin));
	int t2 = (int) obs_button_get_value(OBS_BUTTON(rset->endspin));

	nobs = t2 - t1 + 1;  
    }
    
    if (nobs > 0) {
	sprintf(obstext, _("Observations: %d"), nobs);  
	gtk_label_set_text(GTK_LABEL(rset->obslabel), obstext); 
    }   

    return FALSE;
}

void sample_range_dialog (gpointer p, guint u, GtkWidget *w)
{
    GtkWidget *tempwid, *hbox;
    struct range_setting *rset;
    char obstext[32];

    rset = mymalloc(sizeof *rset);
    if (rset == NULL) return;

    rset->dlg = gtk_dialog_new();

    gtk_signal_connect (GTK_OBJECT(rset->dlg), "destroy", 
			GTK_SIGNAL_FUNC(free_rsetting), rset);

    gtk_window_set_title(GTK_WINDOW(rset->dlg), _("gretl: set sample"));
    gtk_container_set_border_width (GTK_CONTAINER 
				    (GTK_DIALOG (rset->dlg)->vbox), 10);
    gtk_container_set_border_width (GTK_CONTAINER 
				    (GTK_DIALOG (rset->dlg)->action_area), 5); 
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (rset->dlg)->vbox), 5);
    gtk_window_set_position (GTK_WINDOW (rset->dlg), GTK_WIN_POS_MOUSE);

    if (u == SMPLDUM) {
	GList *dumlist;
	int thisdum = 0;

	rset->startspin = rset->endspin = NULL;

	dumlist = get_dummy_list(&thisdum);

	if (dumlist == NULL) {
	    errbox(_("There are no dummy variables in the dataset"));
	    gtk_widget_destroy(rset->dlg);
	    return;
	}

	tempwid = gtk_label_new(_("Name of dummy variable to use:"));
	hbox = gtk_hbox_new(TRUE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), tempwid, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
	
	rset->combo = gtk_combo_new();
	gtk_combo_set_popdown_strings(GTK_COMBO(rset->combo), dumlist); 
	if (thisdum) {
	    gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(rset->combo)->entry), 
			       datainfo->varname[mdata->active_var]);
	}
	gtk_editable_set_editable(GTK_EDITABLE(GTK_COMBO(rset->combo)->entry), FALSE);
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(rset->combo)->entry), "changed",
			   GTK_SIGNAL_FUNC(update_obs_label), rset);

	hbox = gtk_hbox_new(TRUE, 5);
	gtk_box_pack_start(GTK_BOX(hbox), rset->combo, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
    } else { /* plain SMPL */
	GtkWidget *vbox;
	GtkObject *adj;

	rset->combo = NULL;

	hbox = gtk_hbox_new(TRUE, 5);

	tempwid = gtk_label_new(_("Set sample range"));
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   tempwid, FALSE, FALSE, 5);

	/* spinner for starting obs */
	vbox = gtk_vbox_new(FALSE, 5);
	tempwid = gtk_label_new(_("Start:"));
	gtk_box_pack_start(GTK_BOX(vbox), tempwid, FALSE, FALSE, 0);
	adj = gtk_adjustment_new(datainfo->t1, 
				 0, datainfo->n - 1,
				 1, 1, 1);
	rset->startspin = obs_button_new(GTK_ADJUSTMENT(adj));
	gtk_box_pack_start(GTK_BOX(vbox), rset->startspin, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 5);

	/* spinner for ending obs */
	vbox = gtk_vbox_new(FALSE, 5);
	tempwid = gtk_label_new(_("End:"));
	gtk_box_pack_start(GTK_BOX(vbox), tempwid, FALSE, FALSE, 0);
	adj = gtk_adjustment_new(datainfo->t2, 
				 0, datainfo->n - 1, 
				 1, 1, 1);
	rset->endspin = obs_button_new(GTK_ADJUSTMENT(adj));
	gtk_box_pack_start(GTK_BOX(vbox), rset->endspin, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 5);

	/* inter-connect the two spinners */
	gtk_object_set_data(GTK_OBJECT(rset->startspin), "endspin", rset->endspin);
	gtk_object_set_data(GTK_OBJECT(rset->endspin), "startspin", rset->startspin);

	/* pack the spinner apparatus */
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
			   hbox, FALSE, FALSE, 5);
    }

    /* label showing number of observations */
    sprintf(obstext, _("Observations: %d"), datainfo->t2 - datainfo->t1 + 1);
    rset->obslabel = gtk_label_new(obstext);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(rset->dlg)->vbox), 
		       rset->obslabel, FALSE, FALSE, 5);

    if (rset->combo == NULL) {
	gtk_object_set_data(GTK_OBJECT(rset->startspin), "rset", rset);
	gtk_object_set_data(GTK_OBJECT(rset->endspin), "rset", rset);
    } else {
	update_obs_label(GTK_EDITABLE(GTK_COMBO(rset->combo)->entry),
			 rset);
    }
   
    /* Create the "OK" button */
    tempwid = gtk_button_new_with_label (_("OK"));
    GTK_WIDGET_SET_FLAGS(tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG (rset->dlg)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(tempwid), "clicked",
		       GTK_SIGNAL_FUNC(set_sample_from_dialog), rset);
    gtk_widget_grab_default (tempwid);

    /* And a Cancel button */
    tempwid = gtk_button_new_with_label (_("Cancel"));
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG(rset->dlg)->action_area), 
			tempwid, TRUE, TRUE, 0);
    gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", 
			GTK_SIGNAL_FUNC(destroy_rset), rset->dlg);

    gtk_widget_show_all(rset->dlg);
}

/* ARMA options stuff */

struct arma_options {
    int v;
    GtkWidget *dlg;
    GtkWidget *arspin;
    GtkWidget *maspin;
    GtkWidget *verbcheck;
#ifdef HAVE_X12A
    GtkWidget *x12check;
#endif
};

static void free_arma_opts (GtkWidget *w, struct arma_options *opts)
{
    free(opts);
}

static void destroy_arma_opts (GtkWidget *w, gpointer p)
{
    gtk_widget_destroy(GTK_WIDGET(p));
}

static void exec_arma_opts (GtkWidget *w, struct arma_options *opts)
{
    int ar, ma;
    unsigned long aopt = 0L;

    ar = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(opts->arspin));
    ma = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(opts->maspin));
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->verbcheck))) {
	aopt |= OPT_V;
    }	
#ifdef HAVE_X12A
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opts->x12check))) {
	aopt |= OPT_X;
    }
#endif

    do_arma(opts->v, ar, ma, aopt);

    gtk_widget_destroy(GTK_WIDGET(opts->dlg));
}

void arma_options_dialog (gpointer p, guint u, GtkWidget *w)
{
    GtkWidget *tmp, *hbox;
    GSList *group;    
    struct arma_options *opts;
    GtkAdjustment *adj;

    opts = mymalloc(sizeof *opts);
    if (opts == NULL) return;
    
    opts->dlg = gtk_dialog_new();
    opts->v = mdata->active_var;

    gtk_signal_connect (GTK_OBJECT(opts->dlg), "destroy", 
			GTK_SIGNAL_FUNC(free_arma_opts), opts);

    gtk_window_set_title(GTK_WINDOW(opts->dlg), _("ARMA"));
    gtk_container_set_border_width (GTK_CONTAINER 
				    (GTK_DIALOG (opts->dlg)->vbox), 10);
    gtk_container_set_border_width (GTK_CONTAINER 
				    (GTK_DIALOG (opts->dlg)->action_area), 5); 
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (opts->dlg)->vbox), 5);
    gtk_window_set_position (GTK_WINDOW (opts->dlg), GTK_WIN_POS_MOUSE);

    /* horizontal box for spinners */
    hbox = gtk_hbox_new(FALSE, 5);

    /* AR spinner */
    tmp = gtk_label_new (_("AR order:"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
    adj = (GtkAdjustment *) gtk_adjustment_new(1, 0, 4, 1, 1, 0);
    opts->arspin = gtk_spin_button_new(adj, 0, 0);
    gtk_box_pack_start(GTK_BOX(hbox), opts->arspin, FALSE, FALSE, 0);

    /* MA spinner */
    tmp = gtk_label_new (_("MA order:"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
    adj = (GtkAdjustment *) gtk_adjustment_new(1, 0, 4, 1, 1, 0);
    opts->maspin = gtk_spin_button_new(adj, 0, 0);
    gtk_box_pack_start(GTK_BOX(hbox), opts->maspin, FALSE, FALSE, 0);

    /* pack the spinners */
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(opts->dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);

    /* verbosity button */
    hbox = gtk_hbox_new(FALSE, 5);
    opts->verbcheck = gtk_check_button_new_with_label(_("Show details of iterations"));
    gtk_box_pack_start(GTK_BOX(hbox), opts->verbcheck, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(opts->dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);

#ifdef HAVE_X12A
    /* separator */
    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(opts->dlg)->vbox), 
		       tmp, FALSE, FALSE, 5);

    /* native vs X-12-ARIMA radio buttons */
    hbox = gtk_hbox_new(FALSE, 5);
    hbox = gtk_hbox_new(FALSE, 5);
    tmp = gtk_radio_button_new_with_label(NULL, _("Native code"));
    gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(opts->dlg)->vbox), 
		       hbox, FALSE, FALSE, 0);

    hbox = gtk_hbox_new(FALSE, 5);
    group = gtk_radio_button_group(GTK_RADIO_BUTTON(tmp));
    opts->x12check = gtk_radio_button_new_with_label(group, _("Use X-12-ARIMA"));
    gtk_box_pack_start(GTK_BOX(hbox), opts->x12check, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(opts->dlg)->vbox), 
		       hbox, FALSE, FALSE, 5);
#endif /* HAVE_X12A */

    /* Create the "OK" button */
    tmp = gtk_button_new_with_label(_("OK"));
    GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG (opts->dlg)->action_area), 
			tmp, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(tmp), "clicked",
		       GTK_SIGNAL_FUNC(exec_arma_opts), opts);
    gtk_widget_grab_default (tmp);

    /* And a Cancel button */
    tmp = gtk_button_new_with_label(_("Cancel"));
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG(opts->dlg)->action_area), 
			tmp, TRUE, TRUE, 0);
    gtk_signal_connect (GTK_OBJECT (tmp), "clicked", 
			GTK_SIGNAL_FUNC(destroy_arma_opts), opts->dlg);

    /* plus Help */
    tmp = gtk_button_new_with_label(_("Help"));
    gtk_box_pack_start (GTK_BOX(GTK_DIALOG(opts->dlg)->action_area), 
			tmp, TRUE, TRUE, 0);
    gtk_signal_connect (GTK_OBJECT (tmp), "clicked", 
			GTK_SIGNAL_FUNC(context_help), 
			GINT_TO_POINTER(ARMA));

    gtk_widget_show_all(opts->dlg);
}

/* ........................................................... */

#include "../pixmaps/stock_dialog_error_48.xpm"
#include "../pixmaps/stock_dialog_info_48.xpm"

static GtkWidget *get_msgbox_icon (int err)
{
    static GdkColormap *cmap;
    GtkWidget *iconw;
    GdkPixmap *icon;
    GdkBitmap *mask;
    gchar **msgxpm;

    if (err) {
	msgxpm = stock_dialog_error_48_xpm;
    } else {
	msgxpm = stock_dialog_info_48_xpm;
    }

    if (cmap == NULL) {
	cmap = gdk_colormap_get_system();
    }
    icon = gdk_pixmap_colormap_create_from_xpm_d(NULL, cmap, &mask, NULL, 
						 msgxpm);
    iconw = gtk_pixmap_new(icon, mask);

    return iconw;
}

static void msgbox_close (GtkWidget *w, gpointer p)
{
    gtk_widget_destroy(GTK_WIDGET(p));
    gtk_main_quit();
}

static void msgbox (const char *msg, int err) 
{
    GtkWidget *w, *label, *button, *vbox, *hbox, *hsep, *iconw;

    w = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_container_border_width(GTK_CONTAINER(w), 5);
    gtk_window_position (GTK_WINDOW(w), GTK_WIN_POS_MOUSE);
    gtk_window_set_title (GTK_WINDOW (w), (err)? _("gretl error") : 
			  _("gretl info")); 

    vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(w), vbox);

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(vbox), hbox);

    /* icon */
    iconw = get_msgbox_icon(err);
    gtk_box_pack_start(GTK_BOX(hbox), iconw, FALSE, FALSE, 5);

    /* text of message */
    label = gtk_label_new(msg);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

    hsep = gtk_hseparator_new();
    gtk_container_add(GTK_CONTAINER(vbox), hsep);

    /* button */
    hbox = gtk_hbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(vbox), hbox);
    
    if (err) {
	button = gtk_button_new_with_label(_("Close"));
    } else {
	button = gtk_button_new_with_label(_("OK"));
    }

    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 5);

    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(msgbox_close), w);

    gtk_widget_show_all(w);

    gtk_main();
}

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

GtkWidget *standard_button (int code)
{
    const char *button_strings[] = {
	N_("OK"),
	N_("Cancel"),
	N_("Close"),
	N_("Apply"),
	N_("Help")
    };

    return gtk_button_new_with_label(_(button_strings[code]));
}
