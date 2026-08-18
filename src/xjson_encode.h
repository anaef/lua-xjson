/*
 * Lua xjson encoder
 *
 * Copyright (C) 2026 Andre Naef
 */


#ifndef _XJSON_ENCODE_INCLUDED
#define _XJSON_ENCODE_INCLUDED


#include <lua.h>


int xjson_open_encode(lua_State *L);
int xjson_encode(lua_State *L);


#endif /* _XJSON_ENCODE_INCLUDED */
