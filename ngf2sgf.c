/* convert .ngf to .sgf - aeb, 2013-11-01 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "errexit.h"

/*
 * syntax: header of 12 lines followed by a list of moves
 *
 * each move has the fixed prefix PM
 * in some NGF-like files, other lines have the fixed prefix GI
 *
 * Example:

ﾚｰﾃｨﾝｸﾞ対局
19
アギラール  7段*
江維傑      5段P
www.cyberoro.com
0
0
6
20110812 [09:31]
480
黒中押し勝ち!
111
PMABBQEEQ
PMACWEEEE
...

 */
#define SZ	10000
char title[SZ], pw[SZ], pb[SZ], site[SZ], date[SZ], result[SZ];
int size, handicap, iets, komi, minutes, movect, passct;

static void readstring(char *s) {
	char buf[SZ], *p;

	if (fgets(buf, sizeof(buf), stdin) == NULL)
		errexit("premature eof");
	if (!index(buf, '\n'))
		errexit("very long line in input");
	p = buf;
	while (*p)
		p++;
	while (p > buf && (p[-1] == '\n' || p[-1] == '\r'))
		*--p = 0;
	strcpy(s, buf);
}

static void readint(int *v) {
	char buf[SZ], *end;
	int val;

	if (fgets(buf, sizeof(buf), stdin) == NULL)
		errexit("premature eof");
	val = strtoul(buf, &end, 10);
	if (end == buf)
		errexit("no digits in _%s_", buf);
	if (*end != '\n' && *end != '\r')
		errexit("junk after number in _%s_", buf);
	*v = val;
}

static char *rank(char *player) {
	char *rk;

	rk = rindex(player, ' ');
	if (rk != NULL)
		*rk++ = 0;
	return rk;
}

static int is_digit(int c) {
	return c >= '0' && c <= '9';
}

static void checkdate(char *d) {
	char datebuf[SZ];
	int i;

	for (i=0; d[i]; i++)
		datebuf[i] = (is_digit(d[i]) ? '0' : d[i]);
	datebuf[i] = 0;
	/*
	 * usually: 20111003 [19:28]
	 * seen: 20100404
	 * seen: 20100123 [21: 6]
	 */
	if (strcmp(datebuf, "00000000") &&
	    strcmp(datebuf, "00000000 ") &&
	    strcmp(datebuf, "00000000 [00:00]") &&
	    strcmp(datebuf, "00000000 [00: 0]") &&
	    strcmp(datebuf, "00000000 [ 0:00]") &&
	    strcmp(datebuf, "00000000 [ 0: 0]"))
		errexit("date: expected pattern 20111003 [19:28],"
			" got _%s_", d);
}

/* strings for handicaps 2..9 */
char *(handini[8]) = {
	"[pd][dp]", "[pd][dp][pp]", "[dd][pd][dp][pp]",
	"[dd][pd][jj][dp][pp]", "[pj][dj][dd][pp][dp][pd]",
	"[dj][jj][pj][pp][dp][dd][pd]",
	"[dj][jp][jd][pj][dd][dp][pp][pd]",
	"[jp][jd][jj][dj][pj][dp][pp][dd][pd]"
};

/* result */
/* It is rumoured that result strings may be

	White Win by Resign!
	Black Win 6.5 >> White(50):Black(63)
	White loses on time 
	White 19Win  >> White(83):Black(64)
	Progressing!

My examples are mostly Chinese or Japanese or Korean or Thai
(in SJIS, Big5, GB18030 or UTF-8)
and have, e.g.,

	흑 1.5집 승
	白5.5目勝ち!			W+5.5
	黒中押し勝ち!			B+R
	197手完、黒中押し勝ち		altogether 197 moves, B+R
	232手完、黒2目半勝ち		altogether 232 moves, B+2.5
	黒5.5目勝！ >> 白(64):黒(76)
	白時間切れ勝ち！
	白棋12子 >> 白方(94):黑方(82)	W+12 >> W 94, B 82
	Black wins by 1win >> White(58):Black(59)
	ดำชนะด้วยแต้ม!7.5 >> ขาว(63):ดำ(77)

Before parsing the result string, convert all from unknown character set
to UTF-8.
*/


int main() {
	char buf[SZ], *br, *wr;
	int movenr, x, y;

	readstring(title);
	readint(&size);
	if (size < 2 || size > 26)
		errexit("bad board size %d", size);
	readstring(pw);
	readstring(pb);
	readstring(site);
	readint(&handicap);
	readint(&iets);
	readint(&komi);
	readstring(date);
	readint(&minutes);	/* limit? or used? */
	readstring(result);
	readint(&movect);

	printf("(;\n");

	br = rank(pb);
	wr = rank(pw);
	printf("PB[%s]\n", pb);
	if (br)
		printf("BR[%s]\n", br);
	printf("PW[%s]\n", pw);
	if (wr)
		printf("WR[%s]\n", wr);

	if (minutes) {
		int h, m;

		h = minutes/60;
		m = minutes%60;
		printf("TM[");
		if (h)
			printf("%dh", h);
		if (m)
			printf("%dm", m);
		printf("]\n");
	}
	
	if (handicap != 0 && handicap != 1)
		printf("HA[%d]\n", handicap);

	/* ngf2sgf.pl says that no half is added in case of a handicap */
	if (handicap != 0)
		printf("KM[%d]\n", komi);
	else
		printf("KM[%d.5]\n", komi);

	printf("RE[%s]\n", result);

	/* expect 20111003 [19:28] */
	checkdate(date);
	printf("DT[%.4s-%.2s-%.2s]\n",
	       date, date+4, date+6);

	printf("PC[%s]\n", site);

	/* in my examples, this is always 0 */
	if (iets)
		printf("IETS[%d]\n", iets);

	printf("GC[%s]\n", title);
	printf("C[%d moves]\n", movect);
//	printf("RU[Japanese]\n");
	printf("SZ[%d]\n", size);

	printf("\n");
	if (handicap > 1 && handicap <= 9)
		printf("AB%sPL[W]\n", handini[handicap-2]);

	movenr = 0;
	passct = 0;
	while (fgets(buf, sizeof(buf), stdin)) {

		/* sometimes there is a blank line at the end */
		if (*buf == '\n' ||
		    (*buf == '\r' && buf[1] == '\n'))
			continue;

		if (strncmp(buf, "PM", 2))
			errexit("expected PM, got _%s_", buf);
		if (strlen(buf) < 10 || (buf[9] != '\n' && buf[9] != '\r'))
			errexit("unexpected move length");
		movenr++;

		if (buf[2] != 'A' + movenr/26 ||
		    buf[3] != 'A' + movenr%26)
			errexit("expected seq %c%c found %c%c for movenr %d",
				'A' + movenr/26, 'A' + movenr%26,
				buf[2], buf[3], movenr);
		if (buf[4] != 'B' && buf[4] != 'W')
			errexit("expected 'B' or 'W' - got '%c'", buf[4]);
		if (buf[5] != buf[8] || buf[6] != buf[7])
			errexit("expected abba pattern - got %c%c%c%c",
				buf[5], buf[6], buf[7], buf[8]);
		x = buf[5]-'A'-1;
		y = buf[6]-'A'-1;
		if (x == -1 && y == -1) {
			printf(";%c[]", buf[4]);	/* pass */
			passct++;
		} else {
			if (x < 0 || y < 0 || x >= size || y >= size)
				errexit("coordinates (%d,%d) not in 0..%d",
					x, y, size-1);
			printf(";%c[%c%c]", buf[4], 'a'+x, 'a'+y);
		}

		if (movenr % 10 == 0)
			printf("\n");
	}
	printf(")\n");
	/*
	 * usually: movenr == movect, passct == 0
	 * seen: movenr-1 == movect, passct == 1
	 * seen: movenr+1 == movect, passct == 0 (an error?)
	 */
	if (movenr != movect+passct)
		warn("announced %d, found %d+%d moves",
			movect, movenr-passct, passct);
	return 0;
}
