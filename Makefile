CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -s

all: main.o smarthome
smarthome: main.o
	$(CC) $(LDFLAGS) main.o -o smarthome -lcurl
main.o: main.c smarthome.h
	$(CC) $(CFLAGS) -c main.c -o main.o
clean:
	rm -f *.o smarthome