#define ROOT_ONLY	1
#define NONROOT_ONLY	2
#define	MULTIPROP	4

extern void setproprequests(int flags, char *s);
extern void do_stdin(const char *fn);

extern int replacenl, optN;

extern int get_winner(char *buf, int len);
extern int get_loser(char *buf, int len);
extern int get_player(char *buf, int len);
