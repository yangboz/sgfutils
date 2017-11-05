/*
 * Make a diagram (.png, .jpg, .gif) from an SGF file
 *
 * Call: sgftopng [options] OUTFILE < INFILE
 * or:   sgftopng [options] OUTFILE [FROM]-[TO] < INFILE
 *
 * OUTFILE must be something with an extension that convert can handle,
 * such as .png, .jpg, .gif. The default is "out.png".
 *
 * A [FROM]-[TO] range will create unnumbered stones for the stones
 * that were still alive when move number FROM was done, and numbered
 * stones for the moves in the range FROM-TO (inclusive).
 * When FROM is missing, it is taken to be 1.
 * When TO is missing, it is taken to be the final move.
 * A 1000- range will give a diagram with unnumbered stones
 * showing the final board position. Better: -nonrs.
 *
 * Options:
 *  -info : list variants occurring in input file, do not create a diagram
 *  -var 3 : create a diagram of variant 3
 *  -game 2 : create a diagram of the 2nd game in the sgf file
 *  -from 1 : number the numbered stones from 1
 *  -o outfile : alternative way to specify OUTFILE
 * See also usage().
 */

/*
 * TODO:
 * Some way is needed to handle the information "N on M"
 * when several moves happen at the same place.
 *
 * Maybe add a text/title field at the bottom.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>	/* for unlink */

extern void errexit(const char *s, ...) __attribute__ ((noreturn));

/* fatal error */
void errexit(const char *s, ...) {
	va_list p;

	va_start(p, s);
	vfprintf(stderr, s, p);
	va_end(p);

	fprintf(stderr, "\n");
	exit(-1);
}

static void outwarn(const char *s, ...) {
	va_list p;

	va_start(p, s);
	vfprintf(stderr, s, p);
	va_end(p);
}

/* error-free malloc, realloc */
static void *xmalloc(size_t sz) {
	void *p = malloc(sz);
	if (p == NULL)
		errexit("out of memory");
	return p;
}

static void *xrealloc(void *pp, size_t sz) {
	void *p = realloc(pp, sz);
	if (p == NULL)
		errexit("out of memory");
	return p;
}

/* quote a string before feeding it to sh */
static char *quoted(char *s) {
	int n = strlen(s);
	char *p, *r = xmalloc(2*n+3);

	if (!index(s, '"'))
		sprintf(r, "\"%s\"", s);
	else if (!index(s, '\''))
		sprintf(r, "'%s'", s);
	else {
		p = r;
		while (*s) {
			*p++ = '\\';
			*p++ = *s++;
		}
		*p = 0;
	}
	return r;
}

char *inbuf;
int inbufsz;

static void enlarge_inbuf() {
	int sz = (inbufsz ? 4*inbufsz : 100000);

	inbufsz = sz;
	inbuf = xrealloc(inbuf, sz);
}

/* read entire input file into memory - might use mmap if not stdin */
static void readinput() {
	char *p;
	int n;

	enlarge_inbuf();
	p = inbuf;
	while (fgets(p, inbufsz - (p-inbuf), stdin)) {
		n = strlen(p);
		p += n;
		if (inbufsz - (p-inbuf) < 100) {
			enlarge_inbuf();
			p = inbuf;
			n = strlen(p);
			p += n;
		}
	}
}

/* remove whitespace from input */
static void remove_whitespace() {
	char *p, *q;

	p = q = inbuf;
	while (*p) {
		if (*p != ' ' && *p != '\r' && *p != '\n')
			*q++ = *p;
		p++;
	}
	*q = 0;
}

/* add closing parenthesis if needed and there is room */
static char *add_paren() {
	char *p;

	p = inbuf;
	while (*p++);
	if (p - inbuf >= inbufsz)
		errexit("no room to add a parenthesis");
	*p = 0;
	*--p = ')';
	return p;
}

/*
 * Coordinates: the origin is the upper left hand corner.
 * (x,y) for convert is x from left, y from top.
 * So x varies from 0 to cols-1, and y from 0 to rows -1.
 * Similarly, board[x][y] has first coord hor, second vert from the top.
 * And in sgf field aa is the upper left hand corner.
 *
 * The board is rows x cols (default 19x19, set by SZ[cols:rows]).
 * Of this board, a (ymin..ymax) x (xmin..xmax) window is shown
 * (default all, set by VW[aa:ss]).
 */

/* cannot be larger than the max length of a shell command */
#define CBUFSZ 361000

char command[CBUFSZ];

/* max board size */
#define SZ 19

char board[SZ][SZ];
int boardnumber[SZ][SZ];
char *boardlabel[SZ][SZ];

/* max game length */
#define MAXMOVES (2*SZ*SZ)

/* moves in current variation */
int moves[MAXMOVES];

/* current game, variation, move */
int gamenr, varnr, movenr;

/* idem to display */
int displaygame, displayvar, displayfrom, displayto;

/* number used for first numbered stone */
int displaynr0;
int optnonrs = 0;		/* don't number stones */
char *optcircle = 0;		/* later: circle these stones */

#define RSP 23
#define CSP 23
#define MARGIN 14
#define BOARDCOLOR "#f2b06d"	/* tree color */
#define COORDMARGIN	20
#define MARGINSHIFT	5	/* additional space if coord, not edge */

int bdheight;
int bdwidth;
int rowspacing = RSP;
int colspacing = CSP;
int leftmargin, rightmargin, topmargin, bottommargin;
int leftcoordmargin, rightcoordmargin, topcoordmargin, bottomcoordmargin;
int leftxcoordmargin, rightxcoordmargin, topxcoordmargin, bottomxcoordmargin;
int coordleft, coordright, coordtop, coordbottom;
char *bgcolor = BOARDCOLOR;
int pointsize;
int rows, cols;
int topedge, botedge, leftedge, rightedge;
int xmin, xmax, ymin, ymax;

static int lookat(int x, int y, int c, int *groupctp, int *group, int *seen) {
	int z;

	if (x < 0 || y < 0 || x >= cols || y >= rows)
		return 0;
	if (board[x][y] == 0)
		return 1;
	if (board[x][y] == c) {
		z = SZ*x+y;
		if (!seen[z]) {
			group[(*groupctp)++] = z;
			seen[z] = 1;
		}
	}
	return 0;
}

static void possibly_remove(int x, int y) {
	int groupct, group[SZ*SZ], seen[SZ*SZ], done, h, i, j, k;
	int c = board[x][y];

	for (i=0; i<SZ*SZ; i++)
		seen[i] = 0;
	done = 0;
	groupct = 1;
	h = SZ*x+y;
	group[0] = h;
	seen[h] = 1;

	while (done < groupct) {
		h = group[done++];
		i = h/SZ;
		j = h%SZ;
		if (lookat(i-1,j,c,&groupct,group,seen) ||
		    lookat(i+1,j,c,&groupct,group,seen) ||
		    lookat(i,j-1,c,&groupct,group,seen) ||
		    lookat(i,j+1,c,&groupct,group,seen))
			return;
	}

	for (k=0; k<groupct; k++) {
		h = group[k];
		i = h/SZ;
		j = h%SZ;
		board[i][j] = 0;
		boardnumber[i][j] = 0;
	}
}

/* remove what is killed by (x,y), then perhaps (x,y) itself */
static void remove_dead_groups(int x, int y) {
	int c = board[x][y];	/* our color */
	int oc = ('X'+'O'-c);	/* the other color */
	if (x > 0 && board[x-1][y] == oc)
		possibly_remove(x-1,y);
	if (x < cols-1 && board[x+1][y] == oc)
		possibly_remove(x+1,y);
	if (y > 0 && board[x][y-1] == oc)
		possibly_remove(x,y-1);
	if (y < rows-1 && board[x][y+1] == oc)
		possibly_remove(x,y+1);
	possibly_remove(x,y);
}

static inline int toint(char c) {
	return c - 'a';
}

static int tox(char c) {
	int n = toint(c);

	if (n < 0 || n >= cols)
		errexit("bad 1st coord '%c' (%d); #cols=%d", c, n+1, cols);
	return n;
}

static int toy(char c) {
	int n = toint(c);

	if (n < 0 || n >= rows)
		errexit("bad 2nd coord '%c' (%d); #rows=%d", c, n+1, rows);
	return n;
}

static int is_cap_letter(int c) {
	return c >= 'A' && c <= 'Z';
}

static void skiparg(char **pp) {
	char *p = *pp;

	if (*p != '[')
		errexit("program bug: arg does not start with [");
	while (*p && *p != ']') {
		if (*p == '\\' && *p)	/* what with \\[ ? */
			p++;
		p++;
	}
	if (*p == 0)
		errexit("unterminated arg");
	p++;
	*pp = p;
}

static void skipargs(char **pp) {
	while (**pp == '[')
		skiparg(pp);
}

static void skipproperty(char **pp) {
	if (!is_cap_letter(**pp))
		errexit("unrecognized syntax %s", *pp);
	while (is_cap_letter(**pp))
		(*pp)++;
	if (**pp != '[' && **pp != ';' && **pp != '(' && **pp != ')')
		errexit("unrecognized syntax %s", *pp);
	skipargs(pp);
}

/* called with *p pointing at ( */
static char *matching_paren(char *p) {
	int ct = 0;

	while (*p) {
		if (*p == '[') {
			skiparg(&p);
			continue;
		}
		if (*p == '(')
			ct++;
		if (*p == ')')
			ct--;
		if (ct == 0)
			return p;
		p++;
	}
	outwarn("no matching parenthesis - adding one at the end\n");
	return add_paren();
}

static void setrows(int r) {
	rows = r;
	ymin = 0;
	ymax = r-1;
}

static void setcols(int c) {
	cols = c;
	xmin = 0;
	xmax = c-1;
}

/* SZ[size] or SZ[cols:rows] */
static void setsize(char **pp) {
	int r, c;
	char *end;

	c = strtoul(*pp + 1, &end, 10);
	if (*end == ']')
		r = c;
	else if (*end == ':')
		r = strtoul(end + 1, &end, 10);
	if (*end != ']')
		errexit("trailing junk in SZ property");
	if (c < 1 || c > SZ || r < 1 || r > SZ)
		errexit("bad size %d x %d", rows, cols);
	setrows(r);
	setcols(c);
	skiparg(pp);
}

/* select view on board */
static void setxyminmax(int x1, int x2, int y1, int y2) {
	/* this property should follow the SZ property, if any */
	topedge = (y1 == 0);
	botedge = (y2 == rows-1);
	leftedge = (x1 == 0);
	rightedge = (x2 == cols-1);

	xmin = x1;
	xmax = x2;
	ymin = y1;
	ymax = y2;
}

/* VW[an:ms] - show bottom left 13x6 corner */
static void setvisualpart(char **pp) {
	int x1,y1,x2,y2;
	char *p = *pp;

	/* the standard allows arbitrary shapes -
	   we only do a single rectangle */
	if (p[1] == ']') {
		skipargs(pp);
		outwarn("empty VW node - ignored\n");
		return;
	}
	if (p[3] != ':' || p[6] != ']') {
		skipargs(pp);
		outwarn("unsupported VW node - ignored\n");
		return;
	}
	x1 = tox(p[1]);
	y1 = toy(p[2]);
	x2 = tox(p[4]);
	y2 = toy(p[5]);
	setxyminmax(x1, x2, y1, y2);
	skiparg(pp);
}

static void
setmargins(char *optcoord) {
	leftmargin = rightmargin = topmargin = bottommargin = MARGIN;
	if (!optcoord)
		leftcoordmargin = rightcoordmargin =
			topcoordmargin = bottomcoordmargin = 0;
	else if (!*optcoord) {
		coordleft = coordright = coordtop = coordbottom = 1;
		leftcoordmargin = rightcoordmargin =
			topcoordmargin = bottomcoordmargin = COORDMARGIN;
	} else {
		leftcoordmargin = rightcoordmargin =
			topcoordmargin = bottomcoordmargin = 0;
		while (*optcoord) {
			switch (*optcoord++) {
			case 'L':
				coordleft = 1;
				leftcoordmargin = COORDMARGIN; break;
			case 'R':
				coordright = 1;
				rightcoordmargin = COORDMARGIN; break;
			case 'T':
				coordtop = 1;
				topcoordmargin = COORDMARGIN; break;
			case 'B':
				coordbottom = 1;
				bottomcoordmargin = COORDMARGIN; break;
			default:
				errexit("usage: sgftopng -coord[LRTB] ...");
			}
		}
	}
	leftxcoordmargin =  (leftcoordmargin && !leftedge) ? MARGINSHIFT : 0;
	rightxcoordmargin = (rightcoordmargin && !rightedge) ? MARGINSHIFT : 0;
	topxcoordmargin = (topcoordmargin && !topedge) ? MARGINSHIFT : 0;
	bottomxcoordmargin = (bottomcoordmargin && !botedge) ? MARGINSHIFT : 0;

	leftcoordmargin += leftxcoordmargin;
	rightcoordmargin += rightxcoordmargin;
	topcoordmargin += topxcoordmargin;
	bottomcoordmargin += bottomxcoordmargin;

	leftmargin += leftcoordmargin;
	rightmargin += rightcoordmargin;
	topmargin += topcoordmargin;
	bottommargin += bottomcoordmargin;
}

/* expect -view rowmin-rowmax,colmin-colmax */
static void setview(char *s) {
	char *sep = "-,-";	/* expected separators: - , - NUL */
	char *se;
	int i;
	unsigned long xx[4];

	for (i=0; i<4; i++) {
		xx[i] = strtoul(s, &se, 10) - 1;
		if (*se != sep[i])
			goto usage;
		s = se+1;
	}
	if (xx[1] < xx[0] || xx[3] < xx[2])
		goto usage;
	if (xx[0] < 0 || xx[1] >= rows || xx[2] < 0 || xx[3] >= cols)
		goto usage;
	setxyminmax(xx[2], xx[3], xx[0], xx[1]);
	return;

usage:
	errexit("usage: sgftopng -view rowmin-rowmax,colmin-colmax ...");
}

#if 0
static void add1stone(char **pp, char c) {
	char *p = *pp;

	if (p[3] != ']')
		errexit("add1stone: no ]");
	board[tox(p[1])][toy(p[2])] = c;
	skiparg(pp);
}
#endif

static void add1label(char **pp) {
	char *p, *q;

	q = p = *pp;
	while (*q && *q != ']')
		q++;		/* perhaps skiparg? */
	if (q-p < 5 || p[3] != ':' || *q != ']')
		errexit("add1label: not xy:A]");
	boardlabel[tox(p[1])][toy(p[2])] = strndup(p+4, q-p-4);
	skiparg(pp);
}

static void addlabel(char **pp) {
	while (**pp == '[')
		add1label(pp);
}

static void addrange(char **pp, char c) {
	char *s, *t;
	char x, y;

	s = *pp;
	if (s[3] == ']')
		t = s;
	else if (s[3] == ':' && s[6] == ']')
		t = s+3;
	else
		errexit("addrange: unrecognized range %s");
	for (x=s[1]; x<=t[1]; x++)
		for (y=s[2]; y<=t[2]; y++)
			board[tox(x)][toy(y)] = c;
	skiparg(pp);
}

/* e.g. AB[bp:cp][br][ch] */
static void addstones(char **pp, char c) {
	while (**pp == '[')
		addrange(pp, c);
}

static void addbstones(char **pp) {
	addstones(pp, 'X');
}

static void addwstones(char **pp) {
	addstones(pp, 'O');
}

static void playstone(char **pp, char c) {
	char *p;
	int x,y;

	movenr++;
	p = *pp;
	if (p[1] == ']' || !strncmp(p+1, "tt]", 3)) {
		/* pass */
		skiparg(pp);
		return;
	}

	if (p[3] != ']')
		errexit("playstone: no ]");
	x = tox(p[1]);
	y = toy(p[2]);
	board[x][y] = c;

	/* the number is the number of the first stone played there */
	if (!boardnumber[x][y])
		boardnumber[x][y] = movenr;
	else {
// make some interface for outputting ko moves
//		printf("%d:%d\n", movenr, boardnumber[x][y]);
	}

	skiparg(pp);

	if (displayfrom && movenr < displayfrom)
		remove_dead_groups(x,y);
}

static void playbstone(char **pp) {
        playstone(pp, 'X');
}

static void playwstone(char **pp) {
        playstone(pp, 'O');
}

#define SIZE(x)	(sizeof(x)/sizeof((x)[0]))

struct property {
	char *name;
	int ismove;
	void (*fn)(char **);
} props[] = {
	{ "B", 1, playbstone },
	{ "W", 1, playwstone },
	{ "AB", 0, addbstones },
	{ "AW", 0, addwstones },
	{ "LB", 0, addlabel },
	{ "SZ", 0, setsize },
	{ "VW", 0, setvisualpart },
};

static void scansgf(char *begin, char *end) {
	struct property *pr;
	char *p, *q;
	int n;

	p = begin+1;
	while (p < end && *p != ')') {
		if (*p == ' ') {
			p++;
			continue;
		}
		if (*p == ';') {
			p++;
			continue;
		}
		if (*p == '(') {
			q = matching_paren(p);
			if (q[1] == '(')
				p = q;	/* last variant only */
			p++;
			continue;
		}
		for (pr = props; pr < props+SIZE(props); pr++) {
			n = strlen(pr->name);
			if (!strncmp(p, pr->name, n) && p[n] == '[') {
				p += n;
				pr->fn(&p);
				goto prdone;
			}
		}
		/* ignore unknown properties */
		skipproperty(&p);
	prdone:;
/*
 * Do we want to see the nonmoves after the final move?
 * At the start, yes, otherwise perhaps no.
 * Or we must introduce some syntax and make this selectable.
 */
		if (movenr && displayto && movenr >= displayto-1)
			return;
	}
}

static void wipe(char *begin, char *end) {
	while (begin <= end)
		*begin++ = ' ';
}

/* scan the game, and remove the parts in nonselected variations */
static void readsgfvar(char *begin, char *end, int *depth, int *depth0, int islast) {
	char *p, *q;
	int last;

	if (displayvar && varnr > displayvar) {
		wipe(begin, end);
		return;
	}

	(*depth)++;
	p = begin+1;
	while (p < end) {
		if (*p == '[') {	
			skiparg(&p);
			continue;
		}
		if (*p == '(') {
			q = matching_paren(p);
			last = (q[1] != '(');
			readsgfvar(p, q, depth, depth0, last);
			p = q;
			if (displayvar && varnr == displayvar) {
				wipe(p+1,end-1);
				return;
			}
		}
		p++;
	}
	if ((*depth0) < (*depth))
		varnr++;
	(*depth)--;
	*depth0 = *depth;

	if ((displayvar && varnr < displayvar) || (!displayvar && !islast))
		wipe(begin, end);
}

static void readsgfgame(char *begin, char *end) {
	int depth, depth0;

	varnr = depth = depth0 = 0;
	readsgfvar(begin, end, &depth, &depth0,1);
	scansgf(begin,end);
}

/* sometimes a file starts with BOM = U+FEFF, i.e., ef bb bf */
char BOM[3] = "\xef\xbb\xbf";

static void readsgf() {
	char *p, *q;

	readinput();
	remove_whitespace();
	gamenr = 0;

	p = inbuf;
	if (!strncmp(p, BOM, 3))
		p += 3;
	if (*p != '(')
		errexit("sgf does not start with (");
	while (*p) {
		if (*p == '(') {
			q = matching_paren(p);
			if (++gamenr == displaygame)
				readsgfgame(p,q);
			p = q;
		}
		p++;
	}
}

static inline int horx(int i) {
	return leftmargin+i*colspacing;
}

static inline int verty(int j) {
	return topmargin+j*rowspacing;
}

static void compute_dimensions() {
	int r = ymax-ymin;	/* rows-1 */
	int c = xmax-xmin;	/* cols-1 */
	bdheight = r*rowspacing + topmargin + bottommargin + 1;
	bdwidth = c*colspacing + leftmargin + rightmargin + 1;
}

/* as long as ImageMagick cannot center a string at a given position,
   we have to fiddle a bit to get things right */

static int hoffsetdigs(int n) {		/* n is number of digits */
	return (pointsize == 14)
		? ((n == 1) ? 3 : (n == 2) ? 7 : 10)
		: ((n == 1) ? 3 : (n == 2) ? 6 : 9);
}

static int hoffsetnum(int n) {		/* n is number to show */
	int len = (n < 10) ? 1 : (n < 100) ? 2 : 3;
	return hoffsetdigs(len);
}

static int unistrlen(char *s) {
	int len;

	if (*s++ == 0)
		return 0;		/* empty string */
	len = 1;			/* at least 1 */
	while (*s)
		if ((*s++ & 0300) != 0200)
			len++;		/* not unicode tail */
	return len;
}

static int hoffset(char *s) {		/* s is text to show */
	int len = unistrlen(s);
	return hoffsetdigs(len);
}

/* white on black needs to be shifted less */
static int voffset(int w) {
	return 5-w;
}

static void drawgrid(char **pp) {
	int i,j,begin,end;

	for (i=0; i<=ymax-ymin; i++) {
		begin = (leftedge ? leftmargin : leftcoordmargin);
		end = bdwidth - 1 - (rightedge ? rightmargin : rightcoordmargin);
		*pp += sprintf(*pp, " -draw \"line %d,%d %d,%d\"",
			       begin, verty(i), end, verty(i));
	}
	for (j=0; j<=xmax-xmin; j++) {
		begin = (topedge ? topmargin : topcoordmargin);
		end = bdheight - 1 - (botedge ? bottommargin : bottomcoordmargin);
		*pp += sprintf(*pp, " -draw \"line %d,%d %d,%d\"",
			       horx(j), begin, horx(j), end);
	}
}

static void drawcoords(char **pp) {
	int x, y;

	*pp += sprintf(*pp, " -fill black -stroke none");
	for (y = ymin; y <= ymax; y++) {
		int v = rows-y;
		if (coordleft)
		*pp += sprintf(*pp, " -draw \"text %d,%d '%d'\"",
			       (leftcoordmargin-leftxcoordmargin)/2 + 1 - hoffsetnum(v),
			       verty(y-ymin)+voffset(0), v);
		if (coordright)
		*pp += sprintf(*pp, " -draw \"text %d,%d '%d'\"",
			       bdwidth-(rightcoordmargin-rightxcoordmargin)/2 - 1 - hoffsetnum(v),
			       verty(y-ymin)+voffset(0), v);
	}

	for (x = xmin; x <= xmax; x++) {
		char let;

		let = 'A' + x;
		if (let >= 'I')
			let++;

		if (coordtop)
		*pp += sprintf(*pp, " -draw \"text %d,%d '%c'\"",
			       horx(x-xmin)-hoffset("X"),
			       (topcoordmargin-topxcoordmargin)/2 + 6, let);
		if (coordbottom)
		*pp += sprintf(*pp, " -draw \"text %d,%d '%c'\"",
			       horx(x-xmin)-hoffset("X"),
			       bdheight-(bottomcoordmargin-bottomxcoordmargin)/2 + 3, let);
	}
}

/* small black circle, radius 1.5 */
static void drawhoshi(char **pp, int x, int y) {
	*pp += sprintf(*pp, " -fill black -stroke none");
	*pp += sprintf(*pp, " -draw \"circle %d,%d %d,%d.5\"",
		       horx(x), verty(y), horx(x)+1, verty(y));
}

static void drawstone(char **pp, int x, int y) {
	*pp += sprintf(*pp, " -draw \"circle %d,%d %d,%d\"",
		       horx(x), verty(y), horx(x)+rowspacing/2, verty(y));
}

static void drawbs(char **pp, int x, int y) {
	*pp += sprintf(*pp, " -fill black -stroke black");
	drawstone(pp, x, y);
}

static void drawws(char **pp, int x, int y) {
	*pp += sprintf(*pp, " -fill white -stroke black");
	drawstone(pp, x, y);
}

static void drawcircle(char **pp, int x, int y) {
//	*pp += sprintf(*pp, " -draw \"circle %d,%d %d,%d\"",
//		       horx(x), verty(y), horx(x)+rowspacing/3, verty(y));

	*pp += sprintf(*pp, " -draw \"circle %d,%d %d,%d\"",
		       horx(x), verty(y), horx(x)+rowspacing/6, verty(y));
}

static void drawwc(char **pp, int x, int y) {
//	*pp += sprintf(*pp, " -fill none -stroke white");
	*pp += sprintf(*pp, " -fill red -stroke none");
	drawcircle(pp, x, y);
}

static void drawbc(char **pp, int x, int y) {
//	*pp += sprintf(*pp, " -fill none -stroke black");
	*pp += sprintf(*pp, " -fill red -stroke none");
	drawcircle(pp, x, y);
}

static void drawbtext(char **pp, int x, int y, char *s) {
	*pp += sprintf(*pp, " -fill black -stroke none");
	*pp += sprintf(*pp, " -draw \"text %d,%d '%s'\"",
		       horx(x)-hoffset(s), verty(y)+voffset(0), s);
}

static void drawwtext(char **pp, int x, int y, char *s) {
	*pp += sprintf(*pp, " -fill white -stroke white");
	*pp += sprintf(*pp, " -draw \"text %d,%d '%s'\"",
		       horx(x)-hoffset(s), verty(y)+voffset(1), s);
}

static void drawbbtext(char **pp, int x, int y, char *s) {
	*pp += sprintf(*pp, " -fill '%s' -stroke none", bgcolor);
	*pp += sprintf(*pp, " -draw \"rectangle %d,%d %d,%d\"",
		       horx(x)-rowspacing/3, verty(y)-colspacing/3,
		       horx(x)+rowspacing/3, verty(y)+colspacing/3);
	*pp += sprintf(*pp, " -fill black -stroke none");
	*pp += sprintf(*pp, " -draw \"text %d,%d '%s'\"",
		       horx(x)-hoffset(s), verty(y)+voffset(0), s);
}

/* a is a move number, probably in the range 1-361 */
static int intwidth(int a) {
	return (a < 10) ? 1 : (a < 100) ? 2 : 3;
}

#define STACKSZ 1000

static void outinfo1(char *begin, char *end, int *varnr, int *movenr, int *stack, int *stct, int *stct0) {
	char *p, *q;
	int h, i, ct;

	if (*stct + 2 >= STACKSZ)
		errexit("stack overflow");
	stack[(*stct)++] = *movenr;	/* last move */

	p = begin+1;
	while (p < end) {
		if (*p == '[') {	
			skiparg(&p);
			continue;
		}
		if (*p == '(') {
			q = matching_paren(p);
			outinfo1(p, q, varnr, movenr, stack, stct, stct0);
			p = q;
		}
		if (!strncmp(p, ";B[", 3) || !strncmp(p, ";W[", 3))
			(*movenr)++;
		p++;
	}

	stack[(*stct)] = *movenr;	/* current move */

	ct = 0;
	h = *stct0;
	for (i = 0; i < h; i++)
		ct += intwidth(stack[i]+1) + intwidth(stack[i+1]);
	ct += 3*h;

	if (h < *stct) {
		printf("\nvar %d:%*s", ++*varnr, ct, "");
		for (i=h; i<*stct; i++)
			printf(" (%d-%d", stack[i]+1, stack[i+1]);
	}
	printf(")");

	*movenr = stack[--*stct];
	*stct0 = *stct;
}

/* print input skeleton */
static void outinfo() {
	int gamenr, varnr, movenr, stack[STACKSZ], stct, stct0;
	char *p, *q;

	readinput();
	remove_whitespace();
	gamenr = 0;
	stct = 0;

	p = inbuf;
	while (*p) {
		if (*p == '(') {
			printf("Game #%d", ++gamenr);
			varnr = movenr = stct0 = 0;
			q = matching_paren(p);
			outinfo1(p,q,&varnr,&movenr,stack,&stct,&stct0);
			printf("\n\n");
			p = q;
		}
		p++;
	}
}

static int ends_in(char *s, char *tail) {
	char *se, *te;

	se = s;
	while (*se)
		se++;
	te = tail;
	while (*te)
		te++;
	while (te > tail)
		if (se <= s || *--se != *--te)
			return 0;
	return 1;
}

static int is_digit(int c) {
	return (c >= '0' && c <= '9');
}

static int isfromto(char *s) {
	while (is_digit(*s))
		s++;
	if (*s++ != '-')
		return 0;
	while (is_digit(*s))
		s++;
	return (*s == 0);
}

static void setfromto(char *s) {
	unsigned long from, to;
	char *end;

	from = to = 0;

	if (*s != '-')
		from = strtoul(s, &end, 10);
	else
		end = s;

	if (*end++ != '-')
		errexit("no dash in fromto");
	s = end;

	if (*s)
		to = strtoul(s, &end, 10) + 1;

	if (*end)
		errexit("trailing junk in fromto");

	displayfrom = from;
	displayto = to;
}

/* not mentioned: -coord, -view */
static void usage() {
	fprintf(stderr, "\nCall:\n"
" sgftopng < in.sgf                -- write game diagram to out.png\n"
" sgftopng -o outfile < in.sgf     -- write game diagram to outfile\n"
" sgftopng -o outfile 51-100 < in.sgf -- show moves 51..100\n"
" sgftopng -info < in.sgf          -- write variationtree to stdout\n"
" sgftopng -game M -var N ...      -- select game M variation N\n"
"  -from 1                         -- number the numbered stones from 1\n"
"\n"
"Default: game 1, variation 0 (the final one)\n"
"Output format is determined by the name suffix: x.png, x.jpg, x.gif\n"
	"\n");
	exit(1);
}

int main(int argc, char **argv) {
	char *p = command;
	char *outfile = NULL;
	char *infile = NULL;
	char *argp;
	char *optview = NULL;
	char *optfont = NULL;
	char *optcoord = NULL;
	int optdebug = 0;
	int optinfo = 0;
	int i,j,c,nr;
	char *lb;

	/* use temporary files when commandsz is limited */
	int maxcommandlinelength = 0;
	char tmpfile[100];		/* TMP-NNN.png */
	int tmpfct = 0;

	displaygame = 1;
	displayvar = 0;
	displayfrom = displayto = 0;

	/*
	 * There are options, they start with -
	 * and from-to directives, they contain - and otherwise digits only
	 * and possibly one outputfile and one inputfile.
	 * If a file is called *.sgf, it is an input file.
	 * Otherwise expect input in stdin.
	 */

	for (i=1; i<argc; i++) {
		argp = argv[i];

		if (isfromto(argp)) {
			setfromto(argp);
			continue;
		}

		if (*argp == '-') {
			if (argp[1] == '-')
				argp++;	/* allow --option for -option */

			if (!strcmp(argp, "-debug"))
				optdebug = 1;
			else if (!strcmp(argp, "-info"))
				optinfo = 1;
			else if (!strcmp(argp, "-font") && i < argc-1)
				optfont = argv[++i];
			else if (!strncmp(argp, "-coord", 6))
				optcoord = argp+6;
			else if (!strcmp(argp, "-view") && i < argc-1)
				optview = argv[++i];
			else if (!strcmp(argp, "-game") && i < argc-1)
				displaygame = atoi(argv[++i]);
			else if (!strncmp(argp, "-game", 5) && argp[5])
				displaygame = atoi(argp+5);
			else if (!strcmp(argp, "-var") && i < argc-1)
				displayvar = atoi(argv[++i]);
			else if (!strncmp(argp, "-var", 4) && argp[4])
				displayvar = atoi(argp+4);
			else if (!strcmp(argp, "-from") && i < argc-1)
				displaynr0 = atoi(argv[++i]);
			else if (!strncmp(argp, "-from", 5) && argp[5])
				displaynr0 = atoi(argp+5);
			else if (!strcmp(argp, "-nonrs"))
				optnonrs = 1;
			else if (!strcmp(argp, "-circle"))
				optcircle = argp+7;
			else if (!strcmp(argp, "-o") && i < argc-1)
				outfile = argv[++i];
			else if (!strcmp(argp, "-maxcommandsz") && i < argc-1)
				maxcommandlinelength = atoi(argv[++i]);
			else if (!strncmp(argp, "-maxcommandsz", 13)) {
				char *q = argp+13;
				if (*q == ':' || *q == '=')
					q++;
				maxcommandlinelength = atoi(q);
			} else
				usage();	/* unknown option */
			continue;
		}

		if (ends_in(argp, ".sgf")) {
			if (infile)
				errexit("sgftopng: at most one inputfile");
			infile = argp;
			continue;
		}

		if (outfile)
			errexit("sgftopng: at most one outputfile");
		outfile = argp;
	}

	if (optnonrs && !displayfrom)
		displayfrom = 10000;	/* infinity */
	
	if (infile) {
		/* reopen as stdin */
		FILE *f = freopen(infile, "r", stdin);
		if (!f)
			errexit("sgftopng: cannot open %s for reading",
				infile);
	}

	if (optinfo) {
		if (outfile)
			errexit("sgftopng: no outputfile used with -info");
		outinfo();
		return 0;
	}

	if (!outfile)
		outfile = "out.png";
	if (!index(outfile, '.'))
		errexit("outputfile %s has no extension", outfile);

	setrows(SZ);
	setcols(SZ);
	topedge = botedge = leftedge = rightedge = 1;

	readsgf();

	if (optview)
		setview(optview);
	setmargins(optcoord);
	compute_dimensions();

	pointsize = 14;
	if (movenr > 99)
		pointsize = 12;

	p += sprintf(p, "convert -size %dx%d xc:%s",
		     bdwidth, bdheight, bgcolor);

	if (!optfont)
		optfont = "Times-Roman";
	p += sprintf(p, " -font '%s' -pointsize %d", optfont, pointsize);

	drawgrid(&p);
	if (optcoord)
		drawcoords(&p);
	for (i=xmin; i<=xmax; i++) for (j=ymin; j<=ymax; j++) {
		int x,y;
		char txt[10];

		/* break up long commands if requested */
		if (maxcommandlinelength &&
		    strlen(command) >= maxcommandlinelength-250) {
			sprintf(tmpfile, "TMP-%d.png", ++tmpfct);
			p += sprintf(p, " %s", quoted(tmpfile));
			if (optdebug)
				printf("%s\n", command);
			else if (system(command))
				errexit("(partial) draw command failed");
			p = command;
			p += sprintf(p, "convert %s", quoted(tmpfile));
			p += sprintf(p, " -font '%s' -pointsize %d",
				     optfont, pointsize);
		}

		x = i-xmin;
		y = j-ymin;

		c = board[i][j];
		nr = boardnumber[i][j];
		lb = boardlabel[i][j];

		if (nr && nr == movenr && optcircle) {
			if (c == 'O') {
				drawws(&p,x,y);
				drawbc(&p,x,y);
			} else {
				drawbs(&p,x,y);
				drawwc(&p,x,y);
			}
			continue;
		}

		if (nr) {
			if (displayto && nr >= displayto)
				goto skip;
			if (displayfrom && nr < displayfrom)
				nr = 0;
			if (displayfrom && nr && displaynr0)
				nr = nr-displayfrom+displaynr0;
			if (optnonrs)
				nr = 0;
		}

		if (!c && nr)
			errexit("impossible: nr without player");
		if (lb && nr) {
			outwarn("move %d: don't know how to show both "
				"number and label %s - ignored label\n",
				nr, lb);
			lb = 0;
		}
		if (nr)
			sprintf(txt, "%d", nr);
		if (lb)
			sprintf(txt, "%s", lb);

		if (nr || (c && lb)) {
			if (c == 'O') {
				drawws(&p,x,y);
				drawbtext(&p,x,y,txt);
			} else {
				drawbs(&p,x,y);
				drawwtext(&p,x,y,txt);
			}
			continue;
		}

		if (lb) {
			drawbbtext(&p,x,y,txt);
			continue;
		}

		if (c) {
			if (c == 'O')
				drawws(&p,x,y);
			else
				drawbs(&p,x,y);
			continue;
		}
	skip:
		if (rows == 19 && cols == 19 &&
		    (i%6) == 3 && (j%6) == 3)
			drawhoshi(&p,x,y);
	}

	p += sprintf(p, " %s", quoted(outfile));

	if (optdebug)
		printf("%s\n", command);	/* show, don't do */
	else if (system(command))
		errexit("draw command failed");

	/* cleanup */
	if (tmpfct) {
		for (i=1; i <= tmpfct; i++) {
			sprintf(tmpfile, "TMP-%d.png", i);
			if (unlink(tmpfile))
				fprintf(stderr, "cannot delete temp file %s\n",
					tmpfile);
		}
	}

	return 0;
}
