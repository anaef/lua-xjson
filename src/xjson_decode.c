/*
 * Lua xjson decoder
 *
 * Copyright (C) 2026 Andre Naef
 */


#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <lua.h>
#include <lauxlib.h>
#include "xjson.h"
#include "xjson_decode.h"
#include "xjson_number.h"


#define XJSON_DECODER                "xjson.decoder"
#define XJSON_DECODE_DEPTH_MAX       128
#define XJSON_DECODE_STACK_MAX       (XJSON_DECODE_DEPTH_MAX * 3 + 3)
#define XJSON_DECODE_ESCAPE_UNICODE  0x100

#if defined(__GNUC__) || defined(__clang__)
#define XJSON_UNLIKELY(x)  __builtin_expect(!!(x), 0)
#else
#define XJSON_UNLIKELY(x)  (x)
#endif


typedef enum {
	XJSON_DECODE_BOM                = 1 << 0,
	XJSON_DECODE_COMMENTS           = 1 << 1,
	XJSON_DECODE_NONFINITE          = 1 << 2,
	XJSON_DECODE_PROPERTIES         = 1 << 3,
	XJSON_DECODE_NUMBERS_AS_STRINGS = 1 << 4,
	XJSON_DECODE_TRAILING_COMMAS    = 1 << 5,
	XJSON_DECODE_VALIDATE_UTF8      = 1 << 6,
	XJSON_DECODE_INCREMENTAL        = 1 << 7
} xjson_decode_flag_e;

typedef struct xjson_decoder_s xjson_decoder_t;

struct xjson_decoder_s {
	lua_State   *L;                                         /* Lua state */
	const char  *input;                                     /* start of input */
	const char  *current;                                   /* current input position */
	const char  *end;                                       /* end of input */
	char        *buffer;                                    /* file input buffer */
	size_t       buffer_length;                             /* file buffer length */
	char        *string;                                    /* string decoding buffer */
	size_t       string_alloc;                              /* string decoding buffer capacity */
	size_t       string_length;                             /* decoded string length */
	unsigned     depth;                                     /* current nesting depth */
	unsigned     flags;                                     /* decoder flags */
	int          array_sizes[XJSON_DECODE_DEPTH_MAX + 1];   /* array size hints */
	int          object_sizes[XJSON_DECODE_DEPTH_MAX + 1];  /* object size hints */
};


static void xjson_decode_error(xjson_decoder_t *decoder, const char *message);
static int xjson_decode_close(lua_State *L);
static void xjson_decode_read(xjson_decoder_t *decoder, FILE *file);
static void xjson_decode_whitespace(xjson_decoder_t *decoder);
static char *xjson_decode_string_grow(xjson_decoder_t *decoder, size_t size);
static inline char *xjson_decode_string_reserve(xjson_decoder_t *decoder, size_t size);
static inline unsigned xjson_decode_hex(xjson_decoder_t *decoder, const char *p);
static inline const char *xjson_decode_utf8_sequence(xjson_decoder_t *decoder, const char *p,
		unsigned char c);
static void xjson_decode_string(xjson_decoder_t *decoder);
static void xjson_decode_string_escaped(xjson_decoder_t *decoder, const char *start, const char *p);
static void xjson_decode_number(xjson_decoder_t *decoder);
static void xjson_decode_nonfinite(xjson_decoder_t *decoder);
static void xjson_decode_array(xjson_decoder_t *decoder, int array_member);
static void xjson_decode_object(xjson_decoder_t *decoder, int array_member);
static void xjson_decode_value(xjson_decoder_t *decoder, int array_member);


static const uint16_t xjson_decode_escapes[256] = {
	['"']  = '"',
	['\\'] = '\\',
	['/']  = '/',
	['b']  = '\b',
	['f']  = '\f',
	['n']  = '\n',
	['r']  = '\r',
	['t']  = '\t',
	['u']  = XJSON_DECODE_ESCAPE_UNICODE
};


static void xjson_decode_error (xjson_decoder_t *decoder, const char *message) {
	ptrdiff_t  position;

	position = decoder->current - decoder->input;
	if (position >= 0 && (uintmax_t)position < (uintmax_t)LUA_MAXINTEGER) {
		(void)luaL_error(decoder->L, "invalid JSON at byte %I: %s", (lua_Integer)position + 1,
				message);
	}
	(void)luaL_error(decoder->L, "invalid JSON: %s", message);
}

static int xjson_decode_close (lua_State *L) {
	xjson_decoder_t  *decoder;

	decoder = lua_touserdata(L, 1);
	free(decoder->buffer);
	free(decoder->string);
	return 0;
}

static void xjson_decode_read (xjson_decoder_t *decoder, FILE *file) {
	int          fd;
	char        *data;
	off_t        position;
	size_t       alloc, available, count;
	struct stat  status;

	alloc = LUAL_BUFFERSIZE;
	fd = fileno(file);
	if (fd != -1 && fstat(fd, &status) == 0 && S_ISREG(status.st_mode)) {
		position = ftello(file);
		if (position >= 0 && position <= status.st_size &&
				(uintmax_t)(status.st_size - position) <= SIZE_MAX - 2) {
			alloc = (size_t)(status.st_size - position) + 2;
		}
	}
	data = malloc(alloc);
	if (XJSON_UNLIKELY(data == NULL)) {
		(void)luaL_error(decoder->L, "memory allocation error");
	}
	decoder->buffer = data;
	while (1) {
		available = alloc - decoder->buffer_length - 1;
		count = fread(decoder->buffer + decoder->buffer_length, 1, available, file);
		decoder->buffer_length += count;
		if (count != available) {
			if (ferror(file) || !feof(file)) {
				(void)luaL_error(decoder->L, "error reading JSON document");
			}
			break;
		}
		if (alloc < 64 * 1024) {
			if (XJSON_UNLIKELY(alloc > SIZE_MAX / 2)) {
				(void)luaL_error(decoder->L, "JSON document is too large");
			}
			alloc *= 2;
		} else {
			if (XJSON_UNLIKELY(alloc > SIZE_MAX - alloc / 2)) {
				(void)luaL_error(decoder->L, "JSON document is too large");
			}
			alloc += alloc / 2;
		}
		data = realloc(decoder->buffer, alloc);
		if (XJSON_UNLIKELY(data == NULL)) {
			(void)luaL_error(decoder->L, "memory allocation error");
		}
		decoder->buffer = data;
	}
	decoder->buffer[decoder->buffer_length] = '\0';
}

static void xjson_decode_whitespace (xjson_decoder_t *decoder) {
	const char  *p;

	p = decoder->current;
	while (1) {
		while (p < decoder->end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
			p++;
		}
		if (!(decoder->flags & XJSON_DECODE_COMMENTS) || *p != '/') {
			break;
		}
		if (p[1] == '/') {
			p += 2;
			while (p < decoder->end && *p != '\n' && *p != '\r') {
				p++;
			}
			continue;
		}
		if (p[1] == '*') {
			p += 2;
			while (p < decoder->end && (*p != '*' || p[1] != '/')) {
				p++;
			}
			if (p == decoder->end) {
				decoder->current = p;
				xjson_decode_error(decoder, "unterminated comment");
			}
			p += 2;
			continue;
		}
		break;
	}
	decoder->current = p;
}

static char *xjson_decode_string_grow (xjson_decoder_t *decoder, size_t size) {
	char    *string;
	size_t   alloc, required;

	if (XJSON_UNLIKELY(size > SIZE_MAX - decoder->string_length)) {
		(void)luaL_error(decoder->L, "JSON string is too large");
		return NULL;  /* not reached */
	}
	required = decoder->string_length + size;
	alloc = decoder->string_alloc;
	if (alloc == 0) {
		alloc = required;
	}
	while (alloc < required) {
		if (alloc < 64 * 1024) {
			if (XJSON_UNLIKELY(alloc > SIZE_MAX / 2)) {
				(void)luaL_error(decoder->L, "JSON string is too large");
				return NULL;  /* not reached */
			}
			alloc *= 2;
		} else {
			if (XJSON_UNLIKELY(alloc > SIZE_MAX - alloc / 2)) {
				(void)luaL_error(decoder->L, "JSON string is too large");
				return NULL;  /* not reached */
			}
			alloc += alloc / 2;
		}
	}
	string = realloc(decoder->string, alloc);
	if (XJSON_UNLIKELY(string == NULL)) {
		(void)luaL_error(decoder->L, "memory allocation error");
		return NULL;  /* not reached */
	}
	decoder->string = string;
	decoder->string_alloc = alloc;
	return string + decoder->string_length;
}

static inline char *xjson_decode_string_reserve (xjson_decoder_t *decoder, size_t size) {
	if (size <= decoder->string_alloc - decoder->string_length) {
		return decoder->string + decoder->string_length;
	}
	return xjson_decode_string_grow(decoder, size);
}

static inline unsigned xjson_decode_hex (xjson_decoder_t *decoder, const char *p) {
	int            i;
	unsigned       code, value;
	unsigned char  c;

	code = 0;
	for (i = 0; i < 4; i++) {
		c = (unsigned char)p[i];
		if (c >= '0' && c <= '9') {
			value = c - '0';
		} else {
			c |= 0x20;
			if (c >= 'a' && c <= 'f') {
				value = c - 'a' + 10;
			} else {
				decoder->current = p + i;
				xjson_decode_error(decoder, "invalid Unicode escape");
				return 0;  /* not reached */
			}
		}
		code = (code << 4) | value;
	}
	return code;
}

static inline const char *xjson_decode_utf8_sequence (xjson_decoder_t *decoder, const char *p,
		unsigned char c) {
	int            i, length;
	unsigned char  next;

	if (c >= 0xc2 && c <= 0xdf) {
		length = 2;
	} else if (c >= 0xe0 && c <= 0xef) {
		length = 3;
	} else if (c >= 0xf0 && c <= 0xf4) {
		length = 4;
	} else {
		decoder->current = p;
		xjson_decode_error(decoder, "invalid UTF-8 sequence");
		return NULL;  /* not reached */
	}
	if (XJSON_UNLIKELY(decoder->end - p < length)) {
		decoder->current = p;
		xjson_decode_error(decoder, "incomplete UTF-8 sequence");
		return NULL;  /* not reached */
	}
	if (XJSON_UNLIKELY((c == 0xe0 && (unsigned char)p[1] < 0xa0) ||
			(c == 0xed && (unsigned char)p[1] >= 0xa0) ||
			(c == 0xf0 && (unsigned char)p[1] < 0x90) ||
			(c == 0xf4 && (unsigned char)p[1] >= 0x90))) {
		decoder->current = p;
		xjson_decode_error(decoder, "invalid UTF-8 sequence");
		return NULL;  /* not reached */
	}
	for (i = 1; i < length; i++) {
		next = (unsigned char)p[i];
		if (XJSON_UNLIKELY((next & 0xc0) != 0x80)) {
			decoder->current = p + i;
			xjson_decode_error(decoder, "invalid UTF-8 sequence");
			return NULL;  /* not reached */
		}
	}
	return p + length;
}

static void xjson_decode_string (xjson_decoder_t *decoder) {
	const char    *p, *start;
	unsigned char  c;

	p = decoder->current + 1;
	start = p;
	while (p < decoder->end) {
		c = (unsigned char)*p;
		if (c == '"') {
			decoder->current = p + 1;
			lua_pushlstring(decoder->L, start, (size_t)(p - start));
			return;
		}
		if (c == '\\') {
			xjson_decode_string_escaped(decoder, start, p);
			return;
		}
		if (XJSON_UNLIKELY(c < 0x20)) {
			decoder->current = p;
			xjson_decode_error(decoder, "unescaped control character");
		}
		if (!(decoder->flags & XJSON_DECODE_VALIDATE_UTF8) || c < 0x80) {
			p++;
			continue;
		}
		p = xjson_decode_utf8_sequence(decoder, p, c);
	}
	decoder->current = p;
	xjson_decode_error(decoder, "unterminated string");
}

static void xjson_decode_string_escaped (xjson_decoder_t *decoder, const char *start,
		const char *p) {
	char          *write;
	size_t         size;
	unsigned       code, escape, low;
	const char    *sequence;
	unsigned char  c;

	decoder->string_length = 0;
	size = (size_t)(p - start);
	if (size != 0) {
		write = xjson_decode_string_reserve(decoder, size);
		memcpy(write, start, size);
		decoder->string_length = size;
	}
	while (p < decoder->end) {
		c = (unsigned char)*p;
		if (c == '"') {
			decoder->current = p + 1;
			lua_pushlstring(decoder->L, decoder->string, decoder->string_length);
			return;
		}
		if (c == '\\') {
			p++;
			if (p == decoder->end) {
				decoder->current = p;
				xjson_decode_error(decoder, "unterminated escape sequence");
			}
			c = (unsigned char)*p;
			escape = xjson_decode_escapes[c];
			if (escape == 0) {
				decoder->current = p;
				xjson_decode_error(decoder, "invalid escape sequence");
			}
			p++;
			if (escape == XJSON_DECODE_ESCAPE_UNICODE) {
				if (decoder->end - p < 4) {
					decoder->current = p;
					xjson_decode_error(decoder, "incomplete Unicode escape");
				}
				code = xjson_decode_hex(decoder, p);
				p += 4;
				if (code >= 0xd800 && code <= 0xdbff) {
					if (decoder->end - p < 6 || p[0] != '\\' || p[1] != 'u') {
						decoder->current = p;
						xjson_decode_error(decoder, "missing low surrogate");
					}
					p += 2;
					low = xjson_decode_hex(decoder, p);
					p += 4;
					if (low < 0xdc00 || low > 0xdfff) {
						decoder->current = p - 4;
						xjson_decode_error(decoder, "invalid low surrogate");
					}
					code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
				} else if (code >= 0xdc00 && code <= 0xdfff) {
					decoder->current = p - 4;
					xjson_decode_error(decoder, "unexpected low surrogate");
				}
				if (code <= 0x7f) {
					write = xjson_decode_string_reserve(decoder, 1);
					*write = (char)code;
					decoder->string_length++;
				} else if (code <= 0x7ff) {
					write = xjson_decode_string_reserve(decoder, 2);
					write[0] = (char)(0xc0 | (code >> 6));
					write[1] = (char)(0x80 | (code & 0x3f));
					decoder->string_length += 2;
				} else if (code <= 0xffff) {
					write = xjson_decode_string_reserve(decoder, 3);
					write[0] = (char)(0xe0 | (code >> 12));
					write[1] = (char)(0x80 | ((code >> 6) & 0x3f));
					write[2] = (char)(0x80 | (code & 0x3f));
					decoder->string_length += 3;
				} else {
					write = xjson_decode_string_reserve(decoder, 4);
					write[0] = (char)(0xf0 | (code >> 18));
					write[1] = (char)(0x80 | ((code >> 12) & 0x3f));
					write[2] = (char)(0x80 | ((code >> 6) & 0x3f));
					write[3] = (char)(0x80 | (code & 0x3f));
					decoder->string_length += 4;
				}
				continue;
			}
			write = xjson_decode_string_reserve(decoder, 1);
			*write = (char)escape;
			decoder->string_length++;
			continue;
		}
		if (XJSON_UNLIKELY(c < 0x20)) {
			decoder->current = p;
			xjson_decode_error(decoder, "unescaped control character");
		}
		if (!(decoder->flags & XJSON_DECODE_VALIDATE_UTF8) || c < 0x80) {
			write = xjson_decode_string_reserve(decoder, 1);
			*write = (char)c;
			decoder->string_length++;
			p++;
			continue;
		}
		sequence = p;
		p = xjson_decode_utf8_sequence(decoder, sequence, c);
		size = (size_t)(p - sequence);
		write = xjson_decode_string_reserve(decoder, size);
		memcpy(write, sequence, size);
		decoder->string_length += size;
	}
	decoder->current = p;
	xjson_decode_error(decoder, "unterminated string");
}

static void xjson_decode_number (xjson_decoder_t *decoder) {
	const char     *message, *start;
	xjson_number_t  value;

	if (decoder->flags & XJSON_DECODE_NUMBERS_AS_STRINGS) {
		start = decoder->current;
		if (XJSON_UNLIKELY(!xjson_number_parse(&decoder->current, NULL, &message))) {
			xjson_decode_error(decoder, message);
		}
		lua_pushlstring(decoder->L, start, (size_t)(decoder->current - start));
		return;
	}
	if (XJSON_UNLIKELY(!xjson_number_parse(&decoder->current, &value, &message))) {
		xjson_decode_error(decoder, message);
	}
	if (value.type == XJSON_NUMBER_INTEGER) {
		lua_pushinteger(decoder->L, value.value.integer);
	} else {
		lua_pushnumber(decoder->L, value.value.number);
	}
}

static void xjson_decode_nonfinite (xjson_decoder_t *decoder) {
	int          negative;
	const char  *literal;

	negative = *decoder->current == '-';
	literal = decoder->current + negative;
	if (decoder->end - literal >= 8 && memcmp(literal, "Infinity", 8) == 0) {
		if (!(decoder->flags & XJSON_DECODE_NONFINITE)) {
			xjson_decode_error(decoder, "non-finite number is not permitted");
		}
		if (decoder->flags & XJSON_DECODE_NUMBERS_AS_STRINGS) {
			lua_pushlstring(decoder->L, decoder->current, (size_t)negative + 8);
		} else if (negative) {
			lua_pushnumber(decoder->L, -(lua_Number)HUGE_VAL);
		} else {
			lua_pushnumber(decoder->L, (lua_Number)HUGE_VAL);
		}
		decoder->current = literal + 8;
		return;
	}
	if (!negative && decoder->end - literal >= 3 && memcmp(literal, "NaN", 3) == 0) {
		if (!(decoder->flags & XJSON_DECODE_NONFINITE)) {
			xjson_decode_error(decoder, "non-finite number is not permitted");
		}
		if (decoder->flags & XJSON_DECODE_NUMBERS_AS_STRINGS) {
			lua_pushliteral(decoder->L, "NaN");
		} else {
			lua_pushnumber(decoder->L, (lua_Number)NAN);
		}
		decoder->current = literal + 3;
		return;
	}
	xjson_decode_error(decoder, "invalid value");
}

static void xjson_decode_array (xjson_decoder_t *decoder, int array_member) {
	lua_Integer  index;

	if (XJSON_UNLIKELY(decoder->depth == XJSON_DECODE_DEPTH_MAX)) {
		xjson_decode_error(decoder, "maximum nesting depth exceeded");
	}
	decoder->depth++;
	decoder->current++;
	lua_createtable(decoder->L, decoder->array_sizes[decoder->depth - 1], 0);
	xjson_decode_whitespace(decoder);
	index = 0;
	if (decoder->current < decoder->end && *decoder->current == ']') {
		decoder->current++;
		goto done;
	}
	while (1) {
		if (XJSON_UNLIKELY(index == LUA_MAXINTEGER)) {
			xjson_decode_error(decoder, "array is too large");
		}
		index++;
		xjson_decode_value(decoder, 1);
		lua_rawseti(decoder->L, -2, index);
		xjson_decode_whitespace(decoder);
		if (XJSON_UNLIKELY(decoder->current == decoder->end)) {
			xjson_decode_error(decoder, "unterminated array");
		}
		if (*decoder->current == ']') {
			decoder->current++;
			goto done;
		}
		if (XJSON_UNLIKELY(*decoder->current != ',')) {
			xjson_decode_error(decoder, "comma or array end expected");
		}
		decoder->current++;
		xjson_decode_whitespace(decoder);
		if (decoder->current < decoder->end && *decoder->current == ']') {
			if (!(decoder->flags & XJSON_DECODE_TRAILING_COMMAS)) {
				xjson_decode_error(decoder, "value expected after comma");
			}
			decoder->current++;
			goto done;
		}
	}

done:
	if (!(decoder->flags & XJSON_DECODE_INCREMENTAL)) {
		decoder->array_sizes[decoder->depth] = 0;
		decoder->object_sizes[decoder->depth] = 0;
		if (array_member) {
			decoder->array_sizes[decoder->depth - 1] = index > INT_MAX ? INT_MAX : (int)index;
		}
	}
	decoder->depth--;
}

static void xjson_decode_object (xjson_decoder_t *decoder, int array_member) {
	int          object, properties;
	lua_Integer  index;

	if (XJSON_UNLIKELY(decoder->depth == XJSON_DECODE_DEPTH_MAX)) {
		xjson_decode_error(decoder, "maximum nesting depth exceeded");
	}
	decoder->depth++;
	decoder->current++;
	lua_createtable(decoder->L, 0, decoder->object_sizes[decoder->depth - 1]);
	object = lua_gettop(decoder->L);
	properties = 0;
	if (decoder->flags & XJSON_DECODE_PROPERTIES) {
		lua_createtable(decoder->L, decoder->object_sizes[decoder->depth - 1], 0);
		properties = object + 1;
		lua_pushlightuserdata(decoder->L, &xjson_properties);
		lua_pushvalue(decoder->L, properties);
		lua_rawset(decoder->L, object);
	}
	xjson_decode_whitespace(decoder);
	index = 0;
	if (decoder->current < decoder->end && *decoder->current == '}') {
		decoder->current++;
		goto done;
	}
	while (1) {
		if (XJSON_UNLIKELY(index == LUA_MAXINTEGER)) {
			xjson_decode_error(decoder, "object has too many properties");
		}
		index++;
		if (XJSON_UNLIKELY(decoder->current == decoder->end || *decoder->current != '"')) {
			xjson_decode_error(decoder, "object property name expected");
		}
		xjson_decode_string(decoder);
		if (properties != 0) {
			lua_pushvalue(decoder->L, -1);
			lua_rawseti(decoder->L, properties, index);
		}
		xjson_decode_whitespace(decoder);
		if (XJSON_UNLIKELY(decoder->current == decoder->end || *decoder->current != ':')) {
			xjson_decode_error(decoder, "colon expected after property name");
		}
		decoder->current++;
		xjson_decode_value(decoder, 0);
		lua_rawset(decoder->L, object);
		xjson_decode_whitespace(decoder);
		if (XJSON_UNLIKELY(decoder->current == decoder->end)) {
			xjson_decode_error(decoder, "unterminated object");
		}
		if (*decoder->current == '}') {
			decoder->current++;
			goto done;
		}
		if (XJSON_UNLIKELY(*decoder->current != ',')) {
			xjson_decode_error(decoder, "comma or object end expected");
		}
		decoder->current++;
		xjson_decode_whitespace(decoder);
		if (decoder->current < decoder->end && *decoder->current == '}') {
			if (!(decoder->flags & XJSON_DECODE_TRAILING_COMMAS)) {
				xjson_decode_error(decoder, "property name expected after comma");
			}
			decoder->current++;
			goto done;
		}
	}

done:
	if (properties != 0) {
		lua_remove(decoder->L, properties);
	}
	if (!(decoder->flags & XJSON_DECODE_INCREMENTAL) && array_member) {
		decoder->object_sizes[decoder->depth - 1] = index > INT_MAX ? INT_MAX : (int)index;
	}
	decoder->depth--;
}

static void xjson_decode_value (xjson_decoder_t *decoder, int array_member) {
	const char  *p;

	xjson_decode_whitespace(decoder);
	p = decoder->current;
	if (XJSON_UNLIKELY(p == decoder->end)) {
		xjson_decode_error(decoder, "value expected");
	}
	switch (*p) {
	case 'n':
		if (decoder->end - p >= 4 && memcmp(p, "null", 4) == 0) {
			decoder->current = p + 4;
			lua_pushlightuserdata(decoder->L, &xjson_null);
			return;
		}
		break;
	case 'f':
		if (decoder->end - p >= 5 && memcmp(p, "false", 5) == 0) {
			decoder->current = p + 5;
			lua_pushboolean(decoder->L, 0);
			return;
		}
		break;
	case 't':
		if (decoder->end - p >= 4 && memcmp(p, "true", 4) == 0) {
			decoder->current = p + 4;
			lua_pushboolean(decoder->L, 1);
			return;
		}
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		xjson_decode_number(decoder);
		return;
	case '-':
		if (p[1] == 'I' || p[1] == 'N') {
			xjson_decode_nonfinite(decoder);
		} else {
			xjson_decode_number(decoder);
		}
		return;
	case 'I':
	case 'N':
		xjson_decode_nonfinite(decoder);
		return;
	case '"':
		xjson_decode_string(decoder);
		return;
	case '[':
		xjson_decode_array(decoder, array_member);
		return;
	case '{':
		xjson_decode_object(decoder, array_member);
		return;
	}
	xjson_decode_error(decoder, "invalid value");
}

int xjson_open_decode (lua_State *L) {
	luaL_newmetatable(L, XJSON_DECODER);
	lua_pushcfunction(L, xjson_decode_close);
	lua_setfield(L, -2, "__close");
	lua_pop(L, 1);
	return 0;
}

int xjson_decode (lua_State *L) {
	size_t                  flags_length, i, input_length;
	unsigned                decode_flags;
	const char             *flags, *input;
	luaL_Stream            *stream;
	xjson_decoder_t        *decoder;

	/* input */
	input = NULL;
	input_length = 0;
	stream = NULL;
	if (lua_type(L, 1) == LUA_TSTRING) {
		input = lua_tolstring(L, 1, &input_length);
	} else {
		stream = luaL_testudata(L, 1, LUA_FILEHANDLE);
		luaL_argexpected(L, stream != NULL, 1, "string or file");
		luaL_argcheck(L, stream->closef != NULL, 1, "attempt to use a closed file");
	}

	/* flags */
	decode_flags = 0;
	flags = luaL_optlstring(L, 2, "", &flags_length);
	for (i = 0; i < flags_length; i++) {
		switch (flags[i]) {
		case 'b':
			decode_flags |= XJSON_DECODE_BOM;
			break;
		case 'c':
			decode_flags |= XJSON_DECODE_COMMENTS;
			break;
		case 'n':
			decode_flags |= XJSON_DECODE_NONFINITE;
			break;
		case 'o':
			decode_flags |= XJSON_DECODE_PROPERTIES;
			break;
		case 's':
			decode_flags |= XJSON_DECODE_NUMBERS_AS_STRINGS;
			break;
		case 't':
			decode_flags |= XJSON_DECODE_TRAILING_COMMAS;
			break;
		case 'v':
			decode_flags |= XJSON_DECODE_VALIDATE_UTF8;
			break;
		case 'i':
			decode_flags |= XJSON_DECODE_INCREMENTAL;
			break;
		default:
			return luaL_argerror(L, 2, lua_pushfstring(L, "invalid flag '%c'", flags[i]));
		}
	}
	lua_settop(L, 1);

	/* decoder */
	decoder = lua_newuserdatauv(L, sizeof(xjson_decoder_t), 0);
	memset(decoder, 0, sizeof(xjson_decoder_t));
	luaL_setmetatable(L, XJSON_DECODER);
	lua_toclose(L, 2);
	decoder->L = L;
	decoder->flags = decode_flags;
	luaL_checkstack(L, XJSON_DECODE_STACK_MAX, "decoding JSON document");

	/* document */
	if (stream != NULL) {
		xjson_decode_read(decoder, stream->f);
		decoder->input = decoder->buffer;
		input_length = decoder->buffer_length;
	} else {
		decoder->input = input;
	}
	decoder->current = decoder->input;
	decoder->end = decoder->input + input_length;
	if (decoder->flags & XJSON_DECODE_BOM && decoder->end - decoder->current >= 3
			&& (unsigned char)decoder->current[0] == 0xef
			&& (unsigned char)decoder->current[1] == 0xbb
			&& (unsigned char)decoder->current[2] == 0xbf) {
		decoder->current += 3;
	}
	xjson_decode_value(decoder, 0);
	xjson_decode_whitespace(decoder);
	if (decoder->current != decoder->end) {
		xjson_decode_error(decoder, "trailing data");
	}
	return 1;
}
