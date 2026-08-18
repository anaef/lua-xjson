/*
 * Lua xjson module
 *
 * Copyright (C) 2026 Andre Naef
 */


#ifndef _LUA_XJSON_INCLUDED
#define _LUA_XJSON_INCLUDED


#include <lua.h>


extern char xjson_null;
extern char xjson_properties;


int luaopen_xjson(lua_State *L);


#endif /* _LUA_XJSON_INCLUDED */
