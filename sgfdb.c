/*
 * Read a collection of sgf files, and output a data base
 * (store moves and filename and gamenumber)
 * aeb - 2013-05-23
 *
 * Call: sgfdb [-i] [-o outfile] [-t] [-r] [-e .sgf] infiles
 *
 * -o: set outputfile; default is "out.sgfdb"
 * -i: ignore errors
 * -q: quiet
 * -t: trace input
 * -r: recursive (allows directories among the input files, that then
 *     are searched for .sgf files)
 * -e: set the extension used by -r; default is ".sgf"
 *     -e "" does not impose any condition and will take all files
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "errexit.h"
#include "readsgf.h"
#include "sgfdb.h"
#include "ftw.h"
#include "playgogame.h"

struct bingame bg;
char *outfilename = "out.sgfdb";
FILE *outf;
int recursive = 0;
char *file_extension = ".sgf";

#define BLACK_MASK  0x10000
#define WHITE_MASK  0x20000

#define MAXMOVES	10000
int moves[MAXMOVES], mvct;
int size, movect, handct, abct, awct;

int number_of_games;
int gamenr;		/* serial number (from 1) of current game */
int gtlevel;		/* nesting depth of parens */
int skipping;		/* true if not the main game line */
int outgames;

static void
report_on_single_game() {
	struct played_game game;
	short int mv[MAXMOVES];

	game.mv = mv;
	game.mvlen = MAXMOVES;

	/* should make gamenr available to playgogame() for better errmsgs */
	playgogame(size, moves, mvct, abct+awct, &game);

	bg.gamenr = ((number_of_games == 1) ? 0 : gamenr);
	bg.movect = mvct - abct - awct;
	bg.size = size;
	bg.abct = abct;
	bg.awct = awct;
	bg.bcapt = game.counts[1];	/* test for overflow? */
	bg.wcapt = game.counts[2];
	bg.mvct = game.mvct;	/* this includes captures */
	bg.filenamelen = (strlen(infilename) + 2) & ~1;
	bg.sz = sizeof(bg) + bg.filenamelen + bg.mvct * sizeof(short int);

	if (fwrite(&bg, sizeof(bg), 1, outf) != 1)
		errexit("output error");
	if (fwrite(mv, sizeof(short int), bg.mvct, outf) != bg.mvct)
		errexit("output error");
	if (fwrite(infilename, bg.filenamelen, 1, outf) != 1)
		errexit("output error");
	outgames++;
}

static int
is_move(struct property *p) {
	return (p && p->val->next == NULL &&
		(!strcmp(p->id, "B") || !strcmp(p->id, "W")));
}

#define PASS (('t'<<8) | 't')

static int
move_to_int(char *s) {
	if (*s == 0)
		return PASS;
	if (s[1] == 0 || s[2] != 0)
		errexit("unexpected move _%s_", s);
	return (s[0]<<8) + s[1];
}

static void
put_move(struct propvalue *pv, int mask) {
	char *s, *t;

	if (!pv)
		errexit("missing move value");
	s = pv->val;	/* something like "dp" */

	/* strip trailing whitespace */
	t = s;
	while (*t) t++;
	while (--t >= s && (*t == ' ' || *t == '\n' || *t == '\r'))
		*t = 0;

	if (mvct == MAXMOVES)
		errexit("too many moves");
	moves[mvct++] = move_to_int(s) | mask;
}

static void
put_nodesequence(struct node *n) {
	struct property *p;
	int mask;

	while (n) {
#if 0
		/* a move should be the only property in its node */
		p = n->p;
		if (is_move(p)) {
			mask = (p->id[0] == 'B') ? BLACK_MASK : WHITE_MASK;
			put_move(p->val, mask);
		}
#else
		/* but often the first move ends up in the root node... */
		/* maybe we should give a warning */
		p = n->p;
		while (p) {
			if (is_move(p)) {
				mask = (p->id[0] == 'B')
					? BLACK_MASK : WHITE_MASK;
				put_move(p->val, mask);
			}
			p = p->next;
		}
#endif
		n = n->next;
	}
}

static void
get_setup_stones(struct node *node) {
	struct property *p;
	struct propvalue *pv;

	p = node->p;
	while (p) {
		if (!strcmp(p->id, "AB")) {
			pv = p->val;
			while (pv) {
				abct++;
				put_move(pv, BLACK_MASK);
				pv = pv->next;
			}
		} else if (!strcmp(p->id, "AW")) {
			pv = p->val;
			while (pv) {
				awct++;
				put_move(pv, WHITE_MASK);
				pv = pv->next;
			}
		}
		p = p->next;
	}
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

	/* kludge: peek into second node - some broken files have AB there */
	if (abct == 0 && node->next)
		get_setup_stones(node->next);

	sort_initial_stones();
}

#define MAXSZ		31
#define SZ		19
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
			if (size > SZ)
				errexit("SZ[%d] is perhaps a bit large", size);
			return;
		}
		p = p->next;
	}
}

static void
init_single_game(struct gametree *g) {
	gamenr++;
	size = DEFAULTSZ;
	mvct = abct = awct = 0;

	setsize(g->nodesequence);
	get_initial_stones(g->nodesequence);
	handct = (awct ? 0 : abct);	/* we didnt look at a HA[] property */
}


static void put_gametree_sequence(struct gametree *g);

static void
put_gametree(struct gametree *g) {
	gtlevel++;
	if (gtlevel == 1)
		init_single_game(g);

	put_nodesequence(g->nodesequence);
	put_gametree_sequence(g->firstchild);
	if (gtlevel == 1)
		report_on_single_game();
	gtlevel--;
	skipping = (gtlevel > 0);
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

static void
do_stdin(const char *fn) {
	struct gametree *g;

	if (setjmp(jmpbuf))
		goto ret;
	have_jmpbuf = 1;

	readsgf(fn, &g);
	number_of_games = get_number_of_games(g);
	gamenr = gtlevel = skipping = 0;
	put_gametree_sequence(g);
ret:
	have_jmpbuf = 0;
}

void
do_input(const char *fn) {
	do_stdin(fn);
}

static int
has_extension(char *fn, char *s) {
	char *p = fn;

	while (*p)
		p++;
	while (p > fn && *p != '.')
		p--;
	return !strcmp(p, s);
}

static void
open_outfile() {
	char *mode;
	struct sgfdb db;

	/* try not to overwrite some random file */
	mode = (has_extension(outfilename, ".sgfdb") ? "w" : "wx");
	outf = fopen(outfilename, mode);
	if (outf == NULL) {
		errexit((errno == EEXIST)
			? "will not overwrite existing file %s"
			: "could not create outputfile %s", outfilename);
	}

	/* write header */
	db.headerlen = sizeof(struct sgfdb);
	db.magic = DB_MAGIC;
	db.version = DB_VERSION;
	if (fwrite(&db, sizeof(db), 1, outf) != 1)
		errexit("output error writing header of %s", outfilename);

	outgames = 0;
}

static inline char *plur(int n) {
	return (n == 1) ? "" : "s";
}

int
main(int argc, char **argv){
	int opti = 0;

	progname = "sgfdb";
	infilename = "(reading options)";
	ignore_errors = 0;

	while (argc > 1 && argv[1][0] == '-') {
		if (!strcmp(argv[1], "--")) {
			argc--; argv++;
			break;
		}
		if (!strcmp(argv[1], "-e")) {
			if (argc == 1)
				errexit("-e needs following extension");
			file_extension = argv[2];
			argc--; argv++;
			goto next;
		}
		if (!strcmp(argv[1], "-i")) {
			opti = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-o")) {
			if (argc == 1)
				errexit("-o needs following filename");
			outfilename = argv[2];
			argc--; argv++;
			goto next;
		}
		if (!strcmp(argv[1], "-q")) {
			readquietly = 1;
			silent_unless_fatal = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-r")) {
			recursive = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-t")) {
			tracein = 1;
			goto next;
		}
		errexit("Unknown option %s\n\n"
	"Call: sgfdb [-i] [-o foo.sgfdb] [files]\n"
	"or:   sgfdb [-i] [-o foo.sgfdb] -r [-e .mgt] [files/dirs]\n",
			argv[1]);
	next:
		argc--; argv++;
	}

	if (argc == 1) {
		ignore_errors = 0;	/* no jmpbuf here */
		if (recursive)
			errexit("refuse to read from stdin when recursive");
		open_outfile();
		do_stdin(NULL);
		return 0;
	}

	open_outfile();

	while (argc > 1) {
		ignore_errors = opti;
		do_infile(argv[1]);
		argc--; argv++;
	}

	fclose(outf);
	fprintf(stderr, "%s contains %d game%s\n", outfilename,
		outgames, plur(outgames));

	return 0;
}
