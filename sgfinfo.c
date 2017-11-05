/*
 * Parse sgf files - aeb, 991230
 * Added md5 output - aeb, 2013-05-12
 *
 * Call: sgfinfo [options] [--] [files]
 * Read stdin or files, write to stdout
 * -nf: suppress printing of the filename
 * -i: ignore errors (give message and continue with next file)
 * -t: trace: print input as it is being read
 * --: end option list (needed if a file name starts with '-')
 *
 * Select in a list of input files
 * (print inputfilenames satisfying the given restrictions):
 * -h#: handicap was # (-h#-, -h-#, -h#-# as for moves)
 * -m#: game has # moves
 * -m#-, -m-#, -m#-#: movect is at least, at most, between ... and ...
 * -p120C14: move 120 was played on C14 (multiple options can be given)
 * -p1-12cf,dd: positions cf,dd were played between moves 1 and 12
 * -p-cf,dd: positions cf,dd were played between begin and end
 * -Bp, -Wp idem, with back/white move
 * -pat=file.sgf read file with AE, AB, AW restrictions (this closes stdin)
 * -player: give player
 *
 * Game selection in an input file with multiple games:
 * -x, -x#: report, and if # given select, game number
 *
 * Print info or signature:
 * -N: print number of games in a collection
 * -m: print number of moves
 * -k: print first time the given pattern occurs
 * -md5: print md5 signature of this long string
 * -can: print minimal md5 signature of 8 rotated and reflected versions
 * -Ds20,40,60: print Dave Dyer type signature (the numbers may vary)
 * -DsA: equivalent to -s20,40,60
 * -DsB: equivalent to -s31,51,71
 * -DnC: print normalized Dyer type signature
 * -capt: print number of B and W captures
 * -Bcapt, -Wcapt: print number of black/white captures
 * -winner, -loser: print winning/losing player, if any
 *
 * Print moves:
 * -M: print moves themselves, preceded by number
 * -s: (strip) print the sequence of moves as a single long string
 *
 * Print position:
 * -P#: output sgf file with the position after move #
 *  (default: after the final move)
 *
 * Operations (for -md5, -can, -M, -s*, -P):
 * -trunc#, -trunc-#: truncate to # moves, or delete the final # moves
 * -rot#: idem after rotation left about # * 90 degrees, #=0,1,2,3
 *  (default: #=1)
 * -tra#: idem after one of 8 transformations (even: rotation, odd: reflection)
 *  (default: #=1; -rotM is equivalent to -traN for N=2*M;
 *   if N=2*M+1, then -traN is -rotM followed by flip around hor line)
 *
 * Operations (for -pat=file.sgf):
 * -swapcolors: interchange black and white in pattern file
 * -alltra: match for all 16 transformations of pattern file
 *
 * Print properties:
 * -prop: print the property labels, without their values
 * -propXY: print all occurrences of XY[...] including its value
 * --XY: synonym for -propXY
 *
 *
 * Output is not yet well-defined for passes, or handicap HA[#]
 * This is not a general rotator: set AB[] and labels LB[] should
 * also be rotated - see sgftf for that.
 * Pattern search fails after a capture (but works in the db version)
 *
 *
 * -ref=file: use file as a reference, and let @ mean "same as in file"
 * in contexts like -m@ or -propDT=@ etc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/md5.h>
#include "ftw.h"
#include "errexit.h"
#include "readsgf.h"
#include "sgfinfo.h"
#include "playgogame.h"
#include "tests.h"

#ifdef READ_FROM_DB

#include "sgfdbinput.h"
#include "sgfdb.h"
#define DB "db"
int db = 1;
int didplay = 1;
char *file_extension = ".sgfdb";

#else

#include "sgffileinput.h"
#define DB ""
int db = 0;
int didplay = 0;
char *file_extension = ".sgf";

#endif

int recursive = 0;

void report_on_single_game(void);
int reportedfn = 0;
int needplay = 0;
int optb = 1;
int optnf = 0;

int seloptct;
int infooptct;

int argct;		/* number of file args */
int number_of_games;
int optE, optEmin = -1, optEmax = -1, okgames;

/* info-request options */
int optx = 0;		/* report game number */
int optM = 0;		/* report moves and/or color */
int opts = 0;		/* print the sequence of moves as a single string */

int optP = 0;
int optPmvct;

/* transformations */
int opttra = 0;		/* specify transformation */
int opttrunc = 0;	/* truncate */
int trunclen;

/* selection options */
int optxx = 0;		/* which game to report on */

#define PASS (('t'<<8) | 't')

#define MAXMOVES	10000
int size, moves[MAXMOVES], mvct, initct, movect, handct;
int bcaptct, wcaptct;

/* defined when didplay = 1 */
int extmoves[MAXMOVES], extmvct;	/* all including captures */

/* selection criteria */
#define BLACK_MASK	0x10000
#define WHITE_MASK	0x20000
#define EMPTY_MASK	0x40000

#define MAXPLAYS	1000
struct mp { int nrmin, nrmax, pos, color; } movesplayed[MAXPLAYS];
int movesplayedct;

#define SZ 19
int pattern[MAXPLAYS], patternct, patternbwct, patternsize, patindex;
int patternboard[16*SZ*SZ];
int printpatternindex = 0;
int swapcolors = 0;
int alltra = 0;

int gamenr;		/* serial number (from 1) of current game */

/* fill extmoves[] array, given the moves[] array */
static void do_play() {
#ifdef READ_FROM_DB
	errexit("do_play called unnecessarily");
#else
	struct played_game game;
	short int mv[MAXMOVES], *mvp;
	int i;

	game.mv = mv;
	game.mvlen = MAXMOVES;
	playgogame(size, moves, mvct, initct, &game);
	bcaptct = game.counts[1];
	wcaptct = game.counts[2];
	extmvct = game.mvct;

	mvp = mv;
	for (i=0; i<extmvct; i++)
		extmoves[i] = mvp[i];
#endif
}

/* update mvct and extmvct, given movect */
static void truncate_to(int m) {
	int i, n;

	mvct = movect+initct;

	/* If we need to play, we did so */
	if (!needplay)
		return;

	n = 0;
	for (i=0; i<extmvct && n <= movect; i++) {
		if (i >= initct && !(extmoves[i] & PG_CAPTURE))
			n++;
	}
	if (n == movect+1)
		extmvct = i-1;

}

static int lowercasemove(char *s, int *mv) {
	if ((*s >= 'a' && *s <= 's' && s[1] >= 'a' && s[1] <= 's')
	    || (*s == 't' && s[1] == 't')) {
		*mv = ((*s) << 8) + s[1];
		return 1;
	}
	return 0;
}

/* return number of bytes parsed - 0 is an error */
static int uppercasemove0(char *s, int *mv, int outerr) {
	char *msg, *se, x, y;
	int n;

	se = s;
	if (*s >= 'A' && *s <= 'T') {
		x = (*s - 'A' + 'a');
		if (*s == 'I') {
			msg = "I is skipped in the board coordinates";
			goto errleave;
		}
		if (*s > 'I')
			x--;
		n = strtol(s+1, &se, 10);
		if (se == s+1) {
			msg = "missing number in -p option";
			goto errleave;
		}
		if (n <= 0 || n > 19) {
			msg = "number in -p option not in 1..19";
			goto errleave;
		}
		y = 'a' + 19 - n;
		*mv = (x << 8) + y;
	}
	return se - s;

errleave:
	if (outerr)
		errexit(msg);
	return 0;
}

static int letdigsmove(char *s, int *mv) {
	int n;

	n = uppercasemove0(s, mv, 0);
	return (n > 0 && s[n] == 0);
}

static char *uppercasemove(char *s, int *mv) {
	int n;

	n = uppercasemove0(s, mv, 1);
	return s+n;
}

static char *setnrplayrestriction(struct mp *mp, char *s){
	int min, max;

	s = getminmax(s, &min, &max);
	mp->nrmin = min;
	mp->nrmax = max;
	return s;
}

/* expect pos like C14 or cf */
static char *setposplayrestriction(struct mp *mp, char *s){
	char *se;
	int mv;

	mv = 0;
	if (lowercasemove(s, &mv)) {
		s += 2;
	} else {
		se = uppercasemove(s, &mv);
		if (se == s) {
			char msg[100];
			msg[0] = 0;
			if ((*s >= ' ' && *s < 'A') ||
			    (*s > 'Z' && *s < 'a') ||
			    (*s > 'z' && *s < 0177))
				sprintf(msg, " (bad char '%c')", *s);
			errexit("unrecognized move in -p option%s", msg);
		}
		s = se;
	}

	mp->pos = mv;

	return s;
}

static void setplayrestrictions(char *s, int mask){
	int n = movesplayedct;
	struct mp *mp;

	while (*s) {
		if (movesplayedct == MAXPLAYS)
			errexit("MAXPLAYS overflow");
		mp = &movesplayed[movesplayedct];
		mp->color = mask;			/* color */

		s = setnrplayrestriction(mp, s);	/* nrmin, nrmax */
		s = setposplayrestriction(mp, s);	/* pos */
		if (*s == ',')
			s++;

		if (mp->nrmin == UNSET && mp->nrmax == UNSET &&
		    movesplayedct > n) {
			mp->nrmin = (mp-1)->nrmin;
			mp->nrmax = (mp-1)->nrmax;
		}

		movesplayedct++;
	}
}

/* if there are many move restrictions, a pattern search is more efficient */
static int nosuchmove(struct mp *mp) {
	int min, max, pos, bw, i, m, n, mask, result;

	min = mp->nrmin;
	max = mp->nrmax;
	pos = mp->pos;
	bw = mp->color;

	result = (pos | bw);
	mask = (bw ? ~0 : 0xffff);

	/* min, max count from 1 */
	n = 0;
	for (i = 0; i < mvct; i++) {
		m = moves[i];
		if (i >= initct)
			n++;
		if ((m & mask) != result)
			continue;
		if ((min == UNSET || n >= min) && (max == UNSET || n <= max))
			return 0;
	}
	return 1;
}

/* conversion can be avoided by again using a board with border */
/* from (x+1)*(MAXSZ+1)+(y+1) to x*SZ+y -- no error checking */
#define MAXSZ	31
static int move_to_index(int mv) {
	int x, y;

	x = mv/(MAXSZ+1);
	y = mv%(MAXSZ+1);
	return (x-1)*SZ+(y-1);
}

#define FAILURE (-1)

static int findpattern0(int a) {
	int n, i, pos, ipos, m, need, mc;
	int *pb;

	pb = patternboard + a*SZ*SZ;
	need = patternbwct;
	n = 0;
	for (i=0; i<extmvct; i++) {
		pos = extmoves[i];
		if (i >= initct && !(pos & PG_CAPTURE))
			n++;
		if (pos & PG_PASS)
			continue;
		ipos = move_to_index(pos & 0x3ff);
		if (ipos < 0 || ipos >= SZ*SZ)
			errexit("out-of-board move %d", i+1);
		m = pb[ipos];
		if (!m)
			continue;
		if (m == EMPTY_MASK) {
			if (pos & PG_CAPTURE) {
				if (--need == 0)
					return n;
			} else {
				if (pos & PG_PERMANENT)
					return FAILURE;
				need++;
			}
			continue;
		}
		/* pattern array has BLACK_MASK (bit 16)
		   move array has color bit (bit 10) */
		mc = (pos << 6) & 0x30000;	/* move color */
		if (pos & PG_CAPTURE) {
			if (pos & PG_PERMANENT)
				return FAILURE;
			if (mc == m)
				need++;
		} else {
			if (mc == m) {
				if (--need == 0)
					return n;
			} else if (pos & PG_PERMANENT)
				return FAILURE;
		}
	}
	return FAILURE;
}

static int findpattern() {
	int m, n, nmin;

	if (!alltra)
		return findpattern0(0);
	nmin = -1;
	for (m=0; m<16; m++) {
		n = findpattern0(m);
		if (n >= 0 && (nmin < 0 || n < nmin))
			nmin = n;
	}
	return nmin;
}

static void transform0(int *xx, int *yy, int tra, int size) {
	int x, y, xn, yn;
	int sz = size-1;

	x = *xx;
	y = *yy;

	switch (tra) {
	case 0:
		xn = x; yn = y; break;
	case 1:
		xn = x; yn = sz-y; break;
	case 2:
		xn = y; yn = sz-x; break;
	case 3:
		xn = y; yn = x; break;
	case 4:
		xn = sz-x; yn = sz-y; break;
	case 5:
		xn = sz-x; yn = y; break;
	case 6:
		xn = sz-y; yn = x; break;
	case 7:
		xn = sz-y; yn = sz-x; break;
	default:
		errexit("impossible tra arg in transform0()");
	}

	*xx = xn;
	*yy = yn;
}

static void transform(int *xx, int *yy, int tra) {
	int x = *xx, y = *yy;
	int sz = size-1;

	if (x == '?' && y == '?')	/* possibly from Dyer sig */
		return;			/* nothing to rotate */
	if (x == 't' && y == 't')	/* pass */
		return;
	if (x == 'z' && y == 'z')	/* tenuki? */
		return;
	x -= 'a';
	y -= 'a';

	if (x == sz+1 && y == sz+1)		/* pass */
		return;
	if (x < 0 || x > sz || y < 0 || y > sz)
		errexit("off-board move %c%c", x+'a', y+'a');

	transform0(&x, &y, tra, size);

	*xx = x + 'a';
	*yy = y + 'a';
}

/* needs room for 2 bytes */
static void getmovetra(int m, char *buf, int tra) {
	int n, x, y;

	if (m < 0 || m >= mvct)
		x = y = '?';
	else {
		n = moves[m];
		x = ((n>>8) & 0xff);
		y = (n&0xff);
	}
	transform(&x,&y,tra);
	buf[0] = x;
	buf[1] = y;
}

/* needs room for 1 byte */
static char getmovelet(int m) {
	int n;

	n = 0;
	if (m >= 0 && m < mvct)
		n = (moves[m] >> 16);
	return ((n == 2) ? 'W' : (n == 1) ? 'B' : 'X');
}

/* this includes initial moves */
static void outmove(int m) {
	char x[2];

	getmovetra(m, x, opttra);
	putchar(x[0]);
	putchar(x[1]);
}

static void outmovec(int m) {
	putchar(getmovelet(m));
}

static void outmovex(int m) {
	putchar(getmovelet(m));
	outmove(m);
}

/* output position after move m, possibly after a transformation */
static void outpos_at(int m) {
#define EMPTY	0
#define BLACK	1
#define	WHITE	2
	int n, i, pos, ipos, ct, x, y;
	int pb[SZ*SZ];

	for (i=0; i<SZ*SZ; i++)
		pb[i] = EMPTY;
	n = 0;
	for (i=0; i<extmvct; i++) {
		pos = extmoves[i];
		if (i >= initct && !(pos & PG_CAPTURE)) {
			n++;
			if (n > m)
				break;
		}
		if (pos & PG_PASS)
			continue;
		ipos = move_to_index(pos & 0x3ff);
		if (ipos < 0 || ipos >= SZ*SZ)
			errexit("out-of-board extmove[%d]", i);
		if (pos & PG_CAPTURE)
			pb[ipos] = EMPTY;
		else
			pb[ipos] = ((pos >> 10) & 3);
	}

	printf("(;");
	if (size != 19)
		printf("SZ[%d]", size);
	ct = 0;
	for (i=0; i<SZ*SZ; i++) {
		if (pb[i] == BLACK) {
			if (!ct++)
				printf("AB");
			x = (i/SZ);
			y = (i%SZ);
			transform0(&x, &y, opttra, size);
			printf("[%c%c]", x+'a', y+'a');
		}
	}
	ct = 0;
	for (i=0; i<SZ*SZ; i++) {
		if (pb[i] == WHITE) {
			if (!ct++)
				printf("AW");
			x = (i/SZ);
			y = (i%SZ);
			transform0(&x, &y, opttra, size);
			printf("[%c%c]", x+'a', y+'a');
		}
	}
	printf(")\n");
}

/* should also be able to give a pointer */
static int get_filename(char *buf, int len) {
	if (strlen(infilename) >= len)
		return 1;	/* overflow */
	strcpy(buf, infilename);
	return 0;
}

static int get_Dyer_sign(char *choice, char *buf, int len) {
	char *se;
	int n;

	while (*choice && len >= 3) {
		n = strtol(choice, &se, 10);
		if (se == choice)
			errexit("digit expected in Dyer string");
		choice = se;
		if (*choice == ',')
			choice++;
		getmovetra(n-1, buf, opttra);
		buf += 2;
		len -= 2;
	}
	if (*choice || len <= 0)
		return 1;	/* overflow */
	*buf = 0;
	return 0;
}

static int get_Dyer_sigA(char *buf, int len) {
	return get_Dyer_sign("20,40,60", buf, len);
}

static int get_Dyer_sigB(char *buf, int len) {
	return get_Dyer_sign("31,51,71", buf, len);
}

/* complete Dyer */
static int get_Dyer_sigC(char *buf, int len) {
	return get_Dyer_sign("20,40,60,31,51,71", buf, len);
}

/* idem, normalized */
#define MAX_NSIG_LEN	1000
#define MAX_NDYER_LTH	(2*MAX_NSIG_LEN+1)
static int get_nDyer_sign(char *choice, char *buf, int len) {
	int choices[MAX_NSIG_LEN], choicect;
	char ndyer[MAX_NDYER_LTH], ndyert[MAX_NDYER_LTH];
	char *se, *buft;
	int tra, i, n;

	choicect = 0;
	while (*choice) {
		n = strtol(choice, &se, 10);
		if (se == choice)
			errexit("digit expected in Dyer string");
		choice = se;
		if (*choice == ',')
			choice++;
		if (choicect == MAX_NSIG_LEN)
			errexit("too long Dyer signature requested");
		choices[choicect++] = n;
	}

	for (tra = 0; tra < 8; tra++) {
		buft = ndyert;
		for (i = 0; i < choicect; i++) {
			n = choices[i];
			getmovetra(n-1, buft, tra);
			buft += 2;
		}
		*buft = 0;
		if (tra == 0 || strcmp(ndyer, ndyert) > 0)
			strcpy(ndyer, ndyert);
	}

	if (2*choicect+1 > len)
		return 1;	/* overflow */

	strcpy(buf, ndyer);
	return 0;
}

static int get_nDyer_sigA(char *buf, int len) {
	return get_nDyer_sign("20,40,60", buf, len);
}

static int get_nDyer_sigB(char *buf, int len) {
	return get_nDyer_sign("31,51,71", buf, len);
}

static int get_nDyer_sigC(char *buf, int len) {
	return get_nDyer_sign("20,40,60,31,51,71", buf, len);
}

/* move is like Dyer but with output separated by commas */
/* also accept a dash */
/* like get_move(), but including color */
/* mv: do move, c: do color (booleans) */
/* initflag: 0: moves, 1: init, 2: both */
static int get_movemc(char *choice, char *buf, int len, int mv, int c,
		      int initflag) {
	char *se;
	int n, nn, flag;
	int offset, nmax;

	offset = 0;
	if (initflag == 0)
		offset = initct;
	nmax = ((initflag == 0) ? movect : ((initflag == 1) ? initct : mvct));

	flag = 0;
	while (*choice && len >= 3+flag) {
		n = strtol(choice, &se, 10) + offset;
		if (se == choice)
			fatalexit("get_move: digit expected in move number"
				  ", got %s", se);
		choice = se;
		nn = n;
		if (*choice == '-') {
			choice++;
			nn = strtol(choice, &se, 10) + offset;
			if (se == choice) {
				if (*choice)
					fatalexit("get_move: digit expected"
						  " after '-'");
				nn = nmax;
			}
			choice = se;
		}
		if (*choice == ',')
			choice++;
		while (n <= nn && len >= 3+flag) {
			if (flag) {
				*buf++ = ',';
				len--;
			}
			if (n > nmax) {
				/* needed only for initflag==1 */
				if (c) {
					*buf++ = 'X';
					len--;
				}
				if (mv) {
					*buf++ = '?';
					*buf++ = '?';
					len -= 2;
				}
			} else {
				if (c) {
					*buf++ = getmovelet(n-1);
					len--;
				}
				if (mv) {
					getmovetra(n-1, buf, opttra);
					buf += 2;
					len -= 2;
				}
			}
			n++;
			flag = 1;
		}
	}
	if (*choice || len <= 0)
		return 1;	/* overflow */
	*buf = 0;
	return 0;
}

/* move, no color */
static int get_move(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 1, 0, 0);
}

/* both move and color */
static int get_movex(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 1, 1, 0);
}

/* color only */
static int get_movec(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 0, 1, 0);
}

/* move, no color, init moves */
static int get_movei(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 1, 0, 1);
}

/* both move and color, init moves */
static int get_moveix(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 1, 1, 1);
}

/* color only, init moves */
static int get_moveic(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 0, 1, 1);
}

/* move, no color, all moves */
static int get_movea(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 1, 0, 2);
}

/* both move and color, all moves */
static int get_moveax(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 1, 1, 2);
}

/* color only, all moves */
static int get_moveac(char *choice, char *buf, int len) {
	return get_movemc(choice, buf, len, 0, 1, 2);
}

static void getmd5(unsigned char *md5, char *buf, int bufct) {
	MD5_CTX mdContext;

	MD5_Init(&mdContext);
	MD5_Update(&mdContext, buf, bufct);
	MD5_Final(md5, &mdContext);
}

#define MAXBUF 20000
static void getmd5tra(unsigned char *md5, int tra) {
	char buf[MAXBUF];	/* need 2*mvct+1 */
	int i, bufct;

	bufct = 0;
	for (i=0; i<mvct; i++) {
		if (bufct > MAXBUF-2)
			errexit("game too long");
		getmovetra(i, buf+bufct, tra);
		bufct += 2;
	}
	buf[bufct++] = '\n';

	getmd5(md5, buf, bufct);
}

static int get_md5_string(char *buf, int len) {
	unsigned char md5[MD5_DIGEST_LENGTH];
	int i;

	if (len < 2*MD5_DIGEST_LENGTH+1)
		return 1;	/* overflow */

	getmd5tra(md5, opttra);
	for(i = 0; i < MD5_DIGEST_LENGTH; i++)
		buf += sprintf(buf, "%02x", md5[i]);
	*buf = 0;

	return 0;
}

static int get_canx_string(char *buf, int len) {
	unsigned char md5[MD5_DIGEST_LENGTH];
	unsigned char md5t[MD5_DIGEST_LENGTH];
	int i, tra, mintra;

	if (len < 2*MD5_DIGEST_LENGTH+3)
		return 1;	/* overflow */

	mintra = 0;
	getmd5tra(md5, 0);
	for (tra=1; tra<8; tra++) {
		getmd5tra(md5t, tra);
		for(i = 0; i < MD5_DIGEST_LENGTH; i++)
			if (md5t[i] != md5[i])
				break;
		if (md5t[i] < md5[i]) {
			mintra = tra;
			for(i = 0; i < MD5_DIGEST_LENGTH; i++)
				md5[i] = md5t[i];
		}
	}
	for(i = 0; i < MD5_DIGEST_LENGTH; i++)
		buf += sprintf(buf, "%02x", md5[i]);
	/*
	 * Note: this suffix is computed regardless of any -rot or -tra
	 * options, so relates to the original file, not the transformed one
	 * (but the can/md5 value takes truncation into account)
	 */
	buf += sprintf(buf, "-%d", mintra);
	*buf = 0;

	return 0;
}

static int get_can_string(char *buf, int len) {

	get_canx_string(buf, len);
	buf[2*MD5_DIGEST_LENGTH] = 0;	/* delete suffix */

	return 0;
}

void report_on_single_game() {
	int i, bare;

	/* check selection criteria */
	if (optxx && gamenr != optxx)
		return;

	if (needplay && !didplay)
		do_play();

	if (opttrunc) {
		if (trunclen >= 0 && movect > trunclen)
			movect = trunclen;
		if (trunclen < 0) {
			movect += trunclen;
			if (movect < 0)
				movect = 0;
		}
		truncate_to(movect);
	}

	if (!checkints() || !checkstrings() || !checkstringfns())
		return;

	for (i=0; i<movesplayedct; i++)
		if (nosuchmove(&movesplayed[i]))
			return;

	if (patternct) {
		int pi = findpattern();
		if (pi < 0)
			return;
		patindex = pi;
	}

	/* yes, found a candidate - count it */
	okgames++;

	bare = (optb || infooptct == 1);

	/* no info requested? - then report filename only */
	if (!infooptct) {
		if (!optnf) {
			printf("%s", infilename);
			if (number_of_games > 1)
				printf("  # %d", gamenr);
			printf("\n");
		}
		return;
	}

	/* if the info output is multi-line, print filename header */
#ifndef READ_FROM_DB
	if (argct > 1 && !reportedfn++)
#endif
	    if (optx || optM || !bare)
		printf("\n=== %s ===\n", infilename);

	if (optx || (!bare && number_of_games > 1))
		printf("** Game %d **\n", gamenr);

	if (optM) {
		int imin, imax;
		void (*fns[3])(int) = { outmove, outmovec, outmovex };
		void (*outmovefn)();

		/* 1..3 moves / 4..6 init / 7..9 both */
		imin = ((optM < 4) ? initct : 0);
		imax = ((optM >= 4 && optM < 7) ? initct : mvct);
		outmovefn = fns[(optM-1) % 3];

		for (i=imin; i<imax; i++) {
			printf("%d. ", i-imin+1);
			outmovefn(i);
			printf("\n");
		}
		if (infooptct == 1)
			return;
		printf("\n");
	}

	if (optP) {
		/* output a single line */
		outpos_at((optPmvct >= 0) ? optPmvct : movect);
	}

	bare_start(0);

	if (opts) {
		for (i=0; i<mvct; i++)
			outmove(i);
		if (bare)
			bare_start(1);
		else
			printf("\n");
	}

	if (bare) {
		report_all(1);
		if (!optnf) {
			printf("  %s", infilename);
			if (number_of_games > 1)
				printf("  # %d", gamenr);
		}
		printf("\n");
	} else {
		report_all(0);
	}
}

/* option number: empty string denotes 1 */
static int getint(char *s) {
	char *se;
	int n = 1;

	if (*s) {
		n = strtol(s, &se, 10);
		if (se == s)
			errexit("digit expected in option");
		if (*se)
			errexit("garbage after option number");
	}
	return n;
}

static void initpb(int *pb, int tra, int swap) {
	int i, x, y, mask, mv, move;

	/* if we read the pattern before reading the game,
	   the board size is still unknown */
	if (!patternsize)
		patternsize = SZ;

	for (i=0; i<patternct; i++) {
		mv = (pattern[i] & 0xffff);
		mask = (pattern[i] & ~0xffff);
		if (swap && mask != EMPTY_MASK)
			mask ^= (BLACK_MASK | WHITE_MASK);
		x = (mv >> 8) - 'a';
		y = (mv & 0xff) - 'a';
		transform0(&x, &y, tra, patternsize);
		move = x*SZ + y;
		if (move < 0 || move >= SZ*SZ)
			errexit("unrecognized pattern move");
		pb[move] = mask;
	}
}

/* initialize patternboard[] from pattern[] and opttra */
static void init_pattern() {
	int i, j, *pb;

	for (i=0; i<16*SZ*SZ; i++)
		patternboard[i] = 0;

	/* init for all transformations and colors */
	/* make sure that index 0 represents opttra, swapcolors */
	for (j=0; j<8; j++) {
		pb = &patternboard[j*SZ*SZ];
		initpb(pb, opttra ^ j, swapcolors);
	} 
	for (j=0; j<8; j++) {
		pb = &patternboard[(j+8)*SZ*SZ];
		initpb(pb, opttra ^ j, !swapcolors);
	}

	if (printpatternindex && !patternct)
		errexit("pattern index requested, but no pattern?");
}

static void setpatternsize(struct propvalue *pv) {
	patternsize = atoi(pv->val);
}

static void pattern_add1(int mv, int mask) {
	if (patternct == MAXPLAYS)
		errexit("MAXPLAYS overflow");
	pattern[patternct++] = (mv | mask);
	if (mask != EMPTY_MASK)
		patternbwct++;
}

static void pattern_add2(int mv1, int mv2, int mask) {
	int x1, y1, x2, y2, x, y;

	x1 = (mv1 >> 8);
	y1 = (mv1 & 0xff);
	x2 = (mv2 >> 8);
	y2 = (mv2 & 0xff);
	for (x = x1; x <= x2; x++)
		for (y = y1; y <= y2; y++)
			pattern_add1((x<<8) + y, mask);
}

static void pattern_add(struct propvalue *val, int mask) {
	char *s;
	int mv, mv2;

	while (val) {
		s = val->val;
		if (strlen(s) == 2 && lowercasemove(s,&mv))
			pattern_add1(mv, mask);
		else if (strlen(s) == 5 && s[2] == ':' &&
			 lowercasemove(s,&mv) &&
			 lowercasemove(s+3,&mv2))
			pattern_add2(mv, mv2, mask);
		else if (letdigsmove(s,&mv))
			pattern_add1(mv, mask);
		else
			errexit("unrecognized pattern move %s", s);
		val = val->next;
	}
}

/*
 * A basic pattern file is single-node and has SZ and AB/AW/AE only:
 * (;SZ[3]AB[aa][bb]AW[cc])
 * As extension we accept more nodes and also B, W, GM, FF.
 * Also human-style moves like AB[R6].
 */
static void readpatternfile(char *fn) {
	struct gametree *g;
	struct node *node;
	struct property *p;

	readsgf(fn, &g);
	if (g->firstchild || g->nextsibling)
		errexit("pattern file has variations");
	node = g->nodesequence;
	if (!node)
		errexit("no pattern found in pattern file");

	/* also accept ;GM[1]FF[3] to make work with cgoban easier */
	while (node) {
		p = node->p;
		while (p) {
			if (!strcmp(p->id, "GM") || !strcmp(p->id, "FF"))
				/* ignore */;
			else if (!strcmp(p->id, "SZ"))
				setpatternsize(p->val);
			else if (!strcmp(p->id, "AE"))
				pattern_add(p->val, EMPTY_MASK);
			else if (!strcmp(p->id, "AB") || !strcmp(p->id, "B"))
				pattern_add(p->val, BLACK_MASK);
			else if (!strcmp(p->id, "AW") || !strcmp(p->id, "W"))
				pattern_add(p->val, WHITE_MASK);
			else
				errexit("unrecognized property %s "
					"in pattern file", p->id);
			p = p->next;
		}
		node = node->next;
	}
}

static void usage() {
	printf("Call: %s [options] [--] [%sfile(s)]\n"
	       " -nf: no filename\n"
	       " -i: ignore errors\n"
	       " -t: trace input\n"
	       "\nSelect input file:\n"
	       " -m#: game has # moves (-#, #-, #-#: at most, at least, ...)\n"
	       " -p#X,Y,... : moves X, Y, ... were played at moves #\n"
	       " -Bp#X, -Wp#X: idem for black/white moves\n"
	       " -pat=file.sgf: find pattern\n"
	       " -h#: game has handicap # (-#, #-, #-#)\n"
	       "\nSelect game in a multi-game file:\n"
	       " -x#: requested game number\n"
	       "\nDefine and use reference file:\n"
	       " -ref=FILE -propDT=@ (@: same as in FILE)\n"
	       "\nTransform game:\n"
	       " -trunc#: truncate to # moves\n"
	       " -tra#: apply rotation or reflection (#=0,...,7)\n"
	       " -swapcolors (together with -pat): swap colors\n"
	       " -alltra (together with -pat): try all 16 transformations\n"
	       "\nPrint info:\n"
	       " -N: print nr of games\n"
	       " -m: print nr of moves\n"
	       " -M: print moves\n"
	       " -M#: print move #\n"
	       " -s: print moves in a compact string\n"
	       " -k: print move number where pattern (first) found\n"
	       " -h: print handicap\n"
	       " -md5: print md5 signature of moves (only)\n"
	       " -can: print canonical signature of moves (only)\n"
	       " -DsA (= -Ds20,40,60), -DsB (= -Ds31,51,71) Dyer signature\n"
	       " -DnC (= -Dn20,40,60,31,51,71) normalized Dyer signature\n"
#ifndef READ_FROM_DB
	       " -propXY: print property labels XY\n"
#endif
	       " -Bcapt, -Wcapt: print nr of captured B, W stones\n"
	       , progname, DB);
}

void
do_input(const char *s) {
#ifdef READ_FROM_DB
	do_dbin(s);
#else
	do_stdin(s);
#endif
}

static void report_bcapt() {
	set_int_to_report("%d black stone%s captured\n", &bcaptct);
	infooptct++;
}

static void report_wcapt() {
	set_int_to_report("%d white stone%s captured\n", &wcaptct);
	infooptct++;
}

static int optional_num(char *s) {
	while (*s >= '0' && *s <= '9')
		s++;
	return (*s == 0);
}

/* for sh we have to escape ; & $ ( ) \ ' " ... */
static int is_innocent(int c) {
	return (c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') ||
		c == '-' || c == '_';
}

static void esccopy(char *p, char *s) {
	while (*s) {
		if (is_innocent(*s))
			*p++ = *s++;
		else {
			*p++ = '\\';
			*p++ = *s++;
		}
	}
}

/*
 * A rather primitive hack - but it works for me
 * One of the problems is that parsing the "  " is not robust
 * The output of sgfinfo was not designed to be automatically parsed
 */
#include "xmalloc.h"
static void do_reference(char *ref_file, int argc, char **argv) {
	char cmd[1000], *p, *a, *q, *r, *s, *t;
	char buf[10000];
	FILE *pf;
	int i, n;

	p = cmd;
	p += sprintf(p, "sgfinfo");
	for (i = 1; i < argc; i++) {
		a = argv[i];
		q = index(a, '@');
		if (q != NULL) {
			if ((p-cmd) + (q-a) + 2 >= sizeof(cmd))
				errexit("do_reference: cmdbuf overflow");
			q--;
			if (!(*q == ':' || *q == '='))
				q++;
			p += sprintf(p, " %.*s", (int)(q - a), a);
		}
	}
	if ((p-cmd) + 2*strlen(ref_file) + 15 >= sizeof(cmd))
		errexit("do_reference: cmdbuf overflow");
	p += sprintf(p, " -b -nf -- ");
	esccopy(p, ref_file);	/* copy escaped version of filename */

	pf = popen(cmd, "r");
	if (pf == NULL)
		errexit("do_reference: popen failed");
	if (!fgets(buf, sizeof(buf), pf))
		errexit("do_reference: nothing read");
	pclose(pf);

	p = buf;
	r = index(buf, '\n');
	if (r)
		*r = 0;
	for (i = 1; i < argc; i++) {
		a = argv[i];
		q = index(a, '@');
		if (q == NULL)
			continue;
		if (p == NULL)
			errexit("do_reference: not enough results");
		r = strstr(p, "  ");
		if (r) {
			r[0] = 0;
			r += 2;
		}
		n = strlen(a) + strlen(p) + 1;
		s = t = xmalloc(n);
		t += sprintf(t, "%.*s", (int)(q - a), a);
		t += sprintf(t, "%s", p);
		t += sprintf(t, "%s", q+1);
//		printf("replace %s by %s\n", a, s);
		argv[i] = s;
		p = r;
	}
	if (p)
		errexit("do_reference: too many results");
}

/*
 * MA: all moves
 * MI: initial moves only (setup, handicap)
 * M: played moved only
 */
static void handle_M_option(char *opt) {
	int all, init, color, ext, m;
	int (*fns[9])(char *, char *, int) = {
		get_move, get_movec, get_movex,
		get_movei, get_moveic, get_moveix,
		get_movea, get_moveac, get_moveax
	};

	all = init = 0;
	if (*opt == 'A') {
		all = 1;
		opt++;
	} else if (*opt == 'I') {
		init = 1;
		opt++;
	}

	color = ext = 0;
	if (*opt == 'c') {
		color = 1;
		opt++;
	} else if (*opt == 'x') {
		ext = 1;
		opt++;
	}

	m = color + 2*ext + 3*init + 6*all;

	if (*opt == 0) {
		optM = m+1;
		infooptct++;
		return;
	}

	set_stringfn("move %s:  %s\n", opt, fns[m]);
}

int
main(int argc, char **argv) {
	char *p;
	int i;

	progname = "sgf" DB "info";
	infilename = "(reading options)";
	seloptct = infooptct = 0;

	/* Is there a reference file? Then we must not look yet at
	   the other options, since they will be modified. */
	for (i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "-ref", 4)) {
			int d, j;

			p = argv[i]+4;
			d = 1;
			if (*p == 0 && i < argc-1) {
				p = argv[i+1];
				d = 2;
			}
			else if (*p == ':' || *p == '=')
				p++;
			
			for (j=i+d; j<argc; j++)
				argv[j-d] = argv[j];
			argc -= d;

			do_reference(p, argc, argv);
			break;
		}
	}

	while (argc > 1 && (argv[1][0] == '-' || argv[1][0] == '+')) {
		/* see below for condensed 1-letter options */
		if (!strcmp(argv[1], "--")) {
			argc--; argv++;
			break;
		}
#ifndef READ_FROM_DB
		if (!strncmp(argv[1], "--", 2)) {
			/* synonym for -prop */
			setproprequests(0, argv[1]+2);
			goto next;
		}
#endif
		if (!strcmp(argv[1], "-alltra")) {
			alltra = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-Bcapt")) {
			needplay = 1;
			report_bcapt();
			goto next;
		}
		if (!strncmp(argv[1], "-Bp", 3)) {
			setplayrestrictions(argv[1]+3, BLACK_MASK);
			seloptct++;
			goto next;
		}
		if (!strcmp(argv[1], "-b")) {
			optb = 1;	/* is default now */
			goto next;
		}
		if (!strcmp(argv[1], "+b")) {
			optb = 0;
			goto next;
		}
		if (!strncmp(argv[1], "-canx", 5)) {
			set_string("canx: %s\n", argv[1]+5, get_canx_string);
			goto next;
		}
		if (!strncmp(argv[1], "-can", 4)) {
			set_string("can: %s\n", argv[1]+4, get_can_string);
			goto next;
		}
		if (!strcmp(argv[1], "-capt")) {
			needplay = 1;
			report_bcapt();
			report_wcapt();
			goto next;
		}
		if (!strncmp(argv[1], "-DsAB", 5)) {
			set_string("sig-AB: %s\n", argv[1]+5, get_Dyer_sigC);
			goto next;
		}
		if (!strncmp(argv[1], "-DsA", 4)) {
			set_string("sig-A: %s\n", argv[1]+4, get_Dyer_sigA);
			goto next;
		}
		if (!strncmp(argv[1], "-DsB", 4)) {
			set_string("sig-B: %s\n", argv[1]+4, get_Dyer_sigB);
			goto next;
		}
		if (!strncmp(argv[1], "-DsC", 4)) {
			set_string("sig-AB: %s\n", argv[1]+4, get_Dyer_sigC);
			goto next;
		}
		if (!strncmp(argv[1], "-Ds", 3)) {
			set_stringfn("%s:  %s\n", argv[1]+3, get_Dyer_sign);
			goto next;
		}
		if (!strncmp(argv[1], "-DnAB", 5)) {
			set_string("sig-AB: %s\n", argv[1]+5, get_nDyer_sigC);
			goto next;
		}
		if (!strncmp(argv[1], "-DnA", 4)) {
			set_string("sig-A: %s\n", argv[1]+4, get_nDyer_sigA);
			goto next;
		}
		if (!strncmp(argv[1], "-DnB", 4)) {
			set_string("sig-B: %s\n", argv[1]+4, get_nDyer_sigB);
			goto next;
		}
		if (!strncmp(argv[1], "-DnC", 4)) {
			set_string("sig-AB: %s\n", argv[1]+4, get_nDyer_sigC);
			goto next;
		}
		if (!strncmp(argv[1], "-Dn", 3)) {
			set_stringfn("%s:  %s\n", argv[1]+3, get_nDyer_sign);
			goto next;
		}
		if (!strncmp(argv[1], "-e", 2)) {
			file_extension = argv[1]+2;
			goto next;
		}
		if (!strncmp(argv[1], "-E", 2) && argv[1][2]) {
			if (argv[1][2] == '+')
				optEmin = atoi(argv[1]+3);
			else if (argv[1][2] == '-')
				optEmax = atoi(argv[1]+3);
			else
				optEmin = optEmax = atoi(argv[1]+2);
			goto next;
		}
		if (!strncmp(argv[1], "-fn", 3)) {
			set_string("fn: %s\n", argv[1]+3, get_filename);
			goto next;
		}
#ifndef READ_FROM_DB
		if (!strncmp(argv[1], "-fullprop", 9)) {
			fullprop = 1;
			setproprequests(0, argv[1]+9);
			goto next;
		}
#endif
		if (!strcmp(argv[1], "-help") || !strcmp(argv[1], "--help") ||
			!strcmp(argv[1], "-?")) {
			usage();
			exit(0);
		}
		if (!strcmp(argv[1], "-h")) {
			set_int_to_report("handicap: %d\n", &handct);
			infooptct++;
			goto next;
		}
		if (!strncmp(argv[1], "-h", 2)) {
			setminmax(argv[1]+2, &handct, "handicap");
			seloptct++;
			goto next;
		}
		if (!strcmp(argv[1], "-k")) {
			set_int_to_report("pattern at move %d\n", &patindex);
			printpatternindex = 1;
			infooptct++;
			goto next;
		}
#ifndef READ_FROM_DB
		if (!strncmp(argv[1], "-loser", 6)) {
			set_string("loser: %s\n", argv[1]+6, get_loser);
			goto next;
		}
#endif
		if (!strcmp(argv[1], "-i")) {
			ignore_errors = 1;
			goto next;
		}
		if (!strncmp(argv[1], "-M", 2)) {
			handle_M_option(argv[1]+2);
			goto next;
		}
		if (!strcmp(argv[1], "-m")) {
			set_int_to_report("%d move%s\n", &movect);
			infooptct++;
			goto next;
		}
		if (!strncmp(argv[1], "-md5", 4)) {
			set_string("md5: %s\n", argv[1]+4, get_md5_string);
			goto next;
		}
#ifndef READ_FROM_DB
		if (!strncmp(argv[1], "-mprop", 6)) {
			setproprequests(MULTIPROP, argv[1]+6);
			goto next;
		}
#endif
		if (!strncmp(argv[1], "-m", 2)) {
			setminmax(argv[1]+2, &movect, "movect");
			seloptct++;
			goto next;
		}
#ifndef READ_FROM_DB
		if (!strcmp(argv[1], "-N")) {
			optN = 1;
			infooptct++;
			goto next;
		}
#endif
		if (!strcmp(argv[1], "-nf")) {
			optnf = 1;
			goto next;
		}
#ifndef READ_FROM_DB
		if (!strncmp(argv[1], "-nrprop", 7)) {
			setproprequests(NONROOT_ONLY, argv[1]+7);
			goto next;
		}
#endif
		if (!strncmp(argv[1], "-P", 2) && optional_num(argv[1]+2)) {
			/* output the position at the end or after m moves */
			needplay = 1;
			optP = 1;
			optPmvct = (argv[1][2] ? atoi(argv[1]+2) : -1);
			infooptct++;
			goto next;
		}
		if (!strncmp(argv[1], "-pat=", 5)) {
			needplay = 1;
			readpatternfile(argv[1]+5);
			seloptct++;
			goto next;
		}
#ifndef READ_FROM_DB
		/* cannot be confused with games in which la,ye,r are played */
		if (!strncmp(argv[1], "-player", 7)) {
			set_string("player: %s\n", argv[1]+7, get_player);
			goto next;
		}
		if (!strncmp(argv[1], "-prop", 5)) {
			setproprequests(0, argv[1]+5);
			goto next;
		}
#endif
		if (!strncmp(argv[1], "-p", 2)) {
			setplayrestrictions(argv[1]+2, 0);
			seloptct++;
			goto next;
		}
		if (!strcmp(argv[1], "-q")) {	/* also handled below */
			readquietly = 1;
			silent_unless_fatal = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-r")) {
			recursive = 1;
			goto next;
		}
#ifndef READ_FROM_DB
		if (!strcmp(argv[1], "-replacenl")) {
			replacenl = 1;
			goto next;
		}
#endif
		if (!strncmp(argv[1], "-rot", 4)) {
			int n = getint(argv[1]+4);	/* default 1 */
			opttra = 2*(n%4);
			goto next;
		}
#ifndef READ_FROM_DB
		if (!strncmp(argv[1], "-rprop", 6)) {
			setproprequests(ROOT_ONLY, argv[1]+6);
			goto next;
		}
#endif
		if (!strcmp(argv[1], "-s")) {
			opts = 1;
			infooptct++;
			goto next;
		}
		if (!strcmp(argv[1], "-swapcolors")) {
			swapcolors = 1;
			goto next;
		}
		if (!strcmp(argv[1], "-sz")) {
			set_int_to_report("board size: %d\n", &size);
			infooptct++;
			goto next;
		}
		if (!strncmp(argv[1], "-sz", 3)) {
			setminmax(argv[1]+3, &size, "board size");
			seloptct++;
			goto next;
		}
		if (!strcmp(argv[1], "-t")) {
			tracein = 1;
			goto next;
		}
		if (!strncmp(argv[1], "-tra", 4)) {
			int n = getint(argv[1]+4);	/* default 1 */
			if (n < 0 || n >= 8)
				errexit("-tra# option requires # < 8");
			opttra = n;
			goto next;
		}
		if (!strncmp(argv[1], "-trunc", 6)) {
			/* positive: truncate to # moves;
			   negative: remove tail of this length */
			opttrunc = 1;
			trunclen = getint(argv[1]+6);
			goto next;
		}
		if (!strcmp(argv[1], "-Wcapt")) {
			needplay = 1;
			report_wcapt();
			goto next;
		}
		if (!strncmp(argv[1], "-Wp", 3)) {
			setplayrestrictions(argv[1]+3, WHITE_MASK);
			seloptct++;
			goto next;
		}
#ifndef READ_FROM_DB
		if (!strncmp(argv[1], "-winner", 7)) {
			set_string("winner: %s\n", argv[1]+7, get_winner);
			goto next;
		}
#endif
		if (!strcmp(argv[1], "-x")) {
			optx = 1;
//			infooptct++;
			goto next;
		}
		if (!strncmp(argv[1], "-x", 2)) {
			optxx = atoi(argv[1]+2);
//			seloptct++;
			goto next;
		}

		/* allow condensed options like -rqi */
		/* they must all be single letter, without arg */
#ifdef READ_FROM_DB
#define SINGLE_LETTER_OPTIONS "bEikMqrstx"
#else
#define SINGLE_LETTER_OPTIONS "bEikMNqrstx"
#endif
		p = argv[1]+1;
		while (*p)
			if (!index(SINGLE_LETTER_OPTIONS, *p++))
				goto bad;
		p = argv[1]+1;
		while (*p) {
			switch (*p) {
			case 'b':
				optb = 1; break;
			case 'E':
				optE = 1; break;
			case 'i':
				ignore_errors = 1; break;
			case 'k':
				set_int_to_report("pattern at move %d\n",
						  &patindex);
				printpatternindex = 1;
				infooptct++;
				break;
			case 'M':
				optM = 1; break;
#ifndef READ_FROM_DB
			case 'N':
				optN = 1; infooptct++; break;
#endif
			case 'q':	/* also handled above */
				readquietly = 1;
				silent_unless_fatal = 1;
				break;
			case 'r':
				recursive = 1; break;
			case 's':
				opts = 1; infooptct++; break;
			case 't':
				tracein = 1; break;
			case 'x':
				optx = 1; /* infooptct++; */ break;
			}
			p++;
		}

	next:
		argc--; argv++;
		continue;

	bad:
		errexit("unknown option %s", argv[1]);
	}

	init_pattern();

	argct = argc-1;

	if (argc == 1) {
		if (recursive)
			errexit("refuse to read from stdin when recursive");
		do_input(NULL);
		goto done;
	}

	while (argc > 1) {
		infilename = argv[1];
		do_infile(argv[1]);
		argc--; argv++;
	}

done:
	if (optE)
		return ((okgames == 1) ? 0 : ((okgames == 0) ? -1 : 1));
	if (optEmin >= 0 || optEmax >= 0)
		return (((optEmin == -1) || (okgames >= optEmin)) &&
			((optEmax == -1) || (okgames <= optEmax))) ? 0 : 1;
	return 0;
}
