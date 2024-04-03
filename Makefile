CC:=cc
CFLAGS:=-std=c99 -Wpedantic -Wall -Wextra -O3
BIN_PATH:=~/.local/share/bin
SHELL:=bash

.PHONY: all clean install uninstall

all: build/bin/dl

clean:
	rm -rf build;

install: build/bin/dl
	mkdir -p ${BIN_PATH};
	cp ${<} ${BIN_PATH};

uninstall:
	rm -rf ${BIN_PATH}/dl;

build/bin/dl: src/dl.c src/dl.h
	mkdir -p build/bin;
	${CC} ${CFLAGS} -o${@} ${<};
