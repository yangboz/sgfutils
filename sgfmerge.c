/*
 * Merge sgf files - aeb, 2013-05-19
 *
 * -c: strip comments and variations
 * -d: error exit when duplicate fields
 * -t: trace: print input as it is being read
 * -tr: check that TR = triangle property refers to last move, and delete it
 * -m#: allow for # (default 0) differences in the sequence of moves
 *      and report on them in a Game Comment.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "errexit.h"
#include "xmalloc.h"
#include "readsgf.h"

FILE *outf;

int wipetr = 0;
int exit_if_dups = 0;
int stripcomments = 0;
int eof;

/*
 * Allow a few differences (typically 1), and report
 * GC[Some sources have B 105 at cq]
 * The first sgf file provides the main line.
 */
#define MAXDIFS 10
int maxdifs = 0;
int diffct;

static void
write_propvalues(struct propvalue *p) {
	while (p) {
		fprintf(outf, "[%s]", p->val);
		p = p->next;
	}
}

#if 0
static struct property *
get_property(struct gametree *g, char *prop) {
	struct node *n;
	struct property *p;

	if (!g || !(n = g->nodesequence) || !(p = n->p))
		return NULL;
	while (p && strcmp(p->id, prop))
		p = p->next;
	return p;
}
#endif

int gtlevel = 0;
int skipping = 0;

static void
write_init() {
	gtlevel = 0;
	outf = stdout;
}

static int
is_setup(struct property *p) {
	return (p && (!strcmp(p->id, "AB") ||
		      !strcmp(p->id, "AW") ||
		      !strcmp(p->id, "AE")));
}

static int
is_move(struct property *p) {
	return (p && p->val->next == NULL &&
		(!strcmp(p->id, "B") || !strcmp(p->id, "W")));
}

static void
write_move(struct property *p) {
	fprintf(outf, "%s[%s]", p->id, p->val->val);
}

#define SIZE(a)	(sizeof(a) / sizeof((a)[0]))

static void
write_property_sequence(struct property *p) {
	int did_output = 0;	/* precede 1st output with newline */

	/* make two passes:
	   first the known properties in well-defined order,
	   then the rest */
	int i;
	char *known[] = {
		"FF",	/* File Format */
		"EV",	/* Event */
		"RO",	/* Round */
		"ID",
		"PB",	/* Black player */
		"BR",	/* Black rank */
		"PW",	/* White player */
		"WR",	/* White rank */
		"TM",	/* Time */
		"KM",	/* Komi */
		"RE",	/* Result */
		"DT",	/* Date */
		"JD",	/* Japanese date */
		"PC"	/* Place */
	};
	char *ignore[] = {
		"GM",	/* Game: 1 is go */
		"SY",	/* System? */
		"BS",	/* B species: 0 is human */
		"WS",	/* W species: 0 is human */
		"KI"	/* ?? */
	};
	char *strip[] = {
		"C",	/* comment */
		"LB"	/* label */
	};
	struct property *q;

	for(i=0; i<SIZE(known); i++) {
		for (q=p; q; q=q->next) {
			if (!strcmp(q->id, known[i])) {
				/* skip empty strings */
				if (q->val->next == NULL &&
				    q->val->val[0] == 0)
					continue;

				if (!did_output++)
					fprintf(outf, "\n");
				fprintf(outf, "%s", q->id);
				write_propvalues(q->val);
				fprintf(outf, "\n");
			}
		}
	}

	while (p) {
		int single = (p->val->next == NULL);
		int empty = (p->val->val[0] == 0);

		if (single && empty)
			goto skip;
#if 0
		if (!strcmp(p->id, "SZ")) {
			if (strcmp(p->val->val, "19") &&
			    strcmp(p->val->val, " 19 "))
				errexit("not 19x19");
			goto skip;
		}
#endif
		for (i=0; i<SIZE(known); i++)
			if (!strcmp(p->id, known[i]))
				goto skip;
		for (i=0; i<SIZE(ignore); i++)
			if (!strcmp(p->id, ignore[i]))
				goto skip;
		if (stripcomments)
			for (i=0; i<SIZE(strip); i++)
				if (!strcmp(p->id, strip[i]))
					goto skip;
		if (!did_output++)
			fprintf(outf, "\n");
		fprintf(outf, "%s", p->id);
		write_propvalues(p->val);
		fprintf(outf, "\n");
	skip:
		p = p->next;
	}
}

static void
write_nodesequence(struct node *n) {
	struct property *p;
	int ct = 0;

	while (n) {
		p = n->p;
		if (is_move(p)) {
			if (ct++ % 10 == 0)
				fprintf(outf, "\n");
			fprintf(outf, ";");
			write_move(p);
			p = p->next;
		} else {
			fprintf(outf, ";");
		}
		if (p)
			write_property_sequence(p);
		n = n->next;
	}
}

/* forward declaration */
static void write_gametree_sequence(struct gametree *g);

static void
write_gametree(struct gametree *g) {
	int parens;

	gtlevel++;
	parens = (gtlevel == 1 || !stripcomments);
	if (parens)
		fprintf(outf, "(");
	write_nodesequence(g->nodesequence);
	write_gametree_sequence(g->firstchild);
	if (parens)
		fprintf(outf, ")\n");
	gtlevel--;
	skipping = (stripcomments && gtlevel);
}

static void
write_gametree_sequence(struct gametree *g) {
	while (g) {
		if (!skipping)
			write_gametree(g);
		g = g->nextsibling;
	}
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
remove_comments(char *fn, struct gametree *g) {
	struct node *node, *prev;
	struct property *p;
	int warned = 0;
	int moveseen = 0;

	node = g->nodesequence;
	if (!node)
		errexit("no first node?");
	prev = node;
	node = node->next;

	/* remove comments, except on first and last node */
	/* we just called flatten(), so the game does not fork */
	/* AB/AW/AE before the first move is moved to the first node */
	while (node) {
		if (!node->next)
			break;
		p = node->p;
		if (is_move(p)) {
			moveseen = 1;
		} else if (!moveseen && is_setup(p)) {
			struct property *q;

			/* apply surgery */
			node->p = p->next;
			p->next = 0;
			q = g->nodesequence->p;
			while (q->next)
				q = q->next;
			q ->next = p;
			continue;
		} else {
			if (p && !warned++)
				fprintf(stderr,
					"warning: %s: first node property %s"
					" is not a move\n", fn, p->id);
			while (p && !is_move(p))
				p = p->next;
			if (!p) {
				node = prev->next = node->next;
				continue;
			}
			node->p = p;
		}
		if (p->next) {
			if (!warned++)
				fprintf(stderr, "warning: %s stripped\n", fn);
			p->next = 0;
		}
		prev = node;
		node = node->next;
	}

	/* on the last node, make sure the move, if any, comes first */
	if (node && node->p && !is_move(node->p)) {
		struct property *q, *qprev;

		qprev = node->p;
		q = qprev->next;

		while (q && !is_move(q)) {
			qprev = q;
			q = q->next;
		}

		if (q) {
			if (!warned++)
				fprintf(stderr,
					"warning: %s: first node property %s"
					" is not a move\n", fn, node->p->id);

			/* apply surgery: move q to the front */
			qprev->next = q->next;
			q->next = node->p;
			node->p = q;
		}
	}
}

static void
prepare_merge(char *fn, struct gametree **g) {
	int n;

	readsgf(fn, g);

	n = number_of_games(*g);
	if (n != 1)
		errexit("%s has multiple games - first split [sgf -x]", fn);

	flatten(fn, *g);
	remove_comments(fn, *g);
}


struct comment_list {
	char *txt;
	struct comment_list *next;
} *comments, *ctail;

#define MAXCOMMENTLEN 1000

static void add_comment(const char *s, ...) {
	va_list ap;
	char buf[MAXCOMMENTLEN];
	struct comment_list *cp;

	va_start(ap, s);
	vsprintf(buf, s, ap);
	va_end(ap);

	cp = xmalloc(sizeof(*cp));
	cp->txt = strdup(buf);
	cp->next = NULL;

	if (comments) {
		ctail->next = cp;
		ctail = cp;
	} else
		comments = ctail = cp;
}

static void
append_gamecomments(struct gametree *g) {
	int len;
	char *comment, *p;
	struct comment_list *cp;
	struct node *node;
	struct property *gc;
	struct propvalue *pv;

	if (!comments)
		return;		/* nothing to do */

	len = 0;
	for (cp = comments; cp; cp = cp->next)
		len += strlen(cp->txt) + 3;
	
	/* allocate string and write text */
	p = comment = xmalloc(len + 100);

	for (cp = comments; cp; cp = cp->next) {
		if (cp != comments)
			p += sprintf(p, ".\n");
		p += sprintf(p, "%s", cp->txt);
	}

	/* construct new root property and attach it */
	pv = xmalloc(sizeof(struct propvalue));
	pv->val = comment;
	pv->next = NULL;
	gc = xmalloc(sizeof(struct property));
	gc->id = strdup("GC");
	gc->val = pv;
	gc->next = NULL;
	node = g->nodesequence;
	if (node->p) {
		struct property *p = node->p;
		while (p->next)
			p = p->next;
		p->next = gc;
	} else
		node->p = gc;
}

static void
add_diff(char *color, int movenr, char *move) {
	if (diffct++ == MAXDIFS)
		errexit("add_diff: too many differences");
	if (strlen(color) != 1)
		errexit("add_diff: bad color _%s_", color);
	if (strlen(move) != 2)
		errexit("add_diff: bad move _%s_", move);
	add_comment("Some sources have %c %d at %c%c",
		    *color, movenr, move[0], move[1]);
}

static int
has_more_moves(struct node *node) {
	while (node) {
		/* A move, but not a pass? */
		if (is_move(node->p) &&
		    node->p->val && strcmp(node->p->val->val, "tt"))
			return 1;
		node = node->next;
	}
	return 0;
}

/* note: semi-shallow copy */
static void
copy_moves(struct node *to, struct node *from) {
	struct node *n;
	struct property *p;

	while (from && is_move(from->p)) {
		p = xmalloc(sizeof(*p));
		n = xmalloc(sizeof(*n));
		*p = *(from->p);
		p->next = NULL;
		n->p = p;
		n->next = NULL;
		to = to->next = n;
		from = from->next;
	}
}

/* return t unchanged, or stripped from whitespace */
static char *
move_strip_ws(char *t) {
	char res[3], *r, *s;

	if (t[0] && t[1] && !t[2])
		return t;
	r = res;
	s = t;
	while (*s && r < res+3) {
		if (*s != ' ' && *s != '\r' && *s != '\n')
			*r++ = *s++;
		else
			s++;
	}
	if (*s == 0 && r == res+2) {
		*r = 0;
		return strdup(res);
	}
	return t;
}

static char *plur(int m) {
	return (m == 1) ? "" : "s";
}

static void
merge_games(struct gametree *g1, struct gametree *g2) {
	struct node *node1, *node2, *pnode1, *pnode2;
	struct property *p1, *p2;
	char *m1, *m2;
	int movenr;

	pnode1 = g1->nodesequence;
	pnode2 = g2->nodesequence;
	if (!pnode1 || !pnode2)
		errexit("empty game");
	movenr = 0;

	while (1) {
		node1 = pnode1->next;
		node2 = pnode2->next;
		if (!node1 || !node2)
			break;
		movenr++;

		p1 = node1->p;
		p2 = node2->p;
		if (!p1 || !p2) {
			if (!p1 && !p2)
				goto nxt;	/* both empty */
			errexit("empty vs nonempty node at move %d", movenr);
		}
		if (!p1->id || !p2->id)
			errexit("missing propid at move %d", movenr);
		if (!p1->val || !p2->val)
			errexit("missing propval at move %d", movenr);

		if (strcmp(p1->id, p2->id))
			errexit("different move colors %s and %s at move %d",
				p1->id, p2->id, movenr);
		m1 = p1->val->val;
		m2 = p2->val->val;
		if (!m1 || !m2)
			errexit("missing propvalue at move %d", movenr);
		if (*m1 == 0)
			m1 = "tt";	/* %% PASS */
		if (*m2 == 0)
			m2 = "tt";
		if (strcmp(m1, m2)) {
			/* try again after stripping whitespace */
			m1 = move_strip_ws(m1);
			m2 = move_strip_ws(m2);
			if (!strcmp(m1,m2)) {
				p1->val->val = m1;
				p2->val->val = m2;
				goto nxt;
			}

			if (diffct < maxdifs)
				add_diff(p1->id, movenr, p2->val->val);
			else if (maxdifs == 0)
				errexit("different moves (#%d) '%s' and '%s'",
					movenr, p1->val->val, p2->val->val);
			else
				errexit("more than %d difference%s",
					maxdifs, plur(maxdifs));
		}
	nxt:
		pnode1 = node1;
		pnode2 = node2;
	}

	if (node1 && !node2 && has_more_moves(node1))
		add_comment("Some sources have %d moves", movenr);

	if (!node1 && node2 && has_more_moves(node2)) {
		add_comment("Some sources have %d moves", movenr);
		/* We would like to do  pnode1->next = node2;
		   but that will give problems later with merge_tails.
		   Do the copy by hand. */
		copy_moves(pnode1, node2);
	}
}

static void
merge_tails(struct gametree *g1, struct gametree *g2) {
	struct node *node1, *node2;
	struct property *p1, *p2;
	int n1ct, n2ct;

	node1 = g1->nodesequence;
	node2 = g2->nodesequence;

	n1ct = 0;
	while (node1->next) {
		node1 = node1->next;
		n1ct++;
	}

	n2ct = 0;
	while (node2->next) {
		node2 = node2->next;
		n2ct++;
	}

	if (n1ct == 0 || n2ct <= 1)
		return;		/* merge is done for heads */

	p1 = node1->p;
	p2 = node2->p;
	while (p1 && p1->next)
		p1 = p1->next;

	if (p2) {
		if (!p1)
			node1->p = p2->next;
		else
			p1->next = p2->next;
	}
}

static void
merge_heads(struct gametree *g1, struct gametree *g2) {
	struct node *node1, *node2;
	struct property *p1, *p2;

	node1 = g1->nodesequence;
	node2 = g2->nodesequence;

	p1 = node1->p;
	p2 = node2->p;
	if (p1) {
		while (p1->next)
			p1 = p1->next;
		p1->next = p2;
	} else
		node1->p = p2;
}

static int
value_seqs_are_equal(struct propvalue *u, struct propvalue *v) {
	if (u == v)
		return 1;
	if ((u && !v) || (v && !u))
		return 0;
	if (strcmp(u->val, v->val))
		return 0;
	return value_seqs_are_equal(u->next, v->next);
}

static int
properties_are_equal(struct property *p, struct property *q) {
	if (strcmp(p->id, q->id))
		return 0;
	return value_seqs_are_equal(p->val, q->val);
}

static void
remove_duplicates_in_node(struct node *node) {
	struct property *p, *q, *qprev;

	p = node->p;
	while (p) {
		/* compare all following with p */
		qprev = p;
		while (qprev->next) {
			q = qprev->next;
			if (properties_are_equal(p,q))
				qprev->next = q->next;
			else
				qprev = q;
		}
		p = p->next;
	}
}

static int is_result(struct property *p) {
	return !strcmp(p->id, "RE") && p->val && p->val->val && !p->val->next;
}

/* merge RE[B+] with RE[B+R] */
static void
merge_results(struct node *node) {
	struct property *p, **ap, *q, *qprev;
	char *sp, *sq;

	ap = &(node->p);
	p = *ap;
	while (p) {
		if (!is_result(p))
			goto nextp;
		qprev = p;
		while (qprev->next) {
			q = qprev->next;
			if (!is_result(q))
				goto nextq;
			sp = p->val->val;
			sq = q->val->val;
			if (sp[0] != sq[0] || sp[1] != '+' || sq[1] != '+')
				goto nextq;
			if (sq[2] == 0) {
				qprev->next = q->next;
				continue;
			}
			if (sp[2] == 0) {
				*ap = p = p->next;
				goto loop;
			}
		nextq:
			qprev = q;
		}
	nextp:
		ap = &(p->next);
		p = *ap;
	loop:;
	}
}

static void
check_for_dups_in_node(struct node *node) {
	struct property *p, *q;

	p = node->p;
	while (p) {
		q = p->next;
		while (q) {
			if (!strcmp(p->id, q->id))
				errexit("duplicate %s property", p->id);
			q = q->next;
		}
		p = p->next;
	}
}

static int compar(const void *aa, const void *bb) {
	const char *a = *(char * const *) aa;
	const char *b = *(char * const *) bb;

	return strcmp(a,b);
}

static void
sort_setup_in_head(struct gametree *g) {
	struct node *rootnode;
	struct property *p;
	struct propvalue *u, *unext;
	char **arr;
	int i, ct;

	rootnode = g->nodesequence;
	if (!rootnode)
		return;

	for (p = rootnode->p; p; p = p->next) {
		if (!is_setup(p))
			continue;
		ct = 0;
		for (u = p->val; u; u = u->next)
			ct++;
		arr = xmalloc(ct * sizeof(char *));
		i = 0;
		for (u = p->val; u; u = u->next)
			arr[i++] = u->val;
		qsort(arr, ct, sizeof(char *), compar);
		i = 0;
		for (u = p->val; u; u = u->next)
			u->val = arr[i++];
//		free(arr);

		/* collapse duplicate fields */
		for (u = p->val; u; u = unext) {
			unext = u->next;
			while (unext && !strcmp(u->val, unext->val))
				unext = unext->next;
			u->next = unext;
		}
	}
}

static void
remove_duplicates_in_head(struct gametree *g) {
        struct node *node = g->nodesequence;

	remove_duplicates_in_node(node);
	merge_results(node);
}

static void
remove_duplicates_in_tail(struct gametree *g) {
	struct node *node = g->nodesequence;

	while (node->next)
		node = node->next;
	remove_duplicates_in_node(node);
}

static void
check_for_dups_in_head(struct gametree *g) {
	struct node *node = g->nodesequence;

	check_for_dups_in_node(node);
}

static void
check_for_dups_in_tail(struct gametree *g) {
	struct node *node = g->nodesequence;

	while (node->next)
		node = node->next;
	check_for_dups_in_node(node);
}

static void
check_and_delete_triangle(struct gametree *g) {
	struct node *node = g->nodesequence;
	struct node *prev = NULL;
	struct property *p, *prevp, *lastmove;

	while (node->next) {
		prev = node;
		node = node->next;
	}
	p = node->p;
	prevp = NULL;
	lastmove = (is_move(p) ? p : ((prev && prev->p) ? prev->p : NULL));
	while (p) {
		if (!strcmp(p->id, "TR")) {
			if (!lastmove)
				errexit("TR but no moves");
			if (!value_seqs_are_equal(lastmove->val, p->val))
				errexit("TR differs from last move");
			if (prevp)
				prevp->next = p->next;
			else {
				node->p = p->next;
				if (!node->p && prev)
					prev->next = NULL;
			}
		}
		prevp = p;
		p = p->next;
	}
}

static struct gametree *
mergesgf(struct gametree *g1, struct gametree *g2) {
	merge_games(g1,g2);
	merge_tails(g1,g2);
	merge_heads(g1,g2);
	return g1;
}

int
main(int argc, char **argv){
	struct gametree *g, *g2;
	int i;

	progname = "sgfmerge";

	while (argc > 1) {
		if (!strcmp(argv[1], "-c")) {
			stripcomments = 1;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-d")) {
			exit_if_dups = 1;
			argc--; argv++;
			continue;
		}
		if (!strncmp(argv[1], "-m", 2)) {
			maxdifs = atoi(argv[1]+2);
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-t")) {
			tracein = 1;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-tr")) {
			wipetr = 1;
			argc--; argv++;
			continue;
		}
		break;
	}

	prepare_merge((argc > 1) ? argv[1] : "-", &g);

	for (i=2; i<argc; i++) {
		prepare_merge(argv[i], &g2);
		g = mergesgf(g, g2);
	}

	sort_setup_in_head(g);
	remove_duplicates_in_head(g);
	remove_duplicates_in_tail(g);
	if (wipetr)
		check_and_delete_triangle(g);
	if (exit_if_dups) {
		check_for_dups_in_head(g);
		check_for_dups_in_tail(g);
	}
	append_gamecomments(g);

	write_init();
	write_gametree_sequence(g);

	return 0;
}
