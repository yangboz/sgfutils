/*
 * Compare two sgf files - aeb, 2014-10-02
 * Call:
 *   sgfcmp f1 f2
 * or
 *   sgfcmp f1 < f2
 * Output differences in move sequence after removing variations.
 *
 * Options:
 * -1: single-line output (instead of 1 difference per line)
 * -a: output all differences (instead of some default maximum, like 10)
 * -m#: set maximum number of differences to report
 * -q: simple output, only the first difference
 * -s: simple output (don't test for board rotation, insertions, etc.)
 * -sz#: set board size
 * -A: output moves like B14 instead of bf
 * --: end of option list (useful if a filename starts with -)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "errexit.h"
#include "xmalloc.h"
#include "readsgf.h"

/* diff algorithm comparing two strings without repetitions */
/* type is 1: only G1, 2: only G2, 3: both */
#define MAXCHUNKS 20
struct dif {
	int off1, off2, len, type;
} difs[MAXCHUNKS];

/*
 * return a chunkct in the global difs[]
 * return -1 if more than MAXCHUNKS chunks
 */

/* diff algorithm comparing two games */
static int getdifs(int len1, int *game1, int len2, int *game2) {
	int cct, i, e, max, *index1, *index2, ii, jj, i0, j0, j, h;

	/* how long should our arrays be? probably 361 is the right length */
	max = -1;
	for (i=0; i<len1; i++) {
		e = game1[i];
		if (e < 0)
			errexit("getdifs with negative array entries");
		if (e > max)
			max = e;
	}
	for (i=0; i<len2; i++) {
		e = game2[i];
		if (e < 0)
			errexit("getdifs with negative array entries");
		if (e > max)
			max = e;
	}
	max++;
	index1 = xmalloc(max * sizeof(int));
	index2 = xmalloc(max * sizeof(int));

	/* set to undefined, and fill with unique move */
#define NONE	(-1)
#define MORE	(-2)
	for (i=0; i<max; i++)
		index1[i] = index2[i] = NONE;
	for (i=0; i<len1; i++) {
		e = game1[i];
		if (index1[e] == NONE)
			index1[e] = i;
		else
			index1[e] = MORE;
	}
	for (i=0; i<len2; i++) {
		e = game2[i];
		if (index2[e] == NONE)
			index2[e] = i;
		else
			index2[e] = MORE;
	}

	/* now we know which moves occur only once, and how they
	   correspond; find maximal common chunks */
	cct = 0;
	i = 0;
	while (i < len1) {
		for (ii = i; ii<len1; ii++) {
			e = game1[ii];
			if (index1[e] >= 0 && index2[e] >= 0)
				break;
		}
		if (ii == len1) {
			/* no match in i..len1 */
			if (cct == MAXCHUNKS)
				return -1;
			difs[cct].off1 = i;
			difs[cct].off2 = -1;
			difs[cct].len = len1-i;
			difs[cct].type = 1;
			cct++;
			break;
		}

		jj = index2[e];
		i0 = ii;
		j0 = jj;
		while (i0 > i && j0 > 0 &&
		       game1[i0-1] == game2[j0-1])
			i0--, j0--;
		while (ii+1 < len1 && jj+1 < len2 &&
		       game1[ii+1] == game2[jj+1])
			ii++, jj++;

		/* the part i..(i0-1) if any, was not matched */
		if (i < i0) {
			if (cct == MAXCHUNKS)
				return -1;
			difs[cct].off1 = i;
			difs[cct].off2 = -1;
			difs[cct].len = i0-i;
			difs[cct].type = 1;
			cct++;
		}

		/* found a common chunk i0..ii, j0..jj */
		if (cct == MAXCHUNKS)
			return -1;
		difs[cct].off1 = i0;
		difs[cct].off2 = j0;
		difs[cct].len = ii-i0+1;
		difs[cct].type = 3;
		cct++;

		i = ii+1;
	}

	j = 0;
	while (j < len2) {
		jj = len2;
		for (h=0; h<cct; h++) {
			if (difs[h].type == 3 &&
			    difs[h].off2 <= j &&
			    difs[h].off2 + difs[h].len > j) {
				j = difs[h].off2 + difs[h].len;
				goto nextj;
			}
			if (difs[h].type == 3 &&
			    difs[h].off2 > j &&
			    difs[h].off2 < jj)
				jj = difs[h].off2;
		}

		if (cct == MAXCHUNKS)
			return -1;
		difs[cct].type = 2;
		difs[cct].off1 = -1;
		difs[cct].off2 = j;
		difs[cct].len = jj-j;
		cct++;
		j = jj;
	nextj:;
	}

	return cct;
}


/* do not advise a transformation when the result differs in more places */
int maxtradifs;

/* do not report more than maxdifs move differences */
#define MAXDIFS 12
int maxdifs = MAXDIFS;
int opt1line = 0;
int simple = 0;
int quiet = 0;
int optA = 0;
int align = 1;

#define HIGHBIT	0x10000
#define SZ	19
#define SZ2	(SZ*SZ)

int boardsize = SZ;

/* perform one of 8 transformations; coords in 0..(size-1) */
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

/* perform one of 8 transformations; coords in 2-letter format */
static void transform(int *xx, int *yy, int tra) {
        int x = *xx, y = *yy;
        int sz = boardsize-1;

        if (x == '?' && y == '?')       /* possibly from Dyer sig */
                return;                 /* nothing to rotate */
        if (x == 't' && y == 't')       /* pass */
                return;
        if (x == 'z' && y == 'z')       /* tenuki? */
                return;
        x -= 'a';
        y -= 'a';

        if (x == sz+1 && y == sz+1)             /* pass */
                return;
        if (x < 0 || x > sz || y < 0 || y > sz)
                errexit("off-board move %c%c", x+'a', y+'a');

        transform0(&x, &y, tra, sz+1);

        *xx = x + 'a';
        *yy = y + 'a';
}

static void gettramoves(int mvct, int *moves, int *tramoves, int tra) {
        int i, n, x, y;

	for (i=0; i<mvct; i++) {
                n = moves[i];
                x = ((n>>8) & 0xff);
                y = (n & 0xff);
		transform(&x,&y,tra);
		tramoves[i] = (n & HIGHBIT) + (x<<8) + y;
	}
}

static inline int mv_to_int(int m) {
	int x, y, n;

	x = ((m >> 8) & 0xff);
	y = (m & 0xff);
	x -= 'a';
	y -= 'a';
	n = (x < 0 || x >= SZ || y < 0 || y >= SZ) ? SZ2 : x*SZ+y;
	return n;
}

/* renormalize a move array */
static int *moves_to_ints(int n, int *moves) {
	int i, *mm;

	mm = xmalloc(n * sizeof(int));
	for (i=0; i<n; i++)
		mm[i] = mv_to_int(moves[i]);
	return mm;
}

/* compute frequency count of moves in a table of length SZ2+1 */
/* count both B and W for 1, i.e., ignore color */

static void makefinal(int *final, int *moves, int mvct) {
	int i, m, n;

	for (i=0; i<=SZ2; i++)
		final[i] = 0;
	for (i=0; i<mvct; i++) {
		m = moves[i];
		n = mv_to_int(m);
		final[n]++;
	}
}

static int cmpfinal(int *final1, int *final2) {
	int i, ct, d;

	ct = 0;
	for (i=0; i<=SZ2; i++) {
		d = final1[i] - final2[i];
		if (d < 0)
			d = -d;
		ct += d;
	}
	return ct;
}

/* store the list of differences */
#define DIFFSZ 1000
struct diff {
	int mv, m1, m2;
} diffs[DIFFSZ];
int diffct;

static void
add_diff(int mvnr, int m1, int m2) {
	if (diffct == DIFFSZ)
		errexit("too many differences");
	diffs[diffct].mv = mvnr + 1;
	diffs[diffct].m1 = m1;
	diffs[diffct].m2 = m2;
	diffct++;
}

static void
outmv(int m) {
	int x, y;

	if (m == -1)
		printf("--");
	else {
		x = ((m >> 8) & 0xff);
		y = (m & 0xff);
		if (optA) {
			x += 'A'-'a';
			if (x >= 'I')
				x++;
			y = boardsize - (y - 'a');
			printf("%c%d", x, y);
		} else
			printf("%c%c", x, y);
	}
}

static void
outfullmv(int m) {
	int x, y;

	if (m == -1)
		printf("pass");
	else {
		x = ((m >> 8) & 0xff);
		y = (m & 0xff);
		printf("%c[%c%c]", (m & HIGHBIT) ? 'W' : 'B', x, y);
	}
}

static char *plur(int n) {
	return (n == 1) ? "" : "s";
}

static void
outdiffs() {
	int i, ub, low, high;

	ub = diffct;
	if (ub > maxdifs)
		ub = maxdifs;
	if (ub == 0)
		return;

	if (opt1line) {
		printf("move%s ", plur(ub));
		i = 0;
		while (i < ub) {
			if (i)
				printf(",");
			low = high = diffs[i++].mv;
			while (i < ub && diffs[i].mv == high+1)
				high = diffs[i++].mv;
			printf("%d", low);
			if (high != low)
				printf("-%d", high);
		}
		if (ub < diffct && !quiet)
			printf(",...");
		printf(" : ");

		for (i=0; i<ub; i++) {
			if (i)
				printf(",");
			outmv(diffs[i].m1);
		}
		printf(" vs ");
		for (i=0; i<ub; i++) {
			if (i)
				printf(",");
			outmv(diffs[i].m2);
		}
		printf("\n");
		return;
	}

	for (i=0; i<ub; i++) {
		printf("#%d: ", diffs[i].mv);
		if (align && diffs[i].mv < 100)
			printf(" ");
		if (align && diffs[i].mv < 10)
			printf(" ");
		outmv(diffs[i].m1);
		printf(" ");
		outmv(diffs[i].m2);
		printf("\n");
	}
	if (ub < diffct && !quiet)
		printf("...\n");
}

static void outinterval(int a, int b) {
	printf("%d", a);
	if (a != b)
		printf("-%d", b);
}

static void outmvinterval(int a, int b, int *moves) {
	int i;

	for (i=a; i<=b; i++) {
		if (i > a)
			printf(",");
		outmv(moves[i-1]);
	}
}

static void outchunk(struct dif *dp, int *moves1, int *moves2) {
	if (dp->type == 3) {
		printf("common: move%s ", plur(dp->len));
		outinterval(dp->off1 + 1, dp->off1 + dp->len);
		if (dp->off1 != dp->off2) {
			printf(" / ");
			outinterval(dp->off2 + 1, dp->off2 + dp->len);
		}
		if (dp->len <= maxdifs) {
			printf(": ");
			outmvinterval(dp->off1+1, dp->off1 + dp->len, moves1);
		} else if (dp->off1 != dp->off2) {
			printf(": ");
			outmvinterval(dp->off1+1, dp->off1 + maxdifs, moves1);
			printf(",...");
		}
		printf("\n");
	} else if (dp->type == 1) {
		printf("game 1: move%s ", plur(dp->len));
		outinterval(dp->off1 + 1, dp->off1 + dp->len);
		printf(": ");
		outmvinterval(dp->off1 + 1, dp->off1 + dp->len, moves1);
		printf("\n");
	} else if (dp->type == 2) {
		printf("game 2: move%s ", plur(dp->len));
		outinterval(dp->off2 + 1, dp->off2 + dp->len);
		printf(": ");
		outmvinterval(dp->off2 + 1, dp->off2 + dp->len, moves2);
		printf("\n");
	} else
		errexit("strange chunk");
}

static void sortchunks(int cct) {
	int h, i, j;
	struct dif tmp;

	for (j=0; j<cct; j++) if (difs[j].type == 2) {
		/* maybe move up? */
		i = j;
		while (i > 0 && (difs[i-1].type == 1 ||
			(difs[i-1].type == 3 &&
			 difs[i-1].off2 > difs[j].off2)))
			i--;
		if (i != j) {
			tmp = difs[j];
			for (h = j; h > i; h--)
				difs[h] = difs[h-1];
			difs[i] = tmp;
		}
	}
}

static void outchunks(int cct, int *moves1, int *moves2) {
	int i;

	if (cct < -1) {
		printf("many chunks of differences\n");
		return;
	}

	/* output a type-2 chunk as soon as all earlier moves
	   have been covered */
	sortchunks(cct);

	for (i=0; i<cct; i++)
		outchunk(difs+i, moves1, moves2);
}

static int
is_move(struct property *p) {
	return (p && p->val->next == NULL &&
		(!strcmp(p->id, "B") || !strcmp(p->id, "W")));
}

static int
number_of_games(struct gametree *g) {
	int n = 0;

	while (g) {
		g = g->nextsibling;
		n++;
	}
	return n;
}

/*
 * Remove variations, to get a straight-line game
 * old:  <gametree> :: "(" <sequence> <gametree>* ")"
 * new:  <gametree> :: <sequence>
 */
static void
flatten(char *fn, struct gametree *g) {
	struct gametree *gh;
	struct node *node, node0;

	gh = g;
	if (g->firstchild)
		fprintf(stderr, "warning: %s flattened\n", fn);

	node0.next = 0;
	node = &node0;
	while (gh) {
		node->next = gh->nodesequence;
		while (node->next)
			node = node->next;
		gh = gh->firstchild;
	}
	g->firstchild = g->nextsibling = 0;
}

static void
set_size(int *sz, struct gametree *g) {
	struct node *node;
	struct property *p;

	node = g->nodesequence;
	while (node) {
		p = node->p;
		while (p) {
			if (p->id && !strcmp(p->id, "SZ") &&
			    p->val && p->val->val) {
				*sz = atoi(p->val->val);
				return;
			}
			p = p->next;
		}
		node = node->next;
	}
}

static void
remove_nonmoves(struct gametree *g) {
	struct node *node;
	struct property *p, **pp;

	node = g->nodesequence;
	while (node) {
		pp = &(node->p);
		p = *pp;
		while (p) {
			if (!is_move(p)) {
				p = *pp = p->next;
				continue;
			}
			pp = &(p->next);
			p = *pp;
		}
		node = node->next;
	}
}

static int
get_length(struct gametree *g) {
	struct node *node;
	struct property *p;
	int ct;

	ct = 0;
	node = g->nodesequence;
	while (node) {
		p = node->p;
		while (p) {
			ct++;
			p = p->next;
		}
		node = node->next;
	}
	return ct;
}

/* mvnr with 0 offset */
static int
color_as_expected(int mvnr, int m) {
	return !(m & HIGHBIT) == !(mvnr & 1);
}

static int
colors_ok(char *fn, int n, int *moves) {
	int i, ha;

	if (n == 0)
		return 1;

	/* in a handicap game W starts */
	ha = color_as_expected(0, moves[0]);

	for (i=0; i<n; i++) {
		if (color_as_expected(i, moves[i]) != ha) {
			printf("%s: unexpected color in move %d: ", fn, i+1);
			outfullmv(moves[i]);
			printf("\n");
			return 0;
		}
	}
	return 1;
}

static int
getmove(struct property *p) {
	char *m = p->val->val;
	int highbit;

	if (*m == 0)
		m = "tt";	/* %% PASS */
	else if (strlen(m) != 2)
		errexit("move %s does not have length 2", m);
	highbit = ((p->id[0] == 'W') ? HIGHBIT : 0);
		
	return (highbit + (m[0] << 8) + m[1]);
}

static void
getmoves(struct gametree *g, int mvct, int *moves) {
	struct node *node;
	struct property *p;
	int i;

	i = 0;
	node = g->nodesequence;
	while (node) {
		p = node->p;
		while (p) {
			moves[i++] = getmove(p);
			p = p->next;
		}
		node = node->next;
	}
	if (i != mvct)
		errexit("getmoves bug");
}

static void
prepare_cmp(char *fn, struct gametree **g, int *sz) {
	int n;

	readsgf(fn, g);

	n = number_of_games(*g);
	if (n != 1)
		errexit("%s has multiple games - first split [sgf -x]", fn);

	flatten(fn, *g);
	set_size(sz, *g);
	remove_nonmoves(*g);
}

char *traopts[8] = {
	0,
	" -vflip",
	" -rot90",
	" -bflip",
	"",
	" -hflip",
	" -rot270",
	" -dflip"
};

static int
find_tra(char *fn1, char *fn2, int n1, int n2, int *moves1, int *moves2) {
	int n, tra, i, d, min0, min, mintra, *moves, commonstart;
	int final1[SZ2+1], final2[SZ2+1], final[SZ2+1];

	moves = xmalloc(n2 * sizeof(int));

	makefinal(final1, moves1, n1);
	makefinal(final2, moves2, n2);
	mintra = 0;
	min0 = min = cmpfinal(final1, final2);

	if (min0 == 0)
		return 0;		/* identical */

	for (tra = 1; tra < 8; tra++) {
		gettramoves(n2, moves2, moves, tra);
		makefinal(final, moves, n2);
		d = cmpfinal(final1, final);
		if (d < min) {
			min = d;
			mintra = tra;
		}
	}

	if (min == min0)
		return 0;		/* ok without transformation */

	if (min <= maxtradifs) {
		gettramoves(n2, moves2, moves, mintra);
		for (i=0; i<n2; i++)
			moves2[i] = moves[i];
		if (strlen(fn1) + strlen(fn2) < 40)
			printf("comparing  %s  with the result of "
			       "'sgftf%s < %s':\n",
			       fn1, traopts[mintra], fn2);
		else
			printf("comparing\n  %s\nwith the result of\n  "
			       "'sgftf%s < %s'\n: ",
			       fn1, traopts[mintra], fn2);
		return 0;
	}


	/* if min is big, also try with a truncated game */
	if (n1 != n2) {
		n = ((n1 < n2) ? n1 : n2);
		if (n != n1)
			makefinal(final1, moves1, n);
		if (n != n2)
			makefinal(final2, moves2, n);
		mintra = 0;
		min = cmpfinal(final1, final2);
		if (min <= maxtradifs)
			return 0;	/* truncation ok */

		for (tra = 1; tra < 8; tra++) {
			gettramoves(n, moves2, moves, tra);
			makefinal(final, moves, n);
			d = cmpfinal(final1, final);
			if (d < min) {
				min = d;
				mintra = tra;
			}
		}

		if (min <= maxtradifs) {
			gettramoves(n2, moves2, moves, mintra);
			for (i=0; i<n2; i++)
				moves2[i] = moves[i];
			printf("comparing  %s  with the result of "
			       "'sgftf%s < %s':\n",
			       fn1, traopts[mintra], fn2);
			return 0;
		}
	}

	for (i = 0; i<n1 && i<n2; i++)
		if (moves1[i] != moves2[i])
			break;
	commonstart = i;

	if (commonstart >= 10)
		printf("different games starting with the same %d moves\n",
			commonstart);
	else
		printf("different games\n");
	return 1;
}

static void
cmpsgf(char *fn1, char *fn2, struct gametree *g1, struct gametree *g2) {
	int n, n1, n2, *moves1, *moves2, *vals1, *vals2;
	int i, ct, diffct0;

	n1 = get_length(g1);
	n2 = get_length(g2);
	n = ((n1 < n2) ? n1 : n2);

	moves1 = xmalloc(n1 * sizeof(int));
	moves2 = xmalloc(n2 * sizeof(int));

	getmoves(g1, n1, moves1);
	getmoves(g2, n2, moves2);

	if (simple) {
		for (i=0; i<n1 && i<n2; i++)
			if (moves1[i] != moves2[i])
				add_diff(i, moves1[i], moves2[i]);
		for (; i<n1; i++)
			add_diff(i, moves1[i], -1);
		for (; i<n2; i++)
			add_diff(i, -1, moves2[i]);

		outdiffs();
		return;
	}

	/* find the correct tra, examining entire games */
	if (find_tra(fn1, fn2, n1, n2, moves1, moves2))
		return;		/* very different */

	/* look at differences, where different color is a difference */
	diffct = 0;
	for (i=0; i<n; i++)
		if (moves1[i] != moves2[i])
			add_diff(i, moves1[i], moves2[i]);
	diffct0 = diffct;
	for (; i<n1; i++)
		add_diff(i, moves1[i], -1);
	for (; i<n2; i++)
		add_diff(i, -1, moves2[i]);

	if (diffct == 0) {
		printf("identical\n");
		return;
	}

	if (diffct0 == 0) {
		printf("files have %d and %d moves", n1, n2);
		printf(opt1line ? "; " : "\n");
		printf("identical truncations to %d moves\n", n);
		return;
	}

	if (diffct <= maxdifs) {
		outdiffs();
		return;
	}

	if (diffct0 <= maxdifs) {
		printf("files have %d and %d moves", n1, n2);
		printf(opt1line ? "; " : "\n");
		printf("after truncation to %d moves, the differences are",
		       n);
		printf(opt1line ? ": " : "\n");
		diffct = diffct0;
		outdiffs();
		return;
	}

	/* mistake in color letters? */
	if (!colors_ok(fn1, n1, moves1) + !colors_ok(fn2, n2, moves2)) {
		printf("non-alternating colors\n");
		return;
		/* maybe no error just before end-of-game */
	}

#if 0
	/* idem, but ignore the color */
	diffct = 0;
	for (i=0; i<n; i++)
		if ((moves1[i] != moves2[i]) & ~HIGHBIT)
			add_diff(i, moves1[i], moves2[i]);
	diffct0 = diffct;
	for (; i<n1; i++)
		add_diff(i, moves1[i], -1);
	for (; i<n2; i++)
		add_diff(i, -1, moves2[i]);

	if (diffct == 0) {
		printf("identical, apart from the colors\n");
		return;
	}

	if (diffct0 == 0) {
		printf("files have %d and %d moves\n", n1, n2);
		printf("identical truncations to %d moves, "
		       "apart from the colors\n", n);
		return;
	}

	if (diffct <= maxdifs) {
		printf("apart from the colors, the differences are\n");
		outdiffs();
		return;
	}

	if (diffct0 <= maxdifs) {
		printf("files have %d and %d moves\n", n1, n2);
		printf("after truncation to %d moves, "
		       "and apart from the colors, the differences are\n",
		       n);
		diffct = diffct0;
		outdiffs();
		return;
	}
#endif
	/* here many diffs but similar final position,
	   perhaps due to a shift in the moves */

	vals1 = moves_to_ints(n1, moves1);
	vals2 = moves_to_ints(n2, moves2);
	ct = getdifs(n1, vals1, n2, vals2);
	if (ct == -1)
		printf("many diffs (first at move %d) - "
		       "use -a option to see all\n", diffs[0].mv);
	else
		outchunks(ct, moves1, moves2);
}

int
main(int argc, char **argv){
	struct gametree *g1, *g2;
	char *fn1, *fn2, *p;
	int sz1, sz2;

	progname = "sgfcmp";

	while (argc > 1 && argv[1][0] == '-' && argv[1][1]) {
		if (!strncmp(argv[1], "-m", 2)) {
			maxdifs = atoi(argv[1]+2);
			goto nxt;
		}
		if (!strncmp(argv[1], "-sz", 3)) {
			boardsize = atoi(argv[1]+3);
			if (boardsize <= 0)
				errexit("bad size");
			goto nxt;
		}
		if (!strcmp(argv[1], "--"))
			break;

		/* check for condensed single-letter options like -A1 */
		p = argv[1]+1;
		while (*p) {
			switch(*p++) {
			case '1':
				opt1line = 1;
				continue;
			case 'A':
				optA = 1;
				align = 0;
				continue;
			case 'a':
				maxdifs = DIFFSZ;	/* "all" */
				continue;
			case 'q':
				quiet = 1;
				simple = 1;
				maxdifs = 1;
				continue;
			case 's':
				simple = 1;		/* simple, straight */
				continue;
			case 't':
				tracein = 1;
				continue;
			default:
				errexit("unknown option '%s'", argv[1]);
			}
		}
	nxt:
		argc--; argv++;
	}

	if (argc < 2 || argc > 3)
		errexit("Call: sgfcmp [options] f1 f2");

	maxtradifs = 2*maxdifs;

	fn1 = argv[1];
	fn2 = ((argc > 2) ? argv[2] : "-");

	sz1 = sz2 = 0;
	prepare_cmp(fn1, &g1, &sz1);
	prepare_cmp(fn2, &g2, &sz2);

	if (sz1 && sz2 && sz1 != sz2) {
		printf("board sizes differ: %d vs %d\n", sz1, sz2);
		return 0;
	}
	if (sz1)
		boardsize = sz1;
	if (sz2)
		boardsize = sz2;
	if ((sz1 && !sz2 && sz1 != SZ) ||
	    (sz2 && !sz1 && sz2 != SZ))
		printf("warning: board sizes may differ, assuming %d\n",
			boardsize);
	
	cmpsgf(fn1, fn2, g1, g2);

	return 0;
}
