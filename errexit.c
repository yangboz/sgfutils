#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "errexit.h"

int ignore_errors = 0;
int warnings_are_fatal = 0;
int silent_unless_fatal = 0;

const char *progname = "";
const char *infilename = "";		/* name of current input file */

int linenr = 0;
int warnct, errct;
char *((*warn_prefix)()) = 0;

int have_jmpbuf = 0;
jmp_buf jmpbuf;

static void
mywarn(const char *s, va_list ap) {
	fprintf(stderr, "%s", progname);
	if (infilename && *infilename)
		fprintf(stderr, " %s", infilename);
	if (linenr)
		fprintf(stderr, " (line %d)", linenr);
	fprintf(stderr, ": ");
	if (warn_prefix)
		fprintf(stderr, "%s", warn_prefix());
	vfprintf(stderr, s, ap);
	fprintf(stderr, "\n");
}

void
warn(const char *s, ...) {
	va_list ap;

	if (silent_unless_fatal && !warnings_are_fatal)
		goto skip;
	
	va_start(ap, s);
	mywarn(s, ap);
	va_end(ap);

skip:
	warnct++;
	if (warnings_are_fatal)
		exit(1);
}

void
errexit(const char *s, ...) {
	va_list ap;

	if (silent_unless_fatal && have_jmpbuf && ignore_errors)
		goto skip;
	
	va_start(ap, s);
	mywarn(s, ap);
	va_end(ap);

skip:
	errct++;
	if (have_jmpbuf && ignore_errors)
		longjmp(jmpbuf, 1);
	exit(1);
}

/* exit immediately, for program bugs and invocation errors */
void
fatalexit(const char *s, ...) {
	va_list ap;

	va_start(ap, s);
	fprintf(stderr, "%s: ", progname);
	vfprintf(stderr, s, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}
