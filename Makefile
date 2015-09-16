BIN=	bin/librb-http

SRCS=	src/main.c src/server_handler.c
OBJS=	$(SRCS:.c=.o)

.PHONY:

all: $(BIN)

include mklove/Makefile.base

.PHONY: version.c

version.c:
	@rm -f $@
	@echo "const char *librb-http_revision=\"`git describe --abbrev=6 --dirty --tags --always`\";" >> $@
	@echo 'const char *librb-http_version="1.0.0";' >> $@

install: bin-install

clean: bin-clean

-include $(DEPS)
