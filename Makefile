CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c99
LDFLAGS = -lm

all: test bench

test: test_flux_engine.c flux_engine.h
	$(CC) $(CFLAGS) -o test_flux_engine test_flux_engine.c $(LDFLAGS)

bench: bench_flux_engine.c flux_engine.h
	$(CC) $(CFLAGS) -o bench_flux_engine bench_flux_engine.c $(LDFLAGS)

run-test: test
	./test_flux_engine

run-bench: bench
	./bench_flux_engine

clean:
	rm -f test_flux_engine bench_flux_engine

.PHONY: all test bench run-test run-bench clean
