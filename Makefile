VERSION:=v2.0.1
CC:=cc
CFLAGS:=-std=c99 -Wpedantic -Wall -Wextra -Wno-unused-result -O3 -DVERSION=\"${VERSION}\"
LIBS:=-ltdk
BINPATH:=~/.local/share/bin
MANPATH:=~/.local/share/man
SHELL:=zsh

.PHONY: all clean install uninstall

all: build/bin/dl

clean:
	rm -rf build;

install: build/bin/dl
	mkdir -p ${BINPATH} ${MANPATH}/man1;
	cp build/bin/dl ${BINPATH};
	sed s/\$${VERSION}/${VERSION}/ man/dl.1 > ${MANPATH}/man1/dl.1;

uninstall:
	rm -f ${BINPATH}/dl ${MANPATH}/man1/dl.1;

build/bin/dl: src/dl.c
	mkdir -p build/bin;
	${CC} ${CFLAGS} -o ${@} ${<} ${LIBS};
