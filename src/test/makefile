CXX=g++
CFLAGS=-g -O2 -Wall -Wextra -fPIC -Wno-deprecated -std=c++14

INC=-I../include
LIB=-L../lib -lereactor /home/newplan/.software/lib/libprotobuf.a -lrt -lpthread
OBJS = $(addsuffix .o, $(basename $(wildcard *.cc)))

all:
	$(CXX) -o server.prog $(CFLAGS) server.cc echoMsg.pb.cc $(INC) $(LIB)
	$(CXX) -o benchmark.prog $(CFLAGS) benchmark.cc echoMsg.pb.cc $(INC) $(LIB)

clean:
	-rm -f *.o *.d server client *.proc
