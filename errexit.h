extern int ignore_errors;
extern int warnings_are_fatal;
extern int silent_unless_fatal;
extern const char *progname;
extern const char *infilename;
extern int linenr;
extern int warnct, errct;
extern char *((*warn_prefix)());
extern void errexit(const char *s, ...) __attribute__ ((noreturn));
extern void fatalexit(const char *s, ...) __attribute__ ((noreturn));
extern void warn(const char *s, ...);

#include <setjmp.h>
extern int have_jmpbuf;
extern jmp_buf jmpbuf;
