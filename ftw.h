/*
 * do_infile() just calls do_input() when not recursive,
 * or when its arg is a file, and otherwise walks the tree
 */
extern void do_infile(const char *fn);
extern int recursive;
extern char *file_extension;

extern void do_input(const char *fn);
