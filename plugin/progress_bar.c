/*
 *   Copyright (c) by Allin Cottrell
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

/* progress.c for gretl */

#include <gtk/gtk.h>

#ifdef UPDATER
# include <stdio.h>
# include <stdlib.h>
# include "updater.h"
#else
# include "libgretl.h"
#endif

typedef struct _ProgressData {
    GtkWidget *window;
    GtkWidget *label;
    GtkWidget *pbar;
} ProgressData;

/* ........................................................... */

static void destroy_progress (GtkWidget *widget, ProgressData **ppdata)
{
    (*ppdata)->window = NULL;
    g_free(*ppdata);
    *ppdata = NULL;
}

/* ........................................................... */

static int progress_window (ProgressData **ppdata, int flag)
{
    GtkWidget *align;
    GtkWidget *vbox;
#ifdef UPDATER
    GtkWidget *separator;
    GtkWidget *button;
#endif

    *ppdata = malloc(sizeof **ppdata);
    if (*ppdata == NULL) return 1;

    (*ppdata)->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#if GTK_MAJOR_VERSION >= 2
    gtk_window_set_resizable(GTK_WINDOW((*ppdata)->window), FALSE);

    g_signal_connect(G_OBJECT((*ppdata)->window), "destroy",
		     G_CALLBACK(destroy_progress),
		     ppdata);
#else
    gtk_window_set_policy(GTK_WINDOW((*ppdata)->window), FALSE, FALSE, TRUE);

    gtk_signal_connect(GTK_OBJECT((*ppdata)->window), "destroy",
		       GTK_SIGNAL_FUNC(destroy_progress),
		       ppdata);
#endif

    if (flag == SP_LOAD_INIT) {
	gtk_window_set_title(GTK_WINDOW((*ppdata)->window), _("gretl: loading data"));
    }
    else if (flag == SP_SAVE_INIT) {
	gtk_window_set_title(GTK_WINDOW((*ppdata)->window), _("gretl: storing data"));
    } 
    else if (flag == SP_FONT_INIT) {
	gtk_window_set_title(GTK_WINDOW((*ppdata)->window), _("gretl: scanning fonts"));
    }
	
    gtk_container_set_border_width(GTK_CONTAINER((*ppdata)->window), 0);

    vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER((*ppdata)->window), vbox);
    gtk_widget_show(vbox);

    /* Add a label */
    (*ppdata)->label = gtk_label_new("");
    gtk_widget_show((*ppdata)->label);
    gtk_box_pack_start(GTK_BOX(vbox), (*ppdata)->label, FALSE, FALSE, 0);
        
    /* Create a centering alignment object */
    align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 5);
    gtk_widget_show(align);

    /* Create the GtkProgressBar */
    (*ppdata)->pbar = gtk_progress_bar_new();
    gtk_container_add(GTK_CONTAINER(align), (*ppdata)->pbar);
#if GTK_MAJOR_VERSION < 2
    gtk_progress_set_format_string(GTK_PROGRESS((*ppdata)->pbar), "%p%%");
    gtk_progress_set_show_text(GTK_PROGRESS((*ppdata)->pbar), TRUE);
#endif
    gtk_widget_show((*ppdata)->pbar);

    /* Add separator and cancel button? */
#ifdef UPDATER
    separator = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(vbox), separator, FALSE, FALSE, 0);
    gtk_widget_show(separator);

    button = gtk_button_new_with_label(_("Cancel"));
    g_signal_connect_swapped(G_OBJECT(button), "clicked",
			     G_CALLBACK(gtk_widget_destroy),
			     (*ppdata)->window);
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

    GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
    gtk_widget_grab_default(button);
    gtk_widget_show(button);
#endif

    gtk_widget_show((*ppdata)->window);

    return 0;
}

/* ........................................................... */

int show_progress (long res, long expected, int flag)
{
    static long offs;
    static ProgressData *pdata;

    if (expected == 0) return SP_RETURN_DONE;

    if (res < 0 || flag == SP_FINISH) {
	if (pdata != NULL && pdata->window != NULL) {
	    gtk_widget_destroy(GTK_WIDGET(pdata->window)); 
	}
	return SP_RETURN_DONE;
    }

    if (flag == SP_LOAD_INIT || flag == SP_SAVE_INIT || flag == SP_FONT_INIT) {
	gchar *bytestr = NULL;

	offs = 0L;
	if (progress_window(&pdata, flag)) return 0; 
#if GTK_MAJOR_VERSION >= 2
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pdata->pbar), (gdouble) 0);
#else
	gtk_progress_bar_update(GTK_PROGRESS_BAR(pdata->pbar), (gfloat) 0);
#endif
	if (flag == SP_LOAD_INIT) {
	    bytestr = g_strdup_printf("%s %ld Kbytes", _("Retrieving"),
				      expected / 1024);
	}
	else if (flag == SP_SAVE_INIT) {
	    bytestr = g_strdup_printf("%s %ld Kbytes", _("Storing"),
				      expected / 1024);
	}
	else if (flag == SP_FONT_INIT) {
	    bytestr = g_strdup_printf(_("Scanning %ld fonts"), expected);
	}
	gtk_label_set_text(GTK_LABEL(pdata->label), bytestr);
	g_free(bytestr);
	while (gtk_events_pending()) gtk_main_iteration();
    }

    if (flag == SP_NONE && (pdata == NULL || pdata->window == NULL)) {
	return SP_RETURN_CANCELED;
    }

    offs += res;

    if (offs > expected && pdata != NULL) {
	gtk_widget_destroy(GTK_WIDGET(pdata->window)); 
	return SP_RETURN_DONE;
    }

    if (offs <= expected && pdata != NULL) {
#if GTK_MAJOR_VERSION >= 2
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pdata->pbar), 
				      (gdouble) ((double) offs / expected));
#else
	gtk_progress_bar_update(GTK_PROGRESS_BAR(pdata->pbar), 
				(gfloat) ((double) offs / expected));
#endif
	while (gtk_events_pending()) gtk_main_iteration();
    } else {
	if (pdata != NULL && pdata->window != NULL) {
	    gtk_widget_destroy(GTK_WIDGET(pdata->window)); 
	}
	return SP_RETURN_DONE;
    }
	
    return SP_RETURN_OK;
}

