/*
 * sgfx:  give details about a single sgf file
 *
 * This resembles sgfinfo, but there is no selection part
 * since there is a single input only (but -g# can be used
 * to select a game in a multi-game file).
 * The output can be selected per-variation.
 *
 * -g# or -x#: specify game in a multi-game file
 * -v#: specify variation in a tree
 * -flatten: turn the tree into a linear game (the selected variation only)
 * -m#: specify move number
 *
 * -propXY: output the value(s) of property XY
 * -prop: output the properties present for the current move
 * -pm: output the move numbers with additional properties
 *
 * -g, -x: give number of games
 * -v: give number of variations
 * -m: give number of moves in this variation
 * -d: give number of first move not in previous variation
 *
 * -M: print move
 * -replies: print the known replies
 * -s: set separator (default: ", ")
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "errexit.h"
#include "readsgf.h"

struct gamepos {
	struct gametree *g;
	struct node *n;
	int varnr;
	int movect;		/* last move up to and including n (if any) */
};

/* separator between elements of a multiple value */
char *separ = ", ";

static int
is_move(struct property *p) {
        return (p && p->val && p->val->next == NULL &&
                (!strcmp(p->id, "B") || !strcmp(p->id, "W")));
}

static int
has_move(struct node *n) {
	struct property *p;

	for (p = n->p; p; p = p->next)
		if (is_move(p))
			return 1;
	return 0;
}

static void
outmove(struct node *n) {
	struct property *p;

	for (p = n->p; p; p = p->next)
		if (is_move(p))
			printf("%s\n", p->val->val);
}
	
static void
outval(struct propvalue *pv) {
	printf("%s", pv->val);
	while (pv->next) {
		pv = pv->next;
		printf("%s%s", separ, pv->val);
	}
	printf("\n");
}

static void
outprop1(struct node *n, char *propid) {
	struct property *p;

	for (p = n->p; p; p = p->next)
		if (!strcmp(propid, p->id))
			outval(p->val);
}

static void
outprop(struct node *n, char *propid) {
	do {
		outprop1(n, propid);
		n = n->next;
	} while (n && !has_move(n));
}

static void
outprops1(struct node *n) {
	struct property *p;
	int ct = 0;

	for (p = n->p; p; p = p->next) {
		if (ct++)
			putchar(' ');
		printf("%s", p->id);
	}
	printf("\n");
}

static void
outprops(struct node *n) {
	do {
		outprops1(n);
		n = n->next;
	} while (n && !has_move(n));
}

static int get_number_of_moves(struct gametree *g) {
	struct node *n;
	int m = 0;

	while (g) {
		n = g->nodesequence;
		while (n) {
			if (is_move(n->p))
				m++;
			n = n->next;
		}
		g = g->firstchild;
	}
	return m;
}

static struct node *
get_move(struct gametree *g, int movect) {
	struct node *n;
	int m = 0;

	if (movect == 0)
		return g->nodesequence;		/* root node */
	
	while (g) {
		n = g->nodesequence;
		while (n) {
			if (is_move(n->p)) {
				m++;
				if (m == movect)
					return n;
			}
			n = n->next;
		}
		g = g->firstchild;
	}
	errexit("get_move error");
}

static void
outpropmoves(struct gametree *g) {
	struct node *n;
	struct property *p;
	int m = 0, flag, ct;

	ct = 0;
	while (g) {
		n = g->nodesequence;
		while (n) {
			p = n->p;
			flag = 0;
			while (p) {
				if (is_move(p))
					m++;
				else
					flag = 1;
				p = p->next;
			}
			if (flag) {
				if (ct++)
					putchar(' ');
				printf("%d", m);
			}
			n = n->next;
		}
		g = g->firstchild;
	}
	printf("\n");
}
			
static int
get_number_of_variations(struct gametree *g) {
	int nv = 0;

	g = g->firstchild;
	if (!g)
		nv++;
	else while (g) {
		nv += get_number_of_variations(g);
		g = g->nextsibling;
	}
	return nv;
}

/* cut away other branches, so that our variation is the only one left */
static void
select_variation(struct gametree *g, int wanted_varnr) {
	struct gametree *gg;
	int nv;

	if (wanted_varnr == 1)
		return;

	gg = g->firstchild;

	while (1) {
		nv = get_number_of_variations(gg);
		if (nv >= wanted_varnr) {
			g->firstchild = gg;	/* modifies tree */
			select_variation(gg, wanted_varnr);
			return;
		}
		wanted_varnr -= nv;
		gg = gg->nextsibling;
	}
}

static void
do_flatten(struct gametree *g) {
	while (g) {
		g->nextsibling = NULL;
		g = g->firstchild;
	}
}

/* print as many spaces as the printed string would take */
static void
fakeprintf(const char *s, ...) {
	va_list ap;
#define FAKEBUFSZ 1000
	char buf[FAKEBUFSZ];
	int n;

	va_start(ap, s);
	n = vsnprintf(buf, sizeof(buf), s, ap);
	va_end(ap);

	printf("%*.s", n, " ");
}

static void
do_showtree(struct gametree *g) {
	int varnr, m, i;
	struct node *n;
#define MAXDEPTH 1000
	struct gm {
		struct gametree *gg;
		int mv0;
	} gm[MAXDEPTH];
	int depth0, depth;

	varnr = m = depth0 = depth = 0;

	while (g) {
		if (depth >= MAXDEPTH)
			errexit("MAXDEPTH overflow");
		gm[depth].gg = g;
		gm[depth].mv0 = m+1;
		depth++;

		n = g->nodesequence;
		while (n) {
			if (is_move(n->p))
				m++;
			n = n->next;
		}

		g = g->firstchild;
		if (g)
			continue;
		gm[depth].mv0 = m+1;

		varnr++;
		printf("var %d:", varnr);
		for (i=0; i<depth0; i++)
			fakeprintf(" (%d-%d", gm[i].mv0, gm[i+1].mv0 - 1);
		for (i=depth0; i<depth; i++)
			printf(" (%d-%d", gm[i].mv0, gm[i+1].mv0 - 1);

		while (g == NULL && depth > 0) {
			putchar(')');
			depth--;
			m = gm[depth].mv0 - 1;
			g = gm[depth].gg;
			g = g->nextsibling;
		}
		printf("\n");
		depth0 = depth;
	}
}

static void
get_variation(struct gamepos *gp, int wanted_varnr, int wanted_movenr) {
	struct gametree *g, *gg;
	struct node *n;
	int pastvars, m, nv;

	pastvars = m = 0;

	if (wanted_varnr == 1)
		return;

	g = gp->g;

	while (1) {
		n = g->nodesequence;
		while (n) {
			if (is_move(n->p)) {
				m++;
				if (m == wanted_movenr)
					goto out;
			}
			n = n->next;
		}

		gg = g->firstchild;
		if (gg == NULL)
			errexit("NULL too early");
		
		while (1) {
			if (gg == NULL)
				errexit("NULL too early (2)");
			nv = get_number_of_variations(gg);
			if (pastvars+nv >= wanted_varnr)
				break;
			pastvars += nv;
			gg = gg->nextsibling;
		}

		g = gg;

		if (pastvars+1 == wanted_varnr)
			break;
	}
out:
	gp->g = g;
	gp->n = n;
	gp->varnr = pastvars+1;
	gp->movect = m;
}

static int
out_first_move(struct node *n, int *ct) {
	struct property *p;

	while (n) {
		for (p = n->p; p; p = p->next) {
			if (is_move(p)) {
				if ((*ct)++)
					putchar(' ');
				printf("%s", p->val->val);
				return 1;
			}
		}
		n = n->next;
	}
	return 0;
}

static void
outreplies(struct gamepos *gp) {
	struct gametree *g;
	struct node *n;
	int ct = 0;

	g = gp->g;
	n = gp->n;
	if (!n) {
		n = g->nodesequence;
	} else
		n = n->next;
	
	if (out_first_move(n, &ct))
		goto done;

	for (g = g->firstchild; g; g = g->nextsibling) {
		n = g->nodesequence;
		out_first_move(n, &ct);
	}

done:
	printf("\n");
}

/*
 * If yes, then return 1 and point nn at that move.
 * If no, then either because there was a different move (point nn at it),
 * or because there was no move (make nn point at NULL).
 */
static int
first_move_fits(struct node **nn, char *move) {
	struct node *n;
	struct property *p;

	n = *nn;

	while (n) {
		for (p = n->p; p; p = p->next) {
			if (is_move(p)) {
				if (!strncmp(p->val->val, move, 2)) {
					*nn = n;
					return 1;
				}
				return 0;
			}
		}
		n = n->next;
	}

	*nn = NULL;
	return 0;
}
	
static struct node *
aftermove1(struct gamepos *gp, char *move) {
	struct gametree *g;
	struct node *n;
	int varnr;

	g = gp->g;
	n = gp->n;
	if (!n) {
		n = g->nodesequence;
	} else
		n = n->next;
	
	if (first_move_fits(&n, move)) {
		gp->n = n;
		gp->movect++;
		return n;
	}
	if (n)
		return NULL;

	g = g->firstchild;
	if (!g)
		return NULL;

	n = g->nodesequence;
	if (first_move_fits(&n, move)) {
		gp->g = g;
		gp->n = n;
		gp->movect++;
		return n;
	}

	varnr = gp->varnr;
	while (1) {
		varnr += get_number_of_variations(g);
		g = g->nextsibling;
		if (!g)
			break;
		n = g->nodesequence;
		if (first_move_fits(&n, move)) {
			gp->g = g;
			gp->n = n;
			gp->varnr = varnr;
			gp->movect++;
			return n;
		}
	}

	return NULL;
}

static struct node *
aftermoves(struct gamepos *gp, char *moves) {
	struct node *n = gp->n;

	while (*moves) {
		/* skip separator between moves */
		if (index(" ,;:-&", *moves)) {
			moves++;
			continue;
		}
		n = aftermove1(gp, moves);
		if (n == NULL)
			break;
		moves += 2;
	}
	return n;
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

static struct gametree *
get_game(struct gametree *g, int wanted_gamenr) {
	int n = 1;

	while (n < wanted_gamenr) {
		g = g->nextsibling;
		n++;
	}
	return g;
}

static char *plur(int n) {
	return (n==1) ? "" : "s";
}

int main(int argc, char **argv) {
#define MAXPROPX 1000
	int i, inct, ng, nv, nm, pxct;
	char *arg, *infile, *optafter, *optpropx[MAXPROPX];
	struct gametree *g, *gg;
	struct gamepos gp0, gp;
	struct node *n;
	int wanted_gamenr, wanted_varnr, wanted_movenr;
	int out_ng, out_nv, out_nm, out_d, out_M;
	int out_pm, out_props, out_replies, showtree, flatten, dashdash;

	progname = "sgfx";

	/* find input file (or use stdin) */
	inct = 0;
	infile = NULL;			/* stdin */
	wanted_gamenr = 1;		/* default: first game */
	wanted_varnr = 1;		/* default: first variation */
	wanted_movenr = 0;		/* root node */
	out_ng = out_nv = out_nm = out_d = out_M = 0;
	out_pm = out_props = out_replies = showtree = flatten = dashdash = 0;
	pxct = 0;
	optafter = NULL;

	for (i=1; i<argc; i++) {
		arg = argv[i];
		if (*arg != '-' || dashdash) {
			infile = arg;
			inct++;
			continue;
		}
		if (!strcmp(arg, "--")) {
			dashdash = 1;
			continue;
		}
		if (!strcmp(arg, "-after")) {
			if (++i == argc)
				errexit("-after expects an arg");
			optafter = argv[i];
			continue;
		}
		if (!strcmp(arg, "-d")) {
			out_d = 1;
			continue;
		}
		if (!strcmp(arg, "-flatten")) {
			flatten = 1;
			continue;
		}
		if (!strncmp(arg, "-g", 2) || !strncmp(arg, "-x", 2)) {
			if (arg[2] == 0)
				out_ng = 1;
			else
				wanted_gamenr = atoi(arg+2);
			continue;
		}
		if (!strcmp(arg, "-M")) {
			out_M = 1;
			continue;
		}
		if (!strncmp(arg, "-m", 2)) {
			if (arg[2] == 0)
				out_nm = 1;
			else
				wanted_movenr = atoi(arg+2);
			continue;
		}
		if (!strcmp(arg, "-pm")) {
			out_pm = 1;
			continue;
		}
		if (!strcmp(arg, "-prop")) {
			out_props = 1;
			continue;
		}
		if (!strncmp(arg, "-prop", 5)) {
			if (pxct >= MAXPROPX)
				errexit("too many -prop options");
			optpropx[pxct++] = arg+5;
			continue;
		}
		if (!strcmp(arg, "-q")) {
			readquietly = 1;
			continue;
		}
		if (!strcmp(arg, "-replies")) {
			out_replies = 1;
			continue;
		}
		if (!strcmp(arg, "-showtree")) {
			showtree = 1;
			continue;
		}
		if (!strncmp(arg, "-s", 2)) {
			separ = arg+2;
			continue;
		}
		if (!strncmp(arg, "-v", 2)) {
			if (arg[2] == 0)
				out_nv = 1;
			else
				wanted_varnr = atoi(arg+2);
			continue;
		}
		errexit("unrecognized option '%s'", arg);
	}
	if (inct > 1)
		errexit("at most one input file");

	readsgf(infile, &g);

	ng = get_number_of_games(g);
	if (out_ng)
		printf("%d game%s\n", ng, plur(ng));
	if (wanted_gamenr < 1)
		errexit("game numbers count from 1");
	if (wanted_gamenr > ng)
		errexit("input has only %d game%s", ng, plur(ng));
	gg = get_game(g, wanted_gamenr);

	nv = get_number_of_variations(gg);
	if (out_nv)
		printf("%d variation%s\n", nv, plur(nv));
	if (wanted_varnr < 1)
		errexit("variations count from 1");
	if (wanted_varnr > nv)
		errexit("the game has only %d variation%s", nv, plur(nv));

	if (showtree)
		do_showtree(gg);
	
	if (flatten) {
		select_variation(gg, wanted_varnr);
		do_flatten(gg);
		wanted_varnr = 1;
	}

	gp0.g = gg;
	gp0.n = NULL;
	gp0.varnr = 1;
	gp0.movect = 0;

	gp = gp0;
	get_variation(&gp, wanted_varnr, 0);

	if (out_d)
		printf("%d\n", gp.movect+1);

	if (out_pm)
		outpropmoves(gp.g);

	nm = gp.movect + get_number_of_moves(gp.g);
	if (out_nm)
		printf("%d move%s\n", nm, plur(nm));
	if (wanted_movenr < 0)
		errexit("move numbers count from 1");
	if (wanted_movenr > nm) {
		if (wanted_varnr > 1)
			errexit("the variation has only %d moves", nm);
		else
			errexit("the game has only %d moves", nm);
	}
	
	if (wanted_movenr && wanted_movenr <= gp.movect) {
		gp = gp0;
		get_variation(&gp, wanted_varnr, wanted_movenr);
		n = gp.n;
	} else if (wanted_movenr) {
		n = get_move(gp.g, wanted_movenr - gp.movect);
		gp.n = n;
	} else {
		gp = gp0;
		n = gp.n = gg->nodesequence;
	}
	
	if (optafter) {
		n = aftermoves(&gp, optafter);
		if (n == NULL)
			errexit("no such move");
	}
		
	if (out_M)
		outmove(n);

	if (out_props)
		outprops(n);

	for (i=0; i<pxct; i++)
		outprop(n, optpropx[i]);

	if (out_replies)
		outreplies(&gp);
	
	return 0;
}
