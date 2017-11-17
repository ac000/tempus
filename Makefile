CC=gcc
CFLAGS=-Wall -Wextra -g -O2 -std=c99 -D_FILE_OFFSET_BITS=64 -fstack-protector-strong -fPIC
LDFLAGS=-Wl,-z,now,--as-needed -pie
LIBS=`pkg-config --libs gtk+-3.0 glib-2.0 gmodule-2.0`
INCS=`pkg-config --cflags gtk+-3.0 glib-2.0 gmodule-2.0`

all: tempus timer

tempus: tempus.o
	 $(CC) ${LDFLAGS} -o tempus tempus.o ${LIBS} -luuid -ltokyocabinet

tempus.o: tempus.c
	$(CC) $(CFLAGS) -c tempus.c ${INCS}

timer: timer.o
	$(CC) ${LDFLAGS} -o timer timer.o ${LIBS}

timer.o: timer.c
	$(CC) $(CFLAGS) -c timer.c ${INCS}

clean:
	rm -f tempus timer *.o
