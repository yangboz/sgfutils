/*
 * Parse sgf files - aeb, 991230
 *
 * Read stdin, write to stdout unless -x option given.
 * -m: expect a collection with multiple games (and intervening junk)
 * -x: split a game collection into individual files
 *  (names are constructed from ID and/or DT properties, or a counter)
 * -x19: extract only game 19
 * -nn: do not normalize text fields, or their order
 * -nd: do not normalize date
 * -d: report what was changed in the date
 * -pc: parse comments
 * -sz: accept sizes other than 19x19
 * -ll#: set line length to # (default 10 moves/line)
 * -c: strip comments and variations
 * -t: trace: print input as it is being read
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "errexit.h"
#include "readsgf.h"
#include "xmalloc.h"

int splittofiles = 0;
int extractfile = 0;
int stripcomments = 0;
int nonorm = 0;			/* do not normalize text fields */
int nodatenorm = 0;		/* do not normalize dates */
int dateck = 0;			/* tell what changed in the dates */
int opttojp;			/* try to force japanese komi and result */
int parsecomments = 0;
int eof;

int gamect = 0;
int movesperline = 10;
int movesonthisline;		/* count moves on current output line */
struct node *rootnode;

static struct property *
get_property(struct gametree *g, char *prop) {
	struct node *n;
	struct property *p;

	if (!g || !(n = g->nodesequence) || !(p = n->p))
		return NULL;
	while (p && strcmp(p->id, prop))
		p = p->next;
	return p;
}

char outputfilename[80];
int xfnct = 0;
FILE *outf;

static void
construct_filename(struct gametree *g) {
	struct property *dt, *id;
	char datebuf[20], idbuf[20], *s;

	/* Use DT yyyy-mm-dd,.. and ID ii/jj to form
	   G-yy-jj.sgf or I-ii-jj.sgf or D-yyyy-mm-dd.sgf or X-nnnn.sgf */
	dt = get_property(g, "DT");
	id = get_property(g, "ID");
	if (dt) {
		strncpy(datebuf, dt->val->val, sizeof(datebuf));
		datebuf[sizeof(datebuf)-1] = 0;
		for (s = datebuf; *s; s++)
			if(*s == '/')
				*s = ',';
	}
	if (id) {
		strncpy(idbuf, id->val->val, sizeof(idbuf));
		idbuf[sizeof(idbuf)-1] = 0;
		for (s = idbuf; *s; s++)
			if(*s == '/')
				*s = '-';
	}

	if (!dt && !id) {
		sprintf(outputfilename, "X-%04d.sgf", xfnct++);
	} else if (!dt) {
		sprintf(outputfilename, "I-%s.sgf", idbuf);
	} else if (!id) {
		sprintf(outputfilename, "D-%s.sgf", datebuf);
	} else {
		/* assume date has been normalized already */
		datebuf[4] = 0;
		sprintf(outputfilename, "G-%s-", datebuf);
		s = rindex(idbuf, '-');
		if (s)
			s++;
		else
			s = idbuf;
		strcat(outputfilename, s);
		strcat(outputfilename, ".sgf");
	}
}

static void
create_outfile(struct gametree *g) {
	construct_filename(g);
	outf = fopen(outputfilename, "r");
	if (outf != NULL) {
		/* name exists already */
		fprintf(stderr, "warning: %s exists, using X-%04d.sgf\n",
			outputfilename, xfnct);
		construct_filename(NULL);
		fclose(outf);
	}
	outf = fopen(outputfilename, "w");
	if (outf == NULL)
		errexit("cannot open file %s", outputfilename);
}

static char *
dup_and_strip_whitespace(char *s) {
	char *r, *t;

	r = xstrdup(s);
	t = r;
	while (*t)
		t++;
	while (t > r && t[-1] == ' ')
		*--t = 0;
	return r;
}
	
int gtlevel = 0;
int invariation = 0;
int skipping = 0;

static void
write_init() {
	gtlevel = 0;
	outf = stdout;
}

static void
write_propvalues(struct propvalue *p) {
	while (p) {
		fprintf(outf, "[%s]", p->val);
		p = p->next;
	}
}

static int
already_in_rootnode(char *id, char *val) {
	struct property *p;

	p = rootnode->p;
	while (p) {
		if (!strcmp(p->id, id) && !p->val->next &&
		    !strcmp(p->val->val, val))
			return 1;
		p = p->next;
	}
	return 0;
}

static void
newprop(struct property **prev, char *id, char *val) {	
	struct propvalue *pv;
	struct property *p;

	if (already_in_rootnode(id, val))
		return;
	
	pv = xmalloc(sizeof(*pv));
	pv->next = 0;
	pv->val = val;

	p = xmalloc(sizeof(*p));
	p->val = pv;
	p->id = id;
	p->next = (*prev)->next;
	(*prev)->next = p;
	*prev = p;
}

static char *
xstrdup_len(char *s, int len) {
	char *r;

	r = xmalloc(len+1);
	strncpy(r, s, len);
	r[len] = 0;
	return r;
}

static void
newprop_len(struct property **prev, char *id, char *val, int len) {
	char *r;

	r = xstrdup_len(val, len);
	newprop(prev, id, r);
}

static void
parse_comment(struct property *p) {
	struct propvalue *pv;
	char *s, *t;
	struct property *q0;
	int m, nrmoves, nrmoves2;
	char rest[100000];

	pv = p->val;
	if (pv->next)
		return;	/* multipart comment? */
	q0 = p;
	s = pv->val;
	nrmoves = nrmoves2 = -1;

	while (*s) {
		/* '.' is a mistake, as in "Black wins by resignation.." */
		if (*s == ' ' || *s == '\n' || *s == '.') {
			s++;
			continue;
		}

		/* Look for "C[White: Osawa Ginjiro 4-dan  " */
		if (!strncmp(s, "White: ", 7)) {
			s += 7;
			t = s;
			while (*t && (t[0] != ' ' || t[1] != ' '))
				t++;

			newprop_len(&q0, "PW", s, t-s);

			s = t;
			continue;
		}

		if (!strncmp(s, "Black: ", 7)) {
			s += 7;
			t = s;
			while (*t && (t[0] != ' ' || t[1] != ' '))
				t++;

			newprop_len(&q0, "PB", s, t-s);

			s = t;
			continue;
		}

		if (!strncmp(s, "Played on ", 10)) {
			s += 10;
			if (*s == ' ') {
				/* empty date field */
				continue;
			}
			t = s;
			while (*t && (t[0] != ' ' || t[1] != ' '))
				t++;

			newprop_len(&q0, "DT", s, t-s);

			s = t;
			continue;
		}

		/* Usually one sees now "123 moves." or
		   "Moves after 167 not recorded." */
		m = strtoul(s, &t, 10);
		if (t > s && !strncmp(t, " moves.", 7)) {
			nrmoves = m;
			s = t+7;
			continue;
		}

		if (!strncmp(s, "Moves after ", 12)) {
			m = strtoul(s+12, &t, 10);
			if (t > s+12 && !strncmp(t, " not recorded.", 14)) {
				nrmoves2 = m;
				s = t+14;
				continue;
			}
		}

		if (!strncmp(s, "Komi: None.", 11)) {
			newprop(&q0, "KM", "0");
			s += 11;
			continue;
		}

		/* Expect "Game suspended."
		   "Black wins by resignation."
		   "Black wins by 11 points."
		   "Black wins." */
		if (!strncmp(s, "Game suspended.", 15)) {
			newprop_len(&q0, "RE", s, 15);
			s += 15;
			continue;
		}

		if (!strncmp(s, "Black", 5) || !strncmp(s, "White", 5)) {
			if (!strncmp(s+5, " wins by resignation.", 21)) {
				if (*s == 'B')
					newprop(&q0, "RE", "B+R");
				else
					newprop(&q0, "RE", "W+R");
				s += 26;
				continue;
			}

			if (!strncmp(s+5, " wins by ", 9)) {
				m = strtoul(s+14, &t, 10);
				if (t > s+14 && !strncmp(t, " points.", 8)) {
					char res[100];

					sprintf(res, "%c+%d", *s, m);
					m = strlen(res);
					newprop_len(&q0, "RE", res, m);
					s = t+8;
					continue;
				}

				if (m == 1 && t > s+14 &&
				    !strncmp(t, " point.", 7)) {
					if (*s == 'B')
						newprop(&q0, "RE", "B+1");
					else
						newprop(&q0, "RE", "W+1");
					s = t+7;
					continue;
				}
			}

			if (!strncmp(s+5, " wins.", 6)) {
				if (*s == 'B')
					newprop(&q0, "RE", "B+");
				else
					newprop(&q0, "RE", "W+");
				s += 11;
				continue;
			}
		}

		break;
	}

	/* store unrecognized part of string */

	if (s == pv->val)
		return;		/* no changes */

	t = rest;
	*t = 0;

	if (nrmoves >= 0)
		t += sprintf(t, "%d moves.  ", nrmoves);
	if (nrmoves2 >= 0)
		t += sprintf(t, "Moves after %d not recorded.  ", nrmoves2);
	if (t + strlen(s) >= rest + sizeof(rest))
		errexit("parse_comments: rest overflow");
	strcpy(t, s);

	pv->val = "";
	t = dup_and_strip_whitespace(rest);
	if (!already_in_rootnode(p->id, t))
		pv->val = t;
}

static void normalize_rank(struct propvalue *pv);
static void normalize_time(struct propvalue *pv);
static void normalize_date(struct propvalue *pv);
static void normalize_stones(struct propvalue *pv);
static void normalize_komi(struct propvalue *pv);
static void normalize_result(struct property *p, struct propvalue *pv);
static void merge_stones(struct property *p);

#define SIZE(a)	(sizeof(a) / sizeof((a)[0]))

static void
write_property_sequence(struct property *p) {
	int did_output = 0;	/* precede 1st output with newline */

	int i;
	struct sp {
		char *id;
		struct property *p;
	} known[] = {
		{ "FF", NULL },
		{ "EV", NULL },
		{ "EVX", NULL },
		{ "RO", NULL },
		{ "ID", NULL },
		{ "PB", NULL },
		{ "BR", NULL },
		{ "PW", NULL },
		{ "WR", NULL },
		{ "TM", NULL },
		{ "KM", NULL },
		{ "RE", NULL },
		{ "JD", NULL },
		{ "DT", NULL },
		{ "DTX", NULL },
		{ "PC", NULL },
		{ "BC", NULL },
		{ "WC", NULL },
		{ "BT", NULL },
		{ "WT", NULL },
		{ "RU", NULL },
		{ "OH", NULL },
		{ "HA", NULL }
	};
	char *ignore[] = {
		"GM",	/* Game: 1 is go */
		"SY",	/* System? */
		"BS",	/* B species: 0 is human */
		"WS",	/* W species: 0 is human */
		"KI"	/* ?? */
	};
	char *strip[] = {
		"C",	/* comment */
		"LB"	/* label */
	};
	char *stones[] = {
		"AB",	/* add black */
		"AW",	/* add white */
		"AE"	/* add empty */
		"TB",	/* black territory */
		"TW"	/* white territory */
	};
	/* no newline after these */
	char *moveprops[] = {
		"BL", "WL", "OB", "OW", "CR"
	};
	struct property *q;
	int sameline;

	if (parsecomments) {
		/* check for C[] in root node and try to parse */
		/* note: this will reparse things just added */
		for (q=p; q; q=q->next) {
			if (!strcmp(q->id, "C"))
				parse_comment(q);
		}
	}

	if (nonorm) {
		while (p) {
			sameline = 0;
			if (stripcomments)
				for (i=0; i<SIZE(strip); i++)
					if (!strcmp(p->id, strip[i]))
						goto skip0;
			for (i=0; i<SIZE(moveprops); i++)
				if (!strcmp(p->id, moveprops[i])) {
					movesonthisline = movesperline;
					sameline = 1;
				}
			if (!sameline && !did_output++)
				fprintf(outf, "\n");
			fprintf(outf, "%s", p->id);
			write_propvalues(p->val);
			if (!sameline) {
				fprintf(outf, "\n");
				movesonthisline = 0;
			}
		skip0:
			p = p->next;
		}
		return;
	}

	/* merge adjacent AB[] fields */
	for (q=p; q; q=q->next) {
		for(i=0; i<SIZE(stones); i++) {
			if (strcmp(q->id, stones[i]))
				continue;
			while (q->next && !strcmp(q->id, q->next->id))
				merge_stones(q);
		}
	}

	/* make two passes:
	   first the known properties in well-defined order,
	   then the rest */

	for (i=0; i<SIZE(known); i++)
		known[i].p = NULL;

	for (q=p; q; q=q->next) {
		/* normalize some known things */
		if (!strcmp(q->id, "BR")) /* B rank */
			normalize_rank(q->val);
		if (!strcmp(q->id, "WR")) /* W rank */
			normalize_rank(q->val);
		if (!strcmp(q->id, "TM")) /* time */
			normalize_time(q->val);
		if (!strcmp(q->id, "KM")) /* komi */
			normalize_komi(q->val);
		if (!strcmp(q->id, "RE")) /* result */
			normalize_result(q, q->val);
		if (!strcmp(q->id, "DT")) { /* date */
			if (dateck) {
				char *od, *nd;
				od = q->val->val;
				normalize_date(q->val);
				nd = q->val->val;
				if (strcmp(od, nd))
					fprintf(stderr, "date %s becomes %s\n",
						od, nd);
			} else
				normalize_date(q->val);
		}
		for(i=0; i<SIZE(stones); i++) {
			if (!strcmp(q->id, stones[i])) {
				normalize_stones(q->val);
				break;
			}
		}
#if 0
		struct property *id, *ff;

		if (!strcmp(q->id, "ID"))
			id = q;
		if (!strcmp(q->id, "FF"))
			ff = q;
#endif
		/* keep known things apart */
		for(i=0; i<SIZE(known); i++) {
			if (!strcmp(q->id, known[i].id)) {
				known[i].p = q;
				break;
			}
		}
	}

	for (i=0; i<SIZE(known); i++) {
		if ((q = known[i].p) != NULL) {
			if (q->val->next == NULL &&
			    q->val->val[0] == 0)
				continue;
			if (!did_output++)
				fprintf(outf, "\n");
			fprintf(outf, "%s", q->id);
			write_propvalues(q->val);
			fprintf(outf, "\n");
		}
	}

	while (p) {
		int single = (p->val->next == NULL);
		int empty = (p->val->val[0] == 0);
		sameline = 0;

		/* are there other empty properties that should be kept? */
		if (single && empty && strcmp(p->id, "VW"))
			goto skip;

		for (i=0; i<SIZE(known); i++)
			if (p == known[i].p)
				goto skip;
		for (i=0; i<SIZE(ignore); i++)
			if (!strcmp(p->id, ignore[i]))
				goto skip;
		if (stripcomments)
			for (i=0; i<SIZE(strip); i++)
				if (!strcmp(p->id, strip[i]))
					goto skip;
		for (i=0; i<SIZE(moveprops); i++)
			if (!strcmp(p->id, moveprops[i])) {
				movesonthisline = movesperline;
				sameline = 1;
			}
		if (!sameline && !did_output++)
			fprintf(outf, "\n");
		fprintf(outf, "%s", p->id);
		write_propvalues(p->val);
		if (!sameline) {
			fprintf(outf, "\n");
			movesonthisline = 0;
		}
	skip:
		p = p->next;
	}
}

static int
is_move(struct property *p) {
	return (p && p->val->next == NULL &&
		(!strcmp(p->id, "B") || !strcmp(p->id, "W")));
}

static void
write_move(struct property *p) {
	fprintf(outf, "%s[%s]", p->id, p->val->val);
}

/* push move out of the rootnode */
static void
pushdown_moves(struct node *n) {
	struct property **p0, *p, *q;
	struct node *n0, *nn;

	n0 = n;
	p0 = &((*n).p);
	p = *p0;
	while (p) {
		if (is_move(p)) {
			nn = xmalloc(sizeof(*nn));
			nn->next = n0->next;
			n0->next = nn;
			n0 = nn;

			nn->p = p;
			q = p->next;
			p->next = 0;
			*p0 = p = q;
			continue;
		}

		p0 = &((*p).next);
		p = *p0;
	}
}

static void
write_nodesequence(struct node *n) {
	struct property *p;

	if (n == rootnode)
		pushdown_moves(n);
	
	while (n) {
		p = n->p;
		if (is_move(p)) {
			if (movesonthisline == movesperline) {
				fprintf(outf, "\n");
				movesonthisline = 0;
				if (invariation)
					fprintf(outf, "%*s", gtlevel-1, "");
			}
			movesonthisline++;

			fprintf(outf, ";");
			write_move(p);
			p = p->next;
		} else {
			fprintf(outf, ";");
		}
		if (p)
			write_property_sequence(p);
		if (n == rootnode)
			fprintf(outf, "\n");
		n = n->next;
	}
}

/* forward declaration */
static void write_gametree_sequence(struct gametree *g);

static void
write_gametree(struct gametree *g) {
	int mkfile, parens;

	if (gtlevel == 0 && extractfile && ++gamect != extractfile)
		return;
	gtlevel++;
	mkfile = (splittofiles && gtlevel == 1);
	parens = (gtlevel == 1 || !stripcomments);
	if (mkfile)
		create_outfile(g);
	if (parens)
		fprintf(outf, "(");
	if (gtlevel == 1)
		rootnode = g->nodesequence;
	write_nodesequence(g->nodesequence);
	write_gametree_sequence(g->firstchild);
	if (parens) {
		int sp = gtlevel-1;

		fprintf(outf, ")\n");
		movesonthisline = 0;
		/* peek ahead: does a closing parenthesis follow? */	
		if (!g->nextsibling)
			sp--;
		if (sp > 0)
			fprintf(outf, "%*s", sp, "");
	}
	if (mkfile)
		fclose(outf);
	gtlevel--;
	invariation = (gtlevel != 0);
	skipping = (stripcomments && invariation);
}

static void
write_gametree_sequence(struct gametree *g) {
	while (g) {
		if (!skipping)
			write_gametree(g);
		g = g->nextsibling;
	}
}

static int
iswhitespace(int c) {
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
		c == '\f' || c == '\v');
}

static int
is_upper_case(int c) {
	return ('A' <= c && c <= 'Z');
}

static int
is_lower_case(int c) {
	return ('a' <= c && c <= 'z');
}

static int
is_letter(char c) {
	return is_upper_case(c) || is_lower_case(c);
}

static int
is_digit(char c) {
	return ('0'<= c && c <= '9');
}

static int
is_reasonable_year(int y) {
	return (y >= 1000 && y < 2100);
}

static int
maybe_short_19year(int y) {
	return (y >= 32 && y <= 99);	/* not a day or a 2000+ year */
}

static int
is_reasonable_month(int m) {
	return (m >= 1 && m <= 12);
}

static int
is_reasonable_day(int d) {
	return (d >= 1 && d <= 31);
}

static int
is_reasonable_monthday(int d) {
	return is_reasonable_month(d/100) &&
		is_reasonable_day(d%100);
}

static int
is_reasonable_yearmonthday(int d) {
	return is_reasonable_year(d/10000) &&
		is_reasonable_month((d/100)%100) &&
		is_reasonable_day(d%100);
}

static int
starts_with(char *s, char *t) {
	while (*t)
		if (*s++ != *t++)
			return 0;
	return (*t == 0);
}

static int
is_iso_standard_date(char *s) {
	int yy, mm, dd, y2, m2, d2, num[3], nct, n;
	char *se;

	/* expect yyyy-mm-dd,ee,nn-ff or yyyy-mm-dd..ee or so */
	yy = mm = dd = -1;
	nct = 0;
	while (*s) {
		n = strtoul(s, &se, 10);
		if (se-s != 2 && se-s != 4)
			return 0;
		s = se;
		if (nct == 3)
			return 0;
		num[nct++] = n;
		if (*s == '-') {
			if (nct == 3)
				return 0;
			s++;
			continue;
		}

		if (*s == 0) {
			goto ok;
		}
		if (*s == ',') {
			s++;
			goto ok;
		}
		if (s[0] == '.' && s[1] == '.') {
			s += 2;
			goto ok;
		}

		return 0;
	ok:
		d2 = num[--nct];
		m2 = (nct ? num[--nct] : mm);
		y2 = (nct ? num[--nct] : yy);
		if (y2 > yy)
			goto later;
		if (y2 == yy && m2 > mm)
			goto later;
		if (y2 == yy && m2 == mm && d2 >= dd)
			goto later;
		return 0;
	later:
		if (y2 == -1)
			return 0;
		yy = y2;
		mm = m2;
		dd = d2;
	}
	return 1;
}

#define TYPE_SEP	1
#define TYPE_INT	2
#define TYPE_YEAR	3
#define TYPE_MONTH	4
#define TYPE_DAY	5
#define	TYPE_RANGESEP	6
#define TYPE_ORSEP	7
#define TYPE_OFSEP      8
#define TYPE_QSEP	9

static char *
getsep(int a) {
	char *sep;

	switch (a) {
	case TYPE_RANGESEP:
		sep = ".."; break;
	case TYPE_ORSEP:
		sep = " or "; break;
	default:
		sep = ",";
	}
	return sep;
}

static void
normalize_date(struct propvalue *pv) {
	char *p, *s, *sep;
	int i, dd, mm, yy, ddp, mmp, yyp;
	int question = 0;
	int month_in_txt = 0;
#define DSZ 200
#define XTRA 10
	char date[DSZ], odate[2*DSZ], pdate[15];
	char *months[12] = {
		"jan", "feb", "mar", "apr", "may", "jun",
		"jul", "aug", "sep", "oct", "nov", "dec"
	};
	char *ordinals[4] = {
		"st", "nd", "rd", "th"
	};
	struct item {
		int val;
		int type;
	} items[DSZ];
	int itemct;
	int ni[DSZ], nict, di[DSZ], dict, mi[DSZ], mict, yi[DSZ], yict;
	int ti[DSZ], tict, ai[3], aict;

	/* strings in UTF-8, GB2312, Big5, SJIS */
#define DUMMY "\xff\xff"
	char *((ch_ignore[])[4]) = {
		{ "播放", "\xb2\xa5\xb7\xc5", DUMMY,
		  "\x94\x64\x95\xfa" },		/* broadcast */
		{ "日本", "\xc8\xd5\xb1\xbe", "\xa4\xe9\xa5\xbb",
		  "\x93\xfa\x96\x7b" },		/* Nihon (not "day") */
		{ "日付は放送日", DUMMY, DUMMY, DUMMY }
// sometimes a time follows:
// 上午 = morning, 上午 9:0 = 9 a.m.
	};
	char *ch_years[4] = {
		"年", "\xc4\xea", "\xa6\x7e", "\x94\x4e"
	};
	char *ch_months[4] = {
		"月", "\xd4\xc2", "\xa4\xeb", "\x8c\x8e"
	};
	char *ch_intercalary_months[4] = {
		"闰", "\xc8\xf2", DUMMY, DUMMY
	};
	char *ch_days[4] = {
		"日", "\xc8\xd5", "\xa4\xe9", "\x93\xfa"
	};
	struct repl {
		char *repl;
		char *ch[4];
	} ch_replace[] = {
		{ ",",   { "、", "\xa1\xa2", "\xa1\x42", "\x81\x41" }},
		{ "or ", { "或", "\xbb\xf2", "\xa9\xce", "\x88\xbd" }},
		{ "..",  { "至", "\xd6\xc1", "\xa6\xdc", "\x8e\x8a" }},
		// dash can be U+2212
		{ "-", {"−", 0, 0, 0 }},
		// comma can be U+ff0c
		{ ",", {"，", 0, 0, 0 }},
	};
	char *fat_digs[10] = {
		"０", "１", "２", "３", "４", "５", "６", "７", "８", "９"
	};
	struct era {
		char *era;
		int offset;
	} eras[4] = {
		{ "平成", 1988 },
		{ "昭和", 1925 },
		{ "大正", 1911 },
		{ "明治", 1867 }
	};

	if (nodatenorm)
		return;

	if (pv->next)
		errexit("multiple dates?");

	if (is_iso_standard_date(pv->val))
		return;
	
	if (strlen(pv->val) >= sizeof(date) - XTRA)
		errexit("date spec too large");

	strcpy(date+XTRA, pv->val);
	p = date+XTRA;		/* leave room for possible substitutions */

	/* Generate 1999-12-30,31 */
	/* Accept
	 *	   December 30&31, 1999
	 *	   30/31 december 1999
	 *	   1999-Dec-30,31
	 *	   1999/12/30,1999/12/31
	 *	   1999-01-31,02-01
	 *	   1999-12-30,31,2000-01-01
	 *	   1999-12-29..31
	 *	   1999-12-29/30/31
	 *	   1999-12-29,30 or 31
	 *	   1999-12-29,30 and 31
	 *	   31st of December 1999
	 *	   30 and 31 December 1999.
	 *	   December 31 '99
	 *	   19991230
	 *	   Jan-93
	 *	   2013-0629
	 *	   1999-12-06..2000-01-03
	 *	   10-11.01.2002
	 *	   1901-3-26 - 4-14
	 *	   3 October to 7 November 1896.
	 *	   1935年9月7日播放
	 *	   (天保14年) 1843-4-19
	 *	   21 May, 7, 12 and 30 June, 1, 19 and 25 July, 6, 16 and 25 August, 6, 13 and 30 September, 7, 11 and 29 October and 12, 22 and 22 November 1920.
	 */

	/* Convert all to lower case */
        for (s = p; *s; s++)
                if (is_upper_case(*s))
                        *s += ('a' - 'A');

	/* replace fat digits by ordinary digits */
	for (s = p; *s; s++) {
		for (i=0; i<10; i++) {
			if (starts_with(s, fat_digs[i])) {
				int m = strlen(fat_digs[i]);
				char *q, *r;

				*s = '0' + i;
				q = s+1;
				r = s+m;
				while ((*q++ = *r++)) ;
				break;
			}
		}
	}

	/* recognize recent era dates */
	for (i=0; i<4; i++) {
		if (starts_with(p, eras[i].era)) {
			char *pe;
			int m = strlen(eras[i].era);	/* more than 4 bytes */
			int n = strtoul(p+m, &pe, 10);
			int y = eras[i].offset + n;

			if (starts_with(pe, "年") && y >= 1873 && n < 200) {
				sprintf(p, "%d", y);
				s = p+4;
				while ((*s++ = *pe++)) ;
			}
			break;
		}
	}

	/* Parse into integers and separators */
	/* Delete whitespace and initial separators */
	/* Combine multiple separators */
	/* Preserve (or delete?) parenthetical parts -
	   they may be Japanese or Chinese calendar designations,
	   or tell about broadcast dates, newspaper dates etc. */
	s = p;
	itemct = 0;
	while (*s) {
		if (iswhitespace(*s)) {
			s++;
			continue;
		}
		if (is_digit(*s)) {
			items[itemct].val = atoi(s);
			items[itemct].type = TYPE_INT;
			itemct++;
			while (is_digit(*s))
				s++;
			continue;
		}
#if 1
		/* maybe skip parenthetical part */
		if (*s == '(') {
			while (*s && *s++ != ')');
			continue;
		}
		/* maybe skip a time designation [21:15] */
		if (*s == '[') {
			while (*s && *s++ != ']');
			continue;
		}
#endif
		if (is_lower_case(*s)) {
			if (!strncmp("or ", s, 3)) {
				items[itemct].val = 0;
				items[itemct].type = TYPE_ORSEP;
				itemct++;
				s += 3;
				continue;
			}
			if (!strncmp("of ", s, 3)) {
				items[itemct].val = 0;
				items[itemct].type = TYPE_OFSEP;
				itemct++;
				s += 3;
				continue;
			}
			if (!strncmp("and ", s, 4)) {
				/* probably it is OK to treat this as a , */
				items[itemct].val = ',';
				items[itemct].type = TYPE_SEP;
				itemct++;
				s += 4;
				continue;
			}
			if (!strncmp("to ", s, 3)) {
				/* probably it is OK to treat this as .. */
				items[itemct].val = '.';
				items[itemct].type = TYPE_RANGESEP;
				itemct++;
				s += 3;
				continue;
			}
			for (i=0; i<4; i++)
				if(!strncmp(ordinals[i], s, 2))
					break;
			if (i < 4 && !is_lower_case(s[2]) && itemct > 0) {
				if (items[itemct-1].type == TYPE_INT)
					items[itemct-1].type = TYPE_DAY;
				if (items[itemct-1].type != TYPE_DAY) {
					warn("unexpected ordinal in date '%s'",
					     pv->val);
					return;
				}
				s += 2;
				continue;
			}
			for (i=0; i<12; i++)
				if(!strncmp(months[i], s, 3))
					break;
			if (i < 12) {
				month_in_txt = 1;
				items[itemct].val = i+1;
				items[itemct].type = TYPE_MONTH;
				itemct++;
				while (is_lower_case(*s))
					s++;
				continue;
			}
			warn("unrecognized text in date '%s' at '%s'",
			     pv->val, s);
			return;
		}

		/* treat ~ like .. */
		if (*s == '~') {
			items[itemct].val = '~';
			items[itemct].type = TYPE_RANGESEP;
			itemct++;
			s++;
			continue;
		}

		for (i=0; i<SIZE(ch_ignore); i++) {
			char **cp = ch_ignore[i];
			int j;

			for (j=0; j<4; j++)
				if (starts_with(s, cp[j])) {
					s += strlen(cp[j]);
					goto cont;
				}
		}

		for (i=0; i<SIZE(ch_years); i++) {
			if (starts_with(s, ch_years[i])) {
				s += strlen(ch_years[i]);
				if (itemct == 0 ||
				    items[itemct-1].type != TYPE_INT) {
					warn("unexpected Year character");
					continue;
				}
				items[itemct-1].type = TYPE_YEAR;
				goto cont;
			}
		}

		for (i=0; i<SIZE(ch_months); i++) {
			if (starts_with(s, ch_months[i])) {
				s += strlen(ch_months[i]);
				if (itemct == 0 ||
				    items[itemct-1].type != TYPE_INT) {
					warn("unexpected Month character");
					continue;
				}
				items[itemct-1].type = TYPE_MONTH;
				goto cont;
			}
		}

		for (i=0; i<SIZE(ch_intercalary_months); i++) {
			if (starts_with(s, ch_intercalary_months[i])) {
				s += strlen(ch_intercalary_months[i]);
				/* note: precedes month, unlike 月 */
				warn("intercalary months not supported");
				goto cont;
			}
		}

		for (i=0; i<SIZE(ch_days); i++) {
			if (starts_with(s, ch_days[i])) {
				s += strlen(ch_days[i]);
				if (itemct == 0 ||
				    (items[itemct-1].type != TYPE_INT &&
				     items[itemct-1].type != TYPE_DAY)) {
					warn("unexpected Day character %d %d",
					     itemct, items[itemct-1].type);
					continue;
				}
				items[itemct-1].type = TYPE_DAY;
				goto cont;
			}
		}

		for (i=0; i<SIZE(ch_replace); i++) {
			struct repl *rp = ch_replace + i;
			int j;

			for (j=0; j<4; j++) if (rp->ch[j]) {
				if (starts_with(s, rp->ch[j])) {
					int m1 = strlen(rp->ch[j]);
					int m2 = strlen(rp->repl);
					s+= m1-m2;
					if (s < date)
						errexit("XTRA underflow");
					memcpy(s, rp->repl, m2);
					goto cont;
				}
			}
		}

		if (itemct == 0) {
			warn("initial separator in date '%s'", pv->val);
			return;
		}
		if (items[itemct-1].type == TYPE_SEP) {
			if (*s == '?')
				goto cont1;
			if (*s == '.' && items[itemct-1].val == '.') {
				items[itemct-1].type = TYPE_RANGESEP;
			} else {
				warn("consecutive separators %c%c "
				     "in date '%s', tail '%s'",
				     items[itemct-1].val, *s, pv->val, s);
				items[itemct-1].val = 0;
			}
		} else {
			items[itemct].val = *s;
			items[itemct].type = TYPE_SEP;
			if (*s == '\'')
				items[itemct].type = TYPE_QSEP;
			itemct++;
		}
	cont1:
		s++;
	cont:;
	}

	/* An unknown day is sometimes written as 1842-03-00 */
	if (itemct > 0 && items[itemct-1].type == TYPE_INT &&
	    items[itemct-1].val == 0)
		itemct--;

	/* Delete final separators */
	/* Not an error: dates like 1999-12- do occur. */
	while (itemct > 0 && items[itemct-1].type == TYPE_SEP) {
		if (items[itemct-1].val == '?')
			question = 1;
		itemct--;
	}

	/* in a context like mm/dd/yy or dd/mm/yy the third part
	   is the year - if it is 38 then probably 1938 was meant */
	/* ... */

	/* Very large numbers are yearmonthday without separator */
	for(i=0; i<itemct; i++) {
		if (items[i].type == TYPE_INT) {
			if (is_reasonable_yearmonthday(items[i].val)) {
				int j, d;

				for (j=itemct-1; j>i; j--)
					items[j+2] = items[j];
				itemct += 2;
				d = items[i].val;
				items[i].val = d/10000;
				items[i+1].val = (d/100)%100;
				items[i+2].val = d%100;
				items[i].type = TYPE_YEAR;
				items[i+1].type = TYPE_MONTH;
				items[i+2].type = TYPE_DAY;
				i += 2;
			}
		}
	}

	/* 4-digit numbers in the range 0101..1231 may be monthday */
	/* we lost the info about "4 digits" */
	for(i=0; i<itemct; i++) {
		if (items[i].type == TYPE_INT) {
			if (is_reasonable_monthday(items[i].val)) {
				int j, d;

				for (j=itemct-1; j>i; j--)
					items[j+1] = items[j];
				itemct++;
				d = items[i].val;
				items[i].val = d/100;
				items[i].type = TYPE_MONTH;
				items[i+1].val = d%100;
				items[i+1].type = TYPE_DAY;
				i++;
			}
		}
	}

	/* TYPE_QSEP only occurs in front of years */
	for(i=0; i<itemct; i++) {
		if (items[i].type == TYPE_QSEP) {
			if (i == itemct-1 ||
			    (items[i+1].type != TYPE_INT &&
			     items[i+1].type != TYPE_YEAR) ||
			    items[i+1].val >= 100) {
				warn("unexpected ' in date _%s_", pv->val);
				return;
			}
			items[i+1].type = TYPE_YEAR;
			items[i+1].val += 1900;
			/* perhaps '12 means 2012? */
		}
	}

	/* Recognize numbers as years or days */
	for(i=0; i<itemct; i++) {
		if (items[i].type == TYPE_INT) {
			if (is_reasonable_year(items[i].val)) {
				items[i].type = TYPE_YEAR;
			} else if (maybe_short_19year(items[i].val)) {
				items[i].type = TYPE_YEAR;
				items[i].val += 1900;
			} else if (!is_reasonable_day(items[i].val)) {
				warn("unrecognized number in date '%s'",
				     pv->val);
				return;
			} else if (!is_reasonable_month(items[i].val)) {
				items[i].type = TYPE_DAY;
			}
		}
	}

	/* TYPE_OFSEP only occurs between days and months */
	for(i=0; i<itemct; i++) {
		if (items[i].type == TYPE_OFSEP) {
			if (i == 0 || i == itemct-1 ||
			    (items[i-1].type != TYPE_INT &&
			     items[i-1].type != TYPE_DAY) ||
			    (items[i+1].type != TYPE_INT &&
			     items[i+1].type != TYPE_MONTH)) {
				warn("unexpected 'of' in date '%s'", pv->val);
				return;
			}
			items[i-1].type = TYPE_DAY;
			items[i+1].type = TYPE_MONTH;
		}
	}

	/* TYPE_RANGESEP separates possibly incomplete dates */
	/* expect at most one of d,m,y before and after */
	for(i=0; i<itemct; i++) {
		if (items[i].type == TYPE_RANGESEP) {
			int j, seen;

#define D 1
#define M 2
			seen = 0;
			for (j=0; j<i; j++)
				if (items[j].type == TYPE_DAY)
					seen |= D;
			for (j=0; j<i; j++)
				if (items[j].type == TYPE_MONTH)
					seen |= M;
			for (j=0; j<i; j++)
				if (items[j].type == TYPE_INT && seen)
					items[j].type = (seen & D)
						? TYPE_MONTH : TYPE_DAY;
			seen = 0;
			for (j=i+1; j<itemct; j++)
				if (items[j].type == TYPE_DAY)
					seen |= D;
			for (j=i+1; j<itemct; j++)
				if (items[j].type == TYPE_MONTH)
					seen |= M;
			for (j=i+1; j<itemct; j++)
				if (items[j].type == TYPE_INT && seen)
					items[j].type = (seen & D)
						? TYPE_MONTH : TYPE_DAY;
#undef D
#undef M
		}
	}

	/* if we know some months since they were given in text,
	   then all unknown numbers are days */
	if (month_in_txt) {
		for(i=0; i<itemct; i++) {
			if (items[i].type == TYPE_INT &&
			    is_reasonable_day(items[i].val))
				items[i].type = TYPE_DAY;
		}
	}

	/* First try to find an ISO standard sequence with , separator */
	/* Regard ~ as a synonym for .. */

	yy = mm = dd = 0;
	yyp = mmp = ddp = 0;
	aict = 0;
	odate[0] = 0;
	sep = "";
	for(i=0; i<=itemct; i++) {
		int j;

		/* collect y,m,d */
		if (i == itemct) {
			/* nothing */
		} else if (items[i].type == TYPE_RANGESEP) {
			/* nothing */
		} else if (items[i].type == TYPE_SEP) {
			/* early separator, probably '-' */
			if (items[i].val != ',')
				continue;
		} else {
			ai[aict++] = i;
			if (aict < 3)
				continue;

			/* have y,m,d - a following separator must be , or ~ */
			if (i+1 < itemct) {
				i++;
				if (items[i].type != TYPE_RANGESEP &&
				    (items[i].type != TYPE_SEP ||
				     items[i].val != ','))
					goto fail;
			}
		}
		if (aict == 0)
			continue;
		j = ai[--aict];
		if (items[j].type != TYPE_DAY && items[j].type != TYPE_INT)
			goto fail;
		dd = items[j].val;
		if (aict) {
			j = ai[--aict];
			if (items[j].type != TYPE_MONTH &&
			    items[j].type != TYPE_INT)
				goto fail;
			mm = items[j].val;
		}
		if (aict) {
			j = ai[--aict];
			if (items[j].type != TYPE_YEAR)
				goto fail;
			yy = items[j].val;
		}
		if (!yy)
			goto fail;

		if (yy != yyp)
			sprintf(pdate, "%s%d-%02d-%02d", sep, yy, mm, dd);
		else if (mm != mmp)
			sprintf(pdate, "%s%02d-%02d", sep, mm, dd);
		else
			sprintf(pdate, "%s%02d", sep, dd);
		if (strlen(odate) + strlen(pdate) + 2 >= sizeof(odate))
			errexit("date field to long");
		strcat(odate, pdate);
		yyp = yy;
		mmp = mm;
		ddp = dd;

		if (i < itemct) {
			if (items[i].type == TYPE_RANGESEP)
				sep = "..";
			else if (items[i].type == TYPE_SEP)
				sep = ",";
		}
	}
	/* success */
	p = odate;
	if (*p == ',')
		p++;
	goto ret;

fail:
	/* sort and count; ti[] gets all non-year integers */

	nict = dict = mict = yict = tict = 0;
	for(i=0; i<itemct; i++) {
		switch(items[i].type) {
		case TYPE_INT:
			ni[nict++] = i;
			ti[tict++] = i;
			break;
		case TYPE_YEAR:
			yi[yict++] = i;
			break;
		case TYPE_MONTH:
			mi[mict++] = i;
			ti[tict++] = i;
			break;
		case TYPE_DAY:
			di[dict++] = i;
			ti[tict++] = i;
			break;
		}
	}

	/* Dispose of easy cases */
	/* No year? */
	if (yict == 0) {
		warn("no year found in date '%s'?", pv->val);
		return;
	}

	yy = items[yi[0]].val;

	/* Years only? */
	if (tict == 0) {
		char *sep;

		odate[0] = 0;
		for (i=0; i<yict; i++) {
			sep = (i ? "," : "");
			if (i && yi[i] == yi[i-1]+2)
				sep = getsep(items[yi[i]-1].type);
			yy = items[yi[i]].val;
			sprintf(pdate, "%s%d", sep, yy);
			strcat(odate, pdate);
		}
		p = odate;
		goto ret;
	}

	/* List of dates, no unknowns? */
	if (nict || !mict || !dict)
		goto nonsimple;

	/* and only , separators? */
	for(i=0; i<itemct; i++) {
		switch(items[i].type) {
		case TYPE_RANGESEP:
		case TYPE_ORSEP:
		case TYPE_OFSEP:
		case TYPE_QSEP:
			goto nonsimple;
		case TYPE_SEP:
			if (items[i].val != ',')
				goto nonsimple;
		}
	}

	/* d m y - style? */
	if (di[0] < mi[0] && mi[0] < yi[0] &&
	    di[dict-1] < mi[mict-1] && mi[mict-1] < yi[yict-1]) {
		int iy, im, id, y, m, d, yy, mm, dd;

		p = odate;
		iy = im = id = 0;
		y = yi[iy++];
		m = mi[im++];
		d = di[id++];
		yy = items[y].val;
		mm = items[m].val;
		dd = items[d].val;
		p += sprintf(p, "%d-%02d-%02d", yy, mm, dd);
		while (id < dict) {
			d = di[id++];
			dd = items[d].val;
			if (d < m) {
				p += sprintf(p, ",%02d", dd);
				continue;
			}
			if (im == mict)
				goto baddate;
			m = mi[im++];
			mm = items[m].val;
			if (d < m && m < y) {
				p += sprintf(p, ",%02d-%02d", mm, dd);
				continue;
			}
			if (iy == yict)
				goto baddate;
			y = yi[iy++];
			yy = items[y].val;
			if (d < m && m < y) {
				p += sprintf(p, ",%d-%02d-%02d", yy, mm, dd);
				continue;
			}
		baddate:
			warn("bad date '%s'", pv->val);
			return;
		}
		if (im < mict || iy < yict)
			goto baddate;
		p = odate;
		goto ret;
	}

nonsimple:
	if (tict == dict) {
		warn("no month in date '%s'", pv->val);
		return;
	}

	/* no y,y-m today, or any such thing; two years require
	   y1-m1-d1,y2-m2-d2 two months and two days */
	if (yict > 1 && tict < 4) {
		warn("bad date '%s'", pv->val);
		return;
	}

	/* d1 m1 y1 to d2 m2 y2? */
	if (yict == 2 && mict == 2 && tict == 4) {
		if (yi[0] == mi[0]+1 && yi[1] == mi[1]+1) {
			int y1,y2,m1,m2,d1,d2;
			char *sep;

			y1 = items[yi[0]].val;
			y2 = items[yi[1]].val;
			m1 = items[mi[0]].val;
			m2 = items[mi[1]].val;
			while (nict)
				di[dict++] = ni[--nict];
			if (di[0] > di[1]) {
				int tmp = di[0];
				di[0] = di[1];
				di[1] = tmp;
			}
			if (di[0]+1 == mi[0] && di[1]+1 == mi[1]) {
				d1 = items[di[0]].val;
				d2 = items[di[1]].val;
				if (di[1] == yi[0]+2) {
					sep = getsep(items[yi[0]+1].type);
					sprintf(odate,
						"%d-%02d-%02d%s%d-%02d-%02d",
						y1, m1, d1, sep, y2, m2, d2);
					p = odate;
					goto ret;
				}
			}
		}
	}

	/* Year and month only? */
	if (tict == 1) {
		if (nict)
			mi[mict++] = ni[--nict];
		mm = items[mi[0]].val;
		sprintf(odate, "%d-%02d", yy, mm);
		p = odate;
		goto ret;
	}

	/* Single day? */
	if (tict == 2) {
		if (mict == 2 || dict == 2) {
			warn("bad date '%s'", pv->val);
			return;
		}
		if (!dict)
			di[dict++] = ni[--nict]; /* day follows month */
		if (!mict)
			mi[mict++] = ni[--nict];
		mm = items[mi[0]].val;
		dd = items[di[0]].val;
		sprintf(odate, "%d-%02d-%02d", yy, mm, dd);
		p = odate;
		goto ret;
	}

	/* Two days in the same month? */
	if (tict == 3) {
		int cc,dd,ee;
		char *sep;

		/* month is 1st unknown, unless the two
		   remaining days would be out-of-order:
		   dd,ee.mm.yy or mm.dd,ee.yy or yy.mm.dd,ee */

		if (mict > 1 || dict > 2) {
			warn("bad date '%s'", pv->val);
			return;
		}

		cc = items[ti[0]].val;
		dd = items[ti[1]].val;
		ee = items[ti[2]].val;

		if (ee > dd && cc <= 12 && (mict == 0 || mi[0] == ti[0])) {
			mi[0] = ti[0];
			di[0] = ti[1];
			di[1] = ti[2];
		} else if (dd > cc && ee <= 12 &&
			   (mict == 0 || mi[0] == ti[2])) {
			mi[0] = ti[2];
			di[0] = ti[0];
			di[1] = ti[1];
		} else {
			warn("bad date '%s'", pv->val);
			return;
		}

		sep = ",";
		if (di[1] == di[0]+2)
			sep = getsep(items[di[0]+1].type);

		mm = items[mi[0]].val;
		dd = items[di[0]].val;
		ee = items[di[1]].val;

		sprintf(odate, "%d-%02d-%02d%s%02d",
			yy, mm, dd, sep, ee);
		p = odate;
		goto ret;
	}

	/* Go to the first unknown number, and then possibly
	   further until a year is seen. If there was no month
	   then the unknown was a month, otherwise it was a day.
	   E.g. mm dd,yy or mm dd-ee,yy or yy-mm-dd or yy-mm-dd,ee */
	if (nict > 0) {
		int j = ni[0];
		int k = yi[0];
		int maxjk = ((j > k) ? j : k);

		if (mict == 0 || mi[0] > maxjk) {
			for(i = mict; i>0; i--)
				mi[i] = mi[i-1];
			mi[0] = j;
			mict++;
			nict--;
			for(i = 0; i<nict; i++)
				ni[i] = ni[i+1];
		}
	}

	if (mict == 0) {
		warn("bad date '%s'", pv->val);
		return;
	}
	mm = items[mi[0]].val;

	/* Two days like yy-mm-cc,nn-dd? */
	if (yict == 1 && mict == 1 && nict > 0 && nict+dict == 3) {

		/* if days not in order, then the middle one
		   may be a month */
		if (dict == 2) {
			int cc, nn;

			cc = items[di[0]].val;
			dd = items[di[1]].val;
			nn = items[ni[0]].val;
			if (cc >= dd || (ni[0] >= di[0] && ni[0] <= di[1])) {
				sprintf(odate, "%d-%02d-%02d,%02d-%02d",
					yy, mm, cc, nn, dd);
				p = odate;
				goto ret;
			}
		} else if (dict == 1) {
			int cc, ee;

			cc = items[di[0]].val;
			dd = items[ni[0]].val;
			ee = items[ni[1]].val;
			if (dd >= ee) {
				/* not both days */
				if (mi[0] < di[0] && di[0] < ni[0]) {
					sprintf(odate,
						"%d-%02d-%02d,%02d-%02d",
						yy, mm, cc, dd, ee);
					p = odate;
					goto ret;
				}
			}
					
		}
	}

	/* Two days like cc-mm,dd-nn-yy? */
	if (yict == 1 && mict == 2 && dict == 2 && nict == 0 &&
	    mi[0] > di[0] && mi[1] > di[1] && di[1] == mi[0]+2) {
		int cc, nn;
		char *sep;

		cc = items[di[0]].val;
		dd = items[di[1]].val;
		nn = items[mi[1]].val;
		sep = getsep(items[mi[0]+1].type);

		if (mm < nn) {
			sprintf(odate, "%d-%02d-%02d%s%02d-%02d",
				yy, mm, cc, sep, nn, dd);
			p = odate;
			goto ret;
		}
		if (mm == nn) {
			sprintf(odate, "%d-%02d-%02d%s%02d",
				yy, mm, cc, sep, dd);
			p = odate;
			goto ret;
		}
	}

	/* Three days like yy-mm-cc,dd,ee? */
	if (yict == 1 && mict == 1 && nict+dict == 3) {
		int cc,ee;
		char *sep1 = ",";
		char *sep2 = ",";

		if (nict == 3) {
			cc = items[ni[0]].val;
			dd = items[ni[1]].val;
			ee = items[ni[2]].val;
		} else if (nict == 2) {
			cc = items[ni[0]].val;
			dd = items[ni[1]].val;
			ee = items[di[0]].val;
		} else if (nict == 1) {
			cc = items[ni[0]].val;
			dd = items[di[0]].val;
			ee = items[di[1]].val;
		} else {
			cc = items[di[0]].val;
			dd = items[di[1]].val;
			ee = items[di[2]].val;
			if (di[1] == di[0]+2) {
				switch (items[di[0]+1].type) {
				case TYPE_ORSEP:
					sep1 = " or "; break;
				}
			}
			if (di[2] == di[1]+2) {
				switch (items[di[1]+1].type) {
				case TYPE_ORSEP:
					sep2 = " or "; break;
				}
			}
		}
		sprintf(odate, "%d-%02d-%02d%s%02d%s%02d",
			yy, mm, cc, sep1, dd, sep2, ee);
		p = odate;
		goto ret;
	}

	warn("complicated date '%s'", pv->val);	
	return;

ret:
	if (question)
		strcat(p, "?");
	pv->val = xstrdup(p);
}

static void
normalize_rank(struct propvalue *pv) {
#define RSZ 100
	char irank[RSZ], orank[RSZ];
	char *s, *t;
	int c, i, m;
	char *ordinals[4] = {
		"st", "nd", "rd", "th"
	};

	if (pv->next)
		errexit("multiple ranks?");

	if (strlen(pv->val) >= sizeof(irank))
		errexit("rank spec too large");
	strcpy(irank, pv->val);

	/* strip initial and final spaces, collapse multiple spaces */
	s = t = irank;
	while (*s == ' ')
		s++;
	while ((*t++ = *s++) != 0)
		if (s[-1] == ' ')
			while (*s == ' ')
				s++;
	while (t > irank && t[-1] == ' ')
		*--t = 0;

	/* Turn "9 dan" or "9-dan" or "9e dan" or "9th dan"
	   or "9 Dan" or "9dan" into "9d" */
	s = irank;
	t = orank;
	while (*s) {
		c = *t++ = *s++;
		if (!is_digit(c))
			continue;

		if (!strncasecmp(s, "dan", 3)) {
			m = 3;
			goto danseen;
		}
		if ((*s == ' ' || *s == '-') &&
		    !strncasecmp(s+1, "dan", 3)) {
			m = 4;
			goto danseen;
		}
		if (*s == 'e' && 
		    !strncasecmp(s+1, " dan", 4)) {
			m = 5;
			goto danseen;
		}
		if (is_lower_case(*s)) {
			for(i=0; i<4; i++)
				if(!strncmp(s, ordinals[i], 2))
					break;
			if (i<4 && s[2] == ' ' &&
			    !strncasecmp(s+3, "dan", 3)) {
				m = 6;
				goto danseen;
			}
		}

		if (!strncasecmp(s, "kyu", 3)) {
			m = 3;
			goto kyuseen;
		}
		if ((*s == ' ' || *s == '-') &&
		    !strncasecmp(s+1, "kyu", 3)) {
			m = 4;
			goto kyuseen;
		}

		continue;

	kyuseen:
		if (is_letter(s[m]))
			continue;
		s += m;
		*t++ = 'k';
		continue;

	danseen:
		if (is_letter(s[m]))
			continue;
		s += m;
		*t++ = 'd';

		/* Turn "6d pro" into "6p" */
		if (!strncasecmp(s, " pro", 4)) {
			s += 4;
			t--;
			*t++ = 'p';
		}
	}
	*t = 0;
	pv->val = xstrdup(orank);
}

static void
normalize_time(struct propvalue *pv) {
/* seen: 173 bytes */
#define TSZ 1000
	char itime[TSZ], otime[TSZ];
	char *s, *t;
	int c;

	if (pv->next)
		errexit("multiple times?");
	if (strlen(pv->val) >= sizeof(itime))
		errexit("time spec too large");

	strcpy(itime, pv->val);

	/* turn "8 hours each" or "8 hours" into "8h" */
	s = itime;
	t = otime;
	while (*s) {
		c = *t++ = *s++;
		if (is_digit(c) && *s == ' ' &&
		    !strncmp(s+1, "hours", 5)) {
			s += 6;
			*t++ = 'h';
			if (!strncmp(s, " each", 5))
				s += 5;
		}
	}
	*t = 0;
	pv->val = xstrdup(otime);
}

/* recognize "6,5" and "5 1/2" and "5.5 points." and "0.000000"
   and "2 3/4" and "none" and "Reverse, 20p" and "-64.50" */
/* not understood: "5/5", "5+", "66..5", strings in Chinese */
/* conjecturally understood: 214 = 2 1/4 = 2.25, 234 = 2 3/4 = 2.75 */
static void
normalize_komi(struct propvalue *pv) {
#define KSZ 100
	char ikomi[KSZ], okomi[KSZ];
	char *s, *t, *se, *z;
	unsigned long komi, komifrac, num, denom;
	int n, sgn, fraclen;

	if (pv->next)
		errexit("multiple komi?");
	if (strlen(pv->val) >= sizeof(ikomi))
		errexit("komi spec too large");

	strcpy(ikomi, pv->val);
	s = ikomi;
	komi = komifrac = fraclen = sgn = 0;

	/* no komi record */
	if (!strcmp(s, "無貼目記錄")) {
		pv->val = xstrdup("?");
		return;
	}

	/* remove final spaces and . */
	t = ikomi;
	while (*t)
		t++;
	while (t > ikomi && t[-1] == ' ')
		*--t = 0;
	if (t > ikomi && t[-1] == '.')
		*--t = 0;


	/* some ad hoc code to handle e.g. KM[黑贴6.5目] */
	/* remove final 目 if any */
	z = "目";
	n = strlen(z);
	if (t >= s + n && !strcmp(t-n, z)) {
		t -= n;
		*t = 0;
	}

	/* remove initial 黑贴 if any */
	z = "黑贴";
	n = strlen(z);
	if (!strncmp(s, z, n))
		s += n;

	/* find sign */
	while (*s == ' ')
		s++;
	if (*s == '-' && is_digit(s[1])) {
		sgn = 1;
		s++;
	}
	if (!strncasecmp(s, "reverse", 7)) {
		sgn = 1;
		s += 7;
		if (*s == ',')
			s++;
		while (*s == ' ')
			s++;
	}

	/* find value */
	if (is_digit(*s)) {
		komi = strtoul(s, &se, 10);
		if (*se == '.' || *se == ',') {
			s = se+1;
			komifrac = strtoul(s, &se, 10);
			fraclen = se-s;
			/* remove superfluous trailing 0s */
			while (fraclen && komifrac % 10 == 0) {
				komifrac /= 10;
				fraclen--;
			}
		}
		s = se;
	} else if (!strcasecmp(s, "none")) {
		komi = 0;
		s += 4;
	}
	while (*s == ' ')
		s++;

	/* possible fraction like 1/2 or 3/4 */
	if (!fraclen && is_digit(*s)) {
		num = strtoul(s, &se, 10);
		if (*se == '/') {
			denom = strtoul(se+1, &se, 10);
			while (denom != 0 && denom%2 == 0 && num%2 == 0) {
				denom /= 2;
				num /= 2;
			}
			if (num == 1 && denom == 2) {
				fraclen = 1;
				komifrac = 5;
				s = se;
			} else if ((num == 1 || num == 3) && denom == 4) {
				fraclen = 2;
				komifrac = num*25;
				s = se;
			}
		}
		/* in all other cases, we do not recognize the fraction */
	}

	/* strange values are most likely fractions */
	if (!fraclen && (komi == 214 || komi == 234)) {
		fraclen = 2;
		komi = 2;
		komifrac = (komi == 214) ? 25 : 75;
	}

	/* trailing words */
	if (!strncasecmp(s, "points", 6))
		s += 6;
	else if (!strncasecmp(s, "point", 5))
		s += 5;
	else if (*s == 'p')
		s++;

	if (*s && strcmp(s, "目")) {
		fprintf(stderr, "trailing junk %s in KM[%s]\n", s, pv->val);

		/* since we didn't understand the description, leave it */
		return;
	}

	if (opttojp) {
		/* if komi involves quarter points, multiply by two */
		if (fraclen == 2 && (komifrac == 25 || komifrac == 75)) {
			int k = 2*(100*komi+komifrac);
			komi = k/100;
			komifrac = (k/10)%10;
			fraclen = 1;
		}
	}

	t = okomi;
	if (sgn)
		t += sprintf(t, "-");
	t += sprintf(t, "%lu", komi);
	if (fraclen)
		t += sprintf(t, ".%0*lu", fraclen, komifrac);
	pv->val = xstrdup(okomi);
}

#if 0
/* remove [共]nnn手[。、,] from pv, and possibly add MN[nnn] */
/* s points at "手" */
static void
splitoff_moves(char *s, struct property *p, struct propvalue *pv) {
	char *s0 = pv->val;
	char *t = s;
	char *a = "共", *b = "完", *c = "。", *d = "、";
	int na = strlen(a), nb = strlen(b), nc = strlen(c), nd = strlen(d);

	while (t > s0 && t[-1] >= '0' && t[-1] <= '9')
		t--;
	if (s == t)
		return;		/* no digits seen */
	if (t >= s0+na && !strncmp(t-na, a, na))
		t -= na;
	s += strlen("手");
	if (!strncmp(s, b, nb))
		s += nb;
	if (!strncmp(s, c, nc))
		s += nc;
	if (!strncmp(s, d, nd))
		s += nd;
	if (*s == '.' || *s == ',')
		s++;
	while (*s == ' ')
		s++;
	while ((*t++ = *s++));	/* remove substring */
}
#endif

/* silence gcc warning about warn_unused_result */
unsigned long strtoul_dummy;

/* delete 目 in a result like RE[W+5目] */
/* change 和棋 (jigo) into 0 */
/* change 中押胜 into Resign */
/* todo: C[黑粘劫。共228手。黑胜4目] ->
   C[Black connects the ko. 228 moves. Black wins by 4 points. ] */
static void
delete_stones(struct propvalue *pv) {
	char *s, *se;

	s = pv->val;
	if (!(*s == 'B' || *s == 'W') || (s[1] != '+'))
		goto nxt;
	strtoul_dummy = strtoul(s+2, &se, 10);
	if (se == s+2)
		goto nxt;
	if (strcmp(se, "目"))
		goto nxt;
	pv->val = xstrdup_len(s, se-s);
	return;

nxt:
	s = pv->val;
	if ((*s == 'B' || *s == 'W') && (s[1] == '+'))
		s += 2;
	if (!strcmp(s, "和棋")) {
		pv->val = "0";
		return;
	}
	if (!strcmp(s, "中押胜")) {
		s -= 2;
		pv->val = (*s == 'B') ? "B+R" : "W+R";
		return;
	}
}

/*
 * try to recognize
 * "B+R" "B+R." "B+Resign" "B+resign" "B+resignation"
 * "B+T" "B+Time" "B+F" "B+Forfait" "B+Forfeit" "jigo" "J" "?" "unknown"
 * "Draw", "Draw - triple ko" "NR" "Null" "Void" "Void {Triple ko}"
 * "Politely given as a draw, but actually Black wins by 1 point."
 * "B+3.5" "B+3,5" "W wins" "W+0" "W+J" "W+?" "W+3points"
 * "Black wins by 1 1/2 pts." "Black wins by 11.5 point"
 * "B win" "B+" "B+ by .5" "B+ by resign" "B+.75" "B+0.751" "B+1 3/4"
 * "B+0.5 (W connects ko)" "B+0.5 (W connects the ko)"
 * "B+0.75 (Moves beyond 235 not recorded)"
 * "B+10 {Moves beyond 187 not known}"
 * "W+1 (after B -2 time penalty points)"
 * "W+1 (after B -4 points time penalty)"
 * "White + 3" "no result"
 * "White forfeits on time"
 * "White (Feng Yun) wins by 16.5 points."
 * "game suspended" "unfinished"
 * "黒中押し勝ち!"
 * "白中押し勝ち。"
 * "無勝負"
 */
/* not yet understood: strings in Chinese that say something like
   "after 257 moves White won with 5 points" */
/* p is given only in case we want to split RE[] */
static void
normalize_result(struct property *p, struct propvalue *pv) {
	int r, rf, rlen;
#define RESZ	500
	char ires[RESZ], ores[RESZ];
	char *s, *t, *u, *begin, *frac, *end, who;
	int result, resultfrac, fraclen;

#if 0
	s = strstr(pv->val, "手");
	if (s)
		splitoff_moves(s, p, pv);
#endif

	/* delete 目 in a result like RE[W+5目] */
	delete_stones(pv);

	/* for the moment: only implement -tojp */
	if (!opttojp)
		return;

	if (pv->next)
		errexit("multiple results?");
	if (strlen(pv->val) >= sizeof(ires))
		errexit("result spec too large");

	strcpy(ires, pv->val);
	s = ires;
	result = resultfrac = fraclen = 0;

	/* remove initial spaces */
	while (*s == ' ')
		s++;

	/* remove final spaces and . */
	t = s;
	while (*t)
		t++;
	while (t > s && t[-1] == ' ')
		*--t = 0;
	if (t > s && t[-1] == '.')
		*--t = 0;

	/* Jigo? */
	if (!strcmp(s, "0") || !strcmp(s, "Jigo")
	    || !strcmp(s, "Draw")) {
		result = resultfrac = fraclen = 0;
		goto ret;
	}

	/* find '+' */
	u = s;
	while (*u && *u != '+')
		u++;
	if (!*u)
		return;		/* not understood */

	/* winner before the '+' */
	*u = 0;
	if (!strcmp(s, "B") || !strcmp(s, "Black"))
		who = 'B';
	else if (!strcmp(s, "W") || !strcmp(s, "Black"))
		who = 'W';
	else
		return;		/* not understood */

	/* result after the '+' */
	begin = u+1;
	r = strtoul(begin, &end, 10);
	rf = rlen = 0;
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
		return;		/* not understood */

	result = r;
	resultfrac = rf;
	fraclen = rlen;

	if (opttojp) {
		/* if result involves quarter points, multiply by two */
		if (fraclen == 2 && (resultfrac == 25 || resultfrac == 75)) {
			int k = 2*(100*result+resultfrac);
			result = k/100;
			resultfrac = (k/10)%10;
			fraclen = 1;
		}
	}

ret:
	t = ores;
	if (result == 0 && resultfrac == 0)
		sprintf(t, "0");
	else {
		t += sprintf(t, "%c+%d", who, result);
		if (fraclen)
			sprintf(t, ".%0*d", fraclen, resultfrac);
	}
	pv->val = xstrdup(ores);
}

static int compar(const void *aa, const void *bb) {
	const char *a = *(char * const *) aa;
	const char *b = *(char * const *) bb;

	return strcmp(a,b);
}

static void
normalize_stones(struct propvalue *pv) {
	/* make sure stones in AB[dp][pd] are in some fixed order */
	/* note: we may have AB[bp:cp][br][ch][do:eo][ep:eq] ... */
	struct propvalue *u;
	char **arr;
	int i, ct;

	ct = 0;
	for (u = pv; u; u = u->next)
		ct++;
	arr = xmalloc(ct * sizeof(char *));
	i = 0;
	for (u = pv; u; u = u->next)
		arr[i++] = u->val;
	qsort(arr, ct, sizeof(char *), compar);
	i = 0;
	for (u = pv; u; u = u->next)
		u->val = arr[i++];
}

/* merge adjacent AB[pd] and AB[dp] to AB[pd][dp] */
static void
merge_stones(struct property *p) {
	struct property *q;
	struct propvalue *u;

	q = p->next;
	p->next = q->next;
	u = p->val;
	if (!u)
		p->val = q->val;
	else {
		while (u->next)
			u = u->next;
		u->next = q->val;
	}
}

static void
usage() {
	fprintf(stderr, "Usage: %s [-nd] [-d] [-c] [-x[#]] [-ll#] files\n",
		progname);
	exit(1);
}

int
main(int argc, char **argv){
	struct gametree *g;
	int i, inct;

	progname = "sgf";
	inct = 0;

	for (i=1; i<argc; i++) {
		if (!strcmp(argv[i], "-?") || !strcmp(argv[i], "--help"))
			usage();
		if (!strcmp(argv[i], "-c")) {
			stripcomments = 1;
			continue;
		}
		if (!strcmp(argv[i], "-d")) {
			dateck = 1;
			continue;
		}
		if (!strcmp(argv[i], "-pc")) {
			parsecomments = 1;
			continue;
		}
		if (!strcmp(argv[i], "-tojp")) {
			opttojp = 1;
			continue;
		}
		if (!strncmp(argv[i], "-ll", 3)) {
			movesperline = atoi(argv[i]+3);
			if (movesperline < 1)
				movesperline = 1;
			continue;
		}
		if (!strcmp(argv[i], "-m")) {
			multiin = 1;
			continue;
		}
		if (!strcmp(argv[i], "-nd")) {
			nodatenorm = 1;
			continue;
		}
		if (!strcmp(argv[i], "-nn")) {
			nonorm = 1;
			continue;
		}
		if (!strcmp(argv[i], "-t")) {
			tracein = 1;
			continue;
		}
		if (!strcmp(argv[i], "-x")) {
			splittofiles = 1;
			continue;
		}
		if (!strncmp(argv[i], "-x", 2)) {
			extractfile = atoi(argv[i]+2);
			if (!extractfile)
				errexit("files are numbered from 1");
			continue;
		}
		/* unknown option or input file */
		if (argv[i][0] == '-')
			errexit("unknown option '%s'", argv[i]);
		inct++;
	}

	/* sgf will leave non-understood dates, and warn only */
	warnings_are_fatal = 0;

	if (inct == 0) {
		readsgf(NULL, &g);		/* read stdin */

		write_init();
		write_gametree_sequence(g);
	} else {
		/* output a game collection */
		for (i=1; i<argc; i++) {
			if (argv[i][0] == '-')
				continue;
			readsgf(argv[i], &g);
			write_init();
			write_gametree_sequence(g);
		}
	}

	return 0;
}
