PKGNAME=	librbhttp
LIBNAME=	librbhttp
LIBVER=		1

#BIN= bin/rb_http_handler
#BIN_FILES= bin/*
TESTS=tests/rb_http_handler_test.c
SRCS=	src/rb_http_handler.c
OBJS=	$(SRCS:.c=.o)
HDRS=   src/librb-http.h

.PHONY:

.PHONY: version.c

include mklove/Makefile.base

all: lib

librbhttp.lds: librbhttp.lds.pre
	cp $< $@

test: bin/run_tests build-test

bin/run_tests:
	$(CC) $(TESTS) src/rb_http_handler.c $(LIBS) -lcmocka -o bin/run_tests

example:
	$(CC) src/rb_http_handler_example.c -L/usr/local/lib -lrbhttp -o bin/example

build-test:
	bin/run_tests
	rm bin/run_tests

version.c:
	@rm -f $@
	@echo "const char *librb-http_revision=\"`git describe --abbrev=6 --dirty --tags --always`\";" >> $@
	@echo 'const char *librb-http_version="1.0.0";' >> $@

install: lib-install

clean: lib-clean

-include $(DEPS)
