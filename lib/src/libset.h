/*
 *  Copyright (c) 2004 by Allin Cottrell
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

#ifndef LIBSET_H
#define LIBSET_H

enum vcv_codes {
    VCV_UNSET,
    VCV_HESSIAN,
    VCV_IM,
    VCV_OP,
    VCV_QML,
    VCV_BW
};

void set_use_qr (int set);
int get_use_qr (void);

void set_xsect_hccme (const char *s);
void set_tseries_hccme (const char *s);

void set_garch_robust_vcv (const char *s);
int get_garch_vcv_version (void);
int get_garch_robust_vcv_version (void);

int get_force_hc (void);
int get_hc_version (void);
int get_hac_lag (int m);

int get_halt_on_error (void);

double get_hp_lambda (void);

int get_bkbp_k (void);
void get_bkbp_periods (int *periods);

int gretl_get_text_pause (void);

int get_VAR_horizon (void);

double get_nls_toler (void);
int set_nls_toler (double tol);

void set_loop_on (void);
void set_loop_off (void);
int gretl_looping (void);

void gretl_set_batch_mode (int b);
int gretl_in_batch_mode (void);

void gretl_set_gui_mode (int g);
int gretl_in_gui_mode (void);

void set_gretl_echo (int e);
int gretl_echo_on (void);

int execute_set_line (const char *line, PRN *prn);

#endif /* LIBSET_H */

