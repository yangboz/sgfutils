/* Version of readsgf designed for precise checking */

/*
 * FF[4] grammar
 *
 * Collection = GameTree+
 * GameTree   = "(" Sequence GameTree* ")"
 * Sequence   = Node+
 * Node       = ";" { Property }
 * Property   = PropIdent PropValue+
 * PropIdent  = UcLetter+
 * PropValue  = "[" CValueType "]"
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errexit.h"
#include "xmalloc.h"
#include "readsgf.h"

/* read in larger chunks than a single character */

#define INBUFSZ	65536
unsigned char inbuf[INBUFSZ];
unsigned char *inbufp;
int inbufct;
int mywarnct;

int tracein = 0;		/* print input as it is being read */
static int eof, peekc;

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
		errexit("premature eof");

	if (inbufct == 0) {
		inbufp = inbuf;
		inbufct = fread(inbuf, 1, INBUFSZ, stdin);
		if (inbufct == 0) {
			eof = 1;
			return 0;
		}
	}

	inbufct--;
	c = *inbufp++;

	if (c == '\n')
		linenr++;
	if (tracein)
		putchar(c);

	return c;
}

/* next symbol, skip whitespace, except when in text fields */
static inline int
mygetsym() {
	int c;

	c = mygetchar();
	while (iswhitespace(c))
		c = mygetchar();
	return c;
}

static char *propvaluebuf = NULL;
int propvaluebufsz = 0;
int propvalueroomleft = 0;
int propvaluestep = 10000;

/* <propvalue> :: "[" stuff "]" */
/* non-NULL */
static struct propvalue *
read_propvalue_following_sq(void) {
	int c;
	char *p = propvaluebuf;
	struct propvalue *res = (struct propvalue *) ymalloc(sizeof(*res));

	/* do not skip whitespace here: mygetchar, not mygetsym */
	while (1) {
		/* make sure there is room, at least for the final NUL */
		if (propvalueroomleft < 2) {
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
			warn("unescaped ]");
		}
#endif
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
	char propid[80];	/* 2 suffices */
	char *p = propid;

	while (1) {
		c = mygetsym();
		if (!(is_upper_case(c) || is_lower_case(c)))
			break;
		if (is_upper_case(c) && p < propid + sizeof(propid))
			*p++ = c;
	}
	peekc = c;
	*p = 0;

	if (p == propid)
		errexit("propid is lower case only");
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
		if (is_lower_case(c) && !mywarnct++)
			warn("lower case chars in propid");
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

/* <sequence> :: <node> <node>* */
/* non-NULL */
static struct node *
read_sequence(void) {
	struct node *n;

	n = read_node_sequence();
	if (n == NULL)
		errexit("empty node sequence: `(' not followed by `;'");
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

/* may return NULL, namely when no ; follows */
static struct gametree *
read_baretree_sequence(void) {
	struct gametree *gh, *g;
	int c;

	c = mygetsym();
	peekc = c;
	if (c != ';')
		return NULL;

	gh = read_baretree();
	c = mygetsym();
	peekc = c;
	if (c == ';') {
		static int nonff4 = 0;
		if (!nonff4++)
			warn("non-FF[4] variations");
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
			if (peekc == ')')
				warn("according to FF[4], () should be (;)");
			else
				errexit("( not followed by ;");
		} else if (gh) {
			gt->nextsibling = g;
			gt = g;
		} else {
			gh = gt = g;
		}
		if ((c = mygetsym()) != ')')
			errexit("gametree does not end with ')' - got '%c'",
				c);
	}
	peekc = c;	/* used by caller */
	return gh;
}

/* <collection> :: <gametree> <gametree>* */
/* non-NULL */
static struct gametree *
read_collection() {
	struct gametree *g;

	g = read_gametree_sequence();
	if (g == NULL) {
		if (peekc == 0)		/* eof */
			errexit(
"a collection should contain at least one gametree");
		errexit(
"a gametree must start with '(' - found '%c'", peekc);
	}
	return g;
}

/* sometimes a file starts with BOM = U+FEFF, i.e., ef bb bf */
static void
check_for_BOM() {
	int c;

	c = mygetchar();
	if (c == 0xef) {
		c = mygetchar();
		if (c == 0xbb) {
			c = mygetchar();
			if (c == 0xbf) {
				warn("file starts with BOM");
				return;
			}
			goto junk;
		}
		goto junk;
	}
	peekc = c;
	return;

junk:
	errexit("leading junk at start of file - expected '('");
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
	mywarnct = 0;

	check_for_BOM();

	*gg = read_collection();

	linenr = 0;	/* avoid error messages with linenr now */
	return 0;
}
