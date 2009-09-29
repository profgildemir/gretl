/* Reader for SAS xport files */

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include "libgretl.h"
#include "gretl_string_table.h"
#include "swap_bytes.h"

#ifdef WORDS_BIGENDIAN
# define HOST_ENDIAN G_BIG_ENDIAN
#else
# define HOST_ENDIAN G_LITTLE_ENDIAN
#endif

struct SAS_namestr {
    short ntype;
    short nhfun;
    short nlng;
    short nvar0;
    char nname[8];
    char nlabel[40];
    char nform[8];
    short nf1;
    short nfd;
    short nfj;
    char nfill[2];
    char niform[8];
    short nifl;
    short nifd;
    long npos;
};

struct SAS_varinfo {
    int type;
    int size;
    int pos;
    char name[9];
};

struct SAS_fileinfo {
    int opt;       /* option flags */
    int nmembers;  /* number of member datasets */
    int mem2pos;   /* starting offset of second dataset (if present) */
    int nvars;     /* number of variables in first dataset */
    int obsize;    /* size of each observation in bytes */
    int nobs;      /* computed number of observations */
    int nobs_read; /* numbe of obs actually read */
    int maxclen;   /* max. length of character variables, bytes */
    int finished;  /* finished reading data */
    struct SAS_varinfo *vars;
};

enum {
    XPT_LIBRARY = 1,
    XPT_MEMBER,
    XPT_DSCRPTR,
    XPT_NAMESTR,
    XPT_SASREC,
    XPT_DATEREC,
    XPT_OBSREC
};

enum {
    XPT_NUMERIC = 1,
    XPT_CHARACTER
};

enum {
    OPT_CSV = 1
};

#define VERBOSE 1
#define MAXOBS 0
#define CSV_DIG 8

static void SAS_fileinfo_init (struct SAS_fileinfo *finfo,
			       int opt)
{
    finfo->opt = opt;
    finfo->nmembers = 0;
    finfo->mem2pos = 0;
    finfo->nvars = 0;
    finfo->obsize = 0;
    finfo->nobs = 0;
    finfo->nobs_read = 0;
    finfo->maxclen = 0;
    finfo->finished = 0;
    finfo->vars = NULL;
}

#if HOST_ENDIAN == G_LITTLE_ENDIAN
void XREV (char *intp, int k) 
{
    int i, j = k/2;
    char save;

    for (i=0; i<j; i++) {
	save = intp[i];
	intp[i] = intp[k-i-1]; 
	intp[k-i-1] = save;
    }
} 
#else
# define XREV(a,b)
#endif 

void xpt_to_ieee (unsigned char *xport, unsigned char *ieee)
{
    char temp[8];
    register int shift;
    register int nib;
    unsigned long ieee1, ieee2;
    unsigned long xport1 = 0;
    unsigned long xport2 = 0;

    memcpy(temp, xport, 8);
    memset(ieee, 0, 8);

    if (*temp && memcmp(temp+1, ieee, 7) == 0) {
	ieee[0] = ieee[1] = 0xff;
	ieee[2] = ~(*temp);
	return;
    }

    memcpy(((char *) &xport1) + sizeof xport1 - 4, temp, 4); 
    XREV((char *) &xport1, sizeof xport1);
    memcpy(((char *) &xport2) + sizeof xport2 - 4, temp+4, 4); 
    XREV((char *) &xport2, sizeof xport2);

    ieee1 = xport1 & 0x00ffffff;

    if (ieee2 != xport2 && !xport1) {
	return;
    }

    nib = (int) xport1;

    if (nib & 0x00800000) {
	shift = 3;
    } else if (nib & 0x00400000) {
	shift = 2;
    } else if (nib & 0x00200000) {
	shift = 1;
    } else {
	shift = 0;
    }

    if (shift) {
	ieee1 >>= shift;
	ieee2 = (xport2 >> shift) |
	    ((xport1 & 0x00000007) << (29 + (3 - shift)));
    }

    ieee1 &= 0xffefffff;

    ieee1 |=
	(((((long)(*temp & 0x7f) - 65) << 2) + shift + 1023) << 20) |
	(xport1 & 0x80000000);

    XREV((char *) &ieee1, sizeof ieee1); 
    memcpy(ieee, ((char *) &ieee1) + sizeof(unsigned long) - 4, 4);
    XREV((char *) &ieee2, sizeof ieee2); 
    memcpy(ieee+4, ((char *) &ieee2) + sizeof(unsigned long) - 4, 4);
}

union xswitch {
    char s[8];
    double x;
};

static double read_xpt (char *src) 
{
    union xswitch xs;
    char temp[8]; 
    double x;
    int i, na = 0;

    memcpy(temp, src, 8); 
    i = temp[0];

    if (i == 0x5f || i == 0x2e || (i >= 0x41 && i <= 0x5a)) {
	/* SAS "NA" bytes (followed by zero bytes) */
	na = 1;
	for (i=1; i<8; i++) {
	    if (temp[i] != 0x00) {
		na = 0;
		break;
	    }
	}
    }

    if (na) {
	x = -999; /* FIXME */
    } else {
	xpt_to_ieee((unsigned char *) src, (unsigned char *) xs.s); 
#if HOST_ENDIAN == G_LITTLE_ENDIAN
	memcpy(temp, xs.s, 8); 
	for (i=7; i>=0; i--) {
	    xs.s[7-i] = temp[i]; 
	} 
#endif
	x = xs.x;
    }

    return x;
} 

static int read_namestr (FILE *fp, int nsize, int j, struct SAS_varinfo *var)
{
    struct SAS_namestr nstr;
    char vname[9] = {0};
    char label[42] = {0};

    if (fread(&nstr, sizeof nstr, 1, fp) != 1) {
	fprintf(stderr, "couldn't read NAMESTR record\n");
	return 1;
    }

    strncat(vname, nstr.nname, 8);
    tailstrip(vname);

    strncat(label, nstr.nlabel, 40);
    tailstrip(label);

#if HOST_ENDIAN == G_LITTLE_ENDIAN
    reverse_short(nstr.ntype);
    reverse_short(nstr.nlng);
    reverse_uint(nstr.npos);
#endif

    fprintf(stderr, "%d: '%s': '%s'\n", j+1, vname, label);
    fprintf(stderr, " ntype = %d, nlng = %d, npos = %d\n", 
	    (int) nstr.ntype, (int) nstr.nlng, (int) nstr.npos);

    if (nstr.ntype == XPT_NUMERIC && nstr.nlng != 8) {
	fprintf(stderr, "size of data values != 8, don't know how to handle this\n");
	return 1;
    }

    var->type = nstr.ntype;
    var->size = nstr.nlng;
    var->pos = nstr.npos;
    strcpy(var->name, vname);

    /* skip to end */
    fseek(fp, nsize - sizeof nstr, SEEK_CUR);

    return 0;
}

static int is_date_record (char *buf)
{
    char dstr[18], mon[4];
    int dd, yy, hh, mm, ss;
    

    *dstr = '\0';
    strncat(dstr, buf, 16);

    if (sscanf(dstr, "%2d%3s%2d:%2d:%2d:%2d", 
	       &dd, mon, &yy, &hh, &mm, &ss) == 6) {
	fprintf(stderr, "date: %02d %s %02d, %02d:%02d:%02d\n",
		dd, mon, yy, hh, mm, ss);
	return 1;
    }

    return 0;
}

/* this should be 140, or possible 136 in wacky cases */

static int member_get_namestr_size (char *buf)
{
    char tmp[4] = {0};

    strncat(tmp, buf + 75, 3);
    return atoi(tmp);
}

/* retrieve the number of variables from a NAMESTR record */

static int namestr_get_nvars (char *buf)
{
    char tmp[5] = {0};
    int nvars;
    
    strncat(tmp, buf + 54, 4);
    nvars = atoi(tmp);
    fprintf(stderr, "nvars = %d\n", nvars);
    return nvars;
}

static int header_type (char *buf, int quiet)
{
    char hstr[8];
    int ret = 0;

    *hstr = '\0';
    strncat(hstr, buf, 13);

    if (!strcmp(hstr, "HEADER RECORD")) {
	*hstr = '\0';
	strncat(hstr, buf + 20, 7);
	tailstrip(hstr);
	if (!strcmp(hstr, "LIBRARY")) {
	    ret = XPT_LIBRARY;
	} else if (!strcmp(hstr, "MEMBER")) {
	    ret = XPT_MEMBER;
	} else if (!strcmp(hstr, "DSCRPTR")) {
	    ret = XPT_DSCRPTR;
	} else if (!strcmp(hstr, "NAMESTR")) {
	    ret = XPT_NAMESTR;
	} else if (!strcmp(hstr, "OBS")) {
	    ret = XPT_OBSREC;
	}
	if (!quiet) {
	    fprintf(stderr, "Got HEADER RECORD: %s\n", hstr);
	}
    } else if (!strncmp(hstr, "SAS     ", 8)) {
	if (!quiet) {
	    fprintf(stderr, "Got SAS record\n");
	}
	ret = XPT_SASREC;
    } else if (is_date_record(buf)) {
	if (!quiet) {
	    fprintf(stderr, "Got date record\n");
	}
	ret = XPT_DATEREC;
    } 

    return ret;
}

/* try to infer the number fo observations in a MEMBER
   dataset */

static int get_nobs (FILE *fp, struct SAS_fileinfo *finfo)
{
    int epos, pos = ftell(fp);
    int bytes;

    if (finfo->mem2pos > 0) {
	epos = finfo->mem2pos;
    } else {
	fseek(fp, 0L, SEEK_END);
	epos = ftell(fp);
	fseek(fp, pos, SEEK_SET);
    }

    bytes = epos - pos;
    
    fprintf(stderr, "pos = %d, end = %d: data bytes = %d\n", 
	    pos, epos, bytes);

    if (bytes > 0 && finfo->nvars > 0) {
	int i, rem = bytes % 80;
	int maxpos = 0, imax = 0;
	int ostest = 0;

	if (rem > 0) {
	    fprintf(stderr, "%d trailing bytes?\n", rem);
	    bytes -= rem;
	}

	for (i=0; i<finfo->nvars; i++) {
	    if (finfo->vars[i].pos > maxpos) {
		maxpos = finfo->vars[i].pos;
		imax = i;
	    }
	    if (finfo->vars[i].type == XPT_CHARACTER) {
		if (finfo->vars[i].size > finfo->maxclen) {
		    finfo->maxclen = finfo->vars[i].size;
		}
	    }
	    ostest += finfo->vars[i].size;
	}

	finfo->obsize = maxpos + finfo->vars[imax].size;
	finfo->nobs = bytes / finfo->obsize;
	fprintf(stderr, "nobs = %d?\n", finfo->nobs);
	fprintf(stderr, "max char length = %d\n", finfo->maxclen);
	fprintf(stderr, "obsize = %d (test count = %d)\n", finfo->obsize, ostest);
    }

    return (finfo->nobs == 0);
}

#if 0
static int write_subset_file (FILE *fp)
{
    int len = ftell(fp);
    int bytes = 0;
    char buf[80];
    FILE *fq;

    fq = fopen("subset.xpt", "wb");

    if (fq == NULL) {
	return 1;
    }

    rewind(fp);

    while (bytes < len) {
	bytes += fread(buf, 1, sizeof buf, fp);
	fwrite(buf, 1, sizeof buf, fq);
    }

    fclose(fq);

    return 0;
}
#endif

static int real_read_data (FILE *fp, struct SAS_fileinfo *finfo)
{
    char *buf = NULL, *cbuf = NULL;
    char c8[8];
    double x;
    int pos, i, t;

    if (finfo->maxclen > 0) {
	cbuf = malloc(finfo->maxclen + 1);
	if (cbuf == NULL) {
	    return 1;
	}
    }

    buf = malloc(finfo->obsize);
    if (buf == NULL) {
	free(cbuf);
	return 1;
    }

    if (finfo->opt == OPT_CSV) {
	for (i=0; i<finfo->nvars; i++) {
	    printf("%s", finfo->vars[i].name);
	    putchar((i < finfo->nvars - 1)? ',' : '\n');
	}
    }

    t = 0;
    while (fread(buf, 1, finfo->obsize, fp) == finfo->obsize) {
	for (i=0; i<finfo->nvars; i++) {
	    pos = finfo->vars[i].pos;
	    if (finfo->vars[i].type == XPT_NUMERIC) {
		memcpy(c8, buf + pos, 8);
		x = read_xpt(c8);
		if (finfo->opt == OPT_CSV) {
		    printf("%.*g", CSV_DIG, x);
		}
	    } else {
		/* character data */
		*cbuf = '\0';
		strncat(cbuf, buf + pos, finfo->vars[i].size);
		tailstrip(cbuf);
		if (finfo->opt == OPT_CSV) {
		    printf("\"%s\"", cbuf);
		}
	    }
	    if (finfo->opt == OPT_CSV) {
		putchar((i < finfo->nvars - 1)? ',' : '\n');
	    }
	}
	t++;
	if (t == MAXOBS) {
	    fprintf(stderr, "hit MAXOBS=%d at pos=%d\n", MAXOBS, (int) ftell(fp));
	    break;
	}
    }

    if (t > 0) {
	fprintf(stderr, "\nread %d observations\n", t);
	finfo->nobs_read = t;
	finfo->finished = 1;
    }

    free(buf);
    free(cbuf);

    return 0;
}

static int SAS_read_data (FILE *fp, struct SAS_fileinfo *finfo)
{
    char buf[160];
    int htype, rem, nsize = 140;
    size_t nb;
    int j, err = 0;

    nb = fread(buf, 1, 80, fp);
    if (nb != 80) {
	return 1;
    }

    htype = header_type(buf, 0);
    if (htype == 0) {
	fprintf(stderr, "Got some unexpected bytes\n");
	return 1;
    }

    if (htype == XPT_MEMBER) {
	nsize = member_get_namestr_size(buf);
	fprintf(stderr, "member: got nsize = %d\n", nsize);
	if (nsize != 140 && nsize != 136) {
	    err = 1;
	}
    } else if (htype == XPT_NAMESTR) {
	finfo->nvars = namestr_get_nvars(buf);
	if (finfo->nvars <= 0) {
	    err = 1;
	} else {
	    finfo->vars = malloc(finfo->nvars * sizeof *finfo->vars);
	    if (finfo->vars == NULL) {
		err = 1;
	    } 
	    for (j=0; j<finfo->nvars && !err; j++) {
		err = read_namestr(fp, nsize, j, &finfo->vars[j]);
	    }
	}
	if (!err) {
	    /* skip any padding */
	    rem = (finfo->nvars * nsize) % 80;
	    if (rem > 0) {
		rem = 80 - rem;
		fprintf(stderr, "eating %d bytes of padding\n", rem);
		fseek(fp, rem, SEEK_CUR);
	    }
	}
	if (!err) {
	    /* now we should get an OBS record */
	    nb = fread(buf, 1, 80, fp);
	    if (nb != 80) {
		err = 1;
	    }
	}
	if (!err) {
	    if (header_type(buf, 0) == XPT_OBSREC) {
		err = get_nobs(fp, finfo);
		if (!err) {
		    err = real_read_data(fp, finfo);
		}
	    } else {
		fprintf(stderr, "Expected OBS record\n");
		err = 1;
	    }
	}
    }

    return err;
}

static int SAS_read_global_header (FILE *fp, struct SAS_fileinfo *finfo)
{
    char buf[240];
    int err = 0;

    /* we start with 3 80-bytes records */
    if (fread(buf, 1, 240, fp) != 240) {
	err = 1;
    } else if (header_type(buf, 0) != XPT_LIBRARY) {
	err = 1;
    } 

    if (err) {
	fprintf(stderr, "Failed to read SAS global headers\n");
    } else {
	/* find out how many member records there are */
	while (fread(buf, 1, 80, fp)) {
	    if (header_type(buf, 1) == XPT_MEMBER) {
		finfo->nmembers += 1;
		if (finfo->nmembers == 2) {
		    finfo->mem2pos = ftell(fp);
		}
	    }
	}
	fseek(fp, 240, SEEK_SET);
    }

    return err;
}

int xport_get_data (const char *fname, 
		    double ***pZ, DATAINFO *pdinfo,
		    gretlopt opt, PRN *prn)
{
    struct SAS_fileinfo finfo;
    FILE *fp;
    double **newZ = NULL;
    DATAINFO *newinfo = NULL;
    gretl_string_table *st = NULL;
    int err = 0;

    fp = gretl_fopen(fname, "rb");
    if (fp == NULL) {
	return E_FOPEN;
    }

    SAS_fileinfo_init(&finfo, opt);

    err = SAS_read_global_header(fp, &finfo);

    fprintf(stderr, "nmembers = %d\n", finfo.nmembers);
    if (finfo.nmembers > 1) {
	fprintf(stderr, "position of member 2 = %d\n", finfo.mem2pos);
    }

    /* FIXME extract full dimenstions of dataset first */

    while (!err && !finfo.finished) {
	err = SAS_read_data(fp, &finfo);
    }

    fclose(fp);

    free(finfo.vars);

#if 0 /* not yet */
    newinfo = datainfo_new();
    if (newinfo == NULL) {
	pputs(prn, _("Out of memory\n"));
	fclose(fp);
	return E_ALLOC;
    }

    newinfo->v = finfo.nvars + 1;
    newinfo->n = finfo.nobs;

    err = start_new_Z(&newZ, newinfo, 0);
    if (err) {
	pputs(prn, _("Out of memory\n"));
	free_datainfo(newinfo);
	fclose(fp);
	return E_ALLOC;
    }	

    if (err) {
	destroy_dataset(newZ, newinfo);
	if (st != NULL) {
	    gretl_string_table_destroy(st);
	}	
    } else {
	int nvtarg = newinfo->v - 1;

	if (nvread < nvtarg) {
	    dataset_drop_last_variables(nvtarg - nvread, &newZ, newinfo);
	}
	
	if (fix_varname_duplicates(newinfo)) {
	    pputs(prn, _("warning: some variable names were duplicated\n"));
	}

	if (st != NULL) {
	    gretl_string_table_print(st, newinfo, fname, prn);
	    gretl_string_table_destroy(st);
	}

	err = merge_or_replace_data(pZ, pdinfo, &newZ, &newinfo, opt, prn);
    
	if (!err) {
	    dataset_add_import_info(pdinfo, fname, GRETL_SAS);
	}
    }

#endif

    return err;
}  
