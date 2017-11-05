#include <stdio.h>

struct gametree {
	struct node *nodesequence;
	struct gametree *firstchild;
	struct gametree *nextsibling;
};

struct node {
	struct property *p;
	struct node *next;
};

struct property {
	char *id;
	struct propvalue *val;
	struct property *next;
};

struct propvalue {
	char *val;
	struct propvalue *next;
};

extern int readsgf(const char *fn, struct gametree **gg);
extern int multiin, tracein, readquietly, fullprop;

