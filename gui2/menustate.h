#ifndef MENUSTATE_H
#define MENUSTATE_H

void refresh_data (void);

void gretl_set_window_modal (GtkWidget *w);
void gretl_set_window_quasi_modal (GtkWidget *w);

void flip (GtkUIManager *ui, const char *path, gboolean s);

void edit_info_state (gboolean s);
void add_remove_markers_state (gboolean s);
void variable_menu_state (gboolean s);
void main_menubar_state (gboolean s);
void time_series_menu_state (gboolean s);
void panel_menu_state (gboolean s);
void ts_or_panel_menu_state (gboolean s);
void session_menu_state (gboolean s);
void restore_sample_state (gboolean s);
void compact_data_state (gboolean s);
void drop_obs_state (gboolean s);

void main_menus_enable (gboolean s);

GtkWidget *build_var_popup (void);
GtkWidget *build_selection_popup (void);

void clear_sample_label (void);
void set_sample_label (DATAINFO *pdinfo);
void set_main_window_title (const char *name, gboolean modified);

void action_entry_init (GtkActionEntry *entry);

int vwin_add_ui (windata_t *vwin, GtkActionEntry *entries,
		 gint n_entries, const gchar *ui_info);

int vwin_menu_add_item (windata_t *vwin, const gchar *path, 
			GtkActionEntry *entry);

int vwin_menu_add_items (windata_t *vwin, const gchar *path, 
			 GtkActionEntry *entries, int n);

int vwin_menu_add_radios (windata_t *vwin, const gchar *path, 
			  GtkRadioActionEntry *entries, int n,
			  int deflt, GCallback callback);

int vwin_menu_add_menu (windata_t *vwin, const gchar *path, 
			GtkActionEntry *entry);

void vwin_menu_add_separator (windata_t *vwin, const gchar *path);

#endif /* MENUSTATE_H */
