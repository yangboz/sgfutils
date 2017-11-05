/*
 * nk2sgf: replace TE, RD, KO, GK, LT, HD by EV, DT, KM, GM, OT, HA
 * add ; after first ( [followed by TE]
 *
 * Call: nk2sgf < input > output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "readsgf.h"
#include "writesgf.h"
#include "errexit.h"
#include "xmalloc.h"

FILE *outf;

struct repl {
	char *old, *new;
} repl[] = {
	{ "TE", "EV" },
	{ "RD", "DT" },
	{ "KO", "KM" },
	{ "GK", "GM" },
	{ "LT", "OT" },
	{ "HD", "HA" }
};

static int is_whitespace(char c) {
	return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static char *replacement(char *s) {
	int i;
	char *t, *u;

	while (is_whitespace(*s))
		s++;
	for (i=0; i < sizeof(repl)/sizeof(repl[0]); i++) {
		t = s;
		u = repl[i].old;
		while (*t && *u)
			if (*t++ != *u++)
				goto nexti;
		if (*u == 0)
			while (is_whitespace(*t))
				t++;
		if (*t == 0 && *u == 0)
			return repl[i].new;
	nexti:;
	}
	return NULL;
}

int eof;

static void
replace_propid(struct property *p) {
	char *s;

	while (p) {
		s = replacement(p->id);
		if (s)
			p->id = xstrdup(s);
		p = p->next;
	}
}

static void
handle_komi(struct property *p) {
	if (!strcmp(p->id, "KM")) {
		/* remove a leading 黒 (in SJIS) */
		if (p->val && p->val->val &&
		    p->val->val[0] == '\x8d' &&
		    p->val->val[1] == '\x95')
			p->val->val += 2;
	}
}

static void
handle_date(struct property *p) {
	if (!strcmp(p->id, "DT")) {
		/* replace 2014-09-05・06 by 2014-09-05,06 */
		if (p->val && p->val->val) {
			char *q, *s;

			q = p->val->val;
			while (*q) {
				if (q[0] == '\x81' && q[1] == '\x45') {
					*q++ = ',';
					s = q+1;
					while ((*q++ = *s++));
					break;
				}
				q++;
			}
		}
	}
}

static void
handle_property_sequence(struct property *p) {
	while (p) {
		replace_propid(p);
		handle_komi(p);
		handle_date(p);
		p = p->next;
	}
}

static void
handle_nodesequence(struct node *n) {
	while (n) {
		handle_property_sequence(n->p);
		n = n->next;
	}
}

/* forward declaration */
static void handle_gametree_sequence(struct gametree *g);

static void
handle_gametree(struct gametree *g) {
	handle_nodesequence(g->nodesequence);
	handle_gametree_sequence(g->firstchild);
}

static void
handle_gametree_sequence(struct gametree *g) {
	while (g) {
		handle_gametree(g);
		g = g->nextsibling;
	}
}

int
main(int argc, char **argv){
	struct gametree *g;

	progname = "nk2sgf";
	if (argc > 1)
		errexit("Usage: nk2sgf < in > out");

	eof = 0;
	readquietly = 1;
	readsgf(NULL, &g);
	handle_gametree_sequence(g);
	writesgf(g, stdout);

	return 0;
}
