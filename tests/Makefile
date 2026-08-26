# Native (x86_64) unit tests for the portable logic in src/common/crypto.c.
# These run on the dev host (no PS3 needed) and exercise the real mbedTLS
# crypto used by GameStream pairing. They do NOT test PSL1GHT/network code.

MBEDTLS_SRC ?= $(CURDIR)/../.cache/mbedtls-src
MBEDTLS_LIB := $(MBEDTLS_SRC)/library
MBEDTLS_INC := $(MBEDTLS_SRC)/include

CC := gcc
CFLAGS := -O2 -I../src -I$(MBEDTLS_INC)
LDFLAGS := -L$(MBEDTLS_LIB)
LIBS := -lmbedtls -lmbedx509 -lmbedcrypto

.PHONY: all run clean
all: test_crypto

test_crypto: test_crypto.c ../src/common/crypto.c
	$(CC) $(CFLAGS) test_crypto.c ../src/common/crypto.c -o $@ $(LDFLAGS) $(LIBS)

run: test_crypto
	LD_LIBRARY_PATH=$(MBEDTLS_LIB) ./test_crypto

clean:
	rm -f test_crypto
