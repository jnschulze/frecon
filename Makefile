APPNAME = frecon
SOURCES = $(shell echo *.c)
CC = armv7a-cros-linux-gnueabi-gcc --sysroot=/build/daisy
LIB = `pkg-config --libs libdrm` -ltsm
FLAGS = -std=c99 -D_GNU_SOURCE=1 `pkg-config --cflags libdrm`
CFLAGS = -s -O2 -Wall -Wsign-compare -Wpointer-arith -Wcast-qual -Wcast-align

all:	$(APPNAME)

.c.o:
	$(CC) $(CFLAGS) $(FLAGS) -c $*.c

OBJECTS = $(SOURCES:.c=.o)

$(APPNAME):$(OBJECTS)
	$(CC) $(CFLAGS) -o $(APPNAME) $(OBJECTS) $(LIB)

depend:
	makedepend $(SOURCES)

clean:
	@rm -f $(APPNAME) $(OBJECTS)

