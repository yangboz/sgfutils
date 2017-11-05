extern char *getminmax(char *s, int *min, int *max);
extern char *setminmax(char *s, int *val, char *msg);
extern int checkints(void);
extern int checkstrings(void);
extern int checkstringfns(void);
extern void set_int_to_report(char *fmt, int *val);
extern void report_all(int bare);
extern void set_string(char *fmt, char *option, int (*fn)(char *, int));
extern void set_stringfn(char *fmt, char *option,
			 int (*fn)(char *, char *, int));
extern void bare_start(int a);

#define UNSET	(-1)
