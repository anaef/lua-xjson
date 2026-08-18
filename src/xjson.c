/*
 * Lua xjson module
 *
 * Copyright (C) 2026 Andre Naef
 */


#include <lua.h>
#include <lauxlib.h>
#include "xjson.h"
#include "xjson_decode.h"
#include "xjson_encode.h"


char xjson_null;
char xjson_properties;


static const luaL_Reg functions[] = {
	{ "decode", xjson_decode },
	{ "encode", xjson_encode },
	{ NULL, NULL }
};


int luaopen_xjson (lua_State *L) {
	luaL_newlib(L, functions);
	lua_pushlightuserdata(L, &xjson_null);
	lua_setfield(L, -2, "null");
	lua_pushlightuserdata(L, &xjson_properties);
	lua_setfield(L, -2, "properties");
	xjson_open_decode(L);
	xjson_open_encode(L);
	return 1;
}
