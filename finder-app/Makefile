CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -O2 -g
SRC=writer.c
TARGET=writer

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
