/* error-free (and faster) versions of some library routines */

#include <stdlib.h>
#include <string.h>
#include "xmalloc.h"
#include "errexit.h"

static void
fatal(const char *s) {
	ignore_errors = 0;
	errexit(s);
}

void *
xmalloc(int n) {
	void *p = malloc(n);

	if (p == NULL)
		fatal("out of memory");
	return p;
}

void *
xrealloc(void *p, int n) {
	void *pp = realloc(p, n);

	if (pp == NULL)
		fatal("out of memory");
	return pp;
}

char *
xstrdup(char *p) {
	char *pp = strdup(p);

	if (pp == NULL)
		fatal("out of memory");
	return pp;
}

/* together with the x- versions, one can use ordinary free() */

/* much faster version when many small areas are allocated */
#define YMALLOC_INCR	65536

static struct arena {
	struct arena *next;
} *thisarena;
static void *thisfree;
static long int thisleft = -1L;
static long int thislastsz = 0;

#include <stdio.h>
void *ymalloc(int n) {
	void *p;
	struct arena *a;

	while (1) {
		p = thisfree;
		thisfree += n;
		thisleft -= n;
		if (thisleft >= 0)
			return p;

		/* no room */
		thislastsz += YMALLOC_INCR;
		p = malloc(thislastsz);
		if (p == NULL)
			fatal("ymalloc: out of memory");
		a = p;
		a->next = thisarena;
		thisarena = a;
		thisleft = thislastsz - sizeof(struct arena);
		thisfree = p + sizeof(struct arena);
	}
}

void yfree() {
	void *p;

	while ((p = thisarena) != NULL) {
		thisarena = thisarena->next;
		free(p);
	}
	thisleft = -1;
	thislastsz = 0;
}

char *ystrdup(char *p) {
	int n = strlen(p);
	char *q = ymalloc(n+1);

	return strcpy(q,p);
}

/* no yrealloc */
