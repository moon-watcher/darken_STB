CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
SRCS     = main.c world.c entities.c battle.c
OBJS     = $(SRCS:.c=.o)
TARGET   = shards_of_ember

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c game.h darken.h
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
