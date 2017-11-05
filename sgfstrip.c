/*
 * sgfstrip: strip away some occurrences of some properties
 * This works for arbitrary games written in SGF format:
 * the fields are not interpreted.
 *
 * Call: sgfstrip [options] tag1 tag2 .. < input > output
 *
 * Property ids tag1, tag2, ... and empty nodes are removed.
 * Leading and trailing whitespace for all property values is removed.
 *
 * By default, the tags are removed everywhere.
 * -pe: preserve empty nodes
 * -pw: preserve whitespace
 * -h, -m, -t: don't do the action in head / middle / tail
 * -pass: strip trailing passes
 *
 * sgfstrip -C:string - strip comments that contain string
 * sgfstrip -C=string - strip comments that equal string
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "readsgf.h"
#include "writesgf.h"
#include "errexit.h"

FILE *outf;

#define MAXM	1000
struct strip {
	char *propid;
	char *string;
	int eq;
} propids[MAXM];
int propidct;

static int is_upper_case(char c) {
	return c >= 'A' && c <= 'Z';
}

/* if arg is all caps, possibly followed by ':' or '=' and a string, then
   add to the list of items to strip and return 1; otherwise return 0 */
static int check_stripitem(char *arg) {
	char *s, *t;
	int eq;

	s = arg;
	t = NULL;
	eq = 0;
	while (is_upper_case(*s))
		s++;
	if (s == arg || (*s && *s != ':' && *s != '='))
		return 0;
	if (*s == ':' || *s == '=') {
		eq = (*s == '=');
		*s = 0;
		t = s+1;
	}
	if (propidct == MAXM)
		errexit("too many args");
	propids[propidct].propid = arg;
	propids[propidct].string = t;
	propids[propidct].eq = eq;
	propidct++;
	return 1;
}

static int should_strip(struct property *p) {
	char *id, *val;
	int i;

	id = p->id;
	val = (p->val ? p->val->val : NULL);

	for (i=0; i<propidct; i++)
		if (!strcmp(id, propids[i].propid)) {
			char *s = propids[i].string;
			if (!s)
				return 1;
			if (propids[i].eq)
				return (val && strcmp(val, s) == 0);
			return (val && strstr(val, s));
		}
	return 0;
}

int optpw, optpe, opth, optm, optt, optpass;

static int is_whitespace(char c) {
	return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static void remove_whitespace(char **s) {
	char *t = *s;

	while (is_whitespace(*t))
		t++;
	*s = t;
	while (*t)
		t++;
	while (t > *s && is_whitespace(t[-1]))
		t--;
	*t = 0;
}

int eof;

int gtlevel;
int nodect;
int invariation;

static void
strip_propvalues(struct propvalue *p, int action) {
	while (p) {
		if (action && !optpw)
			remove_whitespace(&(p->val));
		p = p->next;
	}
}

static int
is_final_node(struct node *n) {
	return !invariation && (n->next == 0);
}

static int
is_pass(struct property *p) {
	return (p && p->val->next == NULL &&
		(!strcmp(p->id, "B") || !strcmp(p->id, "W")) &&
		(!strcmp(p->val->val, "") || !strcmp(p->val->val, "tt")));
}

static void
strip_property_sequence(struct property **q, int action) {
	struct property *p;

	while (*q) {
		p = *q;

//		int single = (p->val->next == NULL);
//		int empty = (p->val->val[0] == 0);

		if (action && should_strip(p)) {
			*q = p->next;
		} else {
			strip_propvalues(p->val, action);
			q = &(p->next);
		}
	}
}

static void
strip_passes(struct property **q) {
	struct property *p;

	while (*q) {
		p = *q;

		if (is_pass(p))
			*q = p->next;
		else
			q = &(p->next);
	}
}

static void
strip_nodesequence(struct node **nn0) {
	struct node *n, **nn;
	struct property **q;
	int action, is_final;

	nn = nn0;

	while (*nn) {
		n = *nn;
		is_final = is_final_node(n);
		action = ((nodect == 0) ? !opth : (is_final ? !optt : !optm));
		q = &(n->p);
		if (*q)
			strip_property_sequence(q, action);
		if (action && !optpe && n->p == NULL) {
			*nn = n->next;
		} else {
			nn = &(n->next);
			nodect++;
		}
	}

	if (optpass) {
		while (*nn0) {
			nn = nn0;
			n = *nn;
			while (n->next) {
				nn = &(n->next);
				n = *nn;
			};
			q = &(n->p);
			if (*q)
				strip_passes(q);
			if (n->p)
				break;
			*nn = NULL;
		}
	}
}

/* forward declaration */
static void strip_gametree_sequence(struct gametree *g);

static void
strip_gametree(struct gametree *g) {
	int newgame;

	gtlevel++;
	newgame = (gtlevel == 1);
	if (newgame)
		nodect = invariation = 0;
	strip_nodesequence(&(g->nodesequence));
	strip_gametree_sequence(g->firstchild);
	gtlevel--;
	invariation = gtlevel;
}

static void
strip_gametree_sequence(struct gametree *g) {
	while (g) {
		strip_gametree(g);
		g = g->nextsibling;
	}
}

static int
file_exists(char *fn) {
	struct stat sb;

	return (stat(fn, &sb) == 0);
}

static void
usage() {
	fprintf(stderr, "Usage: "
		"sgfstrip [-h] [-m] [-t] [-pw] [-pe] tags "
		"< input > output\n");
	exit(0);
}

int
main(int argc, char **argv){
	int i;
	char *infile = NULL;
	struct gametree *g;

	progname = "sgfstrip";
	optpw = optpe = 0;

	for (i=1; i<argc; i++) {
		if (!strcmp(argv[i], "-?") || !strcmp(argv[i], "--help"))
			usage();
		if (!strcmp(argv[i], "-h")) {
			opth = 1;
			continue;
		}
		if (!strcmp(argv[i], "-m")) {
			optm = 1;
			continue;
		}
		if (!strcmp(argv[i], "-t")) {
			optt = 1;
			continue;
		}
		if (!strcmp(argv[i], "-pass")) {
			optpass = 1;
			continue;
		}
		if (!strcmp(argv[i], "-pe")) {
			optpe = 1;
			continue;
		}
		if (!strcmp(argv[i], "-pw")) {
			optpw = 1;
			continue;
		}
		if (check_stripitem(argv[i]))
			continue;
		if (argv[i][0] == '-')
			errexit("unrecognized option %s", argv[i]);

		/* maybe input file? */
		if (!infile && file_exists(argv[i])) {
			infile = argv[i];
			continue;
		}

		errexit("unrecognized parameter %s - not all caps", argv[i]);
	}

	outf = stdout;

	if (infile) {
		FILE *f = freopen(infile, "r", stdin);
		if (!f)
			errexit("cannot open %s", infile);
	}

	eof = 0;
	readsgf(NULL, &g);
	gtlevel = 0;
	strip_gametree_sequence(g);
	writesgf(g,stdout);

	return 0;
}
