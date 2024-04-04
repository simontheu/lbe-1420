CFLAGS=-g
LBPROGRAMS=lbe-1420-set-freq lbe-1420-status

HIDLIB=-L. -lhidapi-hidraw -Wl,-rpath,.

all: lbe-1420-utils

gpsdo-status: gpsdo-status.cpp libhidapi-hidraw.so
	gcc ${CFLAGS} -o lbe-1420-status lbe-1420-status.cpp -I. -lstdc++ ${HIDLIB}

libhidapi-hidraw.so:
	ln -s libhidapi-hidraw.so.0 libhidapi-hidraw.so

lbe-1420-utils: lbe-1420-status.cpp lbe-1420-set-freq.cpp
	gcc ${CFLAGS} -o lbe-1420-status lbe-1420-status.cpp -I. -lstdc++
	gcc ${CFLAGS} -o lbe-1420-set-freq lbe-1420-set-freq.cpp -I. -lstdc++

all-clean:
	rm ${LBPROGRAMS} libhidapi-hidraw.so

clean:
	rm ${PROGRAMS}


