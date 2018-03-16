GCC_FLAGS = g++ -std=c++11 -Wall

all: Search

MapReduceFramework.a: MapReduceFramework.o
	ar rcs MapReduceFramework.a MapReduceFramework.o

MapReduceFramework.o: MapReduceFramework.cpp MapReduceFramework.h
	$(GCC_FLAGS) -c MapReduceFramework.cpp -o MapReduceFramework.o

Search: Search.cpp MapReduceFramework.a MapReduceClient.h
	$(GCC_FLAGS) Search.cpp MapReduceFramework.a -lpthread -o Search

tar:
	tar cvf ex3.tar Makefile README MapReduceFramework.cpp Search.cpp

valgrind:
	valgrind --leak-check=full --show-possibly-lost=yes --show-reachable=yes --undef-value-errors=yes ./Search
	
clean:
	rm -f *.o *.a *.tar Search
