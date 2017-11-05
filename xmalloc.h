extern void *xmalloc(int n);
extern void *xrealloc(void *p, int n);
extern char *xstrdup(char *p);

/* NB cannot mix ymalloc and xrealloc */
extern void *ymalloc(int n);
extern void yfree(void);
extern char *ystrdup(char *p);
