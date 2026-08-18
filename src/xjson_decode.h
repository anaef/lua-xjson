/*
 * Lua xjson decoder
 *
 * Copyright (C) 2026 Andre Naef
 */


#ifndef _XJSON_DECODE_INCLUDED
#define _XJSON_DECODE_INCLUDED


#include <lua.h>


int xjson_open_decode(lua_State *L);
int xjson_decode(lua_State *L);


#endif /* _XJSON_DECODE_INCLUDED */
