#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errexit.h"
#include "xmalloc.h"
#include "readsgf.h"

/* read in larger chunks than a single character */
// #define TRACING

#define PB	3
#define INBUFSZ	65536
unsigned char inbuf[PB+INBUFSZ];
unsigned char *inbufp;
int inbufct;
int notracect;

int multiin = 0;		/* expect multiple games (+garbage) */
int tracein = 0;		/* print input as it is being read */
int readquietly = 0;		/* suppress "skipping initial garbage" */
int fullprop = 0;		/* don't delete lower case letters in
				   property names */
static int eof, peekc;

/* we guarantee PB characters of pushback */
static inline void
my_pushback(int c) {
	inbufct++;
	*--inbufp = c;
#ifdef TRACING
	notracect++;
#endif
}

/* "White space (space, tab, carriage return, line feed, vertical tab
    and so on) may appear anywhere between PropValues, Properties,
    Nodes, Sequences and GameTrees." */
static inline int
iswhitespace(int c) {
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
		c == '\f' || c == '\v');
}

static int
mygetchar() {
	int c;

	if (peekc) {
		c = peekc;
		peekc = 0;
		return c;
	}

	if (eof)
		errexit("mygetchar called after eof");

	if (inbufct == 0) {
		inbufp = inbuf+PB;
		inbufct = fread((char *) inbuf+PB, 1, INBUFSZ, stdin);
		if (inbufct == 0) {
			eof = 1;
			return 0;
		}
	}

	inbufct--;
	c = *inbufp++;

	if (c == '\n')
		linenr++;

#ifdef TRACING
	if (tracein) {
		if (notracect)
			notracect--;
		else
			putchar(c);
	}
#endif

	return c;
}

/*
 * "Any white space characters outside of enclosing '[]' pairs are ignored"
 *
 * next symbol, skip whitespace, except when in text fields
 * (and possibly when inside a property identifier)
 */
static inline int
mygetsym() {
	int c;

	c = mygetchar();
	while (iswhitespace(c))
		c = mygetchar();
	return c;
}

/*
 * "Any text before the first opening parenthesis is ignored
 *  when reading a file".
 *
 * Often there is a mail or news header in front.
 * We have to skip lines like
 *
 * From someone@somewhere Sat Jan 01 12:13:14 1999
 * Message-ID: <...>
 * X-Mailer: Mozilla 2.02E-CIS  (Win16; I)
 *
 * Strategy: read until '(;' (possibly with intervening whitespace)
 * Return with (; pushed back.
 *
 * A different type of header is the 3-byte sequence ef bb bf,
 * that is U+FEFF, the byte order mark.
 *
 * Nihon Ki-in uses files like ( TE[...] RD[...] GK[1] ;B[pd] ... ;B[aa])
 * We should recognize this.
 */
static void
skip_initial_BOM() {
	int c;

	if (!eof) {
		c = mygetsym();
		if (c != 0xef || eof) {
			my_pushback(c);
			return;
		}
		c = mygetsym();
		if (c != 0xbb || eof) {
			my_pushback(c);
			my_pushback(0xef);
			return;
		}
		c = mygetsym();
		if (c != 0xbf) {
			my_pushback(c);
			my_pushback(0xbb);
			my_pushback(0xef);
			return;
		}
	}
	if (!readquietly)
		fprintf(stderr, "%s: skipped initial BOM\n", infilename);
}

static void
skip_initial_garbage() {
	int c;
	int msg = 0;

	/* skip until (; */
	while (!eof) {
		c = mygetsym();
		if (c == '(') {
			c = mygetsym();
			if (c == ';') {
				my_pushback(';');
				my_pushback('(');
				return;
			}
			if (c == 'T') {
				if (!readquietly)
					fprintf(stderr, "%s: SGF2-style\n",
						infilename);
				my_pushback('T');
				my_pushback(';');
				my_pushback('(');
				return;
			}
			peekc = c;
		}
		if (!msg++ && !eof && !readquietly)
			fprintf(stderr, "skipping initial garbage in %s ...\n",
				infilename);
	}
}

static char *propvaluebuf = NULL;
int propvaluebufsz = 0;
int propvaluestep = 10000;

/* <propvalue> :: "[" stuff "]" */
/* non-NULL */
static struct propvalue *
read_propvalue_following_sq(void) {
	int c;
	char *p = propvaluebuf;
	int propvalueroomleft;
	struct propvalue *res = (struct propvalue *) ymalloc(sizeof(*res));

	/* do not skip whitespace here: mygetchar, not mygetsym */
	while (1) {
		propvalueroomleft = propvaluebufsz - (p - propvaluebuf);
		if (propvalueroomleft < 3) {
			int poffset = p - propvaluebuf;
			propvaluestep *= 2;
			propvalueroomleft += propvaluestep;
			propvaluebufsz += propvaluestep;
			propvaluebuf = xrealloc(propvaluebuf, propvaluebufsz);
			p = propvaluebuf + poffset;
		}

		c = mygetchar();
		if (c == ']') {
#if 0
			break;
#else
			/* maybe it should have been escaped but wasn't? */
			int d = mygetsym();
			peekc = d;
			if (d == ';' || d == '(' || d == ')' || d == '[' ||
			    (d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z')) {
				/* lower case occurs in a context like
				   GaMe[1]goWriteVersion[1.4e] */
				break;
			}
			/* Unfortunetely one sees 2-byte SJIS ending in ']'
			   and followed by '(' - not easy to fix */

			/* One sees bare texts '187手完' (187 moves complete)
			   after the last move and before the ')'.
			   But a test requires more than a single-character
			   peekc. */

			if (!readquietly)
				fprintf(stderr, "%s: warning: unescaped ]\n",
					infilename);
//			errexit("unescaped ] followed by '%c' (0x%02x)", d, d);
//			break;
			/* note: we may have lost whitespace here */
#endif
		}
		*p++ = c;
		if (c == '\\') {
			c = mygetchar();
			*p++ = c;
		}
	}
	*p = 0;

	res->val = ystrdup(propvaluebuf);
	res->next = NULL;
	return res;
}

/* possibly NULL */
static struct propvalue *
read_propvalue_sequence(void){
	int c;
	struct propvalue *rh, *rt, *r;

	rh = NULL;
	rt = NULL;	/* for gcc only */
	while ((c = mygetsym()) == '[') {
		r = read_propvalue_following_sq();
		if (rh) {
			rt->next = r;
			rt = r;
		} else {
			rh = rt = r;
		}
	}
	peekc = c;
	return rh;
}

static int
is_upper_case(int c) {
	return ('A' <= c && c <= 'Z');
}

static int
is_lower_case(int c) {
	return ('a' <= c && c <= 'z');
}

/* <propid> :: <ucletter> <ucletter>* */
/* Called only in read_property(), from read_property_sequence()
   so we know that there is at least one letter. */
/* non-NULL */
static char *
read_propid(void) {
	int c;
	char propid[104];	/* so far, 10 suffices */
	char fullpropid[104];
	char *p = propid, *q = fullpropid;

	while (1) {
		c = mygetsym();
		if (!(is_upper_case(c) || is_lower_case(c)))
			break;
		if (q < fullpropid + sizeof(fullpropid) - 1)
			*q++ = c;
		if (p < propid + sizeof(propid) - 1 &&
		    (is_upper_case(c) || fullprop))
			*p++ = c;
	}
	peekc = c;
	*p = *q = 0;

	if (p == propid)
		errexit("propid '%s' is lower case only", fullpropid);

	if (p >= propid + sizeof(propid))
		errexit("propid too long");
	return ystrdup(propid);
}

/* <property> :: <propid> <propvalue> <propvalue>* */
/* non-NULL */
static struct property *
read_property(void) {
	struct property *res = (struct property *) ymalloc(sizeof(*res));

	res->id = read_propid();
	res->val = read_propvalue_sequence();
	if (res->val == NULL)
		errexit("missing propvalue for %s", res->id);
	res->next = NULL;
	return res;
}

/*
 * The FF[4] grammar prescribes capital letters here.
 * In practice one also finds lower case mixed in,
 * like `White' instead of `W' and `GaMe' instead of `GM'
 * and `File Format' instead of `FF'.
 * In rare cases one has lower case initially, as in 'goWriteVersion'.
 */
/* non-NULL */
static struct node *
read_property_sequence(void) {
	struct node *res = (struct node *) ymalloc(sizeof(*res));
	struct property *pt, *p;
	int c;

	res->p = pt = NULL;
	while (1) {
		c = mygetsym();
		peekc = c;
		if (!(is_upper_case(c) || is_lower_case(c)))
			break;
		p = read_property();
		if (pt) {
			pt->next = p;
			pt = p;
		} else {
			res->p = pt = p;
		}
	}

	res->next = NULL;
	return res;
}

/* <node> :: ";" <property>* */
/* possibly NULL */
static struct node *
read_node_sequence(void) {
	struct node *nh, *nt, *n;
	int c;

	nh = NULL;
	nt = NULL;	/* gcc only */
	while ((c = mygetsym()) == ';') {
		n = read_property_sequence();
		if (nh) {
			nt->next = n;
			nt = n;
		} else {
			nh = nt = n;
		}
	}
	peekc = c;
	return nh;
}

/* variation without ; - starts with RN or N property */
static struct node *
read_korean_node_sequence() {
	struct node *nh, *n;

	nh = n = read_property_sequence();
	if (!n || !n->p || !n->p->id ||
	    (strcmp(n->p->id, "RN") && strcmp(n->p->id, "RF") &&
	     strcmp(n->p->id, "N") && strcmp(n->p->id, "C"))) {
		errexit("empty node_sequence: `(' not followed by `;'"
			" (and not by RN[] or N[] or C[])");
	}
	n = read_node_sequence();
	nh->next = n;
	return nh;
}

/* <sequence> :: <node> <node>* */
/* non-NULL */
static struct node *
read_sequence(void) {
	struct node *n;

	n = read_node_sequence();
	if (n == NULL) {
		if (peekc == 'R' || peekc == 'N' || peekc == 'C')
			read_korean_node_sequence();
		else
			errexit("empty node_sequence: "
				"`(' not followed by `;'");
	}
	return n;
}

/* forward declaration */
static struct gametree *read_gametree_sequence(void);

/*
 * FF[4] says:
 *   <gametree> :: "(" <sequence> <gametree>* ")"
 * one also has to allow
 *   <baretree> :: <sequence> <gametree>*
 *   <gametree> :: "(" <baretree>* ")"
 */

static struct gametree *
read_baretree(void) {
	struct gametree *res = (struct gametree *) ymalloc(sizeof(*res));

	res->nodesequence = read_sequence();
	res->firstchild = read_gametree_sequence();
	res->nextsibling = NULL;
	return res;
}

/* possibly NULL, namely when following char is not one of ;CR */
static struct gametree *
read_baretree_sequence(void) {
	struct gametree *gh, *g;
	int c;

	c = mygetsym();
	peekc = c;
	if (c != ';' && c != 'R' && c != 'N' && c != 'C')
		return NULL;

	gh = read_baretree();
	c = mygetsym();
	peekc = c;
	if (c == ';' || c == 'C' || c == 'R') {
		g = read_baretree_sequence();
		g->nextsibling = gh->firstchild;
		gh->firstchild = g;
	}
	return gh;
}

/* possibly NULL */
static struct gametree *
read_gametree_sequence(void) {
	struct gametree *gh, *gt, *g;
	int c;

	gh = NULL;
	gt = NULL;	/* gcc only */
	while ((c = mygetsym()) == '(') {
		g = read_baretree_sequence();

		if (g == NULL) {
			/* ignore () */
		} else if (gh) {
			gt->nextsibling = g;
			gt = g;
		} else {
			gh = gt = g;
		}
		if ((c = mygetsym()) != ')')
			errexit("gametree does not end with ')' - got '%c'", c);
	}
	peekc = c;
	return gh;
}

/* <collection> :: <gametree> <gametree>* */
/* non-NULL */
static struct gametree *
read_collection() {
	struct gametree *g, *gh, *gn;

	g = read_gametree_sequence();
	if (g == NULL)
		errexit("empty gametree_sequence");
	if (multiin) {
		gh = g;
		while (gh) {
			skip_initial_garbage();
			if (eof)
				break;
			gn = read_gametree_sequence();
			gh->nextsibling = gn;
			gh = gn;
		}
	}
	return g;
}

/* read and parse an sgf file */
int readsgf(const char *fn, struct gametree **gg) {

	infilename = (fn ? fn : "-");

	if (strcmp(infilename, "-")) {
		FILE *f = freopen(fn, "r", stdin);
		if (!f)
			errexit("cannot open %s", fn);
	}

	eof = peekc = 0;
	inbufct = 0;
	linenr = 1;	/* report linenr on error */

	skip_initial_BOM();
	skip_initial_garbage();
	if (eof)
		errexit("no game found");

	*gg = read_collection();

	linenr = 0;	/* avoid error messages with linenr now */
	return 0;
}
