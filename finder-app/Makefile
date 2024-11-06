CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -O2 -g

all: writer

writer: writer.o
	$(CC) $(CFLAGS) -o writer writer.o

writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c

clean:
	rm -f *.o writer
