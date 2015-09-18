BIN= bin/rb_http_handler
BIN_FILES= bin/*
TESTS=tests/rb_http_handler_test.c
SRCS=	src/main.c src/rb_http_handler.c
OBJS=	$(SRCS:.c=.o)

.PHONY:

all: $(BIN)

.PHONY: version.c

include mklove/Makefile.base

tests: bin/run_tests test

bin/run_tests:
	$(CC) $(TESTS) src/rb_http_handler.c $(LIBS) -lcmocka -o bin/run_tests

test:
	bin/run_tests
	rm bin/run_tests

version.c:
	@rm -f $@
	@echo "const char *librb-http_revision=\"`git describe --abbrev=6 --dirty --tags --always`\";" >> $@
	@echo 'const char *librb-http_version="1.0.0";' >> $@

install: bin-install

clean: bin-clean

-include $(DEPS)
