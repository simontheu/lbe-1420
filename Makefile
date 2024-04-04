CFLAGS=-g
LBPROGRAMS=lbe-142x

HIDLIB=-L. -lhidapi-hidraw -Wl,-rpath,.

all: lbe-142x-utils

libhidapi-hidraw.so:
	ln -s libhidapi-hidraw.so.0 libhidapi-hidraw.so

lbe-142x-utils: lbe-142x.c
	gcc ${CFLAGS} -o lbe-142x lbe-142x.c -I.

all-clean:
	rm ${LBPROGRAMS} libhidapi-hidraw.so

clean:
	rm ${PROGRAMS}


