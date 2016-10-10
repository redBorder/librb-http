PKGNAME=	librbhttp
LIBNAME=	librbhttp
LIBVER=		1

#BIN= bin/rb_http_handler
#BIN_FILES= bin/*
TESTS= tests/rb_http_handler_test.c
SRCS=	 src/rb_http_handler.c src/rb_http_plain.c
OBJS=	 $(SRCS:.c=.o)
HDRS=  src/librb-http.h


.PHONY:

.PHONY: version.c

all: lib

include mklove/Makefile.base

librbhttp.lds: librbhttp.lds.pre
	cp $< $@

test: build-test run-tests

build-test:
	$(CC) $(CFLAGS) $(TESTS) src/rb_http_handler.c librbhttp.a -lcmocka $(LDFLAGS) $(LIBS) -o bin/run_tests

example:
	$(CC) $(CFLAGS) src/rb_http_handler_example.c librbhttp.a -lcurl $(LDFLAGS) $(LIBS) -o bin/example

run-tests:
	-CMOCKA_MESSAGE_OUTPUT=XML CMOCKA_XML_FILE=./test-results.xml bin/run_tests
	rm bin/run_tests

version.c:
	@rm -f $@
	@echo "const char *librb-http_revision=\"`git describe --abbrev=6 --dirty --tags --always`\";" >> $@
	@echo 'const char *librb-http_version="1.0.0";' >> $@

install: lib-install

clean: lib-clean

-include $(DEPS)
