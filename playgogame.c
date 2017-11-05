/*
 * playgogame.c - aeb, 2013-05-31
 *
 * Convert a go game presented as a series of moves into a
 * (more compact) form that allows quick searching, by having
 * the captures made explicit, and telling for each move
 * whether the positions involved will change again later.
 *
 * Input: integer array moves[].
 *  Low order 8+8 bits: coordinates. High order 2 bits: color bits.
 *
 * Output: short integer array mv[].
 *  Low order 10 bits: coordinates 32*x+y with x,y in 1..SZ
 *  Next two bits: color
 *  Higher bits: PG_PASS, PG_CAPTURE, PG_PERMANENT.
 *
 * So, the main object is to convert the moves into something where
 * no computation is required in order to see whether stones are captured.
 * A secondary object is to indicate whether this board position will
 * change again later.
 *
void playgogame(int *moves, int mvct, int initct, struct played_game *pg);
 *
 */

#include "errexit.h"
#include "playgogame.h"

static struct played_game *pgg;

/* board size is at most 31 (5 bits used for coord) */
#define MAXSZ	31
#define SZ	19	/* used only in definition of chains[] */
#define D	(MAXSZ+1)
static int sz, sz1;	/* sz and sz+1, probably 19 and 20 */

#define BOARDSIZE	(D*(D+1))
#define POS(i,j)	((i)*D+(j))	/* i,j in 1..19 */
#define X(pos)		((pos)/D)
#define Y(pos)		((pos)%D)

static const int dirs[4] = { -1, 1, -D, D };

#define EMPTY	0
#define BLACK	1
#define WHITE	2
#define BORDER	3

static unsigned char board[BOARDSIZE];
static int last_change[BOARDSIZE];	/* record last change for each pos */
static int current_chain[BOARDSIZE];	/* defined for nonempty positions */

/* probably insufficient for large boards... */
#define CHAINMAX	500
struct chain {
	int liberties;		/* sum of liberties for all stones */
	int sz;			/* number of stones */
	short int stones[SZ*SZ];/* actual stones */
} chains[CHAINMAX];		/* < 500 kB */
int chainct;

static void init(int size) {
	int i, d;

	/*
	 * it is easy to support larger sizes, but do they occur?
	 * on the other hand, 5, 9, 13, 17 do occur
	 */
	if (size > MAXSZ || size > SZ)
		errexit("unsupported board size %d", size);
	sz = size;
	sz1 = d = size+1;

	for (i=0; i < BOARDSIZE; i++)
		board[i] = EMPTY;
	for (i=0; i < d; i++)
		board[i] = BORDER;
	for (i=d; i <= D*d; i += D)
		board[i] = BORDER;
	for (i=D; i <= D*d; i += D)
		board[i] = BORDER;
	for (i=1; i < d; i++)
		board[D*d+i] = BORDER;

	for (i=0; i < BOARDSIZE; i++)
		last_change[i] = 0;

	chainct = 0;

	for (i=0; i<3; i++)
		pgg->counts[i] = 0;
	pgg->mvct = 0;
}

static inline void
add_mv(short int m) {
	if (pgg->mvct == pgg->mvlen)
		errexit("played_game array overflow");
	pgg->mv[pgg->mvct++] = m;
}

static void
add_move(int s) {
	int color;
	short int m;

	color = board[s];
	m = s | (color << 10);

	pgg->counts[0]++;

	last_change[s] = pgg->mvct;
	add_mv(m);
}

static void
add_antimove(int s) {
	int color;
	short int m;

	color = board[s];
	board[s] = EMPTY;
	m = s | (color << 10) | PG_CAPTURE;

	pgg->counts[color]++;

	last_change[s] = pgg->mvct;
	add_mv(m);
}

static void
add_pass(int color) {
	short int m;

	m = (color << 10) | PG_PASS;
	pgg->counts[0]++;	/* count as a move */
	add_mv(m);
}

#include <stdio.h>

/* merge ch1 into probably larger ch */
static int
merge_chains(int ch, int ch1) {
	struct chain *chp, *chp1;
	int i, j, s;

	chp = chains + ch;
	chp1 = chains + ch1;

	chp->liberties += chp1->liberties;
	i = chp->sz;
	for (j = 0; j < chp1->sz; j++) {
		chp->stones[i++] = s = chp1->stones[j];
		current_chain[s] = ch;
	}
	chp->sz = i;

	return ch;
}

static void
remove_chain(int ch) {
	struct chain *chp;
	int i, j, s, t;

	chp = chains + ch;

	for (j = 0; j < chp->sz; j++) {
		s = chp->stones[j];
		add_antimove(s);

		for (i=0; i<4; i++) {
			t = s + dirs[i];
			if (board[t] == WHITE || board[t] == BLACK)
				chains[current_chain[t]].liberties++;
		}
	}
}

static void check_retake_in_ko(int movenr) {
	int hist[4], i;

	/* test situation:
	   B plays at a and captures a single stone at b
	   W plays at b and captures a single stone at a */
	if (pgg->mvct < 4)
		return;
	for (i=0; i<4; i++)
		hist[i] = pgg->mv[pgg->mvct-4+i];
	if ((hist[0] & PG_CAPTURE) || (hist[2] & PG_CAPTURE) ||
	    !(hist[1] & PG_CAPTURE) || !(hist[3] & PG_CAPTURE))
		return;
	if ((hist[0] & 0x3ff) == (hist[3] & 0x3ff) &&
	    (hist[1] & 0x3ff) == (hist[2] & 0x3ff))
		errexit("move %d: illegal ko recapture", movenr);
}

static void do_move(int color, int x, int y, int movenr) {
	int other_color, xy, i, nbr, *cc, ch, ch2, ll;
	unsigned char *p;
	struct chain *cp, *cp2;

	if (x == sz1 && y == sz1) {
		add_pass(color);
		return;
	}

	/* sometimes 'tt' is also used on smaller boards */
	if (x == 20 && y == 20 && sz1 < 20) {
		add_pass(color);
		return;
	}

	if (x < 1 || x > sz || y < 1 || y > sz)
		errexit("move %d: bad move cordinates %d,%d", movenr, x, y);
	xy = POS(x,y);

	p = &(board[xy]);
	if (*p != EMPTY)
		errexit("move %d: play on nonempty position", movenr);
	*p = color;

	add_move(xy);

	/* should re-use chains, so that SZ*SZ suffices */
	if (chainct == CHAINMAX)
		errexit("CHAINMAX overflow");

	cc = &(current_chain[xy]);
	*cc = ch = chainct++;
	cp = &(chains[ch]);
	cp->liberties = 0;
	cp->sz = 1;
	cp->stones[0] = xy;

	other_color = (BLACK+WHITE-color);

	for (i=0; i<4; i++) {
		nbr = p[dirs[i]];
		if (nbr == EMPTY)
			cp->liberties++;
		else if (nbr != BORDER) {
			ch2 = cc[dirs[i]];
			cp2 = &(chains[ch2]);
			ll = --(cp2->liberties);

			if (nbr == other_color) {
				if (ll == 0)
					remove_chain(ch2);
				check_retake_in_ko(movenr);
			} else {
				if (ch2 != ch) {
					ch = merge_chains(ch2, ch);
					cp = &(chains[ch]);
				}
			}
		}
	}
	if (cp->liberties == 0) {
		if (cp->sz == 1) {
			/* this seems to be forbidden in all rulesets */
			errexit("move %d: suicide", movenr);
		} else {
			/* maybe warn(), depending on the rules,
			   but so far I have not seen any examples */
			errexit("move %d: mass suicide", movenr);
			remove_chain(ch);
		}
	}
}

static void check_for_cycles() {
	int i, j, k, m, ct, nri, nrj;
	int diff[2*SZ*SZ], diffct;

	/* we already checked for ko - check here for longer cycles;
	   this is allowed under Japanese but not under Chinese rules
	   (so only warn) */

	ct = pgg->mvct;
	nri = 0;

	for (i = 0; i < ct; i++) {
		if (pgg->mv[i] & PG_CAPTURE)
			continue;
		nri++;
		diffct = 0;

		/* see whether the situation at the start of move i
		   occurs later */
		nrj = nri-1;
		for (j=i; j<ct; j++) {
			m = pgg->mv[j];
			if (!(m & PG_CAPTURE))
				nrj++;
			for (k=0; k<diffct; k++) {
				/* same color, same position? */
				if ((diff[k] & 0xfff) == (m & 0xfff)) {
					diff[k] = diff[--diffct];
					goto check;
				}
			}
			if (m & PG_PERMANENT)
				break;
			diff[diffct++] = m;
			continue;
		check:
			if (diffct)
				continue;
			if (j+1 < ct && (pgg->mv[j+1] & PG_CAPTURE))
				continue;
			warn("cycle: position after move %d "
			     "equals that after move %d",
			     nrj, nri-1);
			return;
		}
	}
}

void playgogame(int size, int *moves, int mvct, int initct,
		struct played_game *pg) {
	int i, m, s, ct;
	short int color;
	unsigned char x, y;

	pgg = pg;

	init(size);

	for (i=0; i<mvct; i++) {
		m = moves[i];
		color = (m >> 16);	/* BLACK == BLACK_MASK >> 16 */
		x = (m >> 8) ;
		y = m;
		x -= ('a' - 1);
		y -= ('a' - 1);
		do_move(color, x, y, (i >= initct) ? i-initct+1 : 0);
	}

	ct = pgg->mvct;
	for (i = 0; i < ct; i++) {
		m = pgg->mv[i];
		s = (m & 0x3ff);
		if (last_change[s] == i)
			pgg->mv[i] = m | PG_PERMANENT;
	}

	check_for_cycles();
}
