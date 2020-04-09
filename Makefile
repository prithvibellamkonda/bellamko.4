CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -lrt -lpthread


all: oss process

.PHONY: clean backup

clean:
	rm -rf *~ *.o *.log oss process

backup:
	rm -rf backup
	mkdir backup
	cp Makefile *.c backup/
