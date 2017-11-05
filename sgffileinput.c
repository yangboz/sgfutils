/* read sgf files */
#include <string.h>
#include <stdlib.h>

#include "errexit.h"
#include "xmalloc.h"
#include "readsgf.h"
#include "sgfinfo.h"
#include "sgffileinput.h"
#include "tests.h"

#define PASS (('t'<<8) | 't')
#define BLACK_MASK	0x10000
#define WHITE_MASK	0x20000
#define MAXMOVES	10000

int replacenl = 0;	/* in propvalues: replace \n by space, remove \r */
int optN = 0;		/* report number of games in this file */

struct gametree *gametree;
struct node *rootnode;
static int gtlevel;		/* nesting depth of parens */
static int skipping;		/* true if not the main game line */

static inline int
is_upper_case(int c) {
	return ('A' <= c && c <= 'Z');
}

static void
copy_without_nl(char *s, char *t) {
	while (*t) {
		if (*t == '\n' || *t == '\r') {
			*s++ = ' ';
			t++;
		} else
			*s++ = *t++;
	}
	*s = 0;
}

/* similar to next, but do not skip moves */
static int
get_props_in_rootnode(char *buf, int len) {
	struct property *p;
	int ct = 0;
	char *q;

	p = rootnode->p;

	while (p && len) {
		if (ct++) {
			*buf++ = ' ';
			len--;
		}
		q = p->id;
		while (len && *q) {
			*buf++ = *q++;
			len--;
		}
		p = p->next;
	}

	if (!len)
		return 1;
	*buf = 0;
	return 0;
}

static void
get_nonmove_props_n(char **abuf, int *alen, int *act, struct node *node) {
	struct property *p;
	char *buf, *q;
	int len, ct;

	buf = *abuf;
	len = *alen;
	ct = *act;

	while (node) {
		p = node->p;
		while (p && len) {
			q = p->id;
			/* might also skip "Black" and "White" */
			if ((q[0] == 'B' || q[0] == 'W') && q[1] == 0)
				goto next;
			if (ct++) {
				*buf++ = ' ';
				len--;
			}
			while (len && *q) {
				*buf++ = *q++;
				len--;
			}
		next:
			p = p->next;
		}
		node = node->next;
	}
	*abuf = buf;
	*alen = len;
	*act = ct;
}

static void
get_nonmove_props_g(char **abuf, int *alen, int *act, struct gametree *g) {
	if (g == NULL)
		return;
	get_nonmove_props_n(abuf, alen, act, g->nodesequence);
	get_nonmove_props_g(abuf, alen, act, g->firstchild);
	get_nonmove_props_g(abuf, alen, act, g->nextsibling);
}

/* on toplevel: do not go to nextsibling, that is another game */
static void
get_nonmove_props_g0(char **abuf, int *alen, int *act, struct gametree *g) {
	if (g == NULL)
		return;
	get_nonmove_props_n(abuf, alen, act, g->nodesequence);
	get_nonmove_props_g(abuf, alen, act, g->firstchild);
}

/* return 1 on overflow */

static int get_nonmove_props(char *buf, int len) {
	int ct = 0;
	get_nonmove_props_g0(&buf, &len, &ct, gametree);
	if (len)
		*buf = 0;
	return (len == 0);
}

static int get_props_in_nonroot(char *buf, int len) {
	int ct = 0;

	get_nonmove_props_n(&buf, &len, &ct, rootnode->next);
	get_nonmove_props_g(&buf, &len, &ct, gametree->firstchild);
	if (len)
		*buf = 0;
	return (len == 0);
}

/*
 * For all properties with more than a single field, output XY-m
 * where m is the number of fields.
 */
static void
get_multiprops_n(char **abuf, int *alen, int *act, struct node *node) {
	struct property *p;
	struct propvalue *pv;
	char *buf, *q;
	int len, ct, mct, ret;

	buf = *abuf;
	len = *alen;
	ct = *act;

	while (node) {
		p = node->p;
		while (p && len) {
			pv = p->val;
			mct = 0;
			while (pv) {
				pv = pv->next;
				mct++;
			}
			if (mct == 1)
				goto next;

			q = p->id;
			if (ct++) {
				*buf++ = ' ';
				len--;
			}
			while (len && *q) {
				*buf++ = *q++;
				len--;
			}
			ret = snprintf(buf, len, "-%d", mct);
			buf += ret;
			len -= ret;
			if (len <= 0) {
				len = 0;
				break;
			}
		next:
			p = p->next;
		}
		node = node->next;
	}
	*abuf = buf;
	*alen = len;
	*act = ct;
}

static void
get_multiprops_g(char **abuf, int *alen, int *act, struct gametree *g) {
	if (g == NULL)
		return;
	get_multiprops_n(abuf, alen, act, g->nodesequence);
	get_multiprops_g(abuf, alen, act, g->firstchild);
	get_multiprops_g(abuf, alen, act, g->nextsibling);
}

static void
get_multiprops_g0(char **abuf, int *alen, int *act, struct gametree *g) {
	if (g == NULL)
		return;
	get_multiprops_n(abuf, alen, act, g->nodesequence);
	get_multiprops_g(abuf, alen, act, g->firstchild);
}

static int
get_multiprops(char *buf, int len) {
	int ct = 0;

	get_multiprops_g0(&buf, &len, &ct, gametree);
	if (len)
		*buf = 0;
	return (len == 0);
}

/* get the value of the 1st occurrence of XY */
static int
get_propXY_n(char *XY, char *buf, int len, struct node *node) {
	struct property *p;

	while (node) {
		p = node->p;
		while (p) {
			if (!strcmp(p->id, XY)) {
				if (!p->val || !p->val->val)
					continue;	/* impossible? */
				if (strlen(p->val->val) >= len)
					return 1;	/* overflow */
				if (!replacenl)
					strcpy(buf, p->val->val);
				else
					copy_without_nl(buf, p->val->val);
				return 0;
			}
			p = p->next;
		}
		node = node->next;
	}
	return -1;	/* not present */
}

static int
get_propXY_g(char *XY, char *buf, int len, struct gametree *g) {
	int r;

	if (g == NULL)
		return -1;
	r = get_propXY_n(XY, buf, len, g->nodesequence);
	if (r < 0)
		r = get_propXY_g(XY, buf, len, g->firstchild);
	if (r < 0)
		r = get_propXY_g(XY, buf, len, g->nextsibling);
	return r;
}

static int
get_propXY_g0(char *XY, char *buf, int len, struct gametree *g) {
	int r;

	if (g == NULL)
		return -1;
	r = get_propXY_n(XY, buf, len, g->nodesequence);
	if (r < 0)
		r = get_propXY_g(XY, buf, len, g->firstchild);
	return r;
}

/* get the value of the 1st occurrence of XY */
static int get_propXY(char *XY, char *buf, int len) {
	return get_propXY_g0(XY, buf, len, gametree);
}

static int get_rpropXY(char *XY, char *buf, int len) {
	struct property *p = rootnode->p;

	while (p) {
		if (!strcmp(p->id, XY)) {
			if (!p->val || !p->val->val)
				continue;	/* impossible? */
			if (strlen(p->val->val) >= len)
				return 1;	/* overflow */
			if (!replacenl)
				strcpy(buf, p->val->val);
			else
				copy_without_nl(buf, p->val->val);
			return 0;
		}
		p = p->next;
	}
	return -1;	/* not present */
}

static int get_nrpropXY(char *XY, char *buf, int len) {
	int r;

	r = get_propXY_n(XY, buf, len, rootnode->next);
	if (r < 0)
		r = get_propXY_g(XY, buf, len, gametree->firstchild);
	return r;
}

static int
get_mpropXY_n(char *XY, char *buf, int len, struct node *node) {
	struct property *p;
	struct propvalue *pv;
	int ret;

	while (node) {
		p = node->p;
		while (p) {
			if (!strcmp(p->id, XY)) {
				ret = snprintf(buf, len, "%s", p->id);
				buf += ret;
				len -= ret;
				if (len <= 0)
					return 1;	/* overflow */
				pv = p->val;
				while (pv) {
					ret = snprintf(buf, len, "[%s]",
						       pv->val);
					buf += ret;
					len -= ret;
					if (len <= 0)
						return 1;
					pv = pv->next;
				}
				return 0;
			}
			p = p->next;
		}
		node = node->next;
	}
	return -1;	/* not present */
}

static int
get_mpropXY_g(char *XY, char *buf, int len, struct gametree *g) {
	int r;

	if (g == NULL)
		return -1;
	r = get_mpropXY_n(XY, buf, len, g->nodesequence);
	if (r < 0)
		r = get_mpropXY_g(XY, buf, len, g->firstchild);
	if (r < 0)
		r = get_mpropXY_g(XY, buf, len, g->nextsibling);
	return r;
}

static int
get_mpropXY_g0(char *XY, char *buf, int len, struct gametree *g) {
	int r;

	if (g == NULL)
		return -1;
	r = get_mpropXY_n(XY, buf, len, g->nodesequence);
	if (r < 0)
		r = get_mpropXY_g(XY, buf, len, g->firstchild);
	return r;
}

static int get_mpropXY(char *XY, char *buf, int len) {
	return get_mpropXY_g0(XY, buf, len, gametree);
}

int get_player(char *buf, int len) {
	int r;
	char *p;

	r = get_propXY("PB", buf, len);
	if (r)
		return r;	/* >0: overflow, <0: not present */
	p = buf;
	while (*p)
		p++;
	if (len < p-buf+2)
		return 1;
	*p++ = ',';
	return get_propXY("PW", p, len - (p-buf));
}

int get_winner(char *buf, int len) {
	int r;

	r = get_propXY("RE", buf, len);
	if (r)
		return r;	/* >0: overflow, <0: not present */
	if (!strncmp(buf, "B+", 2))
		return get_propXY("PB", buf, len);
	if (!strncmp(buf, "W+", 2))
		return get_propXY("PW", buf, len);
	/* jigo, unfinished, RE string not understood */
	return -1;
}

int get_loser(char *buf, int len) {
	int r;

	r = get_propXY("RE", buf, len);
	if (r)
		return r;	/* >0: overflow, <0: not present */
	if (!strncmp(buf, "B+", 2))
		return get_propXY("PW", buf, len);
	if (!strncmp(buf, "W+", 2))
		return get_propXY("PB", buf, len);
	/* jigo, unfinished, RE string not understood */
	return -1;
}

/*
 * for the properties given in the root node:
 * -prop : output all property IDs (but not the values)
 * -propXY : output the value of property XY
 * -propXY=foo : set selection condition XY[foo]
 * -propXY:foo : set selection condition XY[..foo..]
 * (in particular, for the empty string: -propXY: : property XY is present)
 *
 * ! before = or : or NUL indicates negation.
 *
 * The stringfn interface has int f(char *seed, char *buf, int len)
 * with 0: ok, 1: overflow, -1: no value.
 *
 * We have already seen the "-prop" part.
 */
void setproprequests(int flags, char *s) {
	char *p;

	if (*s == 0) {
		set_string("props: %s\n", s,
			   (flags & ROOT_ONLY) ? get_props_in_rootnode :
			   (flags & NONROOT_ONLY) ? get_props_in_nonroot :
			   (flags & MULTIPROP) ? get_multiprops :
			   get_nonmove_props);
		return;
	}

	p = xstrdup(s);
	while (is_upper_case(*s))
		s++;
	if (*s == '!')
		s++;
	if (*s == 0 || *s == '=' || *s == ':')
		set_stringfn("prop%s=%s\n", p,
			     (flags & ROOT_ONLY) ? get_rpropXY :
			     (flags & NONROOT_ONLY) ? get_nrpropXY :
			     (flags & MULTIPROP) ? get_mpropXY :
			     get_propXY);
	else
		errexit("unrecognized -prop%s option", p);
}

#define MAXSZ		31
#define DEFAULTSZ	19

static void
setsize(struct node *node) {
	struct property *p = node->p;

	while (p) {
		if (!strcmp(p->id, "SZ")) {
			if (!p->val || p->val->next)
				errexit("strange SZ property");
			size = atoi(p->val->val);
			if (size < 0 || size > MAXSZ)
				errexit("SZ[%d] out of bounds", size);
			/* playgogame.c has a smaller limit */
			return;
		}
		p = p->next;
	}
}

static void
put_move1(struct propvalue *pv, int mask) {
	char *s, *t;

	if (!pv)
		errexit("missing move value");
	s = pv->val;	/* something like "dp" or "bp:cp" */

	/* strip leading whitespace */
	while (*s && (*s == ' ' || *s == '\n' || *s == '\r'))
		s++;

	/* strip trailing whitespace */
	t = s;
	while (*t) t++;
	while (--t >= s && (*t == ' ' || *t == '\n' || *t == '\r'))
		*t = 0;

	if (*s == 0) {
		if (mvct == MAXMOVES)
			errexit("too many moves");
		moves[mvct++] = (PASS | mask);
	} else if (s[0] && s[1] && s[2] == 0) {
		if (mvct == MAXMOVES)
			errexit("too many moves");
		moves[mvct++] = ((s[0]<<8) | s[1] | mask);
	} else if (s[0] && s[1] && s[2] == ':' && s[3] && s[4] && s[5] == 0) {
		int r1,c1,r2,c2,i,j;

		r1 = s[0];
		c1 = s[1];
		r2 = s[3];
		c2 = s[4];
		if (r1 > r2 || c1 > c2)
			errexit("unexpected range _%s_", s);
		for (i=r1; i<=r2; i++) for (j=c1; j<=c2; j++) {
			if (mvct == MAXMOVES)
				errexit("too many moves");
			moves[mvct++] = ((i<<8) | j | mask);
		}
	} else if (!strcmp(s, "pass")) {
		/* not really legal.. */
		if (mvct == MAXMOVES)
			errexit("too many moves");
		moves[mvct++] = (PASS | mask);
	} else
		errexit("unexpected move _%s_", s);
}

static void
put_move(struct property *p) {
	int mask;

	if (p->id[0] == 'B')
		mask = BLACK_MASK;
	else if (p->id[0] == 'W')
		mask = WHITE_MASK;
	else
		errexit("non B/W move");

	put_move1(p->val, mask);
	movect++;
}

/* currently called for rootnode only */
static void
get_setup_stones(struct node *node) {
	struct property *p;
	struct propvalue *pv;
	int abct, awct;

	abct = awct = 0;

	p = node->p;
	while (p) {
		if (!strcmp(p->id, "AB")) {
			pv = p->val;
			while (pv) {
				abct++;
				put_move1(pv, BLACK_MASK);
				pv = pv->next;
			}
		} else if (!strcmp(p->id, "AW")) {
			pv = p->val;
			while (pv) {
				awct++;
				put_move1(pv, WHITE_MASK);
				pv = pv->next;
			}
		}
		p = p->next;
	}

	handct = (awct ? 0 : abct);
}

static int compar_int(const void *aa, const void *bb) {
	const int *a = aa;
	const int *b = bb;

	return (*a) - (*b);
}

/* make sure the setup stones are in some well-defined order
   so that md5sums do not depend on the ordering of handicap stones */
static void
sort_initial_stones() {
	qsort(moves, mvct, sizeof(moves[0]), compar_int);
}

static void
get_initial_stones(struct node *node) {
	get_setup_stones(node);

#if 0
	/* kludge: peek into second node - some broken files have AB there */
	if (abct == 0 && node->next)
		get_setup_stones(node->next);
#endif
	initct = mvct;

	sort_initial_stones();
}

static void
init_single_game(struct gametree *g) {
	gamenr++;
	size = DEFAULTSZ;
	movect = mvct = 0;
	initct = 0;
	handct = 0;
	gametree = g;
	rootnode = g->nodesequence;
	setsize(rootnode);
	get_initial_stones(rootnode);
}

static int
is_move(struct property *p) {
	return (p && p->val->next == NULL &&
		(!strcmp(p->id, "B") || !strcmp(p->id, "W")));
}

static void
put_nodesequence(struct node *n) {
	struct property *p;

	while (n) {
		p = n->p;
		while (p) {
			if (is_move(p))
				put_move(p);
			p = p->next;
		}
		n = n->next;
	}
}

static void put_gametree_sequence(struct gametree *g);

static void
put_gametree(struct gametree *g) {
	gtlevel++;
	if (gtlevel == 1 && g->nodesequence)
		init_single_game(g);

	put_nodesequence(g->nodesequence);
	put_gametree_sequence(g->firstchild);
	if (gtlevel == 1)
		report_on_single_game();
	gtlevel--;
	skipping = (gtlevel > 0);	/* here we want mainline moves only */
}

static void
put_gametree_sequence(struct gametree *g) {
	while (g) {
		if (!skipping)
			put_gametree(g);
		g = g->nextsibling;
	}
}

static int
get_number_of_games(struct gametree *g) {
	int n = 0;

	while (g) {
		g = g->nextsibling;
		n++;
	}
	return n;
}

static inline char *plur(int n) {
	return (n == 1) ? "" : "s";
}

void
do_stdin(const char *fn) {
	struct gametree *g;

	if (setjmp(jmpbuf))
		goto ret;
	have_jmpbuf = 1;

	readsgf(fn, &g);
	number_of_games = get_number_of_games(g);

	if (optN) {
		if (argct <= 1)
			printf("%d\n", number_of_games);
		else
			printf("%6d game%s in %s\n",
			       number_of_games, plur(number_of_games),
			       infilename);
		return;
	}

	reportedfn = 0;
	gamenr = gtlevel = skipping = 0;

	put_gametree_sequence(g);
ret:
	have_jmpbuf = 0;
	yfree();
}
