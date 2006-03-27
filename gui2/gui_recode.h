#ifndef GUI_RECODE_H
#define GUI_RECODE_H

int maybe_recode_file (const char *fname);

gchar *my_filename_from_utf8 (char *fname);

gchar *my_locale_from_utf8 (const gchar *src);

gchar *force_locale_from_utf8 (const gchar *src);

gchar *my_filename_to_utf8 (char *fname);

gchar *my_locale_to_utf8 (const gchar *src);

#endif
