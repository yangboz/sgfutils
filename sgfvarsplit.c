/*
 * sgfvarsplit: split a game into variations.
 * This works for arbitrary games written in SGF format:
 * the fields are not interpreted.
 * Names are constructed from a counter.
 *
 * By default, the names are "var-%03d.sgf", counting from 1
 * -g#:		split game number # in the collection (default 1)
 * -v#:		output only variation number # (default: all)
 * -d#:		output counter zero padded to # digits
 * -s#:		start counting from #
 * -z:		start counting from 0
 * -x PREFIX:	set prefix to use instead of "var-"
 * -F FORMAT:	format used instead of "var-%d.sgf"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include "xmalloc.h"
#include "errexit.h"

#define DEFAULT_DIGNUM	3
#define DEFAULT_PREFIX	"var-"

char *inbuf;
int inbuflen, inct, lastinct;

static void addchar(int c) {
	if (inct >= inbuflen) {
		inbuflen += 10000;
		inbuf = xrealloc(inbuf, inbuflen);
	}
	inbuf[inct++] = c;
}

int *varstart;
int maxvar;

static void newvarstart(int n) {
	if (n >= maxvar) {
		maxvar += 100;
		varstart = xrealloc(varstart, maxvar*sizeof(int));
	}
	varstart[n] = inct;
}

static void varend(int n) {
	inct = varstart[n];
}

#define MAXNAMLTH	4096
char ofilename[MAXNAMLTH];
int offset;
FILE *outf;
char *format;
int outct;

static void
construct_filename(int nr) {
	int n;

	n = snprintf(ofilename, sizeof(ofilename), format, nr+offset-1);
	if (n >= MAXNAMLTH)
		errexit("output filename too long");
}

static void
create_outfile(int nr) {
	construct_filename(nr);

	outf = fopen(ofilename, "r");
	if (outf != NULL)
		errexit("not overwriting existing %s", ofilename);

	outf = fopen(ofilename, "w");
	if (outf == NULL)
		errexit("cannot open file %s", ofilename);
}

int gamenr, varnr;
int wanted_gamenr, wanted_varnr;

static void outvariation() {
	int n;

	if (gamenr != wanted_gamenr)
		return;

	if (wanted_varnr > 0 && varnr != wanted_varnr)
		return;

	addchar('\n');

	create_outfile(varnr);
	n = fwrite(inbuf, 1, inct, outf);
	if (n != inct)
		errexit("output error");
	fclose (outf);
	outct++;
}

/* states: INIT0, INIT1, STARTED, AFTERCLOSEP, INSIDEBRK, ESC, DONE */
int parenct, ingame, done;

static void (*state)(int);

static void state_init0(int c);
static void state_init1(int c);
static void state_started(int c);
static void state_afterclosep(int c);
static void state_insidebrk(int c);
static void state_escaped(int c);

static void state_init0(int c) {
	if (c == '(')
		state = state_init1;
}

static void state_init1(int c) {
	if (c == ';') {
		state = state_started;
		parenct = 1;
		ingame = 1;
		gamenr++;
		varnr = 0;
		newvarstart(parenct);
		if (gamenr == wanted_gamenr) {
			addchar('(');
			addchar(';');
		}
	} else
		state = state_init0;
}

static void state_started(int c) {
	if (c == '(') {
		newvarstart(parenct);
		parenct++;
	}
	if (c == '[')
		state = state_insidebrk;
	if (c == ')') {
		parenct--;
		if (parenct == 0) {
			done = 1;
			ingame = 0;
			state = state_init0;
		} else
			state = state_afterclosep;
		varnr++;
		outvariation();
		varend(parenct);
	}
}

static void state_afterclosep(int c) {
	if (c != ')') {
		state = state_started;
		(*state)(c);
	} else {
		parenct--;
		if (parenct == 0) {
			done = 1;
			ingame = 0;
			state = state_init0;
		}
		varend(parenct);
	}
}

static void state_insidebrk(int c) {
	if (c == ']')
		state = state_started;
	if (c == '\\')
		state = state_escaped;
}

static void state_escaped(int c) {
	state = state_insidebrk;
}

static void
readsgf(char *fn) {
	char *infilename = (fn ? fn : "-");
	int c;
	FILE *f = NULL;

	if (strcmp(infilename, "-")) {
		f = freopen(fn, "r", stdin);
		if (!f)
			errexit("cannot open %s", fn);
	}

	while (1) {
		while ((c = getchar()) == '\n' || c == '\r' || c == ' ') ;
		if (c == EOF)
			goto eof;
		ungetc(c, stdin);

		parenct = done = ingame = 0;
		state = state_init0;

		while (!done) {
			c = getchar();
			if (c == EOF)
				goto eof;
			if (ingame && gamenr == wanted_gamenr &&
			    (c != '(' || state == state_insidebrk ||
			     state == state_escaped))
				addchar(c);
			if (c == ' ' || c == '\n' || c == '\r')
				continue;
			(*state)(c);
		}
	}

eof:
	if (f)
		fclose(f);
}

/* try to avoid crashes from silly format strings */
static void
check_format() {
	int ct = 0, m;
	char *s = format, *se;

	/* need a single %d or %u */
	while (*s) {
		if (*s++ != '%')
			continue;
		if (*s == '%') {
			s++;
			continue;
		}
		m = strtoul(s, &se, 10);
		if (*se == '$')
			errexit("unsupported %N$-construction in format");

		/* flag characters - note: .I are nonstandard*/
		while (*s && index("#0- +.I", *s))
			s++;

		/* field width */
		if (*s == '*')
			errexit("unsupported *-width in format");
		m = strtoul(s, &se, 10);
		s = se;

		/* precision */
		if (*s == '.') {
			s++;
			if (*s == '*')
				errexit("unsupported *-precision in format");
			m = strtoul(s, &se, 10);
			s = se;
		}

		/* length modifier */
		while (*s && index("hlLqjzt", *s))
			s++;

		/* format character */
		if (!*s)
			errexit("missing format character after %");
		/* c is not meaningful here */
		if (!index("diouxX", *s))
			errexit("format must use integer conversion only");
		s++;
		ct++;
		m = m;	/* for gcc */
	}
	if (ct > 1)
		errexit("format must use a single integer argument");
	if (ct == 0)
		errexit("format does not use any parameter (like %%d)");
}

static int
my_atoi(char *s) {
	unsigned long n;
	char *se;

	n = strtoul(s, &se, 10);
	if (*se)
		errexit("trailing junk '%s' in optarg", se);
	return n;
}

static void
usage() {
	fprintf(stderr, "Usage: sgfvarsplit [-g#] [-v#] [-d#] [-s#] [-z] "
		"[-x prefix] [-F format] [file]\n");
	exit(1);
}

int
main(int argc, char **argv){
	char *prefix = NULL;
	int i, opt, dignum;

	progname = "sgfvarsplit";
	format = NULL;
	offset = 1;		/* start counting from 1 */
	dignum = -1;		/* unset */

	wanted_gamenr = 1;
	wanted_varnr = 0;	/* all of them */

#if 1
	/* just nonsense - allows use of the I flag character */
#include <locale.h>
	if (!setlocale(LC_ALL, ""))
		warn("failed setting locale");
#endif

	while ((opt = getopt(argc, argv, "g:v:d:s:zx:F:")) != -1) {
		switch (opt) {
		case 'g':
			wanted_gamenr = my_atoi(optarg);
			break;
		case 'v':
			wanted_varnr = my_atoi(optarg);
			break;
		case 'd':
			dignum = my_atoi(optarg);
			break;
		case 's':
			offset = my_atoi(optarg);
			break;
		case 'z':
			offset = 0;
			break;
		case 'x':
			prefix = optarg;
			break;
		case 'F':
			format = optarg;
			break;
		default:
			usage();
		}
	}

	if (format && prefix) {
		warn("warning: format overrides prefix");
		prefix = NULL;
	}
	if (format && dignum >= 0) {
		warn("warning: format overrides digwidth");
		dignum = 0;
	}
	if (!format) {
		char formatbuf[100], *p;

		if (!prefix)
			prefix = DEFAULT_PREFIX;
		if (dignum < 0)
			dignum = DEFAULT_DIGNUM;

		p = formatbuf;
		p += sprintf(p, "%s%%", prefix);
		if (dignum)
			p += sprintf(p, "0%d", dignum);
		p += sprintf(p, "d.sgf");
		format = strdup(formatbuf);
		if (!format)
			errexit("out of memory");
	}

	check_format();

	gamenr = 0;

	if (optind == argc) {
		readsgf(NULL);		/* read stdin */
	} else for (i=optind; i<argc; i++) {
		readsgf(argv[i]);
	}

	if (outct == 0)
		errexit("no such variation");

	return 0;
}
