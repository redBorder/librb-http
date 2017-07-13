CC 						     ?= gcc
SHARED_LIBRARY      = librbhttp.so
RUST_STATIC_LIBRARY = librbhttp.a
LIBS                = -lcurl -lz -lpthread -lrd

.PHONY: example

build:
	cargo build

example:
	$(CC) examples/example.c -L target/debug -lrbhttp $(LIBS) -o example

clean:
	rm -rf target/
