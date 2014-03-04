#
# author: hone
#

CC      = g++

CFLAGS += -g
CFLAGS += -Wall
CFLAGS += -O2
CFLAGS += -D_REENTRANT

DEST   = out_demo
LIBOUT = libyhepoll.so

LIBS += -Wl,-rpath,. -L. -lyhsocket -lpthread
LIBS += $(LIBOUT)
LIBS += -lpthread

HEAD = yhepoll.h yhsocket.h
OBJS = main.o yhepoll.o

	#strip $(LIBOUT)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@ 


all: $(LIBOUT) $(DEST)

$(LIBOUT): ${HEAD} yhepoll.o
	$(CC) $(CFLAGS) yhepoll.o -shared -fpic -Wl,-rpath,./ -L./ -lpthread -o $(LIBOUT)

$(DEST): ${HEAD} $(OBJS)
	$(CC) $(CFLAGS) -o $(DEST) main.o $(LIBS) 

clean:
	rm $(DEST) $(LIBOUT) $(OBJS) -rf
