CC:=cc
CFLAGS:=-std=c99 -Wpedantic -Wall -Wextra -O3
BIN_PATH:=~/.local/share/bin
SHELL:=bash

.PHONY: all clean install uninstall

all: bin/dl

clean:
	rm -rf bin;

install: bin/dl
	mkdir -p ${BIN_PATH};
	cp bin/dl ${BIN_PATH};

uninstall:
	rm -rf ${BIN_PATH}/dl;

bin/dl: src/dl.c src/dl.h
	mkdir -p bin;
	${CC} ${CFLAGS} -o${@} ${<};
