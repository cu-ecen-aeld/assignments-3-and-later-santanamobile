CC = $(CROSS_COMPILE)gcc
#CFLAGS = -Wall -Wextra -O0 -g -pthread
CFLAGS = -Wall -O0 -g -pthread
LDFLAGS = -pthread

SRC=aesdsocket.c
TARGET=aesdsocket
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
