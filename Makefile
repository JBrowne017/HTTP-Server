CC = clang
CFLAGS = -Wall -Werror -Wpedantic -Wextra -pthread
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=*.o)
EXECBIN = httpserver

.PHONY: all clean format debug

all: $(EXECBIN)

debug: CFLAGS += -g
debug: all

clean:
	rm -f $(OBJ) $(EXECBIN)

format:
	clang-format -i -style=file *.[c,h]

httpserver: httpserver.o utils.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<
