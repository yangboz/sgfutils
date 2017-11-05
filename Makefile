TPROGS=sgf sgfsplit sgfvarsplit sgfstrip sgfinfo sgfmerge sgftf \
	sgfcheck sgfdb sgfdbinfo sgfcharset sgfcmp sgfx \
	ngf2sgf nip2sgf nk2sgf gib2sgf

PROGS=$(TPROGS) sgftopng ugi2sgf

CSOURCES:=sgf.c sgfsplit.c sgfvarsplit.c sgfstrip.c sgfinfo.c sgfmerge.c \
	sgftf.c sgfcheck.c sgfdb.c readsgf.c readsgf0.c writesgf.c \
	sgffileinput.c sgfdbinput.c sgfcharset.c sgfcmp.c sgfx.c \
	playgogame.c tests.c errexit.c xmalloc.c sgftopng.c \
	ftw.c ugi2sgf.c ngf2sgf.c nip2sgf.c nk2sgf.c gib2sgf.c

OBJECTS:=$(CSOURCES:.c=.o) sgfdbinfo.o

HSOURCES=errexit.h xmalloc.h sgfdb.h readsgf.h writesgf.h sgfinfo.h ftw.h \
	playgogame.h sgffileinput.h sgfdbinput.h tests.h

SOURCES=$(CSOURCES) $(HSOURCES)

CFLAGS=-Wall -Wmissing-prototypes -O3

# For systems where iconv is in a separate library
# LDLIBS=-liconv
# is needed for sgfcharset and ugi2sgf

all: $(PROGS)

$(TPROGS): errexit.o

sgf: sgf.o readsgf.o xmalloc.o

sgfx: sgfx.o readsgf.o xmalloc.o

sgfsplit: sgfsplit.o

sgfvarsplit: sgfvarsplit.o xmalloc.o

sgfstrip: sgfstrip.o readsgf.o writesgf.o xmalloc.o

sgfmerge: sgfmerge.o readsgf.o xmalloc.o

sgfcmp: sgfcmp.o readsgf.o xmalloc.o

sgfcheck: sgfcheck.o readsgf0.o playgogame.o ftw.o xmalloc.o

sgftf: sgftf.o readsgf.o ftw.o xmalloc.o

sgfdb: sgfdb.o readsgf.o playgogame.o ftw.o xmalloc.o

sgfinfo: sgfinfo.o sgffileinput.c readsgf.o playgogame.o tests.o \
	 ftw.o xmalloc.o
	cc $(CFLAGS) $^ -o $@ -lcrypto

sgfdbinfo: sgfdbinfo.o sgfdbinput.o readsgf.o xmalloc.o tests.o ftw.o
	cc $(CFLAGS) $^ -o $@ -lcrypto

sgfcharset: sgfcharset.o xmalloc.o

sgfinfo.o: sgfinfo.c

sgfdbinfo.o: sgfinfo.c
	cc $(CFLAGS) -DREAD_FROM_DB -c sgfinfo.c -o sgfdbinfo.o

sgftopng: sgftopng.o

nk2sgf: readsgf.o writesgf.o xmalloc.o

# Something like this spoils the $^ macro
# $(PROGS): Makefile

DEPFILE=make.depend

makefile: Makefile makefile.tail
	cat $^ > $@
	rm -f $(DEPFILE)
	make --no-print-directory $(DEPFILE)

# gnumake complains about $include
-include $(DEPFILE)

clean:
	rm -f *~ $(OBJECTS) $(PROGS)
