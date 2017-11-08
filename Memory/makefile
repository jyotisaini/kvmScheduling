CC=gcc
CFLAG=-I. -Wall -Wno-error -pthread -lvirt
Memory_coordinator: Memory_coordinator.c
	$(CC) -o Memory_coordinator Memory_coordinator.c $(CFLAG)


all: Memory_coordinator
clean:
	$(RM) -f *.o Memory_coordinator


