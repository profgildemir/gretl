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

#ifndef GUI_UTILS_H
#define GUI_UTILS_H

#define standard_button(s) gtk_button_new_from_stock(s)

void gretl_stock_icons_init (void);

#ifdef ENABLE_NLS
gchar *menu_translate (const gchar *path, gpointer p);
#endif

int probably_native_datafile (const char *fname);

int probably_script_file (const char *fname);

int probably_session_file (const char *fname);

int gretl_mkdir (const char *path);

int copyfile (const char *src, const char *dest);

int isdir (const char *path);

FILE *gretl_tempfile_open (char *fname);

int gretl_tempname (char *fname);

void delete_widget (GtkWidget *widget, gpointer data);

void *mymalloc (size_t size); 

void *myrealloc (void *ptr, size_t size);

void nomem (void);

void mark_dataset_as_modified (void);

void register_data (char *fname, const char *user_fname,
		    int record);

void do_open_data (GtkWidget *w, gpointer data, int code);

void verify_open_data (gpointer userdata, int code);

void verify_open_session (void);

void close_window (gpointer data, guint win_code, GtkWidget *widget);

void windata_init (windata_t *mydata);

void free_windata (GtkWidget *w, gpointer data);

void winstack_init (void);

void winstack_destroy (void);

int winstack_match_data (gpointer p);

GtkWidget *match_window_by_data (gpointer p);

void mark_content_saved (windata_t *vwin);

windata_t *view_buffer (PRN *prn, int hsize, int vsize, 
			const char *title, int role,
			gpointer data);

windata_t *view_file (const char *filename, int editable, int del_file, 
		      int hsize, int vsize, int role);

windata_t *
view_help_file (const char *filename, int role, GtkItemFactoryEntry *menu_items);

windata_t *edit_buffer (char **pbuf, int hsize, int vsize, 
			char *title, int role);

int view_model (PRN *prn, MODEL *pmod, int hsize, int vsize, 
		char *title);

int highest_numbered_variable_in_winstack (void);

void view_window_set_editable (windata_t *vwin);

int validate_varname (const char *varname);

gretlopt get_tex_eqn_opt (void);

gint popup_menu_handler (GtkWidget *widget, GdkEvent *event,
			 gpointer data);

void add_popup_item (const gchar *label, GtkWidget *menu,
		     GCallback callback, gpointer data);

void get_stats_table (void);

void *gui_get_plugin_function (const char *funcname, 
			       void **phandle);

int get_worksheet_data (char *fname, int datatype, int append,
			int *gui_get_data);

char *double_underscores (char *targ, const char *src);

#ifndef G_OS_WIN32
void startR (const char *Rcommand);
int browser_open (const char *url);
#endif

#if defined(HAVE_FLITE) || defined(G_OS_WIN32)
enum {
    AUDIO_TEXT = 0,
    AUDIO_LISTBOX
} audio_render_keys;

void audio_render_window (windata_t *vwin, int key);
#endif

#endif /* GUI_UTILS_H */
