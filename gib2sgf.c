/* convert GIB to SGF - aeb, 2017-01-17 */
/*
 * Note: I do not have a spec of GIB, just a handful of examples.
 * This is a very incomplete attempt.
 * Send examples of games not handled to aeb@cwi.nl .
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "errexit.h"

static int starts_with(char *buf, char *start, char **follow) {
	int n;

	n = strlen(start);
	if (!strncmp(buf, start, n)) {
		*follow = buf+n;
		return 1;
	}
	return 0;
}

static int has_tail(char *buf, char *tail) {
	int m, n;

	m = strlen(buf);
	n = strlen(tail);
	return (m >= n) && !strcmp(buf+m-n, tail);
}

static void strip_newline(char *buf) {
	char *p;

	p = index(buf, '\n');
	if (p == NULL)
		errexit("very long line");
	*p = 0;
	if (p > buf && p[-1] == '\r')
		*--p = 0;
}

#define	NONE	(-1)

static int readnum(char *buf, char **end) {
	unsigned long ul;
	char *q;

	ul = strtoul(buf, &q, 10);
	*end = q;
	if (q == buf)
		return NONE;
	return ul;
}

/* turn "2017- 1- 6-12- 6-22" into DT[2017-01-06 12:06:22] */
static void datefn(char *date) {
	char *p, *q, *r;
	int y,mo,d,h,mi,s,am,pm;

	p = date;
	y = readnum(p, &q);
	if (y == NONE || *q++ != '-')
		goto try2;

	p = q;
	mo = readnum(p, &q);
	if (mo == NONE || *q++ != '-')
		goto try2;

	p = q;
	d = readnum(p, &q);
	if (d == NONE || *q++ != '-')
		goto try2;

	p = q;
	h = readnum(p, &q);
	if (h == NONE || *q++ != '-')
		goto try2;

	p = q;
	mi = readnum(p, &q);
	if (mi == NONE || *q++ != '-')
		goto try2;

	p = q;
	s = readnum(p, &q);
	if (s == NONE || *q)
		goto try2;
	printf("DT[%04d-%02d-%02d %02d:%02d:%02d]\n", y, mo, d, h, mi, s);
	return;

try2:
	/* perhaps 2009年10月 9日 19:24:18 */
	p = date;
	y = readnum(p, &q);
	if (y == NONE || !starts_with(q, "年", &r))
		goto bad;

	p = r;
	mo = readnum(p, &q);
	if (mo == NONE || !starts_with(q, "月", &r))
		goto bad;

	p = r;
	d = readnum(p, &q);
	if (d == NONE || !starts_with(q, "日", &r))
		goto bad;

	p = r;

	am = pm = 0;
	while (*p == ' ')
		p++;
	if (starts_with(p, "下午", &r)) {
		pm = 1;
		p = r;
	} else if (starts_with(p, "上午", &r)) {
		am = 1;
		p = r;
	}

	h = readnum(p, &q);
	if (h == NONE || *q++ != ':')
		goto bad;
	if (pm)
		h += 12;
	if (am) {
		/* nothing */
	}

	p = q;
	mi = readnum(p, &q);
	if (mi == NONE)
		goto bad;

	if (*q == 0) {
		printf("DT[%04d-%02d-%02d %02d:%02d]\n", y, mo, d, h, mi);
		return;
	}

	if (*q++ != ':')
		goto bad;

	p = q;
	s = readnum(p, &q);
	if (s == NONE || *q)
		goto bad;
	printf("DT[%04d-%02d-%02d %02d:%02d:%02d]\n", y, mo, d, h, mi, s);
	return;

bad:
	/* unexpected format, just copy this date */
	printf("DT[%s]\n", date);
}

/* Recognize
   "black wins by resignation",
   "black 3.5 win",
   "black 0.5 points win",
   "white wins by time"
*/
static void resultfn(char *result) {
	char *p, *q, *r, *who;

	if (starts_with(result, "black ", &r) ||
	    starts_with(result, "Black ", &r) ||
	    starts_with(result, "B ", &r) ||
	    starts_with(result, "黑", &r))		/* black */
		who = "B";
	else if (starts_with(result, "white ", &r) ||
		 starts_with(result, "White ", &r) ||
		 starts_with(result, "W ", &r) ||
		 starts_with(result, "白", &r))		/* white */
		who = "W";
	else goto bad;
	p = r;

	if (!strcmp(p, "wins by resignation") ||
	    !strcmp(p, "wins by resign")) {
		printf("RE[%s+R]\n", who);
		return;
	}
	if (!strcmp(p, "wins by time") ||
	    !strcmp(p, "時間勝")) {			/* time wins */
		printf("RE[%s+T]\n", who);
		return;
	}
	q = p;
	while (*q == '.' || (*q >= '0' && *q <= '9'))
		q++;
	if (!strcmp(q, " win") || !strcmp(q, " points win")) {
		printf("RE[%s+%.*s]\n", who, (int)(q-p), p);
		return;
	}

	/* fall through */
bad:
	printf("RE[%s]\n", result);
}

struct item {
	char *sgf;
	char *gib;
	void (*fn)(char *);
} tra[] = {
	{ "PB", "GAMEBLACKNAME=", NULL },	/* e.g. "DEEPZEN (9D)" */
	{ "PW", "GAMEWHITENAME=", NULL },
	{ "PC", "GAMEPLACE=", NULL },
	{ "DT", "GAMEDATE=", datefn },
	{ "RE", "GAMERESULT=", resultfn },
	{ "GN", "GAMENAME=", NULL },
};

/*
	{ "PB", "GAMEBLACKNICK=", NULL },
	{ "PW", "GAMEWHITENICK=", NULL },
	{ "BR", "GAMEBLACKLEVEL=", NULL },
	{ "WR", "GAMEWHITELEVEL=", NULL },
	{ "GC", "GAMECOMMENT=", NULL },
 */

char *handicaps[10] = {
	NULL,
	NULL,
	"AB[pd][dp]",
	"AB[pd][dp][pp]",
	"AB[dd][pd][dp][pp]",
	"AB[dd][pd][jj][dp][pp]",
	"AB[dd][pd][dj][pj][dp][pp]",
	"AB[dd][pd][dj][jj][pj][dp][pp]",
	"AB[dd][jd][pd][dj][pj][dp][jp][pp]",
	"AB[dd][jd][pd][dj][jj][pj][dp][jp][pp]"
};

static void readheaderline(char *buf) {
	int n, i;
	char *p, *q;

	if (strncmp(buf, "\\[", 2))
		errexit("header line does not start with \\[ - got '%s'", buf);
	n = strlen(buf);
	if (n < 4 || strncmp(buf+n-2, "\\]", 2))
		errexit("header line does not end with \\] - got '%s'", buf);
	buf[n-2] = 0;
	p = buf+2;

	for (i=0; i < sizeof(tra)/sizeof(tra[0]); i++) {
		n = strlen(tra[i].gib);
		if (!strncmp(p, tra[i].gib, n)) {
			q = p+n;
			if (tra[i].fn)
				tra[i].fn(q);
			else
				printf("%s[%s]\n", tra[i].sgf, q);
			return;
		}
	}
	/* ignore unrecognized header lines */
}

int nrmoves, movenr, handicap;

/* expect nr 0 &4 */
static void read_nr_moves(char *buf) {
	int n;
	char *q;

	n = readnum(buf, &q);
	/* printf("MN[%d]\n", n); */
	nrmoves = n;

	if (strcmp(q, " 0 &4"))
		errexit("pre-INI line '%s' does not end in ' 0 &4'", buf);
}

/*
 * maybe INI, STO, ... should be matched case insensitively
 */

static void read_ini(char *buf) {
	char *p, *q;
	int h;

	/* expect INI 0 1 ha &4text */

	if (strncmp(buf, "INI ", 4))
		errexit("INI expected; got '%s'", buf);
	if (strncmp(buf+4, "0 1 ", 4))
		errexit("INI 0 1 expected; got '%s'", buf);
	p = buf+8;
	h = strtoul(p, &q, 10);
	if (q == p)
		errexit("INI 0 1 ha expected; got '%s'", buf);
	if (h < 0 || h > 9 || h == 1)
		errexit("Unrecognized handicap %d", h);
	while (*q == ' ')
		q++;
	if (strncmp(q, "&4", 2))
		errexit("INI 0 1 ha &4 expected; got '%s'", buf);
	q += 2;
	while (*q == ' ')
		q++;
	if (*q)
		printf("C[%s]\n", q);
	if (h)
		printf("HA[%d]\n%s\n", h, handicaps[h]);
	movenr = 1;
	handicap = h;
}

static void readmove(char *buf) {
	char *p, *q, who, *cmd;
	int n, x, y, ispass, isidx, isrem;

	movenr++;
	ispass = isidx = isrem = 0;
	cmd = "STO";

	/* expect  STO 0 n who x y  or  SKI 0 n  */

	/* some incomplete games have an IDX command */
	/* its meaning is unclear */
	if (!strncmp(buf, "IDX ", 4)) {
		cmd = "IDX";
		isidx = 1;
	} else
	if (!strncmp(buf, "REM ", 4)) {
		cmd = "REM";
		isrem = 1;
	} else
	if (!strncmp(buf, "SKI ", 4)) {
		cmd = "SKI";
		ispass = 1;
	} else
	if (strncmp(buf, "STO ", 4))
		errexit("STO or SKI expected (move %d); got '%s'",
			movenr, buf);

	if (strncmp(buf+4, "0 ", 2))
		errexit("%s not followed by '0 ' in '%s'", cmd, buf);

	p = buf+6;
	n = readnum(p, &q);
	if (n == NONE)
		errexit("%s 0 not followed by a number in '%s'", cmd, buf);
	while (*q == ' ')
		q++;

	if (isidx) {
		movenr = n;
		if (*q)
			errexit("trailing garbage in '%s'", buf);
		return;
	}

	if (n != movenr) {
		warn("%s 0 followed by unexpected move number "
			"(%d instead of %d)", cmd, n, movenr);
		movenr = n;
	}

	if (isrem)
		return;	/* I don't know what this is */

	if (ispass) {
		if (*q)
			errexit("trailing garbage in '%s'", buf);
		/* should print B[] or W[tt] or so, but who is not given */
		return;
	}

	p = q;
	n = readnum(p, &q);
	if (n == NONE)
		errexit("STO 0 mvnr not followed by playernr in '%s'", buf);
	if (n == 1)
		who = 'B';
	else if (n == 2)
		who = 'W';
	else
		errexit("STO 0 mvnr followed by unknown player number"
			" in '%s'", buf);
	while (*q == ' ')
		q++;
	p = q;

	x = readnum(p, &q);
	if (x == NONE)
		errexit("STO 0 mvnr player not followed by x in '%s'", buf);
	if (x < 0 || x >= 19)
		errexit("unexpected x-coordinate %d in '%s'", x, buf);
	while (*q == ' ')
		q++;
	p = q;
	y = readnum(p, &q);
	if (y == NONE)
		errexit("STO 0 mvnr player x not followed by y"
			" in '%s'", buf);
	if (y < 0 || y >= 19)
		errexit("unexpected y-coordinate %d in '%s'", y, buf);
	while (*q == ' ')
		q++;
	if (*q)
		errexit("trailing garbage in '%s'", buf);
	if (movenr % 10 == 2)
		printf("\n");
	printf(";%c[%c%c]", who, 'a'+x, 'a'+y);
}

int state;
int incomplete;

static void inline0(char *buf) {
	if (!strcmp("2 6 0", buf))
		incomplete = 1;
	else if (strcmp("2 1 0", buf))
		errexit("line 1 of game is not '2 1 0' but '%s'", buf);
	state++;
}

static void inline1(char *buf) {
	read_nr_moves(buf);
	state++;
}

static void iniline(char *buf) {
	read_ini(buf);
	state++;
}

static void stoline(char *buf) {
	readmove(buf);
}

static void (*inputline[])() = {
	inline0, inline1, iniline, stoline
};

static void readgameline(char *buf) {
	(inputline[state])(buf);
}

/*
 * syntax: header \HS .. \HE followed by game \GS .. \GE
 */
#define BUFSZ	100000

static void read_lines(char *start, char *end, void (*fn)(char *)) {
	char buf[BUFSZ], *r;
	int didwarn, startseen, n;

	didwarn = startseen = 0;

	/* read stdin */
	while (fgets(buf, sizeof(buf), stdin)) {

		strip_newline(buf);

		/* some headerlines are spread out over two input lines */
		/* kludge */
		if (starts_with(buf, "\\[GAMETIME=", &r) &&
		    !has_tail(buf, "\\]")) {
			/* read rest of headerline */
			n = strlen(buf);
			if (!fgets(buf+n, sizeof(buf)-n, stdin))
				errexit("premature eof");
			strip_newline(buf);
		}

		if (!startseen) {
			if (!strcmp(buf, start))
				startseen = 1;
			else if (!didwarn) {
				warn("ignoring garbage before %s\n", start);
				didwarn = 1;
			}
			continue;
		}

		if (!strcmp(buf, end))
			return;

		fn(buf);
	}

	errexit("eof before %s", end);
}

static void read_header() {
	printf("(;\n");
	printf("FF[3]GM[1]SZ[19]\n");
	read_lines("\\HS", "\\HE", readheaderline);
}

static void read_game() {
	state = 0;
	read_lines("\\GS", "\\GE", readgameline);
	printf(")\n");

	if (state < 3 && !incomplete)
		warn("no game seen");
}

int main() {
	read_header();
	read_game();
	/* the above readers do immediate output */

	/* ignore possible garbase after the \\GE */
	return 0;
}
