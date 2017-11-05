/*
 * sgfcheck - read and check a collection of sgf files
 *
 * Checks: (i) Does this satisfy FF[4] grammar?
 *  (ii) Is this a legal go game?
 *  (iii) Any likely mistakes?
 *
 * Call: sgfcheck [-r] [-e ext] [infiles]
 *
 * sgfcheck -okfn: print ok filenames to stdout
 * sgfcheck -nokfn: print nok filenames to stdout
 *
 * -noRE: don't check RE field
 * -noKM: don't check KM field
 * -Eresign: do not mutter about "resigner played last move"
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ftw.h"
#include "readsgf.h"
#include "xmalloc.h"
#include "errexit.h"
#include "playgogame.h"

/*
 * FF[4] grammar
 *
 * Notation:
 *  a+: one or more a's
 *  a*: zero or more a's
 *  [a]: at most one a, i.e. nothing or a
 *  a|b: a or b
 *
 * Collection = GameTree+
 * GameTree   = "(" Sequence GameTree* ")"
 * Sequence   = Node+
 * Node       = ";" { Property }
 * Property   = PropIdent PropValue+
 * PropIdent  = UcLetter+
 * PropValue  = "[" CValueType "]"
 *
 * CValueType = ValueType | Compose
 * Compose    = ValueType ":" ValueType
 * ValueType  = None | Number | Real | Double | Color | SimpleText |
 *               Text | Point  | Move | Stone
 *
 * None       = ""
 * Number     = ["+"|"-"] Digit+
 * Real       = Number ["." Digit+]
 * UcLetter   = "A".."Z"
 * Digit      = "0".."9"
 * Double     = "1" | "2"
 * Color      = "B" | "W"
 *
 * Point, Move, Stone: game specific
 * ValueType: property specific
 *  (the expected type is determined by the property)
 *
 * In Text: "\" escapes the following character; needed for \ and : and ].
 *  Needed for : only in case of a compose data type.
 *  So the separator in a compose data type is \: where the \ is not escaped.
 * The sequence ("\" newline) denotes the empty string.
 * Tab and vertical tab are equivalent to space
 * CR LF and LF CR and CR and LF all denote newline
 *  (Apparently, while both CR and LF denote a newline, CR LF also
 *   denotes a single newline; if the text should be well-defined,
 *   a series of newlines must be represented by one of CR+ or LF+
 *   or (CR LF)+ or (LF CR)+. We should complain about ambiguous
 *   whitespace otherwise.)
 * The default character set is Latin-1. Common is CA[UTF-8].
 *
 * SimpleText: as Text, but also newline is equivalent to space.
 */

/*
 * Nodes are numbered from 0.
 * The order of properties in a node is not fixed.
 * Only one of each property is allowed in a node.
 *
 * There are move, setup, root, game-info properties.
 * Move properties must not be mixed with setup properties in the same node.
 * It is bad style to have move properties in the root node.
 * No two game-info nodes in the same variation.
 *
 * Move properties: B, BL, BM, DO, IT, KO, MN, OB, OW, TE, W, WL
 * Setup properties: AB, AW, AE, PL
 * Root properties: AP, CA, FF, GM, ST, SZ
 * GameInfo properties: AN, BR, BT, CP, DT, EV, GC, GN, ON, OT, PB, PC, PW,
 *  RE, RO, RU, SO, TM, US, WR, WT, HA, KM
 *
 * In Go Move is the same as Point, two lower case letters,
 * or, for sizes 27..52, also upper case.
 * Pass is [], or, if sz <= 19, [tt].
 *
 * Compressed point list: aa:ss, a pair denoting a rectangle.
 * Rectangles must be disjoint.
 */

/*
 * VW[...] gets cleared by VW[] with empty value
 */

/*
 * Defects:
 * - File is in ASCII, but the character set of Texts and SimpleTexts
 *   is determined by the CA property in the root. Apparently that
 *   character set is such that one still can recognize "\" and "]",
 *   and "\" quotes a single byte. Multibyte characters can contain
 *   "\" or "]" as second byte, and may need a "\" in the middle.
 * - If the character set does not contain ASCII as a subset,
 *   it is impossible to read Number or Color or Double.
 *   That is: the character set of of ValueType is unknown unless one
 *   has a table with all possible properties and the associated types.
 *
 * - The standard should have specified quoting in a CValueType
 *   so that the final "]" can be recognized without interpreting
 *   the PropIdent. (It requires that for Text and Private properties.)
 * - Compose should have been ValueType ":" CValueType
 *   so that  x:y:z  would be possible.
 * - Double and Color cannot be distinguished from Text,
 *   so their occurrence depends on the property.
 *
 * - Whether a property is a root / move / setup / gameinfo property
 *   depends on the game.
 */

int recursive = 0;
char *file_extension = ".sgf";	/* extension used in recursive call */

int optokfn = 0;
int optnokfn = 0;
int opt_noRE = 0;
int opt_noKM = 0;
int optEresign = 0;

#define BLACK_MASK  0x10000
#define WHITE_MASK  0x20000

#define MAXMOVES	10000
int moves[MAXMOVES], mvct;
int size, movect, handct, abct, awct;

int number_of_games;
int gamenr;		/* serial number (from 1) of current game */
int gtlevel;		/* nesting depth of parens */
int skipping;		/* true if not the main game line */
int nodenr;
int gametype;

int handicap, handicapseen;
int komisign, komi, komifrac, komifraclen, komiseen;
int resultsign, result, resultfrac, resultfraclen, resultseen;
int resultisresign, resultistimeout;

#define WARNPREFIXLEN 100
static char *
warn_prefix1() {
	static char buf[WARNPREFIXLEN], *p;

	p = buf;
	*p = 0;
	if (number_of_games > 1)
		p += sprintf(p, "game #%d, ", gamenr);
	if (nodenr >= 0)
		p += sprintf(p, "node #%d: ", nodenr);
	return buf;
}

#define SIZE(a)	(sizeof(a) / sizeof((a)[0]))
static char *move_props[] = {
	/* "B", "W", */
	"BL", "BM", "DO", "IT", "KO", "OB", "OW", "TE", "WL"
};
static char *setup_props[] = {
	"AB", "AW", "AE", "PL"
};
static char *root_props[] = {
	"AP", "CA", "FF", "GM", "ST", "SZ"
};
static char *gameinfo_props[] = {
	"AN", "BR", "BT", "CP", "DT", "EV", "GC", "GN", "MN", "ON",
	"OT", "PB", "PC", "PW", "RE", "RO", "RU", "SO", "TM", "US",
	"WR", "WT", "HA", "KM"
};
#define PT_MOVE		1
#define PT_SETUP	2
#define PT_ROOT		4
#define PT_GAMEINFO	8
#define PT_OTHER	16

static int get_prop_type(char *s) {
	int i;

	/* do the most frequent case by hand */
	if ((*s == 'B' || *s == 'W') && s[1] == 0)
		return PT_MOVE;
	for (i=0; i<SIZE(move_props); i++)
		if (!strcmp(s, move_props[i]))
			return PT_MOVE;
	for (i=0; i<SIZE(setup_props); i++)
		if (!strcmp(s, setup_props[i]))
			return PT_SETUP;
	for (i=0; i<SIZE(root_props); i++)
		if (!strcmp(s, root_props[i]))
			return PT_ROOT;
	for (i=0; i<SIZE(gameinfo_props); i++)
		if (!strcmp(s, gameinfo_props[i]))
			return PT_GAMEINFO;
	return PT_OTHER;
}

static void
report_on_single_game() {
	struct played_game game;
	short int mv[MAXMOVES];

	game.mv = mv;
	game.mvlen = MAXMOVES;

	if (handicapseen && handicap && mvct) {
		if (!abct)
			warn("HA[%d] but no AB", handicap);
		else if (abct != handicap)
			warn("HA[%d] but AB adds %d stones", handicap, abct);
		if (awct)
			warn("HA[%d] and AW", handicap);
	}

	/* should check that first move is by B unless there is a handicap */
	/* or PL[], or ... */
	if (!(handicapseen && handicap) && !abct && !awct && mvct) {
		if (moves[0] & WHITE_MASK)
			warn("W plays first");
	}

	if (handicapseen && handicap && abct && !awct && mvct > abct) {
		if (moves[abct] & BLACK_MASK)
			warn("B plays first after HA");
	}

	/* If a player resigns, usually his opponent made the last move */
	if (resultisresign && mvct > 0 && !optEresign) {
		if (moves[mvct-1] & resultisresign)
			warn("last move played by resigner");
	}

	/* If a player loses on time, then his opponent made the last move */
	if (resultistimeout && mvct > 0) {
		if (moves[mvct-1] & resultistimeout)
			warn("last move played by timed-out player");
	}

	playgogame(size, moves, mvct, abct+awct, &game);
}

static int
is_move(struct property *p) {
	return (p && p->val->next == NULL &&
		(!strcmp(p->id, "B") || !strcmp(p->id, "W")));
}

/* this is wrong if size > 19 */
#define PASS (('t'<<8) | 't')

static int
move_to_int(char *s) {
	if (*s == 0)
		return PASS;
	if (s[1] == 0 || s[2] != 0)
		errexit("not a valid move _%s_", s);
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
ck_equal_players(int mask) {
	int m;

	if (mvct > abct+awct+1 && (moves[mvct-2] & mask)) {
		m = mvct-abct-awct;
		warn("moves %d and %d were both played by %s",
		     m-1, m, (mask == BLACK_MASK) ? "B" : "W");
	}
}

static void
put_nodesequence(struct node *n) {
	struct property *p;
	int mask;

	while (n) {
#if 0
		/* a move should be the only property in its node */
		/* no, often a label or a time is given */
		p = n->p;
		if (is_move(p)) {
			mask = (p->id[0] == 'B') ? BLACK_MASK : WHITE_MASK;
			put_move(p->val, mask);
			ck_equal_players(mask);
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
				ck_equal_players(mask);
			}
			p = p->next;
		}
#endif
		n = n->next;
	}
}

static int
put_moves(struct propvalue *pv, int mask) {
	char *s, *t, x, y;
	int ct;

	if (!pv)
		errexit("missing setup moves value");
	s = pv->val; 	/* something like "dp:ep" */

	/* strip whitespace */
	t = s;
	while (*t) {
		if (*t == ' ' || *t == '\n' || *t == '\r')
			t++;
		else
			*s++ = *t++;
	}
	*t = 0;

	s = pv->val;
	if (!(t-s == 2 || (t-s == 5 && s[2] == ':')))
		errexit("unrecognized setup moves value");
	t -= 2;
	ct = 0;
	for (x=s[0]; x<=t[0]; x++) {
		for (y=s[1]; y <= t[1]; y++) {
			if (mvct == MAXMOVES)
				errexit("too many setup moves");
			moves[mvct++] = (x<<8) | y | mask;
			ct++;
		}
	}
	if (ct == 0)
		errexit("empty setup moves rectangle");
	return ct;
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
				abct += put_moves(pv, BLACK_MASK);
				pv = pv->next;
			}
		} else if (!strcmp(p->id, "AW")) {
			pv = p->val;
			while (pv) {
				awct += put_moves(pv, WHITE_MASK);
				pv = pv->next;
			}
		}
		p = p->next;
	}
}

static void
get_initial_stones(struct node *node) {
	get_setup_stones(node);

	/* kludge: peek into second node - some broken files have AB there */
	if (abct == 0 && node->next)
		get_setup_stones(node->next);
}

#define MAXSZ		31
#define DEFAULTSZ	19

static void
setsize(struct propvalue *pv) {
	if (!pv || pv->next)
		errexit("nonsupported SZ property"); /* nonsquare? */
	size = atoi(pv->val);
	if (size < 0 || size > MAXSZ)
		errexit("SZ[%d] out of bounds", size);
	/* playgogame.c may have a smaller limit */
}

/* HA[handicap] */
static void sethandicap(struct propvalue *pv) {
	unsigned long ha;
	char *begin, *end;

	if (!pv || pv->next)
		errexit("HA node should have a single value");
	begin = pv->val;

	ha = strtoul(begin, &end, 10);
	if (*end)
		warn("unrecognized HA value");
	if (ha < 0 || ha > 25)
		warn("unlikely handicap value %d", ha);
	handicapseen = 1;
	handicap = ha;
}
	
/* KM[komi] */
static void setkomi(struct propvalue *pv) {
	unsigned long k, kf, klen, ksgn;
	char *begin, *frac, *end;

	if (!pv || pv->next)
		errexit("KM node should have a single value");
	begin = pv->val;

	if (opt_noKM)
		return;
	
	k = kf = klen = ksgn = 0;
	if (*begin == '-') {
		ksgn = 1;
		begin++;
	} else if (*begin == '+')
		begin++;

	k = strtoul(begin, &end, 10);
	if (*end == '.') {
		frac = end+1;
		kf = strtoul(frac, &end, 10);
		klen = end-frac;
		while (klen > 0 && kf%10 == 0) {
			klen--;
			kf /= 10;
		}
	}

	if (*end == 0) {
		komi = k;
		komifrac = kf;
		komifraclen = klen;
		komisign = ksgn;
		komiseen = 1;
	} else
		warn("nonstandard KM node");
}

/*
 * RE[result] - Provides the result of the game.
 * It is MANDATORY to use the following format:
 * "0" (zero) or "Draw" for a draw (jigo),
 * "B+" ["score"] for a black win and
 * "W+" ["score"] for a white win
 * Score is optional (some games don't have a score e.g. chess).
 * If the score is given it has to be given as a real value,
 * e.g. "B+0.5", "W+64", "B+12.5"
 * Use "B+R" or "B+Resign" and "W+R" or "W+Resign" for a win by
 * resignation. Applications must not write "Black resigns".
 * Use "B+T" or "B+Time" and "W+T" or "W+Time" for a win on time,
 * "B+F" or "B+Forfeit" and "W+F" or "W+Forfeit" for a win by forfeit,
 * "Void" for no result or suspended play and
 * "?" for an unknown result.
 */
static void setresult(struct propvalue *pv) {
	unsigned long r, rf, rlen, rsgn;
	char *begin, *frac, *end, who;

	if (!pv || pv->next)
		errexit("RE property should have a single value");
	begin = pv->val;

	if (opt_noRE)		/* don't check RE property */
		return;
	
	/* RE[?] or RE[Void] */
	if (!strcmp(begin, "?") || !strcmp(begin, "Void"))
		return;

	if (!strcmp(begin, "Unfinished") ||
	    !strncmp(begin, "Game suspended", 14) ||
	    !strcmp(begin, "Both lost") ||
	    !strcmp(begin, "Not played")) {
// Not according to FF[4], but ..
//		warn("RE should perhaps have 'Void'");
		return;
	}

	/* RE[0] */
	if (!strcmp(begin, "0") || !strcmp(begin, "Draw") ||
// Not according to FF[4], but ..
	    !strcmp(begin, "Jigo")) {
		result = resultfrac = resultfraclen = 0;
		resultseen = 1;
		return;
	}

	if (!strcasecmp(begin, "J") || !strcasecmp(begin, "Jigo") ||
	    !strcasecmp(begin, "Draw")) {
		warn("RE should have '0' or 'Draw'");
		return;
	}

	r = rf = rlen = rsgn = 0;

	/* RE[B+1.5] */
	who = *begin++;
	if (who != 'B' && who != 'W')
		errexit("RE property does not start with B or W");
	if (who == 'B')
		rsgn = 1;
	if (*begin++ != '+')
		errexit("RE property should have '+' following '%c'",
			begin[-2]);
	if (*begin == 0)
		return;
	if (!strcmp(begin, "R") || !strcmp(begin, "Resign")) {
		/* resultisresign holds mask of resigner */
		resultisresign = ((who == 'W') ? BLACK_MASK : WHITE_MASK);
		return;
	}
	if (!strcmp(begin, "T") || !strcmp(begin, "Time")) {
		/* resultistimeout holds mask of resigner */
		resultistimeout = ((who == 'W') ? BLACK_MASK : WHITE_MASK);
		return;
	}
	if (!strcmp(begin, "F") || !strcmp(begin, "Forfeit") ||
	    /* not FF[4] */
	    !strcmp(begin, "bye"))
		return;
	/* W+0 is ok. Also W+J or W+jigo? */
	r = strtoul(begin, &end, 10);
	if (*end == '.') {
		frac = end+1;
		rf = strtoul(frac, &end, 10);
		rlen = end-frac;
		while (rlen > 0 && rf%10 == 0) {
			rlen--;
			rf /= 10;
		}
	}
	if (*end)
		errexit("nonstandard RE property '%s'", pv->val);

	result = r;
	resultfrac = rf;
	resultfraclen = rlen;
	resultsign = rsgn;
	resultseen = 1;
}

static void
check_KM_vs_RE() {
	if (komifraclen != resultfraclen)
		goto bad;
	if (resultsign == komisign) {
		if (komifrac != resultfrac)
			goto bad;
	} else if (komifrac || resultfrac) {
		unsigned long pow = 1;
		int i;
		for (i=0; i<komifraclen; i++)
			pow *= 10;
		if (komifrac + resultfrac != pow)
			goto bad;
	}
	return;
bad:
	if (komifraclen == 2 || resultfraclen == 2) {
		/* warn? chinese convention - give stones [zi] */
		if ((komifrac == 25 || komifrac == 75) &&
		    (resultfrac == 25 || resultfrac == 75))
			return;

		warn("KM and RE do not differ by an integer "
		     "(and are not both x.25 or x.75)");
		return;	/* this occurs often */
	}

	warn("KM and RE do not differ by an integer");
}

struct {
	char *id;
	void (*fn)(struct propvalue *);
} rootprops[] = {
	{ "SZ", setsize },
	{ "KM", setkomi },
	{ "RE", setresult },
	{ "HA", sethandicap },
};

static void
get_rootnode_properties(struct node *node) {
	struct property *p = node->p;
	int i;

	while (p) {
		for (i=0; i<SIZE(rootprops); i++)
			if (!strcmp(p->id, rootprops[i].id))
				(rootprops[i].fn)(p->val);
		p = p->next;
	}
}

static void
init_single_game(struct gametree *g) {
	gamenr++;
	size = DEFAULTSZ;
	mvct = abct = awct = 0;
	handicapseen = handicap = 0;
	resultseen = komiseen = 0;
	resultisresign = resultistimeout = 0;

	get_rootnode_properties(g->nodesequence); /* SZ, KM, RE */
	if (komiseen && resultseen)
		check_KM_vs_RE();

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

/* Only one of each property is allowed in a node. */
static void
check_node_seq(struct node *node) {
	struct property *p, *q;
	int type, types_seen;
	char *moveprop = NULL;

	while(node) {
		nodenr++;
		p = node->p;
		if (!p || !p->next)
			goto next;	/* at most one property here */
		types_seen = 0;
		while (p) {
			type = get_prop_type(p->id);
			types_seen |= type;
			if ((type & PT_MOVE) && !moveprop)
				moveprop = p->id;
			q = p->next;
			while (q) {
				if (!strcmp(p->id, q->id))
					errexit("duplicated %s tag", p->id);
				q = q->next;
			}
			p = p->next;
		}
		if ((types_seen & PT_MOVE) && (types_seen & PT_SETUP))
			errexit("move and setup properties in the same node");
		if (nodenr == 0 && moveprop)
			warn("bad style: move property %s in root node",
				moveprop);
	next:
		node = node->next;
	}
}

static void check_gametree_seq(struct gametree *g);

static void
check_gametree(struct gametree *g) {
	gtlevel++;
	if (gtlevel == 1) {
		gamenr++;
		nodenr = -1;
	}
	check_node_seq(g->nodesequence);
	check_gametree_seq(g->firstchild);
	gtlevel--;
}

static void
check_gametree_seq(struct gametree *g) {
	while (g) {
		check_gametree(g);
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

	nodenr = -1;
	gamenr = gtlevel = 0;

	warn_prefix = warn_prefix1;	/* give node # */
	check_gametree_seq(g);

	nodenr = -1;
	gamenr = gtlevel = skipping = 0;
	put_gametree_sequence(g);
ret:
	warn_prefix = 0;
	have_jmpbuf = 0;
}

void
do_input(const char *fn) {
	int errct0 = errct;
	int warnct0 = warnct;
	int ok;

	do_stdin(fn);

	ok = (errct == errct0 && warnct == warnct0);
	if (fn == NULL)
		fn = "<stdin>";
	if (optokfn && ok)
		printf("%s\n", fn);
	if (optnokfn && !ok)
		printf("%s\n", fn);
}

int
main(int argc, char **argv){

	progname = "sgfcheck";
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
		if (!strcmp(argv[1], "-nokfn")) {
			optnokfn = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-okfn")) {
			optokfn = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-r")) {
			recursive = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-noKM")) {
			opt_noKM = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-noRE")) {
			opt_noRE = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-Eresign")) {
			optEresign = 1;
			goto next;
		}
#if 0
		if (!strcmp(argv[1], "-t")) {
			tracein = 1;
			goto next;
		}
#endif
		errexit("Unknown option %s\n\n"
	"Call: sgfcheck [files]\n"
	"or:   sgfcheck -r [-e .sgf] [files/dirs]\n",
			argv[1]);
	next:
		argc--; argv++;
	}

	if (argc == 1) {
		ignore_errors = 0;	/* no jmpbuf here */
		if (recursive)
			errexit("refuse to read from stdin when recursive");
		do_input(NULL);
		goto ret;
	}

	while (argc > 1) {
		ignore_errors = 1;	/* we check all input files */
		do_infile(argv[1]);
		argc--; argv++;
	}
ret:
	return errct ? -1 : warnct ? 1 : 0;
}
