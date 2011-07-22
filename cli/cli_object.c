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

/* cli_object.c for gretl: handle commands relating to save objects */

#include "varprint.h"

static int cli_parse_object_request (const char *line, 
				     char *objname, char **param,
				     void **pptr, GretlObjType *type,
				     PRN *prn)
{
    char word[MAXSAVENAME] = {0};
    int action, err = 0;

    /* get object name (if any) and dot param */
    parse_object_command(line, word, param);

    /* if no dot param, nothing doing */
    if (*param == NULL) {
	return OBJ_ACTION_NONE;
    }

    /* see if there's an object associated with the name */
    err = gretl_get_object_and_type(word, pptr, type);

    if (err) {
	/* no matching object (maybe OK in gui) */
	if (*param) {
	    pprintf(prn, _("%s: no such object\n"), word);
	}
	return OBJ_ACTION_NULL;
    }

    action = match_object_command(*param, *type);

    if (action == OBJ_ACTION_INVALID) {
	pprintf(prn, _("command '%s' not recognized"), *param);
	pputc(prn, '\n');
    } else {
	strcpy(objname, word);
    } 

    return action;
}

static int object_command_setup (CMD *pcmd, char *cmdstr,
				 DATASET *dset)
{
    char *myline;
    int err = 0;

    myline = malloc(MAXLINE);

    if (myline == NULL) {
	err = E_ALLOC;
    } else {
	gretl_cmd_init(pcmd);
	*myline = 0;
	strncat(myline, cmdstr, MAXLINE - 1);
	err = parse_command_line(myline, pcmd, dset);
	free(myline);
    }

    return err;
}

static int 
object_model_add_or_omit (MODEL *pmod, int action, char *cmdstr, 
			  DATASET *dset, MODEL *model, 
			  PRN *prn)
{
    CMD mycmd;
    int err;

    err = object_command_setup(&mycmd, cmdstr, dset);
    if (err) {
	return err;
    }

    if (mycmd.opt & (OPT_W | OPT_Q | OPT_Y)) {
	/* not saving a model */
	if (action == OBJ_ACTION_ADD) {
	    err = add_test(pmod, mycmd.list, dset, mycmd.opt, prn);
	} else {
	    err = omit_test(pmod, mycmd.list, dset, mycmd.opt, prn);
	}
    } else {
	MODEL mymod;

	if (action == OBJ_ACTION_ADD) {
	    err = add_test_full(pmod, &mymod, mycmd.list,
				dset, mycmd.opt, prn);
	} else {
	    err = omit_test_full(pmod, &mymod, mycmd.list,
				 dset, mycmd.opt, prn);
	}
	if (!err) {
	    clear_model(model);
	    *model = mymod;
	}
    }	

    if (err) {
	errmsg(err, prn);
    } 

    gretl_cmd_free(&mycmd);

    return err;
}

static int object_VAR_omit (GRETL_VAR *orig, char *cmdstr, 
			    DATASET *dset, PRN *prn)
{
    GRETL_VAR *var;
    CMD mycmd;
    int err;

    err = object_command_setup(&mycmd, cmdstr, dset);
    if (err) {
	return err;
    }

    var = gretl_VAR_omit_test(mycmd.list, orig, dset, 
			      prn, &err);
    gretl_VAR_free(var);

    if (err) {
	errmsg(err, prn);
    }

    gretl_cmd_free(&mycmd);

    return err;    
}

static int saved_object_action (const char *line, 
				DATASET *dset,
				MODEL *model, 
				PRN *prn)
{
    char objname[MAXSAVENAME] = {0};
    char *param = NULL;
    void *ptr = NULL;
    GretlObjType type;
    int action, err = 0;

    if (*line == '!' || *line == '#') { 
	/* shell command or comment */
	return 0;
    }

    /* special: display icon view window, not applicable in CLI program */
    if (!strncmp(line, "iconview", 8)) {
	return 1;
    }

    action = cli_parse_object_request(line, objname, &param, &ptr, &type, prn);

    if (action == OBJ_ACTION_NONE) {
	free(param);
	return 0;
    }

    if (action == OBJ_ACTION_NULL) {
	free(param);
	return 1;
    }

    if (action == OBJ_ACTION_INVALID) {
	free(param);
	return -1;
    }

    if (action == OBJ_ACTION_SHOW) {
	if (type == GRETL_OBJ_EQN) {
	    printmodel((MODEL *) ptr, dset, OPT_NONE, prn);
	} else if (type == GRETL_OBJ_VAR) {
	    gretl_VAR_print((GRETL_VAR *) ptr, dset, OPT_NONE, prn);
	} else if (type == GRETL_OBJ_SYS) {
	    err = equation_system_estimate((equation_system *) ptr, 
					   dset, OPT_NONE, prn);
	} 
    } else if (action == OBJ_ACTION_SHOW_STAT) {
	err = print_object_var(objname, param, dset, prn);
    } else if (action == OBJ_ACTION_IRF) {
	err = gretl_VAR_do_irf((GRETL_VAR *) ptr, line, dset);
    } else if (action == OBJ_ACTION_FREE) {
	gretl_object_unref(ptr, GRETL_OBJ_ANY);
    } else if (action == OBJ_ACTION_ADD) {
	err = object_model_add_or_omit((MODEL *) ptr, action, param, 
				       dset, model, prn);
    } else if (action == OBJ_ACTION_OMIT) {
	if (type == GRETL_OBJ_EQN) {
	    err = object_model_add_or_omit((MODEL *) ptr, action, param, 
					   dset, model, prn);
	} else if (type == GRETL_OBJ_VAR) {
	    err = object_VAR_omit((GRETL_VAR *) ptr, param, 
				  dset, prn);
	}
    }

    if (action == OBJ_ACTION_FREE && !err) {
	pprintf(prn, _("Freed %s\n"), objname);
    }

    free(param);

    return 1;
}
