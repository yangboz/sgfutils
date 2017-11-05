/*
 * sgfsplit: split a game collection into individual files
 *  (and do nothing else). This works for arbitrary games
 *  written in SGF format: the fields are not interpreted.
 * Names are constructed from a counter.
 *
 * By default, the names are "X-%04d.sgf", counting from 1
 * -d#:		output counter zero padded to # digits
 * -s#:		start counting from #
 * -z:		start counting from 0
 * -x PREFIX:	set prefix to use instead of "X-"
 * -F FORMAT:	format used instead of "X-%d.sgf"
 * -p:		preserve trailing junk
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include "errexit.h"

#define DEFAULT_DIGNUM	4
#define DEFAULT_PREFIX	"X-"

#define MAXNAMLTH	4096
char ofilename[MAXNAMLTH];
int xfnct;
FILE *outf;
char *format;
int optc, optp;

static void
construct_next_filename() {
	int n;

	n = snprintf(ofilename, sizeof(ofilename), format, xfnct++);
	if (n >= MAXNAMLTH)
		errexit("output filename too long");
}

static void
create_outfile() {
	construct_next_filename();
	outf = fopen(ofilename, "r");
	while (outf != NULL) {
		/* name exists already */
		warn("not overwriting existing %s", ofilename);
		fclose(outf);
		construct_next_filename();
		outf = fopen(ofilename, "r");
	}
	outf = fopen(ofilename, "w");
	if (outf == NULL)
		errexit("cannot open file %s", ofilename);
}

/* states: INIT0, INIT1, STARTED, INSIDEBRK, ESC, DONE */
int parenct, ingame, done;
static void (*state)(int);

static void state_init0(int c);
static void state_init1(int c);
static void state_started(int c);
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
		if (optc) {
			putc('(', outf);
			putc(';', outf);
		}
	} else
		state = state_init0;
}

static void state_started(int c) {
	if (c == '(')
		parenct++;
	if (c == '[')
		state = state_insidebrk;
	if (c == ')') {
		parenct--;
		if (parenct == 0) {
			done = 1;
			ingame = 0;
			state = state_init0;
		}
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

#define BUFSZ	16384

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
			goto fin;
		ungetc(c, stdin);

		create_outfile();

		parenct = done = ingame = 0;
		state = state_init0;

		while (!done) {
			c = getchar();
			if (c == EOF)
				goto eof;
			if (ingame || !optc)
				putc(c, outf);
			(*state)(c);
		}

		putc('\n', outf);
		fclose (outf);
	}

eof:
	/* no game seen, just trailing garbage - throw it out? */
	fclose(outf);
	if (optp)
		warn("warning: only trailing junk in %s", ofilename);
	else {
		unlink(ofilename);
		if (!optc)
			warn("trailing junk discarded");
	}
	return;

fin:
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
	fprintf(stderr, "Usage: sgfsplit [-d#] [-s#] [-z] "
		"[-x prefix] [-F format] [-c] [-p] [files]\n");
	exit(1);
}

int
main(int argc, char **argv){
	char *prefix = NULL;
	int i, opt, dignum;

	progname = "sgfsplit";
	format = NULL;
	xfnct = 1;		/* start counting from 1 */
	dignum = -1;		/* unset */

#if 1
	/* just nonsense - allows use of the I flag character */
#include <locale.h>
	if (!setlocale(LC_ALL, ""))
		warn("failed setting locale");
#endif

	while ((opt = getopt(argc, argv, "d:s:zx:F:cp")) != -1) {
		switch (opt) {
		case 'd':
			dignum = my_atoi(optarg);
			break;
		case 's':
			xfnct = my_atoi(optarg);
			break;
		case 'z':
			xfnct = 0;
			break;
		case 'x':
			prefix = optarg;
			break;
		case 'F':
			format = optarg;
			break;
		case 'c':
			optc = 1;
			break;
		case 'p':
			optp = 1;
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

	if (optind == argc) {
		readsgf(NULL);		/* read stdin */
	} else for (i=optind; i<argc; i++) {
		readsgf(argv[i]);
	}

	return 0;
}
