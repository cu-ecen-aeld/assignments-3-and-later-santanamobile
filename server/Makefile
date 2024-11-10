CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -O0 -g
SRC=aesdsocket.c
TARGET=aesdsocket

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
