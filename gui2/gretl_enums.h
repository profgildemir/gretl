#ifndef GRETL_ENUMS_H
#define GRETL_ENUMS_H

enum extra_cmds {
    RELABEL = NC,
    VSETMISS,
    GSETMISS,
    SMPLDUM,
    SMPLBOOL,
    SMPLRAND,
    SETSEED,
    MARKERS,
    STORE_MODEL,
    VAR_SUMMARY,
    CORR_SELECTED,
    SUMMARY_SELECTED,
    GENR_NORMAL,
    GENR_UNIFORM,
    ONLINE,
    EXPORT,
    MEANTEST2,
    MODEL_GENR,
    GR_PLOT,
    GR_XY,
    GR_IMP,
    GR_DUMMY,
    GR_BOX,
    GR_NBOX,
    GR_3D,
    COMPACT,
    COEFFINT,
    COVAR,
    STAT_TABLE,
    H_TEST,
    TRAMO,
    X12A,
    VIEW_SERIES,
    VIEW_SCALAR,
    VIEW_MODEL,
    VIEW_LOG,
    VIEW_DATA,
    VIEW_SCRIPT,
    VIEW_CODEBOOK,
    VIEW_MODELTABLE,
    VIEW_FILE,
    DATA_REPORT,
    SCRIPT_OUT,
    CONSOLE,
    EDIT_HEADER,
    EDIT_SCRIPT,
    EDIT_NOTES,
    CLI_HELP,
    GUI_HELP,
    CLI_HELP_ENGLISH,
    GUI_HELP_ENGLISH,
    MODELTABLE,
    GRAPHPAGE,
    CREATE_USERDIR,
    LMTEST_LOGS,
    LMTEST_SQUARES,
    LMTEST_WHITE,
    LMTEST_BG,
    LMTEST_GROUPWISE,
    KERNEL_DENSITY,
    GUI_CMD_MAX
};

enum file_ops {
    OPEN_DATA = GUI_CMD_MAX + 1, /* don't collide with extra_cmds */
    OPEN_DB,
    OPEN_RATSDB,
    OPEN_SCRIPT,
    OPEN_CSV,
    APPEND_DATA,
    APPEND_CSV,
    OPEN_ASCII,
    APPEND_ASCII,
    OPEN_BOX,
    OPEN_GNUMERIC,
    APPEND_GNUMERIC,
    OPEN_EXCEL,
    APPEND_EXCEL,
    OPEN_SESSION,
    END_OPEN,      /* marker for end of file open section */
    SAVE_DATA,
    SAVE_DATA_AS,
    SAVE_GZDATA,
    SAVE_BIN1,
    SAVE_BIN2,
    EXPORT_OCTAVE,
    EXPORT_R,
    EXPORT_R_ALT,
    EXPORT_CSV,
    EXPORT_DAT,
    COPY_CSV,
    END_SAVE_DATA,  /* marker for end of data-saving section */
    SAVE_CMDS,
    SAVE_TEX_TAB,
    SAVE_TEX_EQ,
    SAVE_TEX_TAB_FRAG,
    SAVE_TEX_EQ_FRAG,
    SAVE_SCRIPT,
    SAVE_OUTPUT,
    SAVE_SESSION,
    SAVE_MODEL,
    SAVE_GNUPLOT,
    SAVE_BOXPLOT_EPS,
    SAVE_BOXPLOT_PS,
    SAVE_BOXPLOT_XPM,
    SAVE_LAST_GRAPH,
    SAVE_THIS_GRAPH,
    SAVE_GP_CMDS,
    SAVE_CONSOLE,
    SET_PATH,
    FILE_OP_MAX
};

#define SAVE_DATA_ACTION(i) (i >= SAVE_DATA && i < END_SAVE_DATA)

enum browser_codes {
    TEXTBOOK_DATA = FILE_OP_MAX + 1, /* don't collide with file_ops enum */
    PS_FILES,
    NATIVE_DB,
    RATS_DB,
    REMOTE_DB,
    NATIVE_SERIES,
    RATS_SERIES,
    REMOTE_SERIES,
    MAINWIN
};

enum stat_codes {
    ESS = 1,
    R2,
    TR2,
    DF,
    SIGMA,
    LNL
};

enum exec_codes {
    CONSOLE_EXEC,
    SCRIPT_EXEC,
    SESSION_EXEC,
    REBUILD_EXEC,
    SAVE_SESSION_EXEC
};

enum clipstuff {
    TARGET_STRING,
    TARGET_TEXT,
    TARGET_COMPOUND_TEXT
};

enum copy_variants {
    COPY_SELECTION = 1,
    COPY_TEXT,
    COPY_LATEX,
    COPY_LATEX_EQUATION,
    COPY_RTF,
    COPY_TEXT_AS_RTF,
    COPY_EMF
};

enum data_status {
    HAVE_DATA     = 1 << 0,
    BOOK_DATA     = 1 << 1,
    USER_DATA     = 1 << 2,
    IMPORT_DATA   = 1 << 3,
    GUI_DATA      = 1 << 4,
    MODIFIED_DATA = 1 << 5,
    GZIPPED_DATA  = 1 << 6
};

enum drag_types {
    GRETL_FILENAME,
    GRETL_POINTER,
    GRETL_MODEL_POINTER
};

enum file_lists {
    FILE_LIST_DATA,
    FILE_LIST_SESSION,
    FILE_LIST_SCRIPT,
};

enum graph_types {
    GRETL_GNUPLOT_GRAPH,
    GRETL_BOXPLOT
};

enum varclick_actions {
    VARCLICK_NONE,
    VARCLICK_INSERT_ID,
    VARCLICK_INSERT_NAME,
    VARCLICK_INSERT_TEXT
};

enum font_selections {
    FIXED_FONT_SELECTION,
    APP_FONT_SELECTION,
    GRAPH_FONT_SELECTION
};

enum latex_views {
    LATEX_VIEW_TABULAR,
    LATEX_VIEW_EQUATION,
    LATEX_VIEW_MODELTABLE
};

enum calc_functions {
    CALC_PVAL,
    CALC_DIST,
    CALC_TEST
};

#endif /* GRETL_ENUMS_H */
