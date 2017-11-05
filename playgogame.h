struct played_game {
	int counts[3];		/* # moves, bcapts, wcapts */
	int mvct;		/* movect+bcapt+wcapt */
	int mvlen;		/* array length */
	short int *mv;
};

/* high order bits in mv[], sign bit unused */
#define PG_PASS		0x1000
#define PG_PERMANENT	0x2000
#define PG_CAPTURE	0x4000
/* a pass could also be indicated e.g. by a zero position */

void playgogame(int size, int *moves, int mvct, int initct,
		struct played_game *pg);
