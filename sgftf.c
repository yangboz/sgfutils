/*
 * sgftf - transform sgf file using a symmetry operation
 *   read from stdin or args
 *   write to stdout or to file given by -o
 *   -rot0, -rot90, -rot180, -rot270: rotate (left)
 *   -traN
 *   -swapcolors: interchange B and W
 *
 * -rot#: idem after rotation left about # * 90 degrees, #=0,1,2,3
 *  (default: #=1)
 * -tra#: idem after one of 8 transformations (even: rotation, odd: reflection)
 *  (default: #=1; -rotM is equivalent to -traN for N=2*M;
 *   if N=2*M+1, then -traN is -rotM followed by flip around hor line)
 *
 * The file is unchanged, except that B[], W[], AB[], AW[], AE[], LB[]
 * (and similar) coordinates are transformed.
 * (The change is made to all games in a collection, and to all variations.)
 *
 * Maybe also something like
 *   -trunc#, -trunc-#: truncate to # moves, or delete the final # moves
 * (but what to do with a tree of variations?)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errexit.h"
#include "readsgf.h"
#include "ftw.h"

FILE *outf;

int gtlevel;
int number_of_games;

struct node *rootnode;
int movesperline = 10;
int movesonthisline;

int opttra = 4;		/* specify transformation (default: rotate 180 deg) */
int opttrunc = 0;	/* truncate */
int trunclen;
int swapcolors = 0;
int recursive = 0;
char *file_extension = ".sgf";

#define MAXSZ		19	/* mainly because tt = pass */
#define DEFAULTSZ	19
int size = DEFAULTSZ;
int optsize = 0;

static void
setsize(struct propvalue *pv) {
	if (optsize)
		return;		/* size specified on command line */
	if (!pv || pv->next)
		errexit("nonsupported SZ property"); /* nonsquare? */
	size = atoi(pv->val);
	if (size < 0 || size > MAXSZ)
		errexit("SZ[%d] out of bounds", size);
}

static void transform0(int *xx, int *yy, int tra, int size) {
	int x, y, xn, yn;
	int sz = size-1;

	x = *xx;
	y = *yy;

	switch (tra) {
	case 0:
		xn = x; yn = y; break;
	case 1:
		xn = x; yn = sz-y; break;
	case 2:
		xn = y; yn = sz-x; break;
	case 3:
		xn = y; yn = x; break;
	case 4:
		xn = sz-x; yn = sz-y; break;
	case 5:
		xn = sz-x; yn = y; break;
	case 6:
		xn = sz-y; yn = x; break;
	case 7:
		xn = sz-y; yn = sz-x; break;
	default:
		errexit("impossible tra arg in transform0()");
	}

	*xx = xn;
	*yy = yn;
}

static void transform(int *xx, int *yy, int tra) {
	int x = *xx, y = *yy;
	int sz = size-1;

	if (x == '?' && y == '?')	/* possibly from Dyer sig */
		return;			/* nothing to rotate */
	if (x == 't' && y == 't')	/* pass */
		return;
	x -= 'a';
	y -= 'a';

	if (x == sz+1 && y == sz+1)		/* pass */
		return;
	if (x < 0 || x > sz || y < 0 || y > sz)
		errexit("off-board move %c%c", x+'a', y+'a');

	transform0(&x, &y, tra, size);

	*xx = x + 'a';
	*yy = y + 'a';
}

static void
put_propvalues(struct propvalue *p) {
	while (p) {
		fprintf(outf, "[%s]", p->val);
		p = p->next;
	}
}

#define SIZE(a)	(sizeof(a)/sizeof((a)[0]))

char *coltf[] = {
	"B", "W",
	"AB", "AW"
};

static void
color_transform(struct property *p) {
	int i;

	for (i=0; i<SIZE(coltf); i++) {
		if (!strcmp(p->id, coltf[i])) {
			p->id = coltf[i^1];
			break;
		}
	}
}

static int
is_whitespace(int c) {
	return (c == ' ' || c == '\n' || c == '\t');
}

static char *
transformed(char *s) {
	int x, y;
	char *t;

	while (is_whitespace(*s))
		s++;
	t = s;
	while (*t && *t != ':')
		t++;
	while (t > s && is_whitespace(t[-1]))
		t--;
	if (t == s)		/* something like B[] (an error) */
		return s;	/* do nothing */
	if (t-s != 2)
		errexit("unrecognized string to transform: _%s_", s);
	x = s[0];
	y = s[1];
	transform(&x,&y,opttra);
	s[0] = x;
	s[1] = y;
	return s;
}

/* transform the first part of each propvalue */
char *movelike[] = {
	"B", "W",		// move
	"AB", "AW", "AE", "TR",	// list of point
	"CR", "MA", "SL", "SQ",	// list of point
	"TB", "TW", "DD", "VW",	// elist of point
	"LB"			// list of composed point ':' simpletext
};

static int
is_movelike(char *s) {
	int i;

	for (i=0; i<SIZE(movelike); i++)
		if (!strcmp(s, movelike[i]))
		    return 1;
	return 0;
}

static void coord_transform(struct propvalue *pv) {
	while (pv) {
		pv->val = transformed(pv->val);
		pv = pv->next;
	}
}

static char *
transformed2(char *s) {
	int x, y;
	char *t, *u;

	while (is_whitespace(*s))
		s++;
	t = s;
	while (*t && *t != ':')
		t++;
	u = t;
	while (u > s && is_whitespace(u[-1]))
		u--;
	if (u-s != 2)
		errexit("unrecognized string to transform: _%s_", s);
	x = s[0];
	y = s[1];
	transform(&x,&y,opttra);
	s[0] = x;
	s[1] = y;
	if (*t++ != ':')
		errexit("transformed2: colon expected");
	while (is_whitespace(*t))
		t++;
	u = t;
	while (*u)
		u++;
	while (u > t && is_whitespace(u[-1]))
		u--;
	if (u-t != 2)
		errexit("unrecognized string to transform: _%s_", t);
	x = t[0];
	y = t[1];
	transform(&x,&y,opttra);
	t[0] = x;
	t[1] = y;
	return s;
}

/* transform both parts of a composed propvalue */
//	"AR", "LN"		// list of composed point ':' point
static int
is_arrowlike(char *s) {
	return !strcmp(s, "AR") || !strcmp(s, "LN");
}

static void coord_transform_both(struct propvalue *pv) {
	while (pv) {
		pv->val = transformed2(pv->val);
		pv = pv->next;
	}
}

static void
put_property_sequence(struct property *p) {
	while (p) {
		fprintf(outf, "\n");
		if (swapcolors)
			color_transform(p);
		fprintf(outf, "%s", p->id);

		/* set size on the fly - this takes effect from now on */
		if (!strcmp(p->id, "SZ"))
			setsize(p->val);

		if (is_movelike(p->id))
			coord_transform(p->val);
		if (is_arrowlike(p->id))
			coord_transform_both(p->val);
		put_propvalues(p->val);
		p = p->next;
	}
}

static int
is_move(struct property *p) {
	return (p && p->val->next == NULL &&
		(!strcmp(p->id, "B") || !strcmp(p->id, "W")));
}

static void
put_move(struct property *p) {
	if (swapcolors)
		color_transform(p);
	coord_transform(p->val);
	fprintf(outf, "%s[%s]", p->id, p->val->val);
}

static void
put_nodesequence(struct node *n) {
	struct property *p;

	while (n) {
		p = n->p;

		if (is_move(p)) {
			if (movesonthisline == movesperline) {
				fprintf(outf, "\n");
				movesonthisline = 0;
			}
			movesonthisline++;

			fprintf(outf, ";");
			put_move(p);
			p = p->next;
		} else {
			fprintf(outf, ";");
		}
		if (p)
			put_property_sequence(p);
		if (n == rootnode)
			fprintf(outf, "\n");
		n = n->next;
	}
}

static void put_gametree_sequence(struct gametree *g);

static void
put_gametree(struct gametree *g) {
	gtlevel++;
	if (gtlevel == 1) {
//		init_single_game(g);
		movesonthisline = movesperline;
		rootnode = g->nodesequence;
	}

	fprintf(outf, "(");
	put_nodesequence(g->nodesequence);
	put_gametree_sequence(g->firstchild);
	fprintf(outf, ")\n");
	gtlevel--;
}

static void
put_gametree_sequence(struct gametree *g) {
	while (g) {
		put_gametree(g);
		g = g->nextsibling;
	}
}

static void
do_stdin(const char *fn) {
	struct gametree *g;

	if (setjmp(jmpbuf))
		goto ret;
	have_jmpbuf = 1;

	readsgf(fn, &g);

	gtlevel = 0;
	put_gametree_sequence(g);
ret:
	have_jmpbuf = 0;
}

void
do_input(const char *s) {
	do_stdin(s);
}

/* option number: empty string denotes 1 */
static int getint(char *s) {
	char *se;
	int n = 1;

	if (*s) {
		n = strtol(s, &se, 10);
		if (se == s)
			errexit("digit expected in option");
		if (*se)
			errexit("garbage after option number");
	}
	return n;
}

int
main(int argc, char **argv) {
	char *outfilename = NULL;
	char *p;

	progname = "sgftf";
	infilename = "(reading options)";
	opttra = -1;		/* unspecified */

	while (argc > 1 && argv[1][0] == '-') {
		/* see below for condensed 1-letter options */
		if (!strcmp(argv[1], "--")) {
			argc--; argv++;
			break;
		}
		if (!strcmp(argv[1], "-vflip"))
			argv[1] = "-tra1";
		if (!strcmp(argv[1], "-bflip"))
			argv[1] = "-tra3";
		if (!strcmp(argv[1], "-hflip"))
			argv[1] = "-tra5";
		if (!strcmp(argv[1], "-dflip"))
			argv[1] = "-tra7";

		if (!strncmp(argv[1], "-e", 2)) {
			file_extension = argv[1]+2;
			goto next;
		}
		if (!strcmp(argv[1], "-i")) {
			ignore_errors = 1;
			goto next;
		}
		if (!strncmp(argv[1], "-o", 2)) {
			outfilename = argv[1]+2;
			if (!*outfilename && argc > 2) {
				argc--; argv++;
				outfilename = argv[1];
			}
			goto next;
		}
		if (!strcmp(argv[1], "-q")) {
			readquietly = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-r")) {
			recursive = 1;
			goto next;
		}
		if (!strncmp(argv[1], "-rot", 4)) {
			int n = getint(argv[1]+4);	/* default 1 */
			if (n%90 == 0)
				n /= 90;
			opttra = 2*(n%4);
			goto next;
		}
		if (!strcmp(argv[1], "-swapcolors")) {
			swapcolors = 1;
			goto next;
		}
		if (!strncmp(argv[1], "-sz", 3)) {
			optsize = getint(argv[1]+3);	/* override SZ[] */
			if (optsize < 2 || optsize > 19)
				optsize = 19;
			size = optsize;
			goto next;
		}
		if (!strcmp(argv[1], "-t")) {
			tracein = 1;
			goto next;
		}
		if (!strncmp(argv[1], "-tra", 4)) {
			int n = getint(argv[1]+4);	/* default 1 */
			if (n < 0 || n >= 8)
				errexit("-tra# option requires # < 8");
			opttra = n;
			goto next;
		}
#if 0
		if (!strncmp(argv[1], "-trunc", 6)) {
			/* positive: truncate to # moves;
			   negative: remove tail of this length */
			opttrunc = 1;
			trunclen = getint(argv[1]+6);
			goto next;
		}
#endif

		/* allow condensed options like -qi */
		/* they must all be single letter, without arg */
#define SINGLE_LETTER_OPTIONS "iqrt"
		p = argv[1]+1;
		while (*p)
			if (!index(SINGLE_LETTER_OPTIONS, *p++))
				goto bad;
		p = argv[1]+1;
		while (*p) {
			switch (*p) {
			case 'i':
				ignore_errors = 1; break;
			case 'q':
				readquietly = 1; break;
			case 'r':
				recursive = 1; break;
			case 't':
				tracein = 1; break;
			}
			p++;
		}

	next:
		argc--; argv++;
		continue;

	bad:
		errexit("unknown option %s\n\n"
			"usage: sgftf [-rot90] [-hflip] [-swapcolors] [...] [-o outf] < inf",
			argv[1]);
	}

	if (opttra == -1) {
		/* set default: rotate 180 deg */
		opttra = (swapcolors ? 0 : 4);
	}

	outf = stdout;
	if (outfilename) {
		outf = fopen(outfilename, "w");
		if (outf == NULL)
			errexit("cannot open %s", outfilename);
	}
		
	if (argc == 1) {
		if (recursive)
			errexit("refuse to read from stdin when recursive");
		do_input(NULL);
		goto ret;
	}

	while (argc > 1) {
		do_infile(argv[1]);	/* do_input(), perhaps recursively */
		argc--; argv++;
	}

ret:
	if (outfilename)
		fclose(outf);
	
	return 0;
}
