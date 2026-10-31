CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -std=gnu11 -Isrc -Ivendor
LDLIBS  = -lpthread -lm -ldl
SQLFLAGS = -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -w

all: build/ossp-server

build/sqlite3.o: vendor/sqlite3.c vendor/sqlite3.h
	@mkdir -p build
	@echo "Compiling SQLite (one-time, ~30s)..."
	$(CC) $(SQLFLAGS) -Ivendor -c vendor/sqlite3.c -o $@

build/%.o: src/%.c src/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/ossp-server: build/sqlite3.o build/server.o build/sched.o build/crypto.o
	$(CC) $^ -o $@ $(LDLIBS)

run: all
	./build/ossp-server

clean:
	rm -rf build

.PHONY: all run clean
