PKGNAME=	librbhttp
LIBNAME=	librdhttp
LIBVER=		1

BIN= bin/rb_http_handler
BIN_FILES= bin/*
TESTS=tests/rb_http_handler_test.c
SRCS=	src/rb_http_handler.c
OBJS=	$(SRCS:.c=.o)

.PHONY:

all: $(lib)

.PHONY: version.c

include mklove/Makefile.base

test: bin/run_tests build-test

$(LIBNAME).so.$(LIBVER): $(OBJS)
	$(LD) -g -fPIC -shared -soname,$@ \
		$(LDFLAGS) $(OBJS) -o $@ -lc
	ln -fs $(LIBNAME).so.$(LIBVER) $(LIBNAME).so 


bin/run_tests:
	$(CC) $(TESTS) src/rb_http_handler.c $(LIBS) -lcmocka -o bin/run_tests

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
