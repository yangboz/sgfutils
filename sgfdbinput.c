/* sgfdbinput.c - handle a single data base */

/*
 * void do_dbin(char *fn): open and mmap data base, and call
 *  report_on_single_game() for each game found
 */

#include <stdio.h>		/* for NULL */
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "errexit.h"
#include "sgfdb.h"
#include "sgfinfo.h"
#include "playgogame.h"
#include "sgfdbinput.h"

static void
do_bgin(struct bingame *bg) {
	int i, m, n;
	short *bgm;
	char *bgc;

	if (bg->gamenr) {
		gamenr = bg->gamenr;
		number_of_games = 2;	/* larger than 1 */
	} else {
		gamenr = number_of_games = 1;
	}

	size = bg->size;
	movect = bg->movect;
	initct = bg->abct + bg->awct;
	mvct = initct + movect;
	handct = (bg->awct ? 0 : bg->abct);
	extmvct = bg->mvct;
	bcaptct = bg->bcapt;
	wcaptct = bg->wcapt;

	bgm = (short *)(((char *) bg) + sizeof(struct bingame));
	bgc = (char *)(bgm + extmvct);

	n = 0;
	for (i=0; i<extmvct; i++) {
		extmoves[i] = m = bgm[i];
		if (!(m & PG_CAPTURE)) {
			int x, y, mm;

			if (m & PG_PASS) {
				x = y = 't';
			} else {
#define MAXSZ	31
				mm = m & 0x3ff;
				x = mm/(MAXSZ+1) + 'a' - 1;
				y = mm%(MAXSZ+1) + 'a' - 1;
			}
			moves[n++] = ((m & 0xc00) << 6) + (x << 8) + y;
		}
	}

	infilename = bgc;
	report_on_single_game();
}

void
do_dbin(const char *fn) {
	int infd;
	size_t sz;
	struct stat s;
	void *mm;
	char *mmbegin, *mmend, *db, *bg;

	if (fn == NULL)
		fn = "out.sgfdb";

	infd = open(fn, O_RDONLY);
	if (infd < 0)
		errexit("cannot open %s", fn);
	if (fstat(infd, &s) < 0)
		errexit("cannot stat %s", fn);
	sz = s.st_size;
	mm = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, infd, 0);
	if (mm == MAP_FAILED)
		errexit("cannot mmap %s", fn);
	close(infd);

	db = mmbegin = mm;
	mmend = mmbegin + sz;

	/* check database magic and version */
	{
		struct sgfdb *dba;

		dba = (struct sgfdb *) db;

		if (dba->magic != DB_MAGIC)
			errexit("%s: bad magic", fn);
		if (dba->version != DB_VERSION)
			errexit("%s is an sgfdb version %d, "
				"we only support version %d",
				dba->version, DB_VERSION);
		if (dba->headerlen != sizeof(*dba))
			errexit("%s: bad header", fn);
	}

	bg = db + sizeof(struct sgfdb);

	while (bg < mmend) {
		struct bingame *bga;

		bga = (struct bingame *) bg;

		/* check that entry looks ok - avoid segfaults */
		if (bga->sz < 0 || bg + bga->sz > mmend ||
		    bga->mvct < 0 || bga->filenamelen < 0 ||
		    sizeof(*bga) + 2*bga->mvct + bga->filenamelen > bga->sz) {
			infilename = "";	/* no longer mapped */
			munmap(mm, sz);
			errexit("%s: bad database", fn);
		}

		do_bgin(bga);
		bg += bga->sz;
	}
	munmap(mm, sz);
}
