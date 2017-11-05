/* Given a struct gametree, write an sgf file */
#include <string.h>
#include "readsgf.h"
#include "writesgf.h"

/*
 * Straight output - no modifications here, except for the silly addition
 * of a single ; to change the currently invalid () into (;).
 */

static FILE *outf;
struct node *rootnode;
static int gtlevel;
static int movesonthisline, movesperline;

#define SIZE(a)	(sizeof(a) / sizeof((a)[0]))

static void
write_propvalues(struct propvalue *p) {
	while (p) {
		fprintf(outf, "[%s]", p->val);
		p = p->next;
	}
}

static void
write_property_sequence(struct property *p) {
	int did_output = 0;	/* precede 1st output with newline */
	int i;

	/* no newline after these */
	char *moveprops[] = {
		"BL", "WL", "OB", "OW", "CR"
	};
	int sameline;

	while (p) {
		sameline = 0;
		for (i=0; i<SIZE(moveprops); i++)
			if (!strcmp(p->id, moveprops[i])) {
				movesonthisline = movesperline;
				sameline = 1;
			}
		if (!sameline && !did_output++)
			fprintf(outf, "\n");
		fprintf(outf, "%s", p->id);
		write_propvalues(p->val);
		if (!sameline) {
			fprintf(outf, "\n");
			movesonthisline = 0;
		}
		p = p->next;
	}
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

static void
write_nodesequence(struct node *n) {
	struct property *p;

	/* turn () into (;) */
	if (!n) {
		fprintf(outf, ";");
		return;
	}

	while (n) {
		p = n->p;
		if (is_move(p)) {
			if (movesonthisline == movesperline) {
				fprintf(outf, "\n");
				movesonthisline = 0;
			}
			fprintf(outf, ";");
			write_move(p);
			p = p->next;
			movesonthisline++;
		} else {
			fprintf(outf, ";");
		}
		if (p)
			write_property_sequence(p);
		if (n == rootnode)
			fprintf(outf, "\n");
		n = n->next;
	}
}

/* forward declaration */
static void write_gametree_sequence(struct gametree *g);

static void
write_gametree(struct gametree *g) {
	gtlevel++;
	fprintf(outf, "(");
	if (gtlevel == 1)
		rootnode = g->nodesequence;
	write_nodesequence(g->nodesequence);
	write_gametree_sequence(g->firstchild);
	fprintf(outf, ")\n");
	movesonthisline = 0;
	gtlevel--;
}

static void
write_gametree_sequence(struct gametree *g) {
	while (g) {
		write_gametree(g);
		g = g->nextsibling;
	}
}

static void write_init(FILE *f) {
	outf = f;
	gtlevel = 0;
	movesonthisline = 0;
	movesperline = 10;
}

void writesgf(struct gametree *g, FILE *f) {
	write_init(f);
	write_gametree_sequence(g);
}
