all: juggler

CFLAGS = -std=c99 -Wall -pedantic -march=native -I./BLAKE2/sse -DLOGLEVEL=2

BLAKE2_FILES=BLAKE2/sse/blake2b.c BLAKE2/sse/blake2s.c

ifeq ($(DEBUG),1)
	CFLAGS += -O0
else
	CFLAGS += -O3 -DNDEBUG
endif

juggler: proofofwork.o log.o juggler.c $(BLAKE2_FILES)
	gcc $(CFLAGS) $(BLAKE2_FILES) proofofwork.o log.o juggler.c -o juggler

proofofwork.o: proofofwork.c proofofwork.h log.h
	gcc $(CFLAGS) -c proofofwork.c

log.o: log.c log.h
	gcc $(CFLAGS) -c log.c

clean:
	rm -f log.o proofofwork.o juggler

.PHONY: all clean
