/* convert NIP to SGF - aeb, 2014-03-23 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "errexit.h"

/*
 * syntax: header of 9 lines followed by a list of moves followed by END
 *
 * each move is described by:
 *  (resulting position, move, bcapt, wcapt)
 *
 * Example:

NIP100;
OnAir=2014/03/02;
Title=第61回NHK杯;
Stage=準々決勝第4局;
Start=2014/01/20;
End=2014/01/20;
Player1=瀬戸 大樹 七段;
Player2=結城 聡 NHK杯;
Result=2;
p=0000000000000000000000000000000000000000000000000000000000000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000qd;
h1=0;
h2=0;
...
p=0010110022220000000011122221112100001100221111210211110120021000222200202112002100000002220222202101002211012101000101000122110100211001200011111210112001200100002211200200120221000002202200010201210002002011000000222200211010000010002102021010220011222102111012021000211222110111200100222120222112020100202111221122022100002011022210000010000000000000000000000ka;
h1=5;
h2=5;
END

 *
 * The resulting position is a vector of length 361.
 */

#define SZ      10000

#define HDSZ	100
struct hd {
	char *name;
	char *value;
} headerlines[HDSZ];
int hdct;

#define MVMAX	10000
int moves[MVMAX];
int magicseen, moveseen, movect;

static void outmv(int n, int mv) {
	printf(";%c[%c%c]",
	       (n%2) ? 'W' : 'B', mv>>8, mv & 0xff);
}

static char *getval(char *name) {
	int i;

	for (i=0; i<hdct; i++)
		if (!strcmp(headerlines[i].name, name))
			return headerlines[i].value;
	return NULL;
}

/*
 * Result=2:1 - W+1.5
 * Result=2:5 - W+5.5
 * Result=1:0 - B+0.5
 * Result=1:t - B+T - according to the rumor; I have never seen an example
 * Result=1 - B+R
 */
static void outres() {
	char *v;

	v = getval("Result");
	if (v == NULL)
		errexit("No result");
	printf("RE[");
	if (*v == '1')
		printf("B+");
	else if (*v == '2')
		printf("W+");
	else
		errexit("unknown result color");
	v++;
	if (*v == 0)
		printf("R");
	else if (*v == ':') {
		v++;
		if (!strcmp(v, "t"))
			printf("T");
		else
			printf("%s.5", v);
	} else
		errexit("unknown result value");
	printf("]\n");
}

static void outstr(char *sgf, char *nip) {
	char *v;

	v = getval(nip);
	if (v)
		printf("%s[%s]\n", sgf, v);
}

/* Maybe only if p has format dddd/dd/dd ? */
static void slash_to_hyphen(char *p) {
	if (p) {
		while (*p) {
			if (*p == '/')
				*p = '-';
			p++;
		}
	}
}

static void outdate() {
	char *broadcast, *start, *end;

	start = getval("Start");
	end = getval("End");
	broadcast = getval("OnAir");
	slash_to_hyphen(start);
	slash_to_hyphen(end);
	slash_to_hyphen(broadcast);

	if (start && end) {
		if (!strcmp(start, end))
			printf("DT[%s]\n", start);
		else
			printf("DT[%s..%s]\n", start, end);
	} else if (start) {
		printf("DT[%s]\n", start);
	} else if (end) {
		printf("DT[%s]\n", end);
	}

	if (broadcast)
		printf("GC[Broadcast %s]\n", broadcast);
}

static void outsgf() {
	int i;

	printf("(;\n");
	outstr("EV", "Title");
	outstr("RO", "Stage");
	outstr("PB", "Player1");	/* might separate off BR */
	outstr("PW", "Player2");	/* might separate off WR */
	printf("KM[6.5]\n");
	outres();
	outdate();
	printf("\n");

	for (i=0; i<movect; i++) {
		outmv(i, moves[i]);
		if (i%10 == 9)
			printf("\n");
	}
	printf(")\n");
}

static void handle_move(char *mv) {
	char buf[SZ];
	unsigned char *q;

	if (strlen(mv) != 361+2)
		errexit("unexpected move length");
	q = (unsigned char *) mv + 361;
	if (movect == MVMAX)
		errexit("MVMAX overflow");
	moves[movect++] = (q[0]<<8) + q[1];

	// Should keep track of position, and complain if the first
	// 361 positions disagree.
	// Should keep track of prisoners, and complain if h1,h2 disagree.
	if (!fgets(buf, sizeof(buf), stdin))	/* skip h1= */
		errexit("h1= line expected");
	if (!fgets(buf, sizeof(buf), stdin))	/* skip h2= */
		errexit("h2= line expected");
}

static void handle_line(char *buf) {
	char *p;
	int i;

	if (!magicseen) {
		if (strcmp(buf, "NIP100"))
			errexit("unknown magic: %s", buf);
		magicseen = 1;
		return;
	}

	p = index(buf, '=');
	if (p == NULL)
		errexit("line does not contain '='");
	*p++ = 0;

	if (!strcmp(buf, "p")) {
		handle_move(p);		/* also eats the h1= and h2= lines */
		moveseen = 1;
		return;
	}

	if (moveseen)
		errexit("header line after move seen");

	for (i=0; i<hdct; i++)
		if (!strcmp(buf, headerlines[i].name))
			errexit("duplicate item %s", buf);
	if (hdct == HDSZ)
		errexit("HDSZ overflow");
	headerlines[hdct].name = strdup(buf);
	headerlines[hdct].value = strdup(p);
	hdct++;
}

int main() {
	char buf[SZ], *p;
	unsigned char *up;
	int firstline = 1;

	/* read stdin */
	while (fgets(buf, sizeof(buf), stdin)) {
		p = index(buf, '\n');
		if (p == NULL)
			errexit("line too long");
		*p-- = 0;
		if (*p == '\r')
			*p-- = 0;
		if (!strcmp(buf, "END"))
			goto done;
		if (*p != ';')
			errexit("line does not end in ';'");
		*p-- = 0;

		p = buf;
		if (firstline) {
			firstline = 0;
			/* remove possible BOM */
			up = (unsigned char *) p;
			if (up[0] == 0xef && up[1] == 0xbb && up[2] == 0xbf)
				p += 3;
		}
		handle_line(p);
	}
	errexit("eof before END line");
done:
	outsgf();
	return 0;
}
