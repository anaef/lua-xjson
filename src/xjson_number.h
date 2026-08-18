/*
 * Lua xjson number conversion
 *
 * Copyright (C) 2026 Andre Naef
 */


#ifndef _XJSON_NUMBER_INCLUDED
#define _XJSON_NUMBER_INCLUDED


#include <lua.h>


#define XJSON_NUMBER_BUFFER_SIZE  40

#if LUA_FLOAT_TYPE != LUA_FLOAT_FLOAT && LUA_FLOAT_TYPE != LUA_FLOAT_DOUBLE
#error Lua xjson requires Lua configured with float or double numbers
#endif


typedef enum {
	XJSON_NUMBER_INTEGER,
	XJSON_NUMBER_FLOAT
} xjson_number_type_e;

typedef struct xjson_number_s xjson_number_t;

struct xjson_number_s {
	xjson_number_type_e  type;
	union {
		lua_Integer  integer;
		lua_Number   number;
	} value;
};


int xjson_number_parse(const char **p, xjson_number_t *number, const char **message);
char *xjson_number_format_float(char *p, lua_Number number, unsigned precision);
char *xjson_number_format_integer(char *p, lua_Integer number);


#endif /* _XJSON_NUMBER_INCLUDED */
