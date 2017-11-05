/* lists with conditions to check or data to report */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errexit.h"
#include "tests.h"


/* bounds on number of tests */
#define MAXTIS	100	/* today, 3 suffices */
#define MAXTSS	100
#define MAXTAS	100

/* bounds on number of strings returned */
#define MAXRIS  100
#define MAXRSS  100
#define MAXRAS  100
#define MAXALL	100

/* bounds on the lengths of strings */
#define MAXTSLEN	1000
#define MAXTALEN	1000
#define MAXRSLEN	100000
#define MAXRALEN	10000
/* (we might retry with a larger buffer in case of overflow) */

struct testinterval {
	int min;
	int max;
	int *val;
};
static struct testinterval tis[MAXTIS];
static int tisct;

#define	TEST_NOT	1	/* ! */
#define	TEST_CONTAINS	2	/* strstr (ipv !strcmp) */
				/* maybe also strcasecmp? */
#define TEST_EQUALS	4
#define	TEST_PRESENT	8

struct teststring {
	char *needed;
	int (*fn)(char *, int);		/* args: buf, buflen */
	int flag;
};
static struct teststring tss[MAXTSS];
static int tssct;

struct teststringfn {
	char *seed;
	char *needed;
	int (*fn)(char *, char *, int);	/* args: seed, buf, buflen */
	int flag;
};
static struct teststringfn tas[MAXTAS];
static int tasct;

struct reportint {
	char *fmt;
	int *val;
};
static struct reportint ris[MAXRIS];
static int risct;

struct reportstring {
	char *fmt;
	int (*fn)(char *, int);
};
static struct reportstring rss[MAXRSS];
static int rssct;

struct reportstringfn {
	char *fmt;
	char *seed;
	int (*fn)(char *, char *, int);
};
static struct reportstringfn ras[MAXRAS];
static int rasct;

#define I	1
#define	S	2
#define	SFN	3

struct reportsth {
	int type;
	int index;
};
static struct reportsth allrep[MAXALL];
static int allrepct;

/*
 * read interval
 * expect MIN-MAX or MIN- or -MAX or - or precise value
 */
char *getminmax(char *s, int *min, int *max) {
	char *se;
	int n;

	*min = *max = UNSET;

	if (*s != '-') {
		n = strtol(s, &se, 10);
		if (se != s)
			*min = n;
		s = se;
	}
	if (*s == '-') {
		s++;
		n = strtol(s, &se, 10);
		if (se != s)
			*max = n;
		s = se;
	} else
		*max = *min;

	if (*max != UNSET && *max < *min)
		errexit("in option range, max smaller than min?");

	return se;
}

/*
 * read interval and store in test bounds array
 */
char *setminmax(char *s, int *val, char *msg) {
	struct testinterval *ti;
	char *se;

	if (tisct == MAXTIS)
		errexit("too many interval tests");
	ti = tis+tisct;
	tisct++;

	ti->val = val;

	se = getminmax(s, &(ti->min), &(ti->max));
	if (*se && msg)
		errexit("trailing garbage in %s selection option", msg);
	return se;
}

/*
 * check whether all values lie in the specified intervals
 */
int checkints() {
	struct testinterval *ti;
	int i;

	for (i=0; i<tisct; i++) {
		ti = tis+i;
		if (ti->max != UNSET && *(ti->val) > ti->max)
			return 0;
		if (ti->min != UNSET && *(ti->val) < ti->min)
			return 0;
	}
	return 1;
}

static void setstringtest(char *needed, int (*fn)(char *, int), int flag) {
	struct teststring *ts;

	if (tssct == MAXTSS)
		errexit("too many string tests");
	if (strlen(needed) >= MAXTSLEN)
		errexit("stringtest: MAXTSLEN overflow");
	ts = tss+tssct;
	ts->needed = needed;
	ts->fn = fn;
	ts->flag = flag;
	tssct++;
}

/* return 1 if all tests succeed */

int checkstrings() {
	char buf[MAXTSLEN];
	struct teststring *ts;
	int i, res;

	for (i=0; i<tssct; i++) {
		ts = tss+i;
		res = ts->fn(buf, sizeof(buf));
		if (res > 0)
			return 0;	/* overflow */

		if (res < 0) {		/* not present */
			if ((ts->flag & TEST_NOT) && (ts->flag & TEST_PRESENT))
				continue;
			return 0;
		}

		if (ts->flag & TEST_CONTAINS)
			res = !strstr(buf, ts->needed);
		else if (ts->flag & TEST_EQUALS)
			res = strcmp(buf, ts->needed);
		else
			res = 0;
		
		if (ts->flag & TEST_NOT)
			res = !res;

		if (res)
			return 0;	/* different (...) */
	}
	return 1;
}

static void setstringfntest(char *seed, char *needed,
			    int (*fn)(char *, char *, int), int flag) {
	struct teststringfn *ta;

	if (tasct == MAXTAS)
		errexit("too many string tests");
	if (strlen(needed) >= MAXTALEN)
		errexit("stringtest: MAXTALEN overflow");
	ta = tas+tasct;
	ta->seed = seed;
	ta->needed = needed;
	ta->fn = fn;
	ta->flag = flag;
	tasct++;
}

int checkstringfns() {
	char buf[MAXTALEN];
	struct teststringfn *ta;
	int i, res;

	for (i=0; i<tasct; i++) {
		ta = tas+i;
		res = ta->fn(ta->seed, buf, sizeof(buf));
		if (res > 0)
			return 0;	/* overflow */

		if (res < 0) {		/* not present */
			if ((ta->flag & TEST_NOT) && (ta->flag & TEST_PRESENT))
				continue;
			return 0;
		}

		if (ta->flag & TEST_CONTAINS)
			res = !strstr(buf, ta->needed);
		else if (ta->flag & TEST_EQUALS)
			res = strcmp(buf, ta->needed);
		else
			res = 0;
		
		if (ta->flag & TEST_NOT)
			res = !res;

		if (res)
			return 0;	/* different (...) */
	}
	return 1;
}

static int line_itemct;

void bare_start(int a) {
	line_itemct = a;
}

static void bare_sep() {
	if (line_itemct++)
		printf("  ");
}

static inline char *plur(int n) {
	return (n == 1) ? "" : "s";
}

static void report_int(int bare, int i) {
	struct reportint *ri;
	int val;

	ri = ris+i;
	val = *(ri->val);
	if (bare)
		printf("%d", val);
	else
		/* format might be "%d stone%s" */
		printf(ri->fmt, val, plur(val));
}

static void report_string(int bare, int i) {
	struct reportstring *rs;
	char buf[MAXRSLEN];
	int res;

	rs = rss+i;
	res = rs->fn(buf, sizeof(buf));
	if (res > 0)
		errexit("MAXRSLEN overflow");
	if (res < 0)
		return;		/* no value */
	if (bare)
		printf("%s", buf);
	else
		printf(rs->fmt, buf);
}

static void report_stringfn(int bare, int i) {
	struct reportstringfn *ra;
	char buf[MAXRALEN];
	int res;

	ra = ras+i;
	res = ra->fn(ra->seed, buf, sizeof(buf));
	if (res > 0) {
		printf("\n");
		errexit("MAXRALEN overflow");
	}
	if (res < 0)
		return;		/* no value */
	if (bare)
		printf("%s", buf);
	else
		/* format e.g. "%s:  %s\n" */
		printf(ra->fmt, ra->seed, buf);
}

void report_all(int bare) {
	struct reportsth *r;
	int i;

	for (i=0; i<allrepct; i++) {
		/* separator also when there is no value */
		if (bare)
			bare_sep();

		r = allrep+i;
		switch (r->type) {
		case I:
			report_int(bare, r->index);
			break;
		case S:
			report_string(bare, r->index);
			break;
		case SFN:
			report_stringfn(bare, r->index);
			break;
		default:
			errexit("impossible type in report_all");
		}
	}
}

static void add_report_item(int type, int index) {
	struct reportsth *r;

	if (allrepct == MAXALL)
		errexit("MAXALL overflow");

	r = allrep+allrepct;
	allrepct++;

	r->type = type;
	r->index = index;
}

void set_int_to_report(char *fmt, int *val) {
	struct reportint *ri;

	if (risct == MAXRIS)
		errexit("MAXRIS overflow");

	add_report_item(I, risct);

	ri = ris+risct;
	risct++;

	ri->fmt = fmt;
	ri->val = val;
}

static void set_string_to_report(char *fmt, int (*fn)(char *, int)) {
	struct reportstring *rs;

       	if (rssct == MAXRSS)
		errexit("MAXRSS overflow");

	add_report_item(S, rssct);

	rs = rss+rssct;
	rssct++;

	rs->fmt = fmt;
	rs->fn = fn;
}

static void set_stringfn_to_report(char *fmt, char *seed,
				   int (*fn)(char *, char *, int)) {
	struct reportstringfn *ra;

	if (rasct == MAXRAS)
		errexit("MAXRAS overflow");

	add_report_item(SFN, rasct);

	ra = ras+rasct;
	rasct++;

	ra->fmt = fmt;
	ra->seed = seed;
	ra->fn = fn;
}

extern int infooptct, seloptct;

/* either -X (report) or -X=bar, -X:bar, -X!, -X!=bar, -X!:bar (test) */
void set_string(char *fmt, char *option, int (*fn)(char *, int)) {
	int flags = 0;

	if (*option == 0) {
		infooptct++;
		set_string_to_report(fmt, fn);
		return;
	}

	if (*option == '!') {
		flags = TEST_NOT;
		option++;
	}

	if (*option == '=') {
		option++;
		flags |= TEST_EQUALS;
	} else if (*option == ':') {
		option++;
		flags |= TEST_CONTAINS;
	} else if (*option == 0) {
		flags |= TEST_PRESENT;
	} else
		errexit("expected '=' or ':' preceding %s", option);

	setstringtest(option, fn, flags);
	seloptct++;
}

/*
 * -Xfoo (report) or
 * -Xfoo=bar, -Xfoo:bar, -Xfoo!, -Xfoo!=bar, -Xfoo!:bar (test)
 */
void set_stringfn(char *fmt, char *option, int (*fn)(char *, char *, int)) {
	char *p;
	int flags = 0;

	/* set p to first occurrence of '=' or ':' or NUL */
	p = option;
	while (*p != '=' && *p != ':' && *p)
		p++;

	if (p > option && p[-1] == '!') {
		p[-1] = 0;
		flags |= TEST_NOT;
	}

	if (*p == 0 && !flags) {
		infooptct++;
		set_stringfn_to_report(fmt, option, fn);
		return;
	}

	if (*p == '=') {
		flags |= TEST_EQUALS;
		*p++ = 0;
	} else if (*p == ':') {
		flags |= TEST_CONTAINS;
		*p++ = 0;
	} else
		flags |= TEST_PRESENT;
	
	seloptct++;
	setstringfntest(option, p, fn, flags);
}
