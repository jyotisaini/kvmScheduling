CC=gcc
CFLAG=-I. -Wall -Wno-error -pthread -lvirt
vcpu_schedular: vcpu_schedular.c
	$(CC) -o vcpu_schedular vcpu_schedular.c $(CFLAG)


all: vcpu_schedular
clean:
	$(RM) -f *.o vcpu_schedular

