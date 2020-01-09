.PHONY: clean

CFLAGS  := -Wall -g -std=c++11 
LD      := g++
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread -lglog -Wformat

APPS    := rdma

HEADE   := rdma.h

all: ${APPS}


rdma: rdma.o
	${LD} -o $@ $^ ${LDLIBS}

%.o:%.cpp $(HEADE)
	echo 'compile'
	$(LD) -c $(CFLAGS) $< -o $@

clean:
	rm -f *.o ${APPS} *.txt

server:
	clear
	./rdma -s 12.12.10.104
client:
	clear
	./rdma -s 12.12.11.18 -c 12.12.11.19

syn:
	scp rhzhang@12.12.11.19:/home/rhzhang/rdma_one_side/Makefile ./
	scp rhzhang@12.12.11.19:/home/rhzhang/rdma_one_side/rdma.cpp ./
	scp rhzhang@12.12.11.19:/home/rhzhang/rdma_one_side/rdma.h ./


