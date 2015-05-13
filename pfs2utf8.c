/*
** pfs2utf8.c - Peter's Filesystem-to-UTF8 Converter
**
** Copyright (c) 2015 Peter Eriksson <pen@lysator.liu.se>
*/

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ftw.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <iconv.h>
#include <sys/stat.h>


char *argv0 = "pfs2utf8";
extern char version[];

char *src_charset = "ISO8859-1";
char *dst_charset = "UTF-8";

int debug = 0;
int verbose = 0;
int nowrite = 0;

int fix_flag = 0;
int qp_flag = 0;
int hex_flag = 0;
int all_flag = 0;
int errexit_flag = 0;
int fix_ws_flag = 0;


FILE *logfile = NULL;

unsigned long long nf = 0;
unsigned long long nd = 0;
unsigned long long ns = 0;
unsigned long long nm = 0;

unsigned long long n_ascii = 0;
unsigned long long n_iso8859_1 = 0;
unsigned long long n_utf_8 = 0;
unsigned long long n_other = 0;

unsigned long long n_fixed = 0;

iconv_t ic = NULL;


void
print_hex(const char *buf,
	  FILE *fp)
{
    const unsigned char *p;

    
    p = (const unsigned char *) buf;
    while (*p) {
	fprintf(fp, "%02x", (int) (*p));
	if (p[1])
	    putc(' ', fp);
	++p;
    }
}


void
print_qstr(const char *buf,
	   FILE *fp)
{
    unsigned char *bp;


    if (!buf)
    {
	fprintf(fp, "<null>");
	return;
    }
    
    for (bp = (unsigned char *) buf; *bp; ++bp)
	if (*bp == '(' || *bp == ')' || *bp == '"' || *bp == '\'')
	{
	    putc('\\', fp);
	    putc(*bp, fp);
	}
	else if (*bp == ' ')
	{
	    putc('\\', fp);
	    putc(' ', fp);
	}
	else if (*bp == '\t')
	{
	    putc('\\', fp);
	    putc('t', fp);
	}
	else if (*bp == '\n')
	{
	    putc('\\', fp);
	    putc('n', fp);
	}
	else if (*bp == '\r')
	{
	    putc('\\', fp);
	    putc('r', fp);
	}
	else if (*bp > ' ' && *bp <= '~')
	    putc(*bp, fp);
	else 
	    fprintf(fp, "\\%03o", *bp);
}


int
rename_in_dir(const char *dir,
	      const char *old,
	      const char *new)
{
    char *oldp, *newp;
    int rc;
    struct stat sb;
    
    
    oldp = malloc(strlen(dir)+strlen(old)+2);
    if (!oldp)
	return -1;
    
    newp = malloc(strlen(dir)+strlen(new)+2);
    if (!newp)
    {
	free(oldp);
	return -1;
    }
    
    strcpy(oldp, dir);
    strcat(oldp, "/");
    strcat(oldp, old);
    
    strcpy(newp, dir);
    strcat(newp, "/");
    strcat(newp, new);

    if (debug > 2)
    {
	fprintf(stderr, "*** rename:\n  old=");
	print_qstr(oldp, stderr);
	fprintf(stderr, "\n  new=");
	print_qstr(newp, stderr);
	putc('\n', stderr);
    }

    if (stat(newp, &sb) == 0)
    {
	errno = EEXIST;
	return -1;
    }
    
    if (!nowrite)
	rc = rename(oldp, newp);
    else
	rc = 0;
    
    free(oldp);
    free(newp);
    
    return rc;
}


int
is_iso8859_1(const char *in)
{
    const unsigned char *ip;

    for (ip = (const unsigned char *) in; *ip; ++ip)
	if (*ip < ' ' || (*ip >= 0x7F && *ip <= 0xA0))
	    return 0;

    return 1;
}


int
is_windows_1252(const char *in)
{
    const unsigned char *ip;

    for (ip = (const unsigned char *) in; *ip; ++ip)
	if (*ip < ' ' || *ip == 0x7F ||
	    *ip == 0x81 || *ip == 0x8D || *ip == 0x8F ||
	    *ip == 0x90 || *ip == 0x9D)
	    return 0;
    
    return 1;
}



int
is_utf_8(const char *in)
{
    const unsigned char *ip;
    int state = 0;
    int len = 0;

    
    for (ip = (unsigned char *) in; *ip; ++ip)
    {
	if (debug > 4)
	    fprintf(stderr, "*** is_utf_8: state=%d, len=%d, char=0x%02x\n",
		    state, len, (unsigned int) *ip);

	if (*ip == 0xC0 || *ip == 0xC1 || *ip >= 0xF5) /* Illegal UTF-8 bytes */
	{
	    if (debug > 3)
		fprintf(stderr, "*** is_utf_8: Failed 1 at %lu (%s)\n",
			(unsigned long) (ip - (const unsigned char *) in), in);
	    return 0;
	}
	switch (state)
	{
	  case 0:
	    if (*ip >= ' ' && *ip <= '~')
		continue;

	    if ((*ip & (0x80|0x40|0x20|0x10|0x08|0x04|0x02)) == (0x80|0x40|0x20|0x10|0x08|0x04))
	    {
		state = 1;
		len = 5;
		continue;
	    }
	    
	    if ((*ip & (0x80|0x40|0x20|0x10|0x08|0x04)) == (0x80|0x40|0x20|0x10|0x08))
	    {
		state = 1;
		len = 4;
		continue;
	    }
	    
	    if ((*ip & (0x80|0x40|0x20|0x10|0x08)) == (0x80|0x40|0x20|0x10))
	    {
		state = 1;
		len = 3;
		continue;
	    }
	    
	    if ((*ip & (0x80|0x40|0x20|0x10)) == (0x80|0x40|0x20))
	    {
		state = 1;
		len = 2;
		continue;
	    }
	    
	    if ((*ip & (0x80|0x40|0x20)) == (0x80|0x40))
	    {
		state = 1;
		len = 1;
		continue;
	    }

	    if (debug > 3)
		fprintf(stderr, "*** is_utf_8: Failed 2 at %lu (%s)\n",
			(unsigned long) (ip - (const unsigned char *) in), in);
	    return 0;

	  case 1:
	    if ((*ip & (0x80|0x40)) == 0x80)
	    {
		if (--len == 0)
		    state = 0;
		
		continue;
	    }

	    if (debug > 3)
		fprintf(stderr, "*** is_utf_8: Failed 3 at %lu (%s): 0x%02x\n",
			(unsigned long) (ip - (const unsigned char *) in),
			in, (unsigned int) *ip);
	    return 0;
	}
    }

    if (state != 0)
    {
	if (debug > 3)
	    fprintf(stderr, "*** is_utf_8: Failed 4 at %lu (%s): 0x%02x\n",
		    (unsigned long) (ip - (const unsigned char *) in), in, (unsigned int) *ip);
	return 0;
    }
    
    return 1;
}


char *
conv(const char *in)
{
    char buf[10240], *out;
    size_t inlen, outlen;
    
    
    if (!ic)
	return strdup(buf);

    out = buf;
    inlen = strlen(in);
    outlen = sizeof(buf);

    if (debug && hex_flag)
    {
	putc('\t', stderr);
	print_hex(in, stderr);
    }

    if (iconv(ic, &in, &inlen, &out, &outlen) < 0)
	return NULL;

    if (debug && hex_flag)
    {
	fputs(" -> ", stderr);
	print_hex(buf, stderr);
	putc('\n', stderr);
    }
    
    *out = '\0';
    return strdup(buf);
}

void
error(const char *msg,
      ...)
{
    va_list ap;

    va_start(ap, msg);
    fprintf(stderr, "%s: ", argv0);
    vfprintf(stderr, msg, ap);
    putc('\n', stderr);
    va_end(ap);
    exit(1);
}


int
is_ascii(const char *s)
{
    int c, len;

    
    for (len = strlen(s)-1; len >= 0 && (c = * (unsigned char *) (s+len)); --len)
	if (c < ' ' || c > '~')
	    return 0;
    
    return 1;
}


char *
fix_invalid(const char *inbuf)
{
    unsigned char *outbuf, *obp;
    unsigned char *ibp;
    

    obp = outbuf = (unsigned char *) strdup(inbuf);
    
    for (ibp = (unsigned char *) inbuf; *ibp; ++ibp)
    {
	if (*ibp == 0x8E) /* Old Mac TM */
	    *obp++ = 0xAE;  /* (R) */
	else if (*ibp == 0x90) /* Old Mac ' */
	    *obp++ = '\'';
	else if (*ibp == '\t' || *ibp == '\r' || *ibp == '\n')
	    *obp++ = *ibp;
	else if (*ibp > 0 && *ibp < ' ')
	    ;
	else if (*ibp >= 0x7F && *ibp <= 0x9F)
	    *obp++ = '?';
	else
	    *obp++ = *ibp;
    }

    *obp = 0;
    
    return (char *) outbuf;
}


int
is_whitespace(int c)
{
    return (c == ' '  || c == '\t' || c == 0xA0 || (fix_ws_flag > 2 && (c == '\n' || c == '\r')));
}

char *
fix_whitespace(const char *inbuf)
{
    unsigned char *outbuf, *obp;
    unsigned char *ibp;
    

    for (ibp = (unsigned char *) inbuf; is_whitespace(*ibp); ++ibp)
	;
    
    obp = outbuf = (unsigned char *) strdup((char *) ibp);

    for (; *ibp; ++ibp)
    {
	if (fix_ws_flag > 1 && is_whitespace(*ibp))
	    *obp++ = ' ';
	else
	    *obp++ = *ibp;
    }
    
    while (obp > outbuf && is_whitespace(obp[-1]))
	--obp;
    
    *obp = 0;
    
    return (char *) outbuf;
}


int
walker(const char *path,
       const struct stat *sp,
       int flags,
       struct FTW *ftp)
{
    char *rest, *last, *np;
    char *f_last, *t_last;
    int ff;
    int isof = 0;
    int f_rename = 0;
    

    f_last = t_last = NULL;
    
    rest = strdup(path);
    if (!rest) {
	fprintf(stderr, "*** strdup() failed ***\n");
	return -1;
    }
    
    last = strrchr(rest, '/');
    if (last)
	*last++ = 0;
    else {
      last = rest;
      rest = ".";
    }

    if (debug > 3)
    {
	fprintf(stderr, "*** walker: path=");
	print_qstr(path, stderr);
	fprintf(stderr, " (rest=");
	print_qstr(rest, stderr);
	fprintf(stderr, ", last=");
	print_qstr(last, stderr);
	fprintf(stderr, ")\n");
    }

    if (flags == FTW_F)
      ++nf;
    else if (flags == FTW_D || flags == FTW_DNR || flags == FTW_DP)
      ++nd;
    else if (flags == FTW_SL)
	++ns;
    else
    {
	if (debug)
	    fprintf(stderr, "*** flags=%d\n", flags);
	
	return 0;
    }

    t_last = last;
    if (fix_ws_flag)
    {
	t_last = fix_whitespace(last);
	if (strcmp(t_last, last))
	    f_rename = 1;

	if (!*t_last)
	{
	    if (verbose > 1)
	    {
		if (qp_flag)
		    print_qstr(path, logfile);
		else
		    fputs(path, logfile);
		
		fputs(" [Empty]\n", logfile);
	    }
	    free(t_last);
	    return errexit_flag;
	}
    }
    
    if (is_ascii(t_last))
    {
	++n_ascii;
	

	if (verbose > 2)
	{
	    if (qp_flag)
		print_qstr(path, logfile);
	    else
		fputs(path, logfile);

	    fputs(" [ASCII]\n", logfile);
	}
	
	if (!f_rename)
	{
	    if (t_last != last)
		free(t_last);
	    return 0;
	}
	else
	{
	    np = t_last;
	    goto Rename;
	}
    }
    
    if (is_utf_8(t_last))
    {
	++n_utf_8;
	

	if (verbose > 2)
	{
	    if (qp_flag)
		print_qstr(path, logfile);
	    else
		fputs(path, logfile);

	    fputs(" [UTF-8]\n", logfile);
	}
	
	if (!f_rename)
	{
	    if (t_last != last)
		free(t_last);
	    return 0;
	}
	else
	{
	    np = t_last;
	    goto Rename;
	}
    }


    isof = is_iso8859_1(t_last);
    if (isof)
    {
	++n_iso8859_1;
	
	np = conv(t_last);
	if (!np)
	{
	    fprintf(stderr, "%s: %s: Unable to convert %s to %s\n",
		    argv0, path, t_last, dst_charset);
	    if (t_last != last)
		free(t_last);
	    return errexit_flag;
	}

	if (t_last != last)
	    free(t_last);
	goto Rename;
    }

    
    f_last = fix_invalid(t_last);
    
    ff = strcmp(f_last, t_last);
    if (!ff)
    {
	if (verbose > 1)
	{
	    if (qp_flag)
		print_qstr(path, logfile);
	    else
		fputs(path, logfile);
	    
	    fputs(" [Unfixable]\n", logfile);
	}
	free(f_last);
	if (t_last != last)
	    free(t_last);
	return errexit_flag;
    }
    
    if (t_last != last)
	free(t_last);
    
    ++n_other;
    
    np = conv(f_last);
    if (!np)
    {
	fprintf(stderr, "%s: %s: Unable to convert %s to %s\n",
		argv0, path, f_last, dst_charset);

	free(f_last);
	return errexit_flag;
    }
    
    free(f_last);


  Rename:
    ++nm;
    
    if (fix_flag)
    {
	if (rename_in_dir(rest, last, np) < 0)
	{
	    fprintf(stderr, "%s: rename_in_dir(\"", argv0);
	    print_qstr(rest, stderr);
	    fprintf(stderr, "\", \"");
	    print_qstr(last, stderr);
	    fprintf(stderr, "\", \"");
	    print_qstr(np, stderr);
	    fprintf(stderr, "\"): %s\n", strerror(errno));
	    
	    return errexit_flag;
	}
	
	++n_fixed;
	
	if (verbose)
	{
	    if (qp_flag)
		print_qstr(rest, logfile);
	    else
		fputs(rest, logfile);
	    putc('/', logfile);
	    if (qp_flag)
		print_qstr(last, logfile);
	    else
		fputs(last, logfile);
	    fprintf(logfile, " -> %s", np);

	    if (verbose > 1)
	    {
		putc(' ', logfile);
		putc('[', logfile);
		if (isof)
		    fputs("ISO8859-1", logfile);
		else
		    fputs("Other", logfile);
		putc(']', logfile);
	    }
	    
	    putc('\n', logfile);
	}
    }
    else
    {
	if (qp_flag)
	    print_qstr(path, logfile);
	else
	    fputs(path, logfile);
	
	if (verbose > 1)
	{
	    putc(' ', logfile);
	    putc('[', logfile);
	    if (isof)
		fputs("ISO8859-1", logfile);
	    else
		fputs("Other", logfile);
	    putc(']', logfile);
	}
	
	putc('\n', logfile);
    }
    
    free(np);
    
    return 0;
}


int
main(int argc,
     char *argv[])
{
    struct rlimit rlb;
    int i, j, rc;
    int depth = 1;
    

    logfile = stdout;

    if (getrlimit(RLIMIT_NOFILE, &rlb) < 0)
	error("getrlimit(RLIMIT_NOFILE): %s", strerror(errno));
    
    rlb.rlim_cur = rlb.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &rlb) < 0)
	error("setrlimit(RLIMIT_NOFILE, %d): %s", rlb.rlim_cur, strerror(errno));
    
    depth = rlb.rlim_cur-32;

    for (i = 1; i < argc && argv[i][0] == '-'; i++)
    {
	for (j = 1; argv[i][j]; j++)
	    switch (argv[i][j])
	    {
	      case 'V':
		printf("[pfs2utf8, version %s - Copyright (c) 2015 Peter Eriksson]\n", version);
		break;
		
	      case 'd':
		++debug;
		break;

	      case 'x':
		++hex_flag;
		break;
		
	      case 'v':
		++verbose;
		break;

	      case 'e':
		++errexit_flag;
		break;

	      case 's':
		++fix_ws_flag;
		break;

	      case 'S':
		src_charset = strdup(argv[i]+j+1);
		goto NextArg;

	      case 'L':
		logfile = fopen(argv[i]+j+1, "w");
		if (!logfile)
		    error("fopen(\"%s\"): %s", argv[i]+j+1, strerror(errno));
		goto NextArg;
		
	      case 'F':
		++fix_flag;
		break;
		
	      case 'n':
		++nowrite;
		break;
		
	      case 'q':
		++qp_flag;
		break;

	      case 'h':
		printf("Usage: %s [<options>] <dir-1> [.. <dir-N>]\n", argv[0]);
		puts("Options:");
		puts("  -d\tDebug mode");
		puts("  -v\tIncrease Verbosity level");
		puts("  -n\tNo Modify Filesystem mode");
		puts("  -e\tAbort on Errors");
		puts("  -s\tRemove whitespace (1=leading/trailing SPC/TAB/NBSP, 2=internal, 3=also CR/LF)");
		puts("  -q\tPrint nonprintable characters as \\octal or \\char");
		puts("  -F\tFind-and-Fix mode (default is Find)");
		printf("  -S<s>\tSource charset (default: %s)\n", src_charset);
		printf("  -L<f>\tLog file (default: stdout)\n");
		puts("  -h\tPrint this information");
		exit(0);
		
	      default:
		error("-%c: Invalid switch (use -h for help)", argv[i][j]);
	    }
      NextArg:;
    }
    
    ic = iconv_open(dst_charset, src_charset);
    if (!ic)
    {
	fprintf(stderr, "%s: iconv_open(\"%s\", \"%s\"): %s\n",
		argv[0], dst_charset, src_charset, strerror(errno));
	exit(1);
    }
    
	
    rc = 0;
    for (; i < argc && rc == 0; i++)
	rc = nftw(argv[i], walker, depth, FTW_PHYS|FTW_DEPTH);
    
    if (rc < 0) {
        fprintf(stderr, "%s: nftw(\"%s\") failed: %s\n", argv[0], argv[i], strerror(errno));
        exit(1);
    }

    fprintf(stderr, "%llu match%s (%llu director%s, %llu symlink%s & %llu file%s scanned)",
	    nm, (nm == 1) ? "" : "es",
	    nd, (nd == 1) ? "y" : "ies", 
	    ns, (ns == 1) ? "" : "s", 
	    nf, (nf == 1) ? "" : "s");

    fprintf(stderr, ": %llu ASCII, %llu UTF-8, %llu ISO8859-1, %llu Other: %llu %sRenamed\n",
	    n_ascii, n_utf_8, n_iso8859_1, n_other, n_fixed,
	    nowrite ? "NOT " : "");
    
    exit (rc != 0);
}
