/*
 * Lua xjson encoder
 *
 * Copyright (C) 2026 Andre Naef
 */


#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <lua.h>
#include <lauxlib.h>
#include "xjson.h"
#include "xjson_encode.h"
#include "xjson_number.h"


#define XJSON_ENCODER              "xjson.encoder"
#define XJSON_ENCODE_DEPTH_MAX     128
#define XJSON_ENCODE_STACK_MAX     (XJSON_ENCODE_DEPTH_MAX * 3 + 3)
#define XJSON_ENCODE_BUFFER_INDEX  3
#define XJSON_ENCODER_KEYS         "xjson.encoder.keys"
#define XJSON_ENCODE_KEYS_INITIAL  8
#define XJSON_ENCODE_ESCAPE_HEX    1

#if defined(__GNUC__) || defined(__clang__)
#define XJSON_UNLIKELY(x)  __builtin_expect(!!(x), 0)
#else
#define XJSON_UNLIKELY(x)  (x)
#endif


typedef enum {
	XJSON_ENCODE_NONFINITE         = 1 << 0,
	XJSON_ENCODE_NONFINITE_AS_NULL = 1 << 1,
	XJSON_ENCODE_PRETTY            = 1 << 2,
	XJSON_ENCODE_ESCAPE_SLASH      = 1 << 3,
	XJSON_ENCODE_ESCAPE_UNICODE    = 1 << 4,
	XJSON_ENCODE_VALIDATE_UTF8     = 1 << 5,
	XJSON_ENCODE_BUFFERED          = 1 << 6,
	XJSON_ENCODE_CANONICAL         = 1 << 7,
	XJSON_ENCODE_INCREMENTAL       = 1 << 8
} xjson_encode_flag_e;

typedef struct xjson_encode_buffer_s xjson_encode_buffer_t;
typedef struct xjson_encoder_s xjson_encoder_t;
typedef struct xjson_encode_key_s xjson_encode_key_t;
typedef struct xjson_encode_keys_s xjson_encode_keys_t;

struct xjson_encode_buffer_s {
	char  *data;
};

struct xjson_encoder_s {
	lua_State              *L;                         /* Lua state */
	char                    initial[LUAL_BUFFERSIZE];  /* initial output buffer */
	xjson_encode_buffer_t  *buffer;                    /* dynamic output buffer */
	char                   *output;                    /* output buffer (initial or dynamic) */
	size_t                  alloc;                     /* output buffer capacity */
	size_t                  length;                    /* output buffer length */
	FILE                   *file;                      /* output file */
	int                     error;                     /* file error number */
	unsigned                depth;                     /* current nesting depth */
	unsigned                flags;                     /* encoder flags */
	unsigned                indentation;               /* indentation width */
	unsigned                precision;                 /* floating-point precision */
};

struct xjson_encode_key_s {
	const char  *string;
	size_t       length;
};

struct xjson_encode_keys_s {
	xjson_encode_key_t  *data;
};


#if LUA_VERSION_NUM >= 505
static void *xjson_encode_release(void *ud, void *ptr, size_t osize, size_t nsize);
#endif
static int xjson_encode_close(lua_State *L);
static int xjson_encode_keys_close(lua_State *L);
static char *xjson_encode_grow(xjson_encoder_t *encoder, size_t size);
static inline char *xjson_encode_reserve(xjson_encoder_t *encoder, size_t size);
static int xjson_encode_write(xjson_encoder_t *encoder, const char *p, size_t size);
static inline int xjson_encode_byte(xjson_encoder_t *encoder, char byte);
static int xjson_encode_indent(xjson_encoder_t *encoder, unsigned depth);
static int xjson_encode_string(xjson_encoder_t *encoder, const char *start, size_t length);
static int xjson_encode_string_escaped(xjson_encoder_t *encoder, const char *start, const char *p,
		const char *end);
static int xjson_encode_number(xjson_encoder_t *encoder, int index);
static int xjson_encode_array(xjson_encoder_t *encoder, int index, size_t length);
static inline int xjson_encode_key(xjson_encoder_t *encoder, int index);
static int xjson_encode_object(xjson_encoder_t *encoder, int index);
static int xjson_encode_object_properties(xjson_encoder_t *encoder, int index, int properties);
static int xjson_encode_key_compare(const void *a, const void *b);
static int xjson_encode_object_canonical(xjson_encoder_t *encoder, int index);
static int xjson_encode_table(xjson_encoder_t *encoder, int index);
static int xjson_encode_value(xjson_encoder_t *encoder, int index);


static const char xjson_hex_digits[] = "0123456789abcdef";
static const unsigned char xjson_encode_escapes[256] = {
	[0x00] = XJSON_ENCODE_ESCAPE_HEX,
	[0x01] = XJSON_ENCODE_ESCAPE_HEX,
	[0x02] = XJSON_ENCODE_ESCAPE_HEX,
	[0x03] = XJSON_ENCODE_ESCAPE_HEX,
	[0x04] = XJSON_ENCODE_ESCAPE_HEX,
	[0x05] = XJSON_ENCODE_ESCAPE_HEX,
	[0x06] = XJSON_ENCODE_ESCAPE_HEX,
	[0x07] = XJSON_ENCODE_ESCAPE_HEX,
	['\b']  = 'b',
	['\t']  = 't',
	['\n']  = 'n',
	[0x0b] = XJSON_ENCODE_ESCAPE_HEX,
	['\f']  = 'f',
	['\r']  = 'r',
	[0x0e] = XJSON_ENCODE_ESCAPE_HEX,
	[0x0f] = XJSON_ENCODE_ESCAPE_HEX,
	[0x10] = XJSON_ENCODE_ESCAPE_HEX,
	[0x11] = XJSON_ENCODE_ESCAPE_HEX,
	[0x12] = XJSON_ENCODE_ESCAPE_HEX,
	[0x13] = XJSON_ENCODE_ESCAPE_HEX,
	[0x14] = XJSON_ENCODE_ESCAPE_HEX,
	[0x15] = XJSON_ENCODE_ESCAPE_HEX,
	[0x16] = XJSON_ENCODE_ESCAPE_HEX,
	[0x17] = XJSON_ENCODE_ESCAPE_HEX,
	[0x18] = XJSON_ENCODE_ESCAPE_HEX,
	[0x19] = XJSON_ENCODE_ESCAPE_HEX,
	[0x1a] = XJSON_ENCODE_ESCAPE_HEX,
	[0x1b] = XJSON_ENCODE_ESCAPE_HEX,
	[0x1c] = XJSON_ENCODE_ESCAPE_HEX,
	[0x1d] = XJSON_ENCODE_ESCAPE_HEX,
	[0x1e] = XJSON_ENCODE_ESCAPE_HEX,
	[0x1f] = XJSON_ENCODE_ESCAPE_HEX,
	['"']  = '"',
	['/']  = '/',
	['\\'] = '\\'
};


#if LUA_VERSION_NUM >= 505
static void *xjson_encode_release (void *ud, void *ptr, size_t osize, size_t nsize) {
	(void)ud;
	(void)osize;
	(void)nsize;
	free(ptr);
	return NULL;
}
#endif

static int xjson_encode_close (lua_State *L) {
	xjson_encode_buffer_t  *buffer;

	buffer = lua_touserdata(L, 1);
	free(buffer->data);
	return 0;
}

static int xjson_encode_keys_close (lua_State *L) {
	xjson_encode_keys_t  *keys;

	keys = lua_touserdata(L, 1);
	free(keys->data);
	return 0;
}

static char *xjson_encode_grow (xjson_encoder_t *encoder, size_t size) {
	char                   *data;
	size_t                  alloc;
	xjson_encode_buffer_t  *buffer;

	alloc = encoder->alloc;
	while (size >= alloc - encoder->length) {
		if (XJSON_UNLIKELY(alloc > SIZE_MAX / 2)) {
			(void)luaL_error(encoder->L, "JSON document is too large");
			return NULL;  /* not reached */
		}
		alloc *= 2;
	}
	buffer = encoder->buffer;
	if (encoder->output == encoder->initial) {
		if (buffer == NULL) {
			buffer = lua_newuserdatauv(encoder->L, sizeof(xjson_encode_buffer_t), 0);
			buffer->data = NULL;
			luaL_setmetatable(encoder->L, XJSON_ENCODER);
			lua_copy(encoder->L, -1, XJSON_ENCODE_BUFFER_INDEX);
			lua_pop(encoder->L, 1);
			lua_toclose(encoder->L, XJSON_ENCODE_BUFFER_INDEX);
			encoder->buffer = buffer;
		}
		data = malloc(alloc);
		if (data != NULL) {
			memcpy(data, encoder->initial, encoder->length);
		}
	} else {
		data = realloc(buffer->data, alloc);
	}
	if (XJSON_UNLIKELY(data == NULL)) {
		(void)luaL_error(encoder->L, "memory allocation error");
		return NULL;  /* not reached */
	}
	buffer->data = data;
	encoder->output = data;
	encoder->alloc = alloc;
	return data + encoder->length;
}

static inline char *xjson_encode_reserve (xjson_encoder_t *encoder, size_t size) {
	if (size < encoder->alloc - encoder->length) {
		return encoder->output + encoder->length;
	}
	return xjson_encode_grow(encoder, size);
}

static int xjson_encode_write (xjson_encoder_t *encoder, const char *p, size_t size) {
	char  *output;

	if (size == 0) {
		return 1;
	}
	if (encoder->file != NULL) {
		if (fwrite(p, 1, size, encoder->file) != size) {
			encoder->error = errno;
			return 0;
		}
		return 1;
	} else {
		output = xjson_encode_reserve(encoder, size);
		memcpy(output, p, size);
		encoder->length += size;
		return 1;
	}
}

static inline int xjson_encode_byte (xjson_encoder_t *encoder, char byte) {
	char  *output;

	if (encoder->file != NULL) {
		if (fputc((unsigned char)byte, encoder->file) == EOF) {
			encoder->error = errno;
			return 0;
		}
		return 1;
	} else {
		output = xjson_encode_reserve(encoder, 1);
		*output = byte;
		encoder->length++;
		return 1;
	}
}

static int xjson_encode_indent (xjson_encoder_t *encoder, unsigned depth) {
	size_t             size;
	static const char  spaces[] = "        ";

	size = (size_t)encoder->indentation * depth;
	while (size >= sizeof(spaces) - 1) {
		if (!xjson_encode_write(encoder, spaces, sizeof(spaces) - 1)) {
			return 0;
		}
		size -= sizeof(spaces) - 1;
	}
	return xjson_encode_write(encoder, spaces, size);
}

static int xjson_encode_string (xjson_encoder_t *encoder, const char *start, size_t length) {
	char           *output;
	const char     *end, *p;
	unsigned char   c;

	p = start;
	end = p + length;
	while (p < end) {
		c = (unsigned char)*p;
		switch (c) {
		case '"':
		case '\\':
			return xjson_encode_string_escaped(encoder, start, p, end);
		case '/':
			if (encoder->flags & XJSON_ENCODE_ESCAPE_SLASH) {
				return xjson_encode_string_escaped(encoder, start, p, end);
			}
			break;
		default:
			if (c < 0x20) {
				return xjson_encode_string_escaped(encoder, start, p, end);
			}
		}
		if (c < 0x80 || !(encoder->flags & (XJSON_ENCODE_ESCAPE_UNICODE
				| XJSON_ENCODE_VALIDATE_UTF8))) {
			p++;
			continue;
		}
		if (encoder->flags & XJSON_ENCODE_ESCAPE_UNICODE) {
			return xjson_encode_string_escaped(encoder, start, p, end);
		}
		if (c >= 0xc2 && c <= 0xdf) {
			if (XJSON_UNLIKELY(end - p < 2 || ((unsigned char)p[1] & 0xc0) != 0x80)) {
				return luaL_error(encoder->L, "invalid UTF-8 string");
			}
			p += 2;
		} else if (c >= 0xe0 && c <= 0xef) {
			if (XJSON_UNLIKELY(end - p < 3 || ((unsigned char)p[1] & 0xc0) != 0x80
					|| ((unsigned char)p[2] & 0xc0) != 0x80
					|| (c == 0xe0 && (unsigned char)p[1] < 0xa0)
					|| (c == 0xed && (unsigned char)p[1] >= 0xa0))) {
				return luaL_error(encoder->L, "invalid UTF-8 string");
			}
			p += 3;
		} else if (c >= 0xf0 && c <= 0xf4) {
			if (XJSON_UNLIKELY(end - p < 4 || ((unsigned char)p[1] & 0xc0) != 0x80
					|| ((unsigned char)p[2] & 0xc0) != 0x80
					|| ((unsigned char)p[3] & 0xc0) != 0x80
					|| (c == 0xf0 && (unsigned char)p[1] < 0x90)
					|| (c == 0xf4 && (unsigned char)p[1] >= 0x90))) {
				return luaL_error(encoder->L, "invalid UTF-8 string");
			}
			p += 4;
		} else {
			return luaL_error(encoder->L, "invalid UTF-8 string");
		}
	}
	if (encoder->file != NULL) {
		return xjson_encode_byte(encoder, '"') && xjson_encode_write(encoder, start, length)
				&& xjson_encode_byte(encoder, '"');
	}
	if (XJSON_UNLIKELY(length > SIZE_MAX - 2)) {
		return luaL_error(encoder->L, "JSON string is too large");
	}
	output = xjson_encode_reserve(encoder, length + 2);
	output[0] = '"';
	memcpy(output + 1, start, length);
	output[length + 1] = '"';
	encoder->length += length + 2;
	return 1;
}

static int xjson_encode_string_escaped (xjson_encoder_t *encoder, const char *start, const char *p,
		const char *end) {
	char           escaped[12];
	char          *output_start, *write;
	size_t         escaped_length, prefix, remaining, size;
	unsigned       code, high, low;
	const char    *span_end, *span_start;
	unsigned char  c, c1, c2, c3, escaped_character;

	output_start = write = NULL;
	if (encoder->file == NULL && !(encoder->flags & XJSON_ENCODE_INCREMENTAL)) {
		prefix = (size_t)(p - start);
		remaining = (size_t)(end - p);
		if (XJSON_UNLIKELY(prefix > SIZE_MAX - 2 || remaining > (SIZE_MAX - prefix - 2) / 6)) {
			return luaL_error(encoder->L, "JSON string is too large");
		}
		output_start = write = xjson_encode_reserve(encoder, prefix + remaining * 6 + 2);
		*write++ = '"';
	} else {
		if (!xjson_encode_byte(encoder, '"')) {
			return 0;
		}
	}
	span_start = start;
	while (p < end) {
		span_end = p;
		c = (unsigned char)*p++;
		escaped_character = xjson_encode_escapes[c];
		if (c == '/' && !(encoder->flags & XJSON_ENCODE_ESCAPE_SLASH)) {
			escaped_character = 0;
		}
		if (escaped_character != 0) {
			escaped[0] = '\\';
			if (escaped_character != XJSON_ENCODE_ESCAPE_HEX) {
				escaped[1] = (char)escaped_character;
				escaped_length = 2;
			} else {
				escaped[1] = 'u';
				escaped[2] = '0';
				escaped[3] = '0';
				escaped[4] = xjson_hex_digits[c >> 4];
				escaped[5] = xjson_hex_digits[c & 0x0f];
				escaped_length = 6;
			}
		} else {
			if (c < 0x80 || !(encoder->flags & (XJSON_ENCODE_ESCAPE_UNICODE
					| XJSON_ENCODE_VALIDATE_UTF8))) {
				continue;
			}
			if (c >= 0xc2 && c <= 0xdf) {
				if (XJSON_UNLIKELY(end - p < 1 || ((unsigned char)p[0] & 0xc0) != 0x80)) {
					return luaL_error(encoder->L, "invalid UTF-8 string");
				}
				c1 = (unsigned char)*p++;
				code = ((unsigned)(c & 0x1f) << 6) | (c1 & 0x3f);
			} else if (c >= 0xe0 && c <= 0xef) {
				if (XJSON_UNLIKELY(end - p < 2 || ((unsigned char)p[0] & 0xc0) != 0x80
						|| ((unsigned char)p[1] & 0xc0) != 0x80
						|| (c == 0xe0 && (unsigned char)p[0] < 0xa0)
						|| (c == 0xed && (unsigned char)p[0] >= 0xa0))) {
					return luaL_error(encoder->L, "invalid UTF-8 string");
				}
				c1 = (unsigned char)*p++;
				c2 = (unsigned char)*p++;
				code = ((unsigned)(c & 0x0f) << 12) | ((unsigned)(c1 & 0x3f) << 6)
						| (c2 & 0x3f);
			} else if (c >= 0xf0 && c <= 0xf4) {
				if (XJSON_UNLIKELY(end - p < 3 || ((unsigned char)p[0] & 0xc0) != 0x80
						|| ((unsigned char)p[1] & 0xc0) != 0x80
						|| ((unsigned char)p[2] & 0xc0) != 0x80
						|| (c == 0xf0 && (unsigned char)p[0] < 0x90)
						|| (c == 0xf4 && (unsigned char)p[0] >= 0x90))) {
					return luaL_error(encoder->L, "invalid UTF-8 string");
				}
				c1 = (unsigned char)*p++;
				c2 = (unsigned char)*p++;
				c3 = (unsigned char)*p++;
				code = ((unsigned)(c & 0x07) << 18) | ((unsigned)(c1 & 0x3f) << 12)
						| ((unsigned)(c2 & 0x3f) << 6) | (c3 & 0x3f);
			} else {
				return luaL_error(encoder->L, "invalid UTF-8 string");
			}
			if (!(encoder->flags & XJSON_ENCODE_ESCAPE_UNICODE)) {
				continue;
			}
			escaped[0] = '\\';
			escaped[1] = 'u';
			if (code <= 0xffff) {
				escaped[2] = xjson_hex_digits[(code >> 12) & 0x0f];
				escaped[3] = xjson_hex_digits[(code >> 8) & 0x0f];
				escaped[4] = xjson_hex_digits[(code >> 4) & 0x0f];
				escaped[5] = xjson_hex_digits[code & 0x0f];
				escaped_length = 6;
			} else {
				code -= 0x10000;
				high = 0xd800 + (code >> 10);
				low = 0xdc00 + (code & 0x03ff);
				escaped[2] = xjson_hex_digits[(high >> 12) & 0x0f];
				escaped[3] = xjson_hex_digits[(high >> 8) & 0x0f];
				escaped[4] = xjson_hex_digits[(high >> 4) & 0x0f];
				escaped[5] = xjson_hex_digits[high & 0x0f];
				escaped[6] = '\\';
				escaped[7] = 'u';
				escaped[8] = xjson_hex_digits[(low >> 12) & 0x0f];
				escaped[9] = xjson_hex_digits[(low >> 8) & 0x0f];
				escaped[10] = xjson_hex_digits[(low >> 4) & 0x0f];
				escaped[11] = xjson_hex_digits[low & 0x0f];
				escaped_length = 12;
			}
		}
		size = (size_t)(span_end - span_start);
		if (write != NULL) {
			memcpy(write, span_start, size);
			write += size;
			memcpy(write, escaped, escaped_length);
			write += escaped_length;
		} else {
			if (!xjson_encode_write(encoder, span_start, size)
					|| !xjson_encode_write(encoder, escaped, escaped_length)) {
				return 0;
			}
		}
		span_start = p;
	}
	size = (size_t)(end - span_start);
	if (write != NULL) {
		memcpy(write, span_start, size);
		write += size;
		*write++ = '"';
		encoder->length += (size_t)(write - output_start);
		return 1;
	} else {
		return xjson_encode_write(encoder, span_start, size)
				&& xjson_encode_byte(encoder, '"');
	}
}

static int xjson_encode_number (xjson_encoder_t *encoder, int index) {
	char        number[XJSON_NUMBER_BUFFER_SIZE];
	char       *end, *p;
	lua_Number  value;

	if (lua_isinteger(encoder->L, index)) {
		p = encoder->file == NULL ? xjson_encode_reserve(encoder, XJSON_NUMBER_BUFFER_SIZE)
				: number;
		end = xjson_number_format_integer(p, lua_tointeger(encoder->L, index));
	} else {
		value = lua_tonumber(encoder->L, index);
		if (!isfinite(value)) {
			if (encoder->flags & XJSON_ENCODE_NONFINITE_AS_NULL) {
				return xjson_encode_write(encoder, "null", 4);
			}
			if (!(encoder->flags & XJSON_ENCODE_NONFINITE)) {
				return luaL_error(encoder->L, "cannot encode a non-finite number");
			}
			if (isnan(value)) {
				return xjson_encode_write(encoder, "NaN", 3);
			}
			return signbit(value) ? xjson_encode_write(encoder, "-Infinity", 9)
					: xjson_encode_write(encoder, "Infinity", 8);
		}
		p = encoder->file == NULL ? xjson_encode_reserve(encoder, XJSON_NUMBER_BUFFER_SIZE)
				: number;
		end = xjson_number_format_float(p, value, encoder->precision);
	}
	if (XJSON_UNLIKELY(end == NULL)) {
		return luaL_error(encoder->L, "number formatting error");
	}
	if (encoder->file == NULL) {
		encoder->length += (size_t)(end - p);
		return 1;
	} else {
		return xjson_encode_write(encoder, p, (size_t)(end - p));
	}
}

static int xjson_encode_array (xjson_encoder_t *encoder, int index, size_t length) {
	int     pretty;
	size_t  i;

	pretty = encoder->flags & XJSON_ENCODE_PRETTY;
	if (!xjson_encode_byte(encoder, '[')) {
		return 0;
	}
	if (length == 0) {
		return xjson_encode_byte(encoder, ']');
	}
	if (pretty && !xjson_encode_byte(encoder, '\n')) {
		return 0;
	}
	i = 0;
	while (i < length) {
		i++;
		if (i != 1 && (!xjson_encode_byte(encoder, ',') || (pretty && !xjson_encode_byte(encoder,
				'\n')))) {
			return 0;
		}
		if (pretty && !xjson_encode_indent(encoder, encoder->depth)) {
			return 0;
		}
		if (XJSON_UNLIKELY(lua_rawgeti(encoder->L, index, (lua_Integer)i) == LUA_TNIL)) {
			return luaL_error(encoder->L, "array contains a hole at index %I", (lua_Integer)i);
		}
		if (!xjson_encode_value(encoder, -1)) {
			return 0;
		}
		lua_pop(encoder->L, 1);
	}
	if (pretty && (!xjson_encode_byte(encoder, '\n') || !xjson_encode_indent(encoder,
			encoder->depth - 1))) {
		return 0;
	}
	return xjson_encode_byte(encoder, ']');
}

static inline int xjson_encode_key (xjson_encoder_t *encoder, int index) {
	size_t       length;
	const char  *string;

	string = lua_tolstring(encoder->L, index, &length);
	return xjson_encode_string(encoder, string, length);
}

static int xjson_encode_object (xjson_encoder_t *encoder, int index) {
	int  first, pretty;

	first = 1;
	pretty = encoder->flags & XJSON_ENCODE_PRETTY;
	if (!xjson_encode_byte(encoder, '{')) {
		return 0;
	}
	lua_pushnil(encoder->L);
	while (lua_next(encoder->L, index)) {
		if (lua_type(encoder->L, -2) != LUA_TSTRING) {
			lua_pop(encoder->L, 1);
			continue;
		}
		if (!first) {
			if (!xjson_encode_byte(encoder, ',')) {
				return 0;
			}
		} else {
			first = 0;
		}
		if (pretty && (!xjson_encode_byte(encoder, '\n') || !xjson_encode_indent(encoder,
				encoder->depth))) {
			return 0;
		}
		if (!xjson_encode_key(encoder, -2) || !xjson_encode_byte(encoder, ':') || (pretty &&
				!xjson_encode_byte(encoder, ' ')) || !xjson_encode_value(encoder, -1)) {
			return 0;
		}
		lua_pop(encoder->L, 1);
	}
	if (!first && pretty && (!xjson_encode_byte(encoder, '\n') || !xjson_encode_indent(encoder,
			encoder->depth - 1))) {
		return 0;
	}
	return xjson_encode_byte(encoder, '}');
}

static int xjson_encode_object_properties (xjson_encoder_t *encoder, int index, int properties) {
	int     first, pretty, type;
	size_t  i, length;

	properties = lua_absindex(encoder->L, properties);
	length = lua_rawlen(encoder->L, properties);
	if (XJSON_UNLIKELY((uintmax_t)length > (uintmax_t)LUA_MAXINTEGER)) {
		return luaL_error(encoder->L, "property sequence is too long");
	}
	first = 1;
	pretty = encoder->flags & XJSON_ENCODE_PRETTY;
	if (!xjson_encode_byte(encoder, '{')) {
		return 0;
	}
	i = 0;
	while (i < length) {
		i++;
		type = lua_rawgeti(encoder->L, properties, (lua_Integer)i);
		if (XJSON_UNLIKELY(type != LUA_TSTRING)) {
			return luaL_error(encoder->L, "property name at index %I must be a string",
					(lua_Integer)i);
		}
		lua_pushvalue(encoder->L, -1);
		type = lua_rawget(encoder->L, index);
		if (type == LUA_TNIL) {
			lua_pop(encoder->L, 2);
			continue;
		}
		if (!first) {
			if (!xjson_encode_byte(encoder, ',')) {
				return 0;
			}
		} else {
			first = 0;
		}
		if (pretty && (!xjson_encode_byte(encoder, '\n') || !xjson_encode_indent(encoder,
				encoder->depth))) {
			return 0;
		}
		if (!xjson_encode_key(encoder, -2) || !xjson_encode_byte(encoder, ':') || (pretty &&
				!xjson_encode_byte(encoder, ' ')) || !xjson_encode_value(encoder, -1)) {
			return 0;
		}
		lua_pop(encoder->L, 2);
	}
	if (!first && pretty && (!xjson_encode_byte(encoder, '\n') || !xjson_encode_indent(encoder,
			encoder->depth - 1))) {
		return 0;
	}
	return xjson_encode_byte(encoder, '}');
}

static int xjson_encode_key_compare (const void *a, const void *b) {
	int                        rc;
	size_t                     length;
	const xjson_encode_key_t  *left, *right;

	left = a;
	right = b;
	length = left->length < right->length ? left->length : right->length;
	rc = memcmp(left->string, right->string, length);
	if (rc != 0) {
		return rc;
	}
	return left->length < right->length ? -1 : left->length > right->length;
}

static int xjson_encode_object_canonical (xjson_encoder_t *encoder, int index) {
	int                   first, keys_index, pretty;
	size_t                alloc, count, i;
	xjson_encode_key_t   *data, *key;
	xjson_encode_keys_t  *keys;

	if (!xjson_encode_byte(encoder, '{')) {
		return 0;
	}
	keys = lua_newuserdatauv(encoder->L, sizeof(xjson_encode_keys_t), 0);
	keys->data = NULL;
	luaL_setmetatable(encoder->L, XJSON_ENCODER_KEYS);
	keys_index = lua_gettop(encoder->L);
	lua_toclose(encoder->L, keys_index);
	alloc = 0;
	count = 0;
	lua_pushnil(encoder->L);
	while (lua_next(encoder->L, index)) {
		if (lua_type(encoder->L, -2) != LUA_TSTRING) {
			lua_pop(encoder->L, 1);
			continue;
		}
		if (count == alloc) {
			if (alloc == 0) {
				alloc = XJSON_ENCODE_KEYS_INITIAL;
			} else if (XJSON_UNLIKELY(alloc > SIZE_MAX / sizeof(xjson_encode_key_t) / 2)) {
				return luaL_error(encoder->L, "object has too many properties");
			} else {
				alloc *= 2;
			}
			data = realloc(keys->data, alloc * sizeof(xjson_encode_key_t));
			if (XJSON_UNLIKELY(data == NULL)) {
				return luaL_error(encoder->L, "memory allocation error");
			}
			keys->data = data;
		}
		keys->data[count].string = lua_tolstring(encoder->L, -2, &keys->data[count].length);
		count++;
		lua_pop(encoder->L, 1);
	}
	if (count > 1) {
		qsort(keys->data, count, sizeof(xjson_encode_key_t), xjson_encode_key_compare);
	}
	first = 1;
	pretty = encoder->flags & XJSON_ENCODE_PRETTY;
	for (i = 0; i < count; i++) {
		key = &keys->data[i];
		if (!first) {
			if (!xjson_encode_byte(encoder, ',')) {
				return 0;
			}
		} else {
			first = 0;
		}
		if (pretty && (!xjson_encode_byte(encoder, '\n') || !xjson_encode_indent(encoder,
				encoder->depth))) {
			return 0;
		}
		if (!xjson_encode_string(encoder, key->string, key->length) || !xjson_encode_byte(encoder, ':')
				|| (pretty && !xjson_encode_byte(encoder, ' '))) {
			return 0;
		}
		lua_pushlstring(encoder->L, key->string, key->length);
		lua_rawget(encoder->L, index);
		if (!xjson_encode_value(encoder, -1)) {
			return 0;
		}
		lua_pop(encoder->L, 1);
	}
	lua_settop(encoder->L, keys_index - 1);
	if (!first && pretty && (!xjson_encode_byte(encoder, '\n') || !xjson_encode_indent(encoder,
			encoder->depth - 1))) {
		return 0;
	}
	return xjson_encode_byte(encoder, '}');
}

static int xjson_encode_table (xjson_encoder_t *encoder, int index) {
	int     rc, type;
	size_t  length;

	if (XJSON_UNLIKELY(encoder->depth == XJSON_ENCODE_DEPTH_MAX)) {
		return luaL_error(encoder->L, "JSON document exceeds the nesting limit");
	}
	encoder->depth++;
	index = lua_absindex(encoder->L, index);
	length = lua_rawlen(encoder->L, index);
	if (XJSON_UNLIKELY((uintmax_t)length > (uintmax_t)LUA_MAXINTEGER)) {
		return luaL_error(encoder->L, "array is too long");
	}
	if (length != 0) {
		rc = xjson_encode_array(encoder, index, length);
	} else {
		lua_pushnil(encoder->L);
		if (lua_next(encoder->L, index)) {
			lua_pop(encoder->L, 2);
			type = lua_rawgetp(encoder->L, index, &xjson_properties);
			if (type == LUA_TNIL) {
				lua_pop(encoder->L, 1);
				if (!(encoder->flags & XJSON_ENCODE_CANONICAL)) {
					rc = xjson_encode_object(encoder, index);
				} else {
					rc = xjson_encode_object_canonical(encoder, index);
				}
			} else {
				if (XJSON_UNLIKELY(type != LUA_TTABLE)) {
					return luaL_error(encoder->L, "xjson.properties must be a table");
				}
				rc = xjson_encode_object_properties(encoder, index, -1);
				lua_pop(encoder->L, 1);
			}
		} else {
			rc = xjson_encode_array(encoder, index, 0);
		}
	}
	encoder->depth--;
	return rc;
}

static int xjson_encode_value (xjson_encoder_t *encoder, int index) {
	int          type;
	size_t       length;
	const char  *string;

	type = lua_type(encoder->L, index);
	switch (type) {
	case LUA_TLIGHTUSERDATA:
		if (lua_touserdata(encoder->L, index) == &xjson_null) {
			return xjson_encode_write(encoder, "null", 4);
		}
		break;
	case LUA_TBOOLEAN:
		return lua_toboolean(encoder->L, index) ? xjson_encode_write(encoder, "true", 4)
				: xjson_encode_write(encoder, "false", 5);
	case LUA_TNUMBER:
		return xjson_encode_number(encoder, index);
	case LUA_TSTRING:
		string = lua_tolstring(encoder->L, index, &length);
		return xjson_encode_string(encoder, string, length);
	case LUA_TTABLE:
		return xjson_encode_table(encoder, index);
	}
	return luaL_error(encoder->L, "cannot encode a value of type %s", luaL_typename(encoder->L,
			index));
}

int xjson_open_encode (lua_State *L) {
	luaL_newmetatable(L, XJSON_ENCODER);
	lua_pushcfunction(L, xjson_encode_close);
	lua_setfield(L, -2, "__close");
	lua_pop(L, 1);
	luaL_newmetatable(L, XJSON_ENCODER_KEYS);
	lua_pushcfunction(L, xjson_encode_keys_close);
	lua_setfield(L, -2, "__close");
	lua_pop(L, 1);
	return 0;
}

int xjson_encode (lua_State *L) {
	int                     flag_index, rc, value_index;
#if LUA_VERSION_NUM >= 505
	char                   *output;
#endif
	size_t                  flags_length, i;
	unsigned                encode_flags, indentation, precision;
	const char             *flags;
	luaL_Stream            *stream;
	xjson_encoder_t         encoder;
	xjson_encode_buffer_t  *buffer;

	/* value */
	luaL_checkany(L, 1);
	stream = luaL_testudata(L, 1, LUA_FILEHANDLE);
	if (stream == NULL) {
		value_index = 1;
		flag_index = 2;
	} else {
		luaL_argcheck(L, stream->closef != NULL, 1, "attempt to use a closed file");
		luaL_checkany(L, 2);
		value_index = 2;
		flag_index = 3;
	}

	/* flags */
	encode_flags = 0;
	indentation = 0;
	precision = 0;
	flags = luaL_optlstring(L, flag_index, "", &flags_length);
	for (i = 0; i < flags_length; i++) {
		switch (flags[i]) {
		case 'd':
			if (++i == flags_length || !((flags[i] >= '1' && flags[i] <= '9')
					|| (flags[i] >= 'a' && flags[i] <= 'f'))) {
				return luaL_argerror(L, flag_index, "flag 'd' requires a precision from 1 to f");
			}
			precision = flags[i] <= '9' ? (unsigned)(flags[i] - '0') : (unsigned)(flags[i] - 'a'
					+ 10);
			break;
		case 'n':
			encode_flags |= XJSON_ENCODE_NONFINITE;
			break;
		case 'N':
			encode_flags |= XJSON_ENCODE_NONFINITE_AS_NULL;
			break;
		case 'p':
			encode_flags |= XJSON_ENCODE_PRETTY;
			indentation = 2;
			if (i + 1 < flags_length && flags[i + 1] >= '0' && flags[i + 1] <= '8') {
				indentation = (unsigned)(flags[++i] - '0');
			}
			break;
		case 's':
			encode_flags |= XJSON_ENCODE_ESCAPE_SLASH;
			break;
		case 'u':
			encode_flags |= XJSON_ENCODE_ESCAPE_UNICODE | XJSON_ENCODE_VALIDATE_UTF8;
			break;
		case 'v':
			encode_flags |= XJSON_ENCODE_VALIDATE_UTF8;
			break;
		case 'b':
			encode_flags |= XJSON_ENCODE_BUFFERED;
			break;
		case 'c':
			encode_flags |= XJSON_ENCODE_CANONICAL;
			break;
		case 'i':
			encode_flags |= XJSON_ENCODE_INCREMENTAL;
			break;
		default:
			return luaL_argerror(L, flag_index, lua_pushfstring(L, "invalid flag '%c'", flags[i]));
		}
	}
	if (stream == NULL && (encode_flags & XJSON_ENCODE_BUFFERED)) {
		return luaL_argerror(L, flag_index, "flag 'b' requires a file");
	}

	/* encoder */
	encoder.L = L;
	encoder.buffer = NULL;
	encoder.output = encoder.initial;
	encoder.alloc = sizeof(encoder.initial);
	encoder.length = 0;
	encoder.file = (stream == NULL || (encode_flags & XJSON_ENCODE_BUFFERED)) ? NULL : stream->f;
	encoder.error = 0;
	encoder.depth = 0;
	encoder.flags = encode_flags;
	encoder.indentation = indentation;
	encoder.precision = precision;
	lua_settop(L, XJSON_ENCODE_BUFFER_INDEX - 1);
	if (encoder.file == NULL) {
		if (encode_flags & XJSON_ENCODE_CANONICAL) {
			buffer = lua_newuserdatauv(L, sizeof(xjson_encode_buffer_t), 0);
			buffer->data = NULL;
			luaL_setmetatable(L, XJSON_ENCODER);
			lua_toclose(L, XJSON_ENCODE_BUFFER_INDEX);
			encoder.buffer = buffer;
		} else {
			lua_pushnil(L);  /* defer dynamic buffer */
		}
	}
	luaL_checkstack(L, XJSON_ENCODE_STACK_MAX, "encoding JSON document");

	/* document */
	rc = xjson_encode_value(&encoder, value_index);
	if (rc && (encode_flags & XJSON_ENCODE_PRETTY)) {
		rc = xjson_encode_byte(&encoder, '\n');
	}
	if (rc && stream != NULL && (encode_flags & XJSON_ENCODE_BUFFERED)
			&& fwrite(encoder.output, 1, encoder.length, stream->f) != encoder.length) {
		encoder.error = errno;
		rc = 0;
	}
	if (!rc) {
		lua_settop(L, 0);
		errno = encoder.error;
		return luaL_fileresult(L, 0, NULL);
	}
	if (stream != NULL) {
		lua_settop(L, 1);
		return 1;
	} else {
#if LUA_VERSION_NUM >= 505
		if (encoder.output == encoder.initial) {
			lua_pushlstring(L, encoder.output, encoder.length);
		} else {
			if (XJSON_UNLIKELY((uintmax_t)encoder.length > (uintmax_t)LUA_MAXINTEGER)) {
				return luaL_error(L, "JSON document is too large");
			}
			buffer = encoder.buffer;
			output = realloc(buffer->data, encoder.length + 1);
			if (output != NULL) {
				buffer->data = output;
			}
			buffer->data[encoder.length] = '\0';
			output = buffer->data;
			buffer->data = NULL;
			lua_pushexternalstring(L, output, encoder.length, xjson_encode_release, NULL);
		}
#else
		lua_pushlstring(L, encoder.output, encoder.length);
#endif
		return 1;
	}
}
