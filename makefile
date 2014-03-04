#
# author: hone
#

CC      = gcc

CFLAGS += -g
CFLAGS += -Wall
CFLAGS += -O2
CFLAGS += -D_REENTRANT
CFLAGS += -fPIC

DEST   = out_demo
LIBOUT = libyhepoll.so

LIBS += -Wl,-rpath,./lib -L./lib -lyhsocket -lpthread 
LIBS += $(LIBOUT)
LIBS += -lpthread
#LIBS += ./libunpipc.a

HEAD = yhepoll.h yhsocket.h
OBJS = main.o yhepoll.o error.o

	#strip $(LIBOUT)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@ 


all: $(LIBOUT) $(DEST)

$(LIBOUT): $(HEAD) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -shared -fPIC -Wl,-rpath,./ -L./ -lpthread -o $(LIBOUT) && \
		mkdir -p lib && \
		cp libyhepoll.so  yhepoll.h lib

$(DEST): ${HEAD} $(OBJS)
	$(CC) $(CFLAGS) -o $(DEST) main.o $(LIBS) 

clean:
	rm $(DEST) $(LIBOUT) $(OBJS) -rf
