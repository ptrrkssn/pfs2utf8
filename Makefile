# Makefile

CC=gcc -m64
CFLAGS=-O -g -Wall

BINS=pfs2utf8
OBJS=pfs2utf8.o version.o

all: $(BINS)

pfs2utf8: $(OBJS)
	$(CC) -o pfs2utf8 $(OBJS)

version version.c:
	git tag | sed -e 's/^v//' | nawk '{ print "char version[] = \"" $$1 "\";" } END {exit 1}' >.version && mv .version version.c

clean:
	-rm -f $(BINS) core *~ \#* *.o

distclean: clean
	-rm -f version.c
