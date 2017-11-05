/* data base record, of variable length */

struct bingame {
	short int sz;		/* total size of this record in bytes */
	short int gamenr;
	short int movect;	/* number of moves */
	unsigned char size;	/* size of board */
	unsigned char abct;	/* initial number of black stones */
	unsigned char awct;	/* initial number of white stones */
	unsigned char bcapt;	/* number of black stones captured */
	unsigned char wcapt;	/* number of white stones captured */
	short int mvct;		/* length of array mv[] */
	short int filenamelen;	/* length of trailing filename (unused) */
	short int mv[0];
	char fn[0];
};

/*
 * Here fn[] is an array of length filenamelen containing a filename,
 * gamenr counts games inside that file,
 * mv[] is an array of mvct shorts,
 * fn[] the filename.
 * There may be padding - don't trust the offsets of mv, fn.
 *
 * Handicap stones and captured stones are indicated in the
 * mv[] array, by moves at the indicated places. The first abct+awct
 * moves, and moves that toggle a board position, are not counted
 * for the move number. In a handicap game move 1 is by white.
 *
 * Both filenamelen and movect are redundant.
 */

/* the data base has the following format */
struct sgfdb {
	int headerlen;
	short int magic;
	short int version;
	struct bingame games[0];
};

#define DB_MAGIC	0x6a11
#define DB_VERSION	2
