include config.mk

SRC = slm.c
OBJ = ${SRC:.c=.o}

all: slm

config.h:
	cp config.def.h config.h

.c.o:
	@echo "  CC    $<"
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

slm: libutf/libutf.a ${OBJ}
	@echo "  LD    $@"
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

libutf/libutf.a:
	@git submodule init
	@git submodule update
	@${MAKE} -C libutf

clean:
	rm -f slm ${OBJ}

.PHONY: all clean
