/*
 *  Copyright (c) by Allin Cottrell 2002
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

typedef struct {
    int version;
    int nsheets;
    int selected;
    int col_offset, row_offset;
    char **sheetnames;
    guint32 *byte_offsets;
    void *colspin, *rowspin;
    int d1904;
    int debug;
} wbook;

typedef struct {
    int maxcol, maxrow;
    int text_cols, text_rows;
    int col_offset, row_offset;
    int ID;
    char *name;
    double **Z;
    char **varname;
    char **label;
} wsheet;

