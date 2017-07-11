CC 						     ?= gcc
SHARED_LIBRARY      = librbhttp.so
RUST_STATIC_LIBRARY = librbhttp.a
LIBS                = -lcurl -lz -lpthread -lrd

build:
	cargo build
	$(CC) -shared -o $(SHARED_LIBRARY) \
		-Wl,--whole-archive \
		target/debug/$(RUST_STATIC_LIBRARY) \
		-Wl,--no-whole-archive

example:
	$(CC) examples/example.c -L. -lrbhttp $(LIBS) -o example

clean:
	rm -rf target/
