/* doslines.c -- adjust linelengths in text file */
	
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXLEN 78

void compress_spaces (char *s)
{
    char *p;

    if (s == NULL || *s == 0) return;

    p = s;
    while (*s) {
	if (*s == '\t' || *s == '\n') *s = ' ';
	s++;
    }

    s = p;
    while (*s) {
        if (*s == ' ') {
            p = s + 1;
            if (*p == 0) break;
            while (*p == ' ') p++;
            if (p - s > 1) memmove(s + 1, p, strlen(p) + 1);
        }
        s++;
    }
}

static int blank_string (const char *s)
{
    while (*s) {
	if (!isspace(*s)) return 0;
	s++;
    }

    return 1;
}

void trim (char *s)
{
    int i, n = strlen(s);

    for (i=n-1; i>0; i--) {
	if (s[i] == ' ') {
	    s[i] = '\0';
	    break;
	}
    }
}

static int format_buf (char *buf)
{
    char *p, *q, line[80];
    int n, out;

    compress_spaces(buf);
    n = strlen(buf);

    p = buf;
    out = 0;
    while (out < n - 1) {
	*line = 0;
	q = p;
	strncat(line, p, MAXLEN);
	trim(line);
	p = q + strlen(line);
	if (!blank_string(line)) {
	    printf("%s\n", (*line == ' ')? line + 1 : line);
	}
	out += strlen(line);
    }
    
    return 0;
}

void strip_marker (char *s, int n)
{
    int i;

    for (i=0; i<n; i++) {
	s[i] = ' ';
    }
}

int main (int argc, char *argv[])
{ 
    char buf[8096];
    char line[128];
    int blank = 0, inpara = 0;
    char *p;

    while (fgets(line, sizeof line, stdin)) {
	if ((p = strstr(line, "[PARA]"))) {
	    strip_marker(p, 6);
	    *buf = 0;
	    inpara = 1;
	}
	if ((p = strstr(line, "[/PARA]"))) {
	    strip_marker(p, 7);
	    strcat(buf, line);
	    format_buf(buf);
	    inpara = 0;
	}
	
	if (inpara) {
	    strcat(buf, line);
	} else {
	    if (blank_string(line)) blank++;
	    if (blank == 2) {
		blank = 0;
	    } else {
		fputs(line, stdout);
	    }
	}
    }

    return 0;
}
