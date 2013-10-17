include config.mk

SRC = slml.c
OBJ = ${SRC:.c=.o}

all: slml

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
	rm -f slml ${OBJ}

.PHONY: all clean
