/*
 * do_infile() just calls do_input() when not recursive,
 * or when its arg is a file, and otherwise walks the tree
 */
#include <sys/stat.h>
#include <string.h>
#include <ftw.h>
#include "ftw.h"
#include "errexit.h"

static int
walkfn(const char *fpath, const struct stat *sb, int typeflag) {
	const char *p;
	int extlen;

	if (typeflag != FTW_F)
		return 0;		/* not a regular file */

	extlen = strlen(file_extension);
	p = fpath;
	while (*p)
		p++;
	if (p-fpath < extlen || strcmp(p-extlen, file_extension))
		return 0;		/* not a .sgf file */
	do_input(fpath);
	return 0;			/* nonzero if walk has to be aborted */
}

static void
do_indir(const char *s) {
	/* don't use more than 64 open fd's */
	ftw(s, walkfn, 64);
}

void
do_infile(const char *fn) {
	struct stat sb;

	if (!recursive) {
		do_input(fn);
		return;
	}

	if (stat(fn, &sb) < 0)
		errexit("cannot stat %s", fn);
	if (S_ISREG(sb.st_mode))
		do_input(fn);
	else if (S_ISDIR(sb.st_mode))
		do_indir(fn);
	else
		errexit("%s: unrecognized file type", fn);
}
