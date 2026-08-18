LUA_ABI=5.4
LUA_INCDIR=/usr/include/lua$(LUA_ABI)
LUA_BIN=/usr/bin/lua$(LUA_ABI)
LIBDIR=/usr/local/lib/lua/$(LUA_ABI)
CFLAGS=-Wall -Wextra -Wpointer-arith -Werror -fPIC -O3 -D_REENTRANT -D_GNU_SOURCE
LDFLAGS=-shared -fPIC

export LUA_CPATH=$(PWD)/?.so

default: all

all: xjson.so

xjson.so: xjson.o xjson_decode.o xjson_encode.o xjson_number.o
	gcc $(LDFLAGS) -o xjson.so xjson.o xjson_decode.o xjson_encode.o xjson_number.o -lm

xjson.o: src/xjson.h src/xjson_decode.h src/xjson_encode.h src/xjson.c
	gcc -c -o xjson.o $(CFLAGS) -I$(LUA_INCDIR) src/xjson.c

xjson_decode.o: src/xjson.h src/xjson_decode.h src/xjson_number.h src/xjson_decode.c
	gcc -c -o xjson_decode.o $(CFLAGS) -I$(LUA_INCDIR) src/xjson_decode.c

xjson_encode.o: src/xjson.h src/xjson_encode.h src/xjson_number.h src/xjson_encode.c
	gcc -c -o xjson_encode.o $(CFLAGS) -I$(LUA_INCDIR) src/xjson_encode.c

xjson_number.o: src/xjson_number.h src/xjson_number.c
	gcc -c -o xjson_number.o $(CFLAGS) -I$(LUA_INCDIR) src/xjson_number.c

.PHONY: test valgrind
test:
	$(LUA_BIN) test/test.lua

valgrind:
	valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all \
		--error-exitcode=1 $(LUA_BIN) test/test.lua

install:
	cp xjson.so $(LIBDIR)

clean:
	-rm -f xjson.o xjson_decode.o xjson_encode.o xjson_number.o xjson.so
