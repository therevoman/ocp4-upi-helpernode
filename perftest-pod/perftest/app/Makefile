.PHONY: clean

CFLAGS  := -Wall -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := ErnicImitator ImageReceiver

all: ${APPS}

ErnicImitator: rdma-common.o ErnicImitator.o
	${LD} ${CFLAGS} -o $@ $^ ${LDLIBS}

ImageReceiver: rdma-common.o ImageReceiver.o
	${LD} ${CFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}

