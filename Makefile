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

slml: ${OBJ}
	@echo "  LD    $@"
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f slm ${OBJ}

.PHONY: all clean
