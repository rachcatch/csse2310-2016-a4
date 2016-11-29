CC = gcc
CFLAGS = -std=gnu99 -Wall -pedantic -pthread
DEBUG = -g
TARGETS = 2310controller 2310team

.PHONY: all clean

all: $(TARGETS)

debug: CFLAGS += $(DEBUG)
debug: clean $(TARGETS)

shared.o: shared.c shared.h
	$(CC) $(CFLAGS) -c shared.c -o shared.o

2310team: team.c shared.o
	$(CC) $(CFLAGS) team.c shared.o -o 2310team

2310controller: controller.c shared.o
	$(CC) $(CFLAGS) controller.c shared.o -o 2310controller

clean:
	rm $(TARGETS) *.o
