/*
 * sgfcharset.c
 *
 * sgfcharset [options] [--] files
 *
 * Report whether the file is ASCII or UTF-8,
 * and if not, give a good guess as to what it could be,
 * If desired, convert the file to UTF-8 and add a CA[] property.
 *
 * For ASCII a strict interpretation is used,
 * where no control characters are present, except \r and \n.
 * One of the reasons is that character sets that follow ISO-2022
 * use ESC and SO and SI to switch among subsets.
 * We want ordinary ASCII, not such a beast.
 *
 * The definition of UTF-8 is the straightforward one, without
 * surrogate pairs, with Unicode characters coded by 1-3 bytes.
 * 
 * Other sets considered are Latin-1, GB2312, GBK, Big5, SJIS and EUC-KR.
 * CP932 is an extension of SJIS.
 * CP950 is an extension of Big5.
 * GB18030 is an extension of GB2312.
 * eucJP-open is an extension of eucJP.
 *
 * Call:
 * sgfcharset [--] files:   report the guessed charset of each file to stdout
 *   -na: don't mention ASCII
 *   -nu: don't mention UTF-8
 *   -nok: don't mention cases with one, confirmed, candidate (implies -na)
 *   -v: give verbose output; -v -v: more verbose
 *   -q: be more quiet
 *
 * sgfcharset -toutf8 [-from CHARSET] files: convert
 *   -from CHARSET: don't guess, but convert from CHARSET to UTF-8
 * If input is from stdin, output is to stdout.
 * If input is from "file", output is to "file.utf8".
 *   -replace: replace input file by output file
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <iconv.h>
#include "errexit.h"
#include "xmalloc.h"

int optna = 0;
int optnu = 0;
int optcv = 0;
int optnok = 0;
int optq = 0;
int verbose = 0;
char *optcharset;
int optrename = 0;
int optsc = 0;		/* 1: no semicolon needed at start, ( suffices */
int optf = 0;		/* 1: force, discard non-understood bytes */

#define verboseprint	if (verbose > 1) printf

/*
 * convert a buffer from a given charset to utf-8
 *
 * outct includes a trailing NUL
 */
static void convert_to_utf8(char *charset, char *inbuf, int inct,
		    char **outbuf, int *outct) {
	iconv_t cd;
	char *inp, *outp, *outstart;
	size_t inleft, outleft, outlen, ret;
	int errcnt = 0;		/* number of illegal bytes */

	if (!strcasecmp(charset, "UTF-8") ||
	    !strcasecmp(charset, "UTF8")) {
		*outbuf = inbuf;
		*outct = inct;
		return;		/* nothing to do */
	}

	cd = iconv_open("UTF-8", charset);
	if (cd == (iconv_t) -1)
		errexit("charset %s not supported", charset);

	inleft = inct;
	inp = inbuf;
	outleft = outlen = 2*inct + 1000;	/* should suffice */
	outp = outstart = xmalloc(outlen+1);	/* +1 for final NUL */
try:
	ret = iconv(cd, &inp, &inleft, &outp, &outleft);
	if (ret == (size_t) -1) {
		switch(errno)  {
		case EILSEQ:
			if (optf) {
				errcnt++;
				/* keep going */
				if (inleft && outleft) {
					inleft--;
					inp++;
					*outp++ = '?';
					outleft--;
					goto try;
				}
			}
			/* now inp points at the invalid sequence */
			errexit("invalid %s input, offset %d", charset,
				inp-inbuf);
		case EINVAL:
			errexit("incomplete input");
		case E2BIG:
			/* try again with a larger buffer? */
			errexit("no room for output");
		default:
			errexit("error converting input from %s to UTF-8",
				charset);
		}
	}
	if (errcnt > 1)
		warn("while converting to UTF-8: "
		     "%d bytes were discarded", errcnt);
	else if (errcnt == 1)
		warn("while converting to UTF-8: "
		     "1 byte was discarded");
	else if (ret)
		warn("while converting to UTF-8: "
		     "%d characters were converted non-reversibly", ret);

	/* now flush out the final bytes */
	ret = iconv(cd, NULL, NULL, &outp, &outleft);
	if (ret == (size_t) -1)
		errexit("the final iconv flush failed");
	if (ret)
		warn("the final iconv flush returned %d", ret);

	*outp++ = 0;
	outlen = outp - outstart;

#if 0
	/* now outp points at the first unused byte,
	   realloc the area to have the right size */
	outstart = xrealloc(outstart, outlen);
#endif

	*outbuf = outstart;
	*outct = outlen;

	iconv_close(cd);
}

/* no TAB, BELL, ESC, DEL, SI, SO etc. */
static int my_isascii(unsigned char *buf, int n) {
	int i, c;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c >= 040 && c <= 0176) || c == '\n' || c == '\r')
			continue;
		return 0;
	}
	return 1;
}
/*
 * Found precisely one file with other ASCII chars: 0x1b=ESC and 0x0e=SO,
 * namely igs-02-09-11.sgf. It says:
 * "ESC $)C donjuan 2d*: sure with kib but not with SO 4\212 SI"
 * where ESC $)C chooses the G1 set of KS X 1001-1992 (ISO-2022-KR)
 * and SO says that ASCII bytes encode "high ASCII",
 * and S1 reverts. See also rfc1557.txt and KSC5601.
 * The 4\212, i.e. \x34\x8a does not seem to be a valid text segment?
 */

/*
 * 377-edream-tochi-fzy.sgf uses ASCII, except that it uses 0x90 with
 * meaning "the current move" in a move comment. That means that it
 * defines a private character set.
 */

/*
 * Examples with precisely 1 high byte:
 *  "HonÕinbo", a single \d5 (12x),
 *  "BR[¾9 dan]", a single \be (9x),
 *  "RE[B+°]", a single \b0 (3x),
 *  "WR[3 danÎ]", a single \ce (2x),
 *  "capture the é stone", a single \e9,
 *  "Emmanuel Béranger", idem,
 *  "RE[B+RÂ]", a single \c2,
 *  "ÑiberGO Server", a single \d1 - should have been ÜberGO,
 *  "D¸sseldorf", a single \b8 - should have been Düsseldorf.
 * These are mistakes, but a single high byte points at latin-1,
 * unless there is evidence to the contrary.
 * Correct: "Düsseldorf", "bien, ça stabilise", "wins by 33½",
 * and perhaps "not alive yet ¡".
 *
 * Several gtl files have a number of occurrences of doesn*t and don*t
 * and didn*t and doesn*t and aren*t and wouldn*t and white*s where the
 * non-ASCII byte is 0264 = b4 = latin1 acute accent, latin9=8859-15 Ž,
 * used as apostrophe.
 * There are also many mixed files, partly latin1, partly utf8.
 */
static int islatin1(unsigned char *buf, int n) {
	int i, c, highbytect, high;

	highbytect = 0;
	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x7f) < 040 && c != '\n' && c != '\r')
			return 0;
		if (c & 0x80) {
			high = c;
			highbytect++;
		}
		/* probably not in an SGF file */
		if (c == 0xa4 || c == 0xa6 || c == 0xac ||
		    c == 0xb5 || c == 0xb6 || c == 0xf7)
			return 0;
	}
	if (highbytect == 1) {
		/* output utf-8 */
		verboseprint("single high byte '%c%c' = 0%o = 0x%02x\n",
			     0xc0+(high>>6), 0x80+(high&0x3f), high, high);
		return 2;	/* may be overruled */
	}
	return 1;
}

/*
 * Some files say that they are UTF-8 but are not
 * www.romaniango.org/partide/World_OZA_Toyota_Denso/WO2006_F-01.sgf
 * said UTF-8 but was in SJIS.
 * A largish number of gtl.xmp.net/reviews/sgf/XXX.sgf files claims
 * incorrectly to be UTF-8.
 */

static int isutf8(unsigned char *buf, int n) {
	int i, ct;
	unsigned int c;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;
		if ((c & 0xe0) == 0xc0)
			ct = 1;
		else if ((c & 0xf0) == 0xe0)
			ct = 2;
		else
			return 0;

		while (ct--) {
			if (++i == n)
				return 0;	/* incomplete */
			c = buf[i];
			if ((c & 0xc0) != 0x80)
				return 0;
		}
	}
	return 1;
}

static int isiso2022kr(unsigned char *buf, int n) {
	/* since this (as implemented in iconv) is a 7-bit character set,
	   and we already tried ASCII and UTF-8, we are sure not to have
	   ISO-2022-KR */
	return 0;
}

/* Some frequent characters - used only to find the
   coding system, in cases of doubt. */

static int gb2312_chars[] = {
	0xa1a3,		/* 。, . */
	0xa1a2,		/* 、, , (chinese) */
	0xa3ac,		/* ，, , (roman) */
	0xc4ea,		/* 年, year */
	0xd4c2,		/* 月, month */
	0xc8d5,		/* 日, day */
	0xb7d6,		/* 分, minute */
	0xcab1,		/* 时, time */
	0xb0d7,		/* 白, white */
	0xbada,		/* 黑, black */
	0xc4bf,		/* 目, eye */
	0xb6ce,		/* 段, dan */
	0xd2bb,		/* 一, one */
	0xb6fe,		/* 二, two */
	0xc8fd,		/* 三, three */
	0xcbc4,		/* 四, four */
	0xcee5,		/* 五, five */
	0xc1f9,		/* 六, six */
	0xc6df,		/* 七, seven */
	0xb0cb,		/* 八, eight */
	0xbec5,		/* 九, nine */
	0xd7d3,		/* 子, zǐ - stone */
	0xcad6,		/* 手, shǒu - move */
	0xcaa4,		/* 胜, shèng - win */
	0xcfc8,		/* 先, xiān - first */
	0xc6e5,		/* 棋, qí - go, as in 持棋 chí qí - jigo etc. */
	0xb1be,		/* 本, e.g., first character in "Honinbo" 本因坊 */
	0xd2f2,		/* 因, 2nd idem */
	0xb9fa,		/* 国, country, in 中国 - China, 韩国 - Korea */
	0xd6d0,		/* 中 - in */
	0xc8cb,		/* 人 - people */
	0xb5c4,		/* 的, of */
	0xb5da,		/* 第, section */
	0xb6c1, 0xc3eb,	/* 读秒, countdown, byoyomi */
	0
};

static int big5_chars[] = {
	0xb6c2,		/* 黑, black */
	0xa5d5,		/* 白, white */
	0xac71,		/* 段, dan */
	0xafc5,		/* 級, kyu */
	0xa440,		/* 一, one */
	0xa447,		/* 二, two */
	0xa454,		/* 三, three */
	0xa57c,		/* 四, four */
	0xa4ad,		/* 五, five */
	0xa4bb,		/* 六, six */
	0xa443,		/* 七, seven */
	0xa44b,		/* 八, eight */
	0xa445,		/* 九, nine */
	0xa140,		/* space */
	0xa147,		/* ：, : */
	0xa67e,		/* 年, year */
	0xa4eb,		/* 月, month */
	0xa4e9,		/* 日, day */
	0xa470, 0xaec9,	/* 小時, hours */
	0xa4c0,		/* 分, minutes */
	0xaced,		/* 秒, seconds */
	0xa455, 0xa4c8,	/* 下午, afternoon */
	0
};

/* a455 a4c8 */

static int shiftjis_chars[] = {
	/* fat roman digits, big-endian */
	0x824f, 0x8250, 0x8251, 0x8252, 0x8253,
	0x8254, 0x8255, 0x8256, 0x8257, 0x8258,
	/* fat parentheses */
	0x8169, 0x816a,
	0x8142,		/* 。, . */
	0x8145,		/* ・ */
	0x944e,		/* 年, year */
	0x8c8e,		/* 月, month */
	0x93fa,		/* 日, day */
	0x9492,		/* 白, white */
	0x8d95,		/* 黒, black */
	0x88ea,		/* 一, one */
	0x93f1,		/* 二, two */
	0x8e4f,		/* 三, three */
	0x8e6c,		/* 四, four */
	0x8cdc,		/* 五, five */
	0x985a,		/* 六, six */
	0x8eb5,		/* 七, seven */
	0x94aa,		/* 八, eight */
	0x8be3,		/* 九, nine */
	0x8e71,		/* 子, child */
	0x96da,		/* 目, eye */
	0x8f9f,		/* 勝, wins */
	0
};
/* also: RO[0x8c88 0x8f9f] = RO[決勝] = Final Round */

#ifdef EUCJP
/* no examples seen of this, the only matches belong in fact to GB2312 */
static int euc_jp_chars[] = {
//	0xa1a3,		/* 。 */ /* also very common in GB2312 */
	0xa1a6,		/* ・ */
	0xc7af,		/* 年, year */
//	0xb7ee,		/* 月, month */ /* this is 奉=bong in GB2312 (5x)*/
	0xc6fc,		/* 日, day */
	0xc7f2,		/* 白, white */
	0xb9f5,		/* 黒, black */
//	0xb0ec,		/* 一, one */ /* this is 办=do in GB2312 (625x) */
	0xc6f3,		/* 二, two */
//	0xbbb0,		/* 三, three */ /* this is 话=words in GB2312 (5x) */
	0xbbcd,		/* 四, four */
	0xb8de,		/* 五, five */
	0xcfbb,		/* 六, six */
//	0xbcb7,		/* 七, seven */ /* this is 挤=squeeze in GB2312 (7x) */
	0xc8ac,		/* 八, eight */
	0xb6e5,		/* 九, nine */
	0
};
#endif

/* euc-kr (iso-2022-kr)
 - note: many games labeled ISO-2022-KR are in fact UTF-8
 - other games labeled ISO-2022-KR are in fact EUC-KR
 *
 * see also http://www.badukworld.co.kr/biz/gloss2.html
 * see also http://senseis.xmp.net/?KoreanGoTerms
 * see also http://bigo.baduk.org/downloads/Korean.lng
 */
static int euc_kr_chars[] = {
	0xb9dd,		/* 반 = half */
	0xc3ca,		/* 초 = seconds */
	0xbad0,		/* 분 = minutes */
//	0xb4dc,	- seen	/* 단 = dan */
	0xb9e9,		/* 백 - baek = white */
	0xc8e6, 	/* 흑 - heuk = black */
//	0xb4fd,	- not seen	/* 덤 - deom = komi */
	0xb5b5,		/* 도 - diagram */
	0xbcb1,		/* 선 - play */
	0xc1fd,		/* 집 - territory */
	0xc8a3,		/* 호 - territory */

// longer common words or expressions
//	{ 0xbdc3, 0xb0a3, 0 },		/* 시간 = hour, time */
	0xbdc3, 0xb0a3,

//	{ 0xbad2, 0xb0e8, 0xbdc2, 0 },	/* 불계승 = win by resignation */
	0xbad2, 0xb0e8, 0xbdc2, 

	0xbcad,	0xbfef,	/* 서울 = Seoul */
	0xb0ad,		/* 강 */

	0
};

static inline int
is_goodchar(int *goodchars, int c) {
	int d;

	while ((d = *goodchars++) != 0)
		if (c == d)
			return 1;
	return 0;
}

#ifdef EUCJP
static int iseucjp(unsigned char *buf, int n) {
	int i, c, d, e, cd, score = 0;
	int *ok = euc_jp_chars;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;
		if (++i == n) {
			verboseprint("not EUC-JP: incomplete char\n");
			return 0;
		}
		d = buf[i];
		if (c == 0x8e && (d >= 0xa1 && d <= 0xdf))
			continue;		/* kana, maybe score++ */
		if (c == 0x8f && (d >= 0xa1 && d <= 0xfe)) {
			if (++i == n) {
				verboseprint("not EUC-JP: incomplete char\n");
				return 0;
			}
			e = buf[i];
			if (e >= 0xa1 && e <= 0xfe)
				continue;	/* ok, maybe score++ */
			verboseprint("not EUC-JP: %02x%02x%02x\n", c, d, e);
			return 0;
		}
		if (c >= 0xa1 && c <= 0xfe && d >= 0xa1 && d <= 0xfe) {
			cd = (c << 8) | d;
			int incr;
			score += (incr = is_goodchar(ok, cd));
			if (incr)
				verboseprint("EUC-JP ok: %02x%02x\n", c, d);
			continue;
		}
		verboseprint("not EUC-JP: %02x%02x\n", c, d);
		return 0;
	}
	return score+1;
}
#endif

static int isgb2312(unsigned char *buf, int n) {
	int i, c, d, cd, row, score = 0;
	int *ok = gb2312_chars;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;
		if (++i == n) {
			verboseprint("not GB2312: incomplete char\n");
			return 0;
		}
		d = buf[i];
		if (c <= 0xa0 || d <= 0xa0)
			goto bad;
		if (c-0xa0 > 94 || d-0xa0 > 94)
			goto bad;
		row = c - 0xa0;
		if (row >= 88 || (row >= 10 && row <= 15))
			goto bad;
		cd = (c << 8) | d;
		score += is_goodchar(ok, cd);
		continue;
	bad:
		verboseprint("not GB2312: %02x%02x\n", c, d);
		return 0;
	}
	return score+1;
}

/* gbk contains gb2312 and more - when gb2312 succeeds, also gbk will */
/* use only when gb2312 fails */
/* gb18030 is even larger */
static int isgbk(unsigned char *buf, int n) {
	int i, c, d, cd, score = 0;
	int *ok = gb2312_chars;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;
		if (++i == n) {
			verboseprint("not GBK: incomplete char\n");
			return 0;
		}
		d = buf[i];
		if (d == 0x7f || d == 0xff)
			goto bad;
		if (c >= 0xa1 && c <= 0xa9 && d >= 0xa1)
			goto ok;
		if (c >= 0xb0 && c <= 0xf7 && d >= 0xa1)
			goto ok;
		if (c >= 0x81 && c <= 0xa0 && d >= 0x40)
			goto ok;
		if (c >= 0xa8 && c <= 0xfe && d >= 0x40 && d <= 0xa0)
			goto ok;
	bad:
		verboseprint("not GBK: %02x%02x\n", c, d);
		return 0;		/* there are also user-defined chars */
	ok:
		cd = (c << 8) | d;
		score += is_goodchar(ok, cd);
		continue;
	}
	return score+1;
}

static int isgb18030(unsigned char *buf, int n) {
	int i, c, d, e, f, cd, score = 0;
	int *ok = gb2312_chars;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;
		if (++i == n)
			goto incomplete;
		d = buf[i];
		if (d == 0x7f || d == 0xff)
			goto bad;
		if (c >= 0xa1 && c <= 0xa9 && d >= 0xa1)
			goto ok;
		if (c >= 0xb0 && c <= 0xf7 && d >= 0xa1)
			goto ok;
		if (c >= 0x81 && c <= 0xa0 && d >= 0x40)
			goto ok;
		if (c >= 0xa8 && c <= 0xfe && d >= 0x40 && d <= 0xa0)
			goto ok;
		/* are these now all ok? */
		if (c >= 0x81 && c <= 0xfe && d >= 0x40)
			goto ok;
		if (c >= 0x81 && c <= 0xfe && d >= '0' && d <= '9') {
			if (i+2 >= n)
				goto incomplete;
			e = buf[++i];
			f = buf[++i];
			if (e >= 0x81 && e <= 0xfe && f >= '0' && f <= '9')
				continue;
			verboseprint("not GB18030: %02x%02x%02x%02x\n",
				     c, d, e, f);
			return 0;
		}
	bad:
		verboseprint("not GB18030: %02x%02x\n", c, d);
		return 0;		/* there are also user-defined chars */
	ok:
		cd = (c << 8) | d;
		score += is_goodchar(ok, cd);
		continue;
	}
	return score+1;

incomplete:
	verboseprint("not GB18030: incomplete char\n");
	return 0;
}

static int isbig5(unsigned char *buf, int n) {
	int i, c, d, cd, score = 0;
	int *ok = big5_chars;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;
		if (c < 0xa1 || c > 0xf9) {
			verboseprint("not Big5: first byte %02x\n", c);
			return 0;
		}
		if (++i == n) {
			verboseprint("not Big5: incomplete char\n");
			return 0;
		}
		d = buf[i];
		if (d <= 0x3f || (d >= 0x7f && d <= 0xa0) || d == 0xff) {
			verboseprint("not Big5: second byte %02x\n", d);
			return 0;
		}
		cd = (c << 8) | d;
		score += is_goodchar(ok, cd);
	}
	return score+1;
}

static int isshiftjis(unsigned char *buf, int n) {
	int i, c, d, cd, kana = 0, score = 0;
	int *ok = shiftjis_chars;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;	/* ASCII */
		if (c >= 0xa1 && c <= 0xdf)
			continue;	/* single-byte katakana */
		if (c == 0x80 || c == 0xa0 || c >= 0xf0)
			return 0;
		if (c == 0x85 || c == 0x86) {
			verboseprint("not SJIS: first byte %02x\n", c);
			return 0;	/* seen: 81, 82, 89-93, 95, 97 */
		}
		if (++i == n) {
			verboseprint("not SJIS: incomplete char\n");
			return 0;
		}
		d = buf[i];
		if (d <= 0x3f || d == 0x7f || d >= 0xfd) {
			verboseprint("not SJIS: second byte %02x\n", d);
			return 0;
		}
		cd = (c << 8) | d;
		score += is_goodchar(ok, cd);
		if (cd >= 0x829f && cd <= 0x82f1)
			kana++;	/* hiragana */
		if (cd >= 0x8340 && cd <= 0x8396)
			kana++;	/* katakana */
	}
	if (kana)
		score++;
	return score + 1;
}

static int iscp932(unsigned char *buf, int n) {
	int i, c, d, cd, kana = 0, score = 0;
	int *ok = shiftjis_chars;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;	/* ASCII */
		if (c >= 0xa1 && c <= 0xdf)
			continue;	/* katakana */
		if (c == 0x80 || c == 0x85 || c == 0x86 || c == 0xa0 ||
		    (c >= 0xfd)) {
			verboseprint("not CP932: first byte %02x\n", c);
			return 0;	/* seen: 81, 82, 89-93, 95, 97 */
		}
		if (++i == n) {
			verboseprint("not CP932: incomplete char\n");
			return 0;
		}
		d = buf[i];
		if (d <= 0x3f || d == 0x7f || d >= 0xfd) {
			verboseprint("not CP932: second byte %02x\n", d);
			return 0;
		}
		cd = (c << 8) | d;
		score += is_goodchar(ok, cd);
		if (cd >= 0x829f && cd <= 0x82f1)
			kana++;	/* hiragana */
		if (cd >= 0x8340 && cd <= 0x8396)
			kana++;	/* katakana */
	}
	if (kana)
		score++;
	return score + 1;
}

static int iseuc_kr(unsigned char *buf, int n) {
	int i, c, d, cd, score = 0, row;
	int *ok = euc_kr_chars;

	for (i=0; i<n; i++) {
		c = buf[i];
		if ((c & 0x80) == 0)
			continue;
		if (++i == n) {
			verboseprint("not EUC-KR: incomplete char\n");
			return 0;
		}
		d = buf[i];
		if (c <= 0xa0 || d <= 0xa0)
			goto bad;
		if (c-0xa0 > 94 || d-0xa0 > 94)
			goto bad;
		row = c - 0xa0;
		/* difficult to distinguish from GB* */
		/* (random) test for values seen so far: */
		if ((row < 16 || row > 93) && row != 1 && row != 3 && row != 4)
			goto bad;
//	ok:
		cd = (c << 8) | d;
		score += is_goodchar(ok, cd);
		continue;
	bad:
		verboseprint("not EUC-KR: %02x%02x\n", c, d);
		return 0;
	}
	return score+1;
}

static int is_uc(int c) {
	return (c >= 'A' && c <= 'Z');
}

static int to_upper(int c) {
	if (c >= 'a' && c <= 'z')
		return c - ('a' - 'A');
	return c;
}

static char *get_uc_CA(unsigned char *buf, int n) {
	char *s, *p, *pe, *res;
	int i, m;

	s = (char *) buf;
	while (1) {
		p = strstr(s+1, "CA[");
		if (!p)
			return NULL;
		if (!is_uc(p[-1]))
			break;
	}

	p += 3;
	/* do index(p, ']') by hand, taking care of escapes */
	for (pe = p; pe < s+n-1; pe++) {
		if (*pe == ']')
			goto gotend;
		if (*pe == '\\')
			pe++;
	}
	return NULL;

gotend:
	m = pe-p;
	res = xmalloc(m+1);
	for (i=0; i<m; i++)
		res[i] = to_upper(p[i]);
	res[m] = 0;
	return res;
}

#define SIZE(a)	(sizeof(a) / sizeof((a)[0]))

struct {
	char *name;
	int (*testfn)(unsigned char *buf, int n);
	int *goodchars;
} charset_tests[] = {
//	{ "ASCII", my_isascii },
//	{ "UTF-8", isutf8 },
	{ "ISO-2022-KR", isiso2022kr },
	{ "EUC-KR", iseuc_kr },
	{ "GB2312", isgb2312 },
	{ "GBK", isgbk },
	{ "GB18030", isgb18030 },
	{ "Big5", isbig5 },
	{ "SJIS", isshiftjis },
	{ "CP932", iscp932 },
#ifdef EUCJP
	{ "EUCJP", iseucjp },
#endif
	{ "ISO-8859-1", islatin1 },
};

#define M	(SIZE(charset_tests))

static int is_impossible(char *charset, unsigned char *buf, int n) {
	int i, score;

	/* we already checked for UTF-8 and would not be here if it was */
	if (!strcasecmp(charset, "UTF-8"))
		return 1;
	
	for (i=0; i<M; i++)
		if (!strcasecmp(charset, charset_tests[i].name))
			goto test;
	return 0;	/* no idea */

test:
	/* here no list ok[] needs to be inspected */
	score = charset_tests[i].testfn(buf,n);
	return (score == 0);
}

static int is_unknown(char *charset) {
	iconv_t cd;

	cd = iconv_open("UTF-8", charset);
	if (cd == (iconv_t) -1)
		return 1;
	iconv_close(cd);
	return 0;
}

static void outgoodscores(char *s, char **names, int *scores, int ct) {
	int i, first = 0;

	printf("%s:%s", infilename, s);
	for (i=0; i<ct; i++) {
		if (scores[i] <= 1)
			continue;
		if (first++)
			printf(",");
		printf(" %s", names[i]);
		if (verbose)
			printf(" (%d)", scores[i]);
	}
	printf("\n");
}

/* unescape for the purpose of looking at the text; this destroys buf;
   the original cannot be reconstructed */
static void unescape(unsigned char *buf, int *nn) {
	int n = *nn;
	unsigned char *p, *q;

	p = buf;
	while (n--) {
		if (*p++ == '\\')
			goto esc;
	}
	return;		/* buffer unchanged */
esc:
	n++;
	q = --p;
	while (n-- > 0) {
		if (*p == '\\') {
			p++;
			n--;
		}
		*q++ = *p++;
	}
	*q = 0;
	*nn = q - buf;
}

/* unescape by writing a NUL over each syntactic non-escaped ']' */
static void unescape_with_NULs(unsigned char *buf, int *nn) {
	int n = *nn;
	unsigned char *p, *q;

	p = q = buf;
	while (n-- > 0) {
		if (*p == ']') {
			p++;
			if (n == 0 || *p == ';' || *p == '(' || *p == ')' ||
			    *p == '[' || *p == ' ' || *p == '\t' ||
			    *p == '\n' || *p == '\r' ||
			    (*p >= 'A' && *p <= 'Z') ||
			    (*p >= 'a' && *p <= 'z')) {
				/* probably syntactic */
				*q++ = 0;
				continue;
			}
			p--;
		}
		if (*p == '\\') {
			/* this might be the 2nd byte of a multibyte char */
			/* view it as an escape (and delete it)
			   only when followed by \ or ] */
			p++;
			if (*p == '\\' || *p == ']')
				n--;
			else
				p--;
		}
		*q++ = *p++;
	}
	*nn = q - buf;
}

/* unescape by writing a NUL over each syntactic non-escaped ']' */
static void unescape_with_NULs_SJIS(unsigned char *buf, int *nn) {
	int n = *nn;
	int is2byte;
	unsigned char *p, *q;

	p = q = buf;
	while (n-- > 0) {
		if (*p == ']') {
			p++;
			if (n == 0 || *p == ';' || *p == '(' || *p == ')' ||
			    *p == '[' || *p == ' ' || *p == '\t' ||
			    *p == '\n' || *p == '\r' ||
			    (*p >= 'A' && *p <= 'Z') ||
			    (*p >= 'a' && *p <= 'z')) {
				/* probably syntactic */
				*q++ = 0;
				continue;
			}
			p--;
		}
		if (*p == '\\') {
			/* this might be the 2nd byte of a multibyte char */
			/* view it as an escape (and delete it)
			   only when followed by \ or ] */
			p++;
			if (*p == '\\' || *p == ']')
				n--;
			else
				p--;
		}
		is2byte = ((*p >= 0x80 && *p <= 0x9f) || (*p >= 0xe0));
		*q++ = *p++;
		if (is2byte && n) {
			n--;
			*q++ = *p++;
		}
	}
	*nn = q - buf;
}

/* we assume that the NULs survived: they were in a context that had
   an ASCII ']', so are not the 2nd byte of a multibyte char */

/*
 * Do we have to escape ':'? Only in properties where it has a syntactic
 * meaning, and the present meaning is not the syntactic one.
 * AP[program:version] might have further colons in the program name,
 * but I have not seen exx.
 * In FG[number:simpletext], LB[point:simpletext], LN[point:point],
 * SZ[number:number] the syntactically interesting colon is the first
 * so no escaping is needed.
 * Maybe the conclusion is that colon never needs escaping.
 */

static void escape_from_NULSs(unsigned char **abuf, int *nn) {
	int n = *nn;
	unsigned char *p, *q, *buf;

	q = buf = xmalloc(2*n+1);
	p = *abuf;
	while (n-- > 0) {
		if (*p == ']' || *p == '\\')
			*q++ = '\\';
		if (*p == 0)
			*p = ']';
		*q++ = *p++;
	}
	*q = 0;
	free(*abuf);
	*abuf = buf;
	*nn = q-buf;
}

static void guess_charset(unsigned char *buf, int n) {
	int i, score, ct, okct;
	char *names[M], *ownca;
	int scores[M];

	/* do this first, now that we still know where properties end */
	ownca = get_uc_CA(buf, n);

	/* remove backslashes */
	unescape(buf, &n);

	if (ownca) {
		if (is_impossible(ownca, buf, n))
			printf("%s: it says CA[%s], but that seems wrong\n",
			       infilename, ownca);
		else if (is_unknown(ownca)) {
			char *p;

			if (strlen(ownca) > 50)
				sprintf(ownca+20, " ...");
			while ((p = index(ownca, '\n')) != NULL)
				*p = ' ';
			printf("%s: it says CA[%s], but that is unknown\n",
			       infilename, ownca);

			if (!strncmp(ownca, "KS_C_5601", 9)) {
				ownca = "EUC-KR";
				printf(".. but sounds Korean - maybe try %s\n",
				       ownca);
			}
		} else {
			printf("%s: it says CA[%s], and that is possible\n",
			       infilename, ownca);
//			return;
		}
	}

	ct = okct = 0;
	for (i=0; i<SIZE(charset_tests); i++) {
		score = charset_tests[i].testfn(buf,n);
		if (!score)
			continue;
		names[ct] = charset_tests[i].name;
		scores[ct] = score;
		ct++;
		if (score > 1)
			okct++;

		/* GB2312 is a subset of GBK, which is a subset of GB18030.
		   If it can be the smaller, skip the test for the larger */
		if (!strcmp(charset_tests[i].name, "GB2312"))
			i++;
		if (!strcmp(charset_tests[i].name, "GBK"))
			i++;
	}

	if (okct == 1) {
		/* this is what we hoped: only one good candidate */
		if (!optnok)
			outgoodscores("", names, scores, ct);
		return;
	}

	if (okct) {
		/* several good candidates */
		outgoodscores(" perhaps one of", names, scores, ct);
		return;
	}

	if (ct == 1) {
		printf("%s: possibly %s\n", infilename, names[0]);
		return;
	}

	if (ct) {
		int first = 0;

		/* no good candidates */
		printf("%s: possibly one of", infilename);
		for (i=0; i<ct; i++) {
			if (first++)
				printf(",");
			printf(" %s", names[i]);
		}
		printf("\n");
		return;
	}

	printf("%s: no idea\n", infilename);
}

#define XTRA	100		/* for CA[UTF-8], 9 suffices */

/* here buf[n] = 0 */
static int addCA_and_write(char *buf, int n) {
	char *nbuf = xmalloc(n+XTRA);
	char *p, *p1, *p2, *q;
	int m, buflen;
	FILE *outf;
	char *ofn;

	/* find (; and maks p point past it */
	p = strstr(buf, "(;");
	if (p)
		p += 2;
	else {
		/* try again, more carefully.. */
		p = index(buf, '(');
		if (p) {
			p++;
			while (*p == ' ' || *p == '\r' ||
			       *p == '\n' || *p == '\t')
				p++;
		}
		if (p && *p == ';')
			p++;
		else if (!p || !optsc)
			errexit("bad SGF - no (; start");
	}

	p1 = strstr(p, "CA[");		/* should really parse buffer */
	if (p1 == NULL) {
		m = p-buf;
		memcpy(nbuf, buf, m);
		q = nbuf+m;
		q += sprintf(q, "CA[UTF-8]");
		memcpy(q, p, n-m);
		buflen = (q-nbuf)+n-m;	/* n+9 */
	} else {
		p2 = index(p1, ']');
		if (p2 == NULL)
			errexit("bad SGF");
		m = p1-buf;
		memcpy(nbuf, buf, m);
		q = nbuf+m;
		q += sprintf(q, "CA[UTF-8]");
		m = n - (p2+1-buf);
		memcpy(q, p2+1, m);
		buflen = (q-nbuf)+m;
	}

//	escape(buf, &buflen);

	if (!strcmp(infilename, "-")) {
		outf = stdout;
		ofn = NULL;
	} else {
		m = strlen(infilename);
		ofn = xmalloc(m+XTRA);
		sprintf(ofn, "%s.utf8", infilename);
		outf = fopen(ofn, "w");
		if (outf == NULL)
			errexit("cannot open %s for writing", ofn);
	}
	m = fwrite(nbuf, 1, buflen, outf);
	if (m != buflen)
		errexit("output error");
	free(nbuf);

	if (ofn) {
		if (fclose(outf))
			errexit("output error");
		if (optrename) {
			if (rename(ofn, infilename)) {
				perror("rename");
				errexit("rename %s to %s failed",
					ofn, infilename);
			}
		}
		free(ofn);
	}
	return 0;
}

static void copy_to_stdout(unsigned char *buf, int n) {
	int nn;

	nn = fwrite(buf, 1, n, stdout);
	if (nn != n)
		errexit("output error");
}

static void guess_and_convert(unsigned char *buf, int n) {
	int i, score, okct, olen;
	char *name, *ownca;
	char *obuf;

	/* if they told us, no need to guess */
	if (optcharset) {
		name = optcharset;
		goto convert;
	}

	/* if the file itself has an idea, try whether it looks reasonable */
	ownca = get_uc_CA(buf, n);
	if (ownca) {
	try:
		if (is_impossible(ownca, buf, n))
			fprintf(stderr, "%s: CA[%s] is incorrect - ignored\n",
				infilename, ownca);
		else if (is_unknown(ownca)) {
			char *p;

			if (!strncmp(ownca, "KS_C_5601", 9)) {
				ownca = "EUC-KR";
				goto try;
			}

			if (strlen(ownca) > 50)
				sprintf(ownca+20, " ...");
			while ((p = index(ownca, '\n')) != NULL)
				*p = ' ';
			fprintf(stderr, "%s: CA[%s] is unknown - ignored\n",
				infilename, ownca);
		} else {
			name = ownca;
			goto convert;
		}
	}

	okct = 0;
	name = NULL;		/* gcc only */
	for (i=0; i<SIZE(charset_tests); i++) {
		score = charset_tests[i].testfn(buf,n);
		if (score <= 1)
			continue;
		okct++;
		name = charset_tests[i].name;

		/* GB2312 is a subset of GBK, which is a subset of GB18030.
		   If it can be the smaller, skip the test for the larger */
		if (!strcmp(charset_tests[i].name, "GB2312"))
			i++;
		if (!strcmp(charset_tests[i].name, "GBK"))
			i++;

		/* SJIS is a subset of CP932.
		   If it can be the smaller, skip the test for the larger */
		if (!strcmp(charset_tests[i].name, "SJIS"))
			i++;
	}

	if (okct != 1) {
		/* no good candidate, or several: leave the file as it is */
		fprintf(stderr, "%s: unknown charset - unchanged\n",
			infilename);
		if (!strcmp(infilename, "-"))
			copy_to_stdout(buf, n);
		return;
	}

	/* convert and add CA[] property */
convert:
	/* we have to unescape, convert, and escape; unfortunately
	   the structure is lost upon unescaping; it is necessary
	   to do the conversion property by property? */
	/* try to terminate property fields by writing a NUL
	   instead of the ']' */
	if (!strcmp(name, "SJIS") || !strcmp(name, "cp932"))
		unescape_with_NULs_SJIS(buf, &n);
	else
		unescape_with_NULs(buf, &n);
	convert_to_utf8(name, (char *) buf, n, &obuf, &olen);
	escape_from_NULSs((unsigned char **) &obuf, &olen);
	addCA_and_write(obuf, olen-1);

	/* maybe only if verbose or if only a single file? */
	/* maybe mention output file? */
	if (!optq)
		fprintf(stderr, "%s: converted from %s to UTF-8\n",
			infilename, name);
}

/* read all of stdin into a buffer - we expect small files only */
#define DEFAULT_BUFSTEP	32768
static unsigned char *stdin_buf = NULL;
static int stdin_buflen = 0;
static int stdin_bufstep = DEFAULT_BUFSTEP;

static void get_more_mem() {
	stdin_bufstep += stdin_bufstep/4;
	stdin_buflen += stdin_bufstep;
	stdin_buf = xrealloc(stdin_buf, stdin_buflen);
}

static void get_stdin(unsigned char **buf, int *nn) {
	int n, n0;

	n0 = 0;
        while (1) {
		if (n0+1 >= stdin_buflen)
			get_more_mem();
		n = fread(stdin_buf + n0, 1, stdin_buflen - n0 - 1, stdin);
		if (!n)
			break;		/* error or eof */
		n0 += n;
	}
	stdin_buf[n0] = 0;
	*buf = stdin_buf;
	*nn = n0;
}

/* read/map entire file into buf - fread is more convenient than mmap */
static void
getfile(const char *fn, unsigned char **buf, int *n) {
	FILE *f = NULL;

	if (strcmp(fn, "-")) {
		f = freopen(fn, "r", stdin);
		if (!f)
			errexit("cannot open %s", fn);
	}
	get_stdin(buf, n);
	if (f)
		fclose(f);
}

/* guess character set, perhaps convert */
static void doinfile(char *fn) {
	unsigned char *buf;
	int n;

	infilename = (fn ? fn : "-");
	getfile(infilename, &buf, &n);

	if (my_isascii(buf,n)) {
		if (!optna) {
			if (!optcv)
				printf("%s: ASCII\n", infilename);
			else if (strcmp(infilename, "-"))
				fprintf(stderr,
					"%s: already ASCII - not converted\n",
					infilename);
			else
				copy_to_stdout(buf, n);
		}
		goto next;
	}

	if (isutf8(buf,n)) {
		if (!optnu) {
			if (!optcv)
				printf("%s: UTF-8\n", infilename);
			else if (strcmp(infilename, "-"))
				fprintf(stderr,
					"%s: already UTF-8 - not converted\n",
					infilename);
			else
				copy_to_stdout(buf, n);
		}
		goto next;
	}

	if (optcv)
		guess_and_convert(buf, n);
	else
		guess_charset(buf, n);

next:;
}

int main(int argc, char **argv) {

	progname = "sgfcharset";
	optcharset = NULL;

	optna = optnu = optcv = 0;
	while (argc > 1 && argv[1][0] == '-') {
		if (!strcmp(argv[1], "--")) {
			argc--; argv++;
			break;
		}
		if (!strcmp(argv[1], "-q")) {
			optq = 1;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-v")) {
			verbose++;	/* can be given more than once */
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-vv")) {
			verbose += 2;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-na")) {
			optna = 1;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-nok")) {
			optnok = optna = 1;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-nu")) {
			optnu = optna = 1;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-toutf8")) {
			optcv = 1;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-replace")) {
			optrename = 1;
			argc--; argv++;
			continue;
		}
		if (!strncmp(argv[1], "-from", 5)) {
			optcharset = argv[1]+5;
			argc--; argv++;
			if (*optcharset)
				continue;
			if (argc == 1)
				errexit("-from needs a following charset");
			optcharset = argv[1];
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-sc")) {
			optsc = 1;
			argc--; argv++;
			continue;
		}
		if (!strcmp(argv[1], "-f")) {
			optf = 1;
			argc--; argv++;
			continue;
		}
		errexit("unrecognized option: '%s'\n\n"
"usage: sgfcharset files:  report the guessed charset of each file\n"
"         options: -- / -na / -nu / -nok / -v / -q\n"
"       sgfcharset -toutf8 files:  convert files\n"
"         options: -from CHARSET / -replace\n\n"
			, argv[1]);
	}

	if (optcharset && !optcv)
		errexit("-from is only meaningful together with -toutf8");
	if (optrename && !optcv)
		errexit("-replace is only meaningful together with -toutf8");

	/*
	 * get the files and guess their character sets
	 */
	if (argc == 1) {
		doinfile(NULL);
	} else {
		if (optq)
			optna = optnu = 1;
		while (argc > 1) {
			doinfile(argv[1]);
			argc--; argv++;
		}
	}
	return 0;
}
