/* convert .ugi to .sgf - aeb, 2013-10-23 */

/* to do: escape text,
   add line nrs in error msgs,
   open outfile later (to avoid empty files on error),
   recognize .Fig
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#include <iconv.h>

char *progname;
char *infilename;
char *infilebasename;
char *infilesuffix;

int opt_no_images = 0;			/* 1: don't output images */
int opt_add_image_prefix = 0;		/* 1: add infilebasename prefix */
char *opt_image_prefix = NULL;		/* constant image name prefix */
int opt_print_timeused = 0;
int opt_output_figs = 0;
int opt_quiet = 0;
int warnings_are_fatal = 1;

#define NAMEMAX	1000
struct game_info {
	char *name, *value;
	int namelen, valuelen;
} g[NAMEMAX];
int gct;

/* depending on coordinatetype we may have to flip the y coord */
int size = 19;
int needflip = 0;

#define MOVEMAX	1000
struct move {
	char x, y, col;
	int plnr, movenr, secs, totsecs;
	char *labels, *comment;
} moves[MOVEMAX];
int mvct;

/* maybe it happens for arbitrary movenr that several moves
   can be played; for the time being we only have examples
   initially, for the placement of handicap stones */
#define IMOVEMAX 100
struct move imoves[IMOVEMAX];
int imvct;

/* moves in .Fig subsections - there can be more than 10000! */
#define VMOVEMAX 100000
struct move vmoves[VMOVEMAX];
int vmvct;

/* descriptors of .Fig subsections */
#define FIGSUBMAX 1000
struct vmoved {
	int mvnr;		/* parameter of .Fig */
	int start, end;		/* interval in vmoves[] */
} figsubs[FIGSUBMAX];
int figsubct;


static void complain(const char *s, va_list ap) {
	if (progname && *progname)
		fprintf(stderr, "%s: ", progname);
	if (infilename && *infilename)
		fprintf(stderr, "%s: ", infilename);
	vfprintf(stderr, s, ap);
	fprintf(stderr, "\n");
}

static void mutter(const char *s, ...) {
	va_list ap;

	if (!opt_quiet) {
		va_start(ap, s);
		complain(s, ap);
		va_end(ap);
	}
}

static void warn(const char *s, ...) {
        va_list ap;

	if (!opt_quiet) {
		va_start(ap, s);
		complain(s, ap);
		va_end(ap);
	}

        if (warnings_are_fatal)
                exit(1);
}


/* fatal error */
static void errexit(const char *s, ...) __attribute__ ((noreturn));

static void errexit(const char *s, ...) {
	va_list ap;

	va_start(ap, s);
	complain(s, ap);
	va_end(ap);

	exit(-1);
}



/* error-free memory allocation */
static void *xmalloc(int n) {
        void *p = malloc(n);

        if (p == NULL)
                errexit("out of memory");
        return p;
}

static void *xrealloc(void *p, int n) {
	void *pp = realloc(p, n);

	if (pp == NULL)
		errexit("out of memory");
	return pp;
}

static char *xstrndup(const char *s, int n) {
	char *p = strndup(s, n);

	if (p == NULL)
		errexit("out of memory");
	return p;
}



static int infilesz(FILE *inf) {
	struct stat stat;
	int fd;

	fd = fileno(inf);
	if (fstat(fd, &stat) == -1)
		return 0;
	return stat.st_size;
}

/* read file into buf and add NUL; leave room for \n */
static void getinfile(FILE *inf, char **bufp, int *szp) {
	int n, sz, bufsz;
	char *buf;

	/* determine or guess input size */
	n = 0;
	if (inf != stdin)
		n = infilesz(inf)+1;
	if (n == 0)
		n = 400000;	/* random */

	/* read input file into buf */
	bufsz = n;
	buf = xmalloc(bufsz);
	sz = 0;

	while (1) {
		if (sz+2 >= bufsz) {
			bufsz += 400000;
			buf = xrealloc(buf, bufsz);
		}
		n = fread(buf+sz, 1, bufsz-sz-2, inf);
		if (n == 0)
			break;		/* error or eof */
		sz += n;
	}
	buf[sz] = 0;

	*bufp = buf;
	*szp = sz;

	fclose(inf);
}

/* use Unix line endings; make sure there is a \n at the end */
static void fix_newlines(char *buf) {
	char *p, *q;

	p = q = buf;
	while (*p) {
		if (*p == '\r') {
			p++;
			if (*p != '\n')
				*q++ = '\n';
		} else
			*q++ = *p++;
	}
	if (q[-1] != '\n')
		*q++ = '\n';
	*q = 0;
}

static int isutf8(unsigned char *buf, int n) {
        int i, ct;
        unsigned int c;

        for (i=0; i<n; i++) {
                c = buf[i];
                if ((c & 0x80) == 0)
                        continue;
                if ((c & 0xe0) == 0xc0)
                        ct = 1;
                else if ((c & 0xf0) == 0xe0)
                        ct = 2;
                else
                        return 0;

                while (ct--) {
                        if (++i == n)
                                return 0;       /* incomplete */
                        c = buf[i];
                        if ((c & 0xc0) != 0x80)
                                return 0;
                }
        }
        return 1;
}

static char *getlang(char *buf) {
	char *p, *q;

	p = strstr(buf, "[Header]");
	if (p == NULL)
		return NULL;		/* no Header section */
	q = p;
	while (1) {
		while (*q && *q != '\n')
			q++;
		if (*q == 0 || *++q == '[')
			return NULL;	/* end of Header section */
		if (strncmp(q, "Lang=", 5) == 0) {
			p = q = q+5;
			while (*q != '\n')
				q++;
			return xstrndup(p, q-p);
		}
	}
}

/*
 * convert a buffer from a given charset to utf-8
 *
 * outct includes a trailing NUL
 */
static void convert_to_utf8(char *charset, char *inbuf, int inct,
		    char **outbuf, int *outct) {
	iconv_t cd;
	char *inp, *outp, *outstart;
	size_t inleft, outleft, outlen, ret;

	if (!strcasecmp(charset, "UTF-8") ||
	    !strcasecmp(charset, "UTF8")) {
		*outbuf = inbuf;
		*outct = inct;
		return;		/* nothing to do */
	}

	cd = iconv_open("UTF-8", charset);
	if (cd == (iconv_t) -1)
		errexit("charset %s not supported", charset);

	inleft = inct;
	inp = inbuf;
	outleft = outlen = 2*inct + 1000;	/* should suffice */
	outp = outstart = xmalloc(outlen+1);	/* +1 for final NUL */

	ret = iconv(cd, &inp, &inleft, &outp, &outleft);
	if (ret == (size_t) -1) {
		switch(errno)  {
		case EILSEQ:
			/* now inp points at the invalid sequence */
			errexit("invalid %s input, offset %d",
				charset, inp-inbuf);
		case EINVAL:
			errexit("incomplete input");
		case E2BIG:
			/* try again with a larger buffer? */
			errexit("no room for output");
		default:
			errexit("error converting input to UTF-8");
		}
	}
#if 0
	if (ret)
		warn("while converting to UTF-8: "
		     "%d characters were converted non-reversibly", ret);
#endif

	*outp++ = 0;
	outlen = outp - outstart;

	*outbuf = outstart;
	*outct = outlen;

	iconv_close(cd);
}

static void do_convert_to_utf8(char **buf, int *n) {
	char *charset, *outbuf;
	int outct;

	/* check for a Lang=... line in the Header section */
	charset = getlang(*buf);
	if (charset == NULL || *charset == 0)
		charset = "SJIS";

	/* Microsoft CP932 strictly contains SJIS, and in Japan
	   one generally calls CP932 for SJIS. Let us use that,
	   and avoid problems on, e.g., fb 59 which stands for U+71c1. */
	if (!strcmp(charset, "SJIS"))
		charset = "CP932";
	
	convert_to_utf8(charset, *buf, *n, &outbuf, &outct);
	if (*buf != outbuf) {
		free(*buf);
		*buf = outbuf;
	}
	*n = outct-1;		/* count without trailing NUL */
}

static int asctohex(int c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	errexit("bad hexdump data");
}

static void outhexdump(FILE *f, char *start, char *end) {
	int n;
	char *buf, *p, *q;
	unsigned char c1, c2;

	n = (end - start + 1)/2;
	if (end != start+2*n)
		warn("hexdump of odd length");
	buf = xmalloc(n);
	p = start;
	q = buf;
	while (p < end) {
		c1 = *p++;
		c2 = *p++;
		*q++ = (asctohex(c1) << 4) + asctohex(c2);
	}
	n = fwrite(buf, 1, q-buf, f);
	if (n != q-buf)
		errexit("error writing hexdump");
}

/*
 * expect  name=image
 * write a file  infilebasename_name.jpg  containing image
 * return pointer to following line
 */
static char *outimage(char *img) {
	FILE *imgf;
	char *p, *q, *r, *imgname, *suffix;
	int n;

	p = q = img;
	while (*q != '=' && *q != '\n')
		q++;
	if (*q != '=')
		errexit("no '=' in [Files] line");
	r = q+1;
	while (*r != '\n')
		r++;

	if (opt_no_images)
		goto skip;
	
	/* all known examples have .jpg */
	if (!strncmp(q+1,"FFD8FFE0",8) || !strncmp(q+1,"FFD8FFE1",8))
		suffix = "jpg";
	else
		suffix = "img";

	n = strlen(infilebasename) + 1 + (q-p) + 5;
	imgname = xmalloc(n);
	if (opt_add_image_prefix) {
		sprintf(imgname, "%s_%.*s.%s",
			infilebasename, (int)(q-p), p, suffix);
	} else if (opt_image_prefix) {
		sprintf(imgname, "%s%.*s.%s",
			opt_image_prefix, (int)(q-p), p, suffix);
	} else {
		sprintf(imgname, "%.*s.%s", (int)(q-p), p, suffix);
	}

	imgf = fopen(imgname, "w");
	if (imgf == NULL)
		errexit("cannot create %s", imgname);
	outhexdump(imgf, q+1, r);
	fclose(imgf);
	free(imgname);

skip:
	return r+1;
}

/* does p start with q? if yes, return pointer to tail */
static inline char *startswith(char *p, char *q) {
	while (*q)
		if (*p++ != *q++)
			return 0;
	return p;
}

static inline char *skip_to_nextfield(char *p) {
	while (*p != ',' && *p != '\n')
		p++;
	return p;	/* points at ',' */
}

static inline char *skip_to_nextline(char *p) {
	while (*p != '\n')
		p++;
	p++;
	return p;
}

static char *skip_to_nextsection(char *p) {
	while (*p && *p != '[')
		p = skip_to_nextline(p);
	return p;
}

static char *skip_to_end(char *p) {
	while (*p)
		p++;
	return p;
}

static char *get_definitions(char *p) {
	char *q, *r;

	while (*p && *p != '[') {
		q = p;
		while (*q != '=' && *q != '\n')
			q++;
		if (*q == '\n') {
			warn("no = in definition line");
			p = q+1;
			continue;
		}
		r = skip_to_nextline(q+1);

		if (gct == NAMEMAX)
			errexit("NAMEMAX overflow");
		g[gct].name = p;
		g[gct].value = q+1;
		g[gct].namelen = q-p;
		g[gct].valuelen = r-q-2;
		gct++;

		p = r;
	}

	return p;
}

static char *process_header(char *p) {
	p = get_definitions(p);
	return p;
}

static int find_definition(char *p) {
	int i, n;

	n = strlen(p);
	for (i=0; i<gct; i++)
		if (g[i].namelen == n && strncmp(p, g[i].name, n) == 0)
			return i;
	return -1;
}


static void find_size() {
	int i, n;
	char *p, *end;

	size = 19;
	i = find_definition("Size");
	if (i >= 0) {
		p = g[i].value;
		n = strtoul(p, &end, 10);
		if (*end != '\n')
			warn("unrecognized size line");
		else
			size = n;
		if (size != 9 && size != 19)
			warn("unusual size %d", size);
	}

	/* JPN: no flip; IGS: flip; default: ? */
	needflip = 0;
	i = find_definition("CoordinateType");
	if (i >= 0) {
		p = g[i].value;
		if (g[i].valuelen == 3 && !strncmp(p, "IGS", 3))
			needflip = 1;
	}
}

/* maybe note which are the player pictures and which the ad icon
   so that we can select just the player pictures */

static char *process_remote(char *p) {
	return skip_to_nextsection(p);
}

static char *process_files(char *p) {
	while (*p && *p != '[')
		p = outimage(p);
	return p;
}

/* read 00:03:35 */
static char *gethms(char *p, int *totsecs) {
	char *end;
	int tot, n;

	n = strtoul(p, &end, 10);
	if (end-p != 2 || *end != ':')
		goto badfmt;
	tot = 3600*n;
	n = strtoul(p+3, &end, 10);
	if (end-p != 5 || *end != ':')
		goto badfmt;
	tot += 60*n;
	n = strtoul(p+6, &end, 10);
	if (end-p != 8)
		goto badfmt;
	tot += n;
	*totsecs = tot;
	return end;

badfmt:
	errexit("bad cumulative time format");
}
	
static char *readmove(char *p, struct move *thismove) {
	char *end;
	int n;

	/* moves look like PP,B1,1,0 */
	/* or: YA,B1,221,0 - where Black passes */
	/* seen once: YS,W2,304,18 - where W2 passes */
	/* seen once: YZ,MK,278,0 - with Y for pass */
	/* seen once: YR,W1,188,10 - where W passes */

	if (*p < 'A' || *p > 'Z') {
		char *q = skip_to_nextline(p);
		errexit("bad x in move: %.*s", (int)(q-p), p);
	}
	thismove->x = tolower(*p++);

	if (*p < 'A' || *p > 'Z')
		errexit("bad y");
	thismove->y = tolower(*p++);

	if (*p++ != ',')
		errexit("comma expected in move");

	/* seen: YA, YR, YS, YZ, ZZ */
	if (thismove->x == 'y' || thismove->x == 'z') {
		thismove->x = 0;	/* pass */

		if (thismove->y == 'z' && !strncmp(p, "MK,", 3)) {
			p += 3;
			thismove->col = (mvct%2) ? 'B' : 'W';
			goto nextfld;
		}

		if (thismove->y != 'a' && thismove->y != 'r' &&
		    thismove->y != 's' && thismove->y != 'z')
			warn("unknown type of pass 'Y%c'", p[-2]);
	}
		
	if (*p != 'B' && *p != 'W')
		errexit("B or W expected in move");
	thismove->col = *p++;

	thismove->plnr = n = strtoul(p, &end, 10);
	if (n < 1 || n > 2)
		errexit("playernumber %d in move", n);
	p = end;

	if (*p++ != ',')
		errexit("comma expected in move");

nextfld:
	thismove->movenr = strtoul(p, &end, 10);
	p = end;

	if (*p++ != ',')
		errexit("comma expected in move");

	thismove->secs = n = strtoul(p, &end, 10);
	p = end;

	thismove->totsecs = 0;
	if (*p != '\n') {
		int totsecs;

		/* seen: FC,W1,1,0 '00:00:31
		   ZZ,W1,283,0 '00:15:00 with cumulative times */
		while (*p == ' ')
			p++;
		if (*p++ != '\'')
			goto junk;
		p = gethms(p, &totsecs);
		thismove->totsecs = totsecs;
	}
	if (*p++ != '\n')
		goto junk;

	thismove->comment = thismove->labels = NULL;

	return p;

junk:
	errexit("trailing junk in move");
}

static char *process_data(char *p) {
	struct move thismove;
	int n;

	/* start at move 1, not 0, so that whole-game comments
	   can be attached to move 0 */
	while (*p && *p != '[') {
		if (*p == '\n') {
			p++;
			continue;
		}

		/* nonsense end? */
		if (startswith(p, ".EndFig")) {
			warn("bad line following Data section");
			p = skip_to_nextsection(p);
			break;
		}

		/* seen once: advertising text after the game */
		if (startswith(p, " .Text,0,999,")) {
			char *q = skip_to_nextline(p);
			mutter("ignored .Text line: %.*s", (int)(q-p), p);
			p = q;
			continue;
		}
	
		p = readmove(p, &thismove);

		n = thismove.movenr;
		if (n == 0) {
			if (imvct == IMOVEMAX)
				errexit("IMOVEMAX overflow");
			imoves[imvct++] = thismove;
		} else {
			if (n != mvct)
				errexit("move number %d in line %d", n, mvct);
			if (mvct == MOVEMAX)
				errexit("MOVEMAX overflow");
			moves[mvct++] = thismove;
		}
	}
	return p;
}

static inline char flip(char c) {
	int row;

	if (!needflip)
		return c;
	
	row = size - 1 - (c - 'a');
	if (row < 0)
		errexit("coord %c on a board of size %d", c, size);
	return 'a' + row;
}

static void add_label(char *p, char **label){
	int x, y, n;
	char *q, *r, *s, *end;

	/* expect 6,16,E which has to become LB[fd:E] */

	q = skip_to_nextline(p);

	x = strtoul(p, &end, 10);
	p = end;
	if (*p++ != ',')
		errexit("comma expected in label definition");
	y = strtoul(p, &end, 10);
	p = end;
	if (*p++ != ',')
		errexit("comma expected in label definition");
	if (x <= 0 || y <= 0 || x > 26 || y > 26)
		errexit("bad label coordinates");
	if (p == q-1)
		errexit("missing label");

	if (*label == 0) {
		s = xmalloc(7 + q-p);
		sprintf(s, "LB[%c%c:%.*s]", 'a'+x-1, flip('a'+y-1),
			(int)(q-p-1), p);
		*label = s;
	} else {
		r = *label;
		n = strlen(r) + 5 + q-p;
		s = xmalloc(n);
		sprintf(s, "%s[%c%c:%.*s]", r, 'a'+x-1, flip('a'+y-1),
			(int)(q-p-1), p);
		free(r);
		*label = s;
	}
}

static void add_comment(char *p, char **comment) {
	int n;
	char *q, *r, *s;

	q = skip_to_nextline(p);
	if (*comment == 0) {
		*comment = xstrndup(p, q-p);
	} else {
		r = *comment;
		n = strlen(r) + (q-p) + 1;
		s = xmalloc(n);
		sprintf(s, "%s%.*s", r, (int)(q-p), p);
		free(r);
		*comment = s;
	}
}

static char *process_figure_text(char *p) {
	char *end;
	int mvnr;

	p += 6;
	mvnr = strtoul(p, &end, 10);
	if (end == p) {
		warn("missing mvnr in Figure.Text");
		mvnr = -1;
	} else if (mvnr < 0 || mvnr >= mvct) {
		warn("invalid mvnr %d in Figure.Text", mvnr);
		mvnr = -1;
	}

	/* there may be further parameters to .Text - ignore for now */
	p = skip_to_nextline(p);

	while (*p && *p != '[') {
		if (startswith(p, ".EndText"))
			return skip_to_nextline(p);

		if (mvnr < 0) {
			p = skip_to_nextline(p);
			continue;
		}

		if (startswith(p, ".#,")) {
			add_label(p+3, &(moves[mvnr].labels));
			p = skip_to_nextline(p);
			continue;
		}

		add_comment(p, &(moves[mvnr].comment));
		p = skip_to_nextline(p);
	}
		
	return p;
}

static char *process_figure_fig_text(char *p, int mvnr) {
	p = skip_to_nextline(p);	/* skip .Text\n */
	while (*p && *p != '[') {
		if (startswith(p, ".EndText\n"))
			return skip_to_nextline(p);

		if (mvnr < 0) {
			p = skip_to_nextline(p);
			continue;
		}

		if (startswith(p, ".#,")) {
			add_label(p+3, &(vmoves[mvnr].labels));
			p = skip_to_nextline(p);
			continue;
		}

		add_comment(p, &(vmoves[mvnr].comment));
		p = skip_to_nextline(p);
	}
	return p;
}

/*
 * Expected: .Fig,45,1,1,19,19,解説譜,11,0
 * arg 1: move - there may be several .Fig subsections with the same move
 * args2-5: viewing rectangle
 * arg 6: figure heading
 * arg 7: figure serial number
 * arg 8: 0
 *
 * This line is followed by a number of lines giving moves,
 * then .Text (without args), text, .EndText, .EndFig
 *
 * The moves following .Fig,N,... start with a number of moves with
 * number 0 in lex order of board position that create a starting position.
 * Afterwards a variation (of length 0 or more) with moves numbered from 1.
 * The starting position may be that after move N, or after N-1,
 * or after N-E for some larger E.
 *
 * A cheap rendering would be as a separate game, with
 * (AB[]AW[] ;moves C[text]).
 * If it is true that the starting position occurred in the game
 * then it may be possible to use SGFs syntax for variations.
 */
static char *process_figure_fig(char *p) {
	char *end;
	int mvnr, fignr;
	struct move thismove;

	if (!opt_output_figs) {
		while (*p && *p != '[') {
			p = skip_to_nextline(p);
			if (startswith(p, ".EndFig")) {
				p = skip_to_nextline(p);
				break;
			}
		}
		return p;
	}

	if (figsubct == FIGSUBMAX)
		errexit("FIGSUBMAX overflow");
	fignr = figsubct++;
	
	p += 5;
	mvnr = strtoul(p, &end, 10);
	if (end == p) {
		warn("missing mvnr in Figure.Fig");
		mvnr = -1;
	} else if (mvnr < 0 || mvnr >= mvct) {
		warn("invalid mvnr %d in Figure.Fig", mvnr);
		mvnr = -1;
	}

	figsubs[fignr].mvnr = mvnr;
	figsubs[fignr].start = vmvct;

	/* there may be further parameters to .Fig - ignore for now */
	p = skip_to_nextline(p);

	while (*p && *p != '[') {
		if (*p == '\n') {
			p++;
			continue;
		}
		if (startswith(p, ".EndFig")) {
			p = skip_to_nextline(p);
			break;
		}
		if (startswith(p, ".Text\n")) {
			if (vmvct == figsubs[fignr].start)
				warn(".Text not belonging to a move");
			else
				p = process_figure_fig_text(p, vmvct-1);
			continue;
		}
		p = readmove(p, &thismove);
		if (vmvct == VMOVEMAX)
			errexit("VMOVEMAX overflow");
		vmoves[vmvct++] = thismove;
	}

	figsubs[fignr].end = vmvct;
	return p;
}

static char *process_figure(char *p) {
	char *q;

	find_size();

	while (*p && *p != '[') {
		if (*p == '\n') {
			p++;
			continue;
		}
		if (startswith(p, ".Text,")) {
			p = process_figure_text(p);
			continue;
		}
		if (startswith(p, ".Fig,")) {
			p = process_figure_fig(p);
			continue;
		}
		q = skip_to_nextline(p);
		fprintf(stderr, "ignored: %.*s\n", (int)(q-p), p);
		p = q;
	}
	return p;
}

static char *process_comment(char *p) {
	return skip_to_nextsection(p);
}

static char *process_messageline(char *p) {
	return skip_to_nextsection(p);
}

static char *process_reviewnode(char *p) {
	return skip_to_nextsection(p);
}

static char *process_end(char *p) {
	return skip_to_end(p);
}

static void initialize() {
	/* reset global variables */
	gct = 0;
	moves[0].comment = NULL;
	mvct = 1;
	imvct = 0;
	vmvct = 0;
	figsubct = 0;
}

/*
 * Read file, and store info for SGF.
 * Output any photographs.
 *
 * There is a problem with immediately writing SGF:
 * Maybe we want to convert PlayerB=A, PlayerB2=B
 * into PB[A & B].
 */
struct sect {
	char *header;
	char *(*f)(char *);
} sects[] = {
	{ "[Header]\n", process_header },
	{ "[Remote]\n", process_remote },
	{ "[Files]\n", process_files },
	{ "[Data]\n", process_data },
	{ "[Figure]\n", process_figure },
	{ "[Comment]\n", process_comment },
	{ "[MessageLine]\n", process_messageline },
	{ "[ReviewNode]\n", process_reviewnode },
	{ "[End]\n", process_end }
};

#define SIZE(a)	(sizeof(a)/sizeof((a)[0]))

static void process_file(char *buf) {
	char *p, *q;
	int i;

	initialize();

	p = buf;

	/* there may be a header consisting of comment lines,
	   sometimes followed by a blank line */
	while (*p == '#')
		p = skip_to_nextline(p);
	while (*p == '\n')
		p++;
	
	while (*p) {
		for (i=0; i<SIZE(sects); i++) {
			q = startswith(p, sects[i].header);
			if (q) {
				p = sects[i].f(q);
				goto nextsect;
			}
		}

		q = p;
		while (*q != '\n')
			q++;
		warn("unknown section %.*s", q-p, p);
		p = skip_to_nextsection(q+1);

	nextsect:;
	}
}

static void out_text(FILE *outf, char *ugi, char *sgf) {
	int i;

	i = find_definition(ugi);
	if (i >= 0 && g[i].valuelen > 0)
		fprintf(outf, "%s[%.*s]\n", sgf, g[i].valuelen, g[i].value);
}

static char *out_field(FILE *outf, char *p, char *sgf) {
	char *q;

	q = skip_to_nextfield(p);
	fprintf(outf, "%s[%.*s]\n", sgf, (int)(q-p), p);
	if (*q == ',')
		q++;
	return q;
}

static char *out_barefield(FILE *outf, char *p) {
	char *q;

	q = skip_to_nextfield(p);
	fprintf(outf, "%.*s", (int)(q-p), p);
	if (*q == ',')
		q++;
	return q;
}

static void out_double(FILE *outf, char *p) {
	char *q;
	int n;

	q = skip_to_nextfield(p);

	/* normalize 2.50 and 5.00 */
	n = q-p;
	while (n > 0 && p[n-1] == '0')
		n--;
	if (n > 0 && p[n-1] == '.')
		n--;
	fprintf(outf, "%.*s", n, p);

	/* this may still give KM[0,50] (only in a single file?) */
}

/* year or Nth, event, round */
static void out_event(FILE *outf) {
	int i;
	char *p;

	/* roughly: out_text(outf, "Title", "EV"); */

	i = find_definition("Title");
	if (i >= 0) {
		p = g[i].value;
		while (*p == ',' || *p == ' ')
			p++;
		if (*p == '\n')
			return;
		fprintf(outf, "EV[");
		while (1) {
			p = out_barefield(outf, p);
			if (*p == '\n')
				break;
			fprintf(outf, ", ");
		}
		fprintf(outf, "]\n");
	}
}

char *honinbo = "本因坊";
char *honinbo25 = "二十五世本因坊";

static char *is_honinbo(char *p) {
	return startswith(p, honinbo);
}

static char *is_honinbo25(char *p) {
	return startswith(p, honinbo25);
}

/* player, rank */
static void out_rankedplayer(FILE *outf, char *p) {
	char *q;

	q = out_barefield(outf, p);
	if (*q != '\n') {
		fprintf(outf, ", ");
		out_barefield(outf, q);
	}
}

/* check for PlayerB, PlayerB2, BMemb1 and idem for W */
/* I have never seen PlayerB3 or BMemb2 */
static int Nth_player(int n, char col) {
	char label1[20], label2[20];
	int i, j;

	sprintf(label1, "%cMemb%d", col, n);
	i = find_definition(label1);
	if (n == 1) {
		sprintf(label2, "Player%c", col);
		j = find_definition(label2);
	} else {
		sprintf(label2, "Player%c%d", col, n);
		j = find_definition(label2);
	}
	if (i >= 0 && j >= 0) {
		warn("both %s and %s", label1, label2);
		return j;
	}
	if (j >= 0)
		return j;
	return i;
}

/* common code for PlayerB / PB and PlayerW / PW */
static void out_px(FILE *outf, char col) {
	int i, j, n;
	char *p, *p1, *q, *sgfpl, *sgfrk;

	sgfpl = (col == 'B') ? "PB" : "PW";
	sgfrk = (col == 'B') ? "BR" : "WR";

	n = 0;
	i = Nth_player(++n, col);
	j = Nth_player(++n, col);
	if (i < 0) {
		if (j >= 0)
			warn("Second player but no first?");
		return;
	}
	if (j < 0) {
		/* name,rank,... */
		p = g[i].value;
		p1 = is_honinbo25(p);
		if ((p1 = is_honinbo25(p)) != 0) {
			out_field(outf, p1, sgfpl);
			fprintf(outf, "%s[%s]\n", sgfrk, honinbo25);
		} else if ((p1 = is_honinbo(p)) != 0) {
			out_field(outf, p1, sgfpl);
			fprintf(outf, "%s[%s]\n", sgfrk, honinbo);
		} else {
			q = out_field(outf, p, sgfpl);		/* player */
			if (*q != ',' && *q != '\n')
				out_field(outf, q, sgfrk);	/* rank */
			/* perhaps (..) is always country? */
		}
	} else {
		/* several players, do not separate rank */
		fprintf(outf, "%s[", sgfpl);
		out_rankedplayer(outf, g[i].value);
		while (j >= 0) {
			fprintf(outf, " & ");
			out_rankedplayer(outf, g[j].value);
			j = Nth_player(++n, col);
		}
		fprintf(outf, "]\n");
	}
}

static void out_players(FILE *outf) {
	out_px(outf, 'B');
	out_px(outf, 'W');
}

static void out_hms(FILE *outf, int tm) {
	int h, m, s;

	h = tm/3600;
	m = (tm/60)%60;
	s = tm%60;
	if (h)
		fprintf(outf, "%dh", h);
	if (m)
		fprintf(outf, "%dm", m);
	if (s)
		fprintf(outf, "%ds", s);
}

static void out_ptime(FILE *outf) {
	int i, tm;
	char *p;

	/* very roughly: out_text(outf, "Ptime", "TM"); */

	/* 4 fields - for now, let me follow JvL and print TM[]
	   for the 2nd subitem of the first field */
	i = find_definition("Ptime");
	if (i >= 0) {
		p = g[i].value;
		while (*p != ';' && *p != '\n')
			p++;
		if (*p++ != ';')
			return;
		tm = atoi(p);
		if (tm) {
			fprintf(outf, "TM[");
			out_hms(outf, 60*tm);
			fprintf(outf, "]\n");
		}
	}

	/* we do not claim FF[4], so do not have to give TM in seconds */
}

static void out_ha_komi(FILE *outf) {
	int i, ha;
	char *p, *q;

	i = find_definition("Hdcp");
	if (i >= 0) {
		/* handicap,komi */
		p = g[i].value;
		ha = strtoul(p, &q, 10);
		if (q > p && ha != 0)
			fprintf(outf, "HA[%d]\n", ha);

		if (*q++ != ',') {
			warn("comma expected in Hdcp field");
			return;
		}

		fprintf(outf, "KM[");
		out_double(outf, q);
		fprintf(outf, "]\n");
	}
}

static void out_result(FILE *outf) {
	int i;
	char *p, *q;

	/* roughly: out_text(outf, "Winner", "RE"); */

	i = find_definition("Winner");
	if (i >= 0) {
		p = g[i].value;

		/* e.g. Yoda Norimoto vs Hikosaka Naoto: triple ko */
		if (startswith(p, "N1,E\n")) {
			fprintf(outf, "RE[Void]\n");
			return;
		}
		/* e.g. Kato Masao vs Cho Chikun: triple ko */
		if (startswith(p, "N1,\n")) {
			fprintf(outf, "RE[Void]\n");
			return;
		}

		/* JvL documents values B, W, A, O, D, E for the first field */
		if (*p == '\n' || p[1] != ',')
			goto unkn;
		if (*p == 'O')
			fprintf(outf, "RE[Both lost]\n");
		else if (*p == 'A')
			fprintf(outf, "RE[Unfinished]\n");
		else if (*p == 'P')
			fprintf(outf, "RE[Playing]\n");
		else if (*p == 'N')
			fprintf(outf, "RE[Void]\n");
		else if (*p == 'E')
			fprintf(outf, "RE[?]\n");
		else if (*p == 'D')
			fprintf(outf, "RE[Draw]\n");
		else if (*p == 'B' || *p == 'W') {
			fprintf(outf, "RE[%c+", *p);
			q = p+2;
			if (*q == 'C' || *q == 'c') /* I never saw 'c' */
				fprintf(outf, "R");
			else if (*q == 'F')
				/* saw B+F2 for illegal ko capture */
				fprintf(outf, "F");
			else
				out_double(outf, q);
			fprintf(outf, "]\n");
		}
		else goto unkn;
	}
	return;

unkn:
	warn("unrecognized result");
	out_text(outf, "Winner", "RE");
}

static int setymd(char *p, int *y, int *m, int *d) {
	char *end;

	/* dummy? */
	if (!strncmp(p, "//,", 3))
		return 0;

	*y = strtoul(p, &end, 10);
	if (end - p != 4)
		warn("%d-digit year", end-p);
	p = end;
	if (*p++ != '/')
		warn("/ expected in date");
	*m = strtoul(p, &end, 10);
	if (end - p != 2)
		warn("%d-digit month", end-p);
	p = end;
	if (*p++ != '/')
		warn("/ expected in date");
	*d = strtoul(p, &end, 10);
	if (end - p != 2)
		warn("%d-digit day", end-p);
	if (*end != ',')
		warn("trailing junk in date");
	return 1;
}

static void out_date(FILE *outf) {
	int i, y1, m1, d1, y2, m2, d2, have1, have2;
	char *p, *q;

	/* very roughly: out_text(outf, "Date", "DT"); */

	i = find_definition("Date");
	if (i < 0)
		return;

	/* 4 subfields: start date, time, end date, time */
	p = g[i].value;
	q = skip_to_nextfield(p);
	if (*q == ',')
		q = skip_to_nextfield(q+1);
	if (*q == ',')
		q++;
	else
		q = NULL;

	have1 = have2 = 0;
	if (*p != ',' && *p != '\n')
		have1 = setymd(p, &y1, &m1, &d1);
	if (q && *q != ',' && *q != '\n')
		have2 = setymd(q, &y2, &m2, &d2);

	if (have1 && !have2)
		fprintf(outf, "DT[%04d-%02d-%02d]\n", y1, m1, d1);
	else if (have2 && !have1)
		fprintf(outf, "DT[%04d-%02d-%02d]\n", y2, m2, d2);
	else if (have1 && have2) {
		fprintf(outf, "DT[%04d-%02d-%02d", y1, m1, d1);
		if (y2 != y1)
			fprintf(outf, ",%04d-%02d-%02d", y2, m2, d2);
		else if (m2 != m1)
			fprintf(outf, ",%02d-%02d", m2, d2);
		else if (d2 != d1)
			fprintf(outf, ",%02d", d2);
		fprintf(outf, "]\n");
	}
}

static void out_place(FILE *outf) {
	out_text(outf, "Place", "PC");
}

static void out_size(FILE *outf) {
	out_text(outf, "Size", "SZ");
}

static void out_rules(FILE *outf) {
	int i, n;
	char *p;

	/* roughly: out_text(outf, "Rule", "RU"); */

	i = find_definition("Rule");
	if (i >= 0) {
		p = g[i].value;
		n = g[i].valuelen;
		if (n == 3 && !strncmp(p, "JPN", n))
			fprintf(outf, "RU[Japanese]\n");
		else
			fprintf(outf, "RU[%.*s]\n", n, p);
	}

	/* JvL has CH -> Chinese, AR -> Other, N -> PairGo;
	   however, my PairGo files have Rule=JPN
	   Maybe this N was mistaken and belongs under Ptime? */
}

static void out_writer(FILE *outf) {
	out_text(outf, "Writer", "US");
}

static void out_commentator(FILE *outf) {
	out_text(outf, "Commentator", "AN");
}

static void out_copyright(FILE *outf) {
	out_text(outf, "Copyright", "CP");
}

static void out_comment(FILE *outf) {
	out_text(outf, "Comment", "GC");
}

static void out_move(FILE *outf, int x, int y, int allowpass) {
	if (x == 0) {
		if (allowpass)
			fprintf(outf, "[]");
		else
			errexit("pass while placing handicap?");
	} else
		fprintf(outf, "[%c%c]", x, flip(y));
}
	
static void out_moves(FILE *outf) {
	int i, totb, totw, cumtotb, cumtotw;

	/* The board is vertically flipped, so we need the size */
	find_size();
	/* Maybe it was called already in the Figure section */

	totb = totw = cumtotb = cumtotw = 0;

	if (moves[0].comment)
		fprintf(outf, "C[%s]\n", moves[0].comment);
	
	if (imvct) {
		int ct;

		ct = 0;
		for (i=0; i<imvct; i++)
			if (imoves[i].col == 'B')
				ct++;
		if (ct == 0)
			goto addwhite;

		fprintf(outf, "AB");
		for (i=0; i<imvct; i++)
			if (imoves[i].col == 'B')
				out_move(outf, imoves[i].x, imoves[i].y, 0);
	addwhite:
		ct = 0;
		for (i=0; i<imvct; i++)
			if (imoves[i].col == 'W')
				ct++;
		if (ct == 0)
			goto adddone;

		fprintf(outf, "AB");
		for (i=0; i<imvct; i++)
			if (imoves[i].col == 'W')
				out_move(outf, imoves[i].x, imoves[i].y, 0);
	adddone:;
	}

	for (i=1; i<mvct; i++) {
		if ((i-1)%10 == 0)
			fprintf(outf, "\n");

		fprintf(outf, ";%c", moves[i].col);
		out_move(outf, moves[i].x, moves[i].y, 1);

		if (moves[i].col == 'B') {
			totb += moves[i].secs;
			cumtotb = moves[i].totsecs;
		} else {
			totw += moves[i].secs;
			cumtotw = moves[i].totsecs;
		}

		if (moves[i].labels)
			fprintf(outf, "%s", moves[i].labels);
		
		if (moves[i].comment)
			fprintf(outf, "\nC[%s]\n", moves[i].comment);
	}

	/* I conjecture that the .secs field gives the time in seconds.
	   For an 8h game one sees totals like 7h36m35s, 9h19m50s.
	   Maybe this per move time keeping is not very accurate. */

	/* prefer cumulative times when available */
	if (cumtotb + cumtotw) {
		totb = cumtotb;
		totw = cumtotw;
	}
	
	if (opt_print_timeused && totb + totw) {
		fprintf(outf, "\nC[Time used: B ");
		out_hms(outf, totb);
		fprintf(outf, "  W ");
		out_hms(outf, totw);
		fprintf(outf, "]\n");
	}
}

static void out_fig(FILE *outf, int n) {
	int s, e, i, ct;

	s = figsubs[n].start;
	e = figsubs[n].end;

	fprintf(outf, "(;\n");

	ct = 0;
	for (i=s; i<e; i++)
		if (vmoves[i].movenr == 0 && vmoves[i].col == 'B')
			ct++;
	if (ct) {
		fprintf(outf, "AB");
		for (i=s; i<e; i++)
			if (vmoves[i].movenr == 0 && vmoves[i].col == 'B')
				out_move(outf, vmoves[i].x, vmoves[i].y, 0);
	}

	ct = 0;
	for (i=s; i<e; i++)
		if (vmoves[i].movenr == 0 && vmoves[i].col == 'W')
			ct++;
	if (ct) {
		fprintf(outf, "AW");
		for (i=s; i<e; i++)
			if (vmoves[i].movenr == 0 && vmoves[i].col == 'W')
				out_move(outf, vmoves[i].x, vmoves[i].y, 0);
	}

	for (i=s; i<e; i++) if (vmoves[i].movenr == 0) {
		if (vmoves[i].labels)
			fprintf(outf, "%s", vmoves[i].labels);
		if (vmoves[i].comment)
			fprintf(outf, "\nC[%s]\n", vmoves[i].comment);
	}
		
	ct = 0;
	for (i=s; i<e; i++) {
		if (vmoves[i].movenr == 0)
			continue;
		if (ct++ % 10 == 0)
			fprintf(outf, "\n");
		fprintf(outf, ";%c", vmoves[i].col);
		out_move(outf, vmoves[i].x, vmoves[i].y, 0);
		if (vmoves[i].labels)
			fprintf(outf, "%s", vmoves[i].labels);
		if (vmoves[i].comment)
			fprintf(outf, "\nC[%s]\n", vmoves[i].comment);
	}

	fprintf(outf, ")\n");
}

static void out_figs(FILE *outf) {
	int i;

	for (i=0; i<figsubct; i++)
		out_fig(outf, i);
}


static void write_output(FILE *outf) {

	fprintf(outf, "(;\n");
	out_event(outf);
	out_players(outf);
	out_ptime(outf);
	out_ha_komi(outf);
	out_result(outf);
	out_date(outf);
	out_place(outf);
	out_size(outf);
	out_rules(outf);
	out_writer(outf);
	out_commentator(outf);
	out_copyright(outf);
	out_comment(outf);

	out_moves(outf);

	fprintf(outf, ")\n");

	if (opt_output_figs)
		out_figs(outf);
}

static void convert1(FILE *inf, FILE *outf) {
	char *buf;
	int sz;

	getinfile(inf, &buf, &sz);
	fix_newlines(buf);

	if (!isutf8((unsigned char *) buf, sz))
		do_convert_to_utf8(&buf, &sz);

	gct = 0;
	process_file(buf);
	write_output(outf);
	fclose(outf);
	free(buf);
}

static void doconvert(char *inname) {
	FILE *inf, *outf;
	char *outname, *p;
	int n;

	if (inname == NULL) {
		inf = stdin;
		outf = stdout;
		infilebasename = "stdin";
		infilesuffix = NULL;
	} else {
		inf = fopen(inname, "r");
		if (inf == NULL)
			errexit("cannot open %s", inname);

		infilebasename = strdup(inname);
		p = rindex(infilebasename, '.');
		if (p == NULL) {
			infilesuffix = NULL;
		} else {
			*p = 0;
			infilesuffix = p+1;
		}

		n = strlen(infilebasename);
		outname = xmalloc(n+5);
		sprintf(outname, "%s.sgf", infilebasename);

		outf = fopen(outname, "w");
		if (outf == NULL)
			errexit("cannot create %s", outname);
	}

	infilename = inname;

	convert1(inf, outf);
}

/*
 * call:
 *   ugi2sgf < in > out
 * or:
 *   ugi2sgf *.ugi *.ugf *.ugz
 *
 * photographs from infile.ug* are created as infile-label.jpg
 *
 * perhaps add -i for ignore errors, -q for quiet / -v for verbose
 */
int main(int argc, char **argv) {
	int i;

//	progname = "ugi2sgf";

	while (argc > 1 && argv[1][0] == '-') {
		if (!strcmp(argv[1], "--"))
			break;
		if (!strcmp(argv[1], "-i")) {
			warnings_are_fatal = 0;
			goto skip;
		}
		if (!strcmp(argv[1], "-q")) {
			opt_quiet = 1;
			goto skip;
		}
		if (!strcmp(argv[1], "-ip")) {
			opt_add_image_prefix = 1;
			goto skip;
		}
		if (!strncmp(argv[1], "-ip=", 4)) {
			opt_image_prefix = argv[1]+4;
			goto skip;
		}
		if (!strcmp(argv[1], "-ni")) {
			opt_no_images = 1;
			goto skip;
		}
		if (!strcmp(argv[1], "-fig")) {
			opt_output_figs = 1;
			goto skip;
		}
		if (!strcmp(argv[1], "-tu")) {
			opt_print_timeused = 1;
			goto skip;
		}
		errexit("unknown option '%s'", argv[1]);
	skip:
		argv++;
		argc--;
	}

	if (argc > 1) {
		for (i=1; i<argc; i++)
			doconvert(argv[i]);
	} else
		doconvert(NULL);

	return 0;
}
