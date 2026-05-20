CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
TARGET = vfs
SRCS   = vfs.c shell.c
OBJS   = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c vfs.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

# Remove the filesystem too (fresh start)
reset: clean
	rm -f filesystem.bin

.PHONY: all clean reset
