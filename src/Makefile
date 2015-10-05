all: juggler

CFLAGS = -O3

juggler: proofofwork.o log.o
	gcc $(CFLAGS) -lcrypto proofofwork.o log.o juggler.c -o juggler

proofofwork.o: proofofwork.c proofofwork.h log.h
	gcc $(CFLAGS) -c proofofwork.c

log.o: log.c log.h
	gcc $(CFLAGS) -c log.c

clean:
	rm -f log.o proofofwork.o juggler

.PHONY: all
