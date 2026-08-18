# Lua xjson Documentation

This page describes the values and functions provided by Lua xjson.


## Values

### `xjson.null`

A userdata value representing the JSON `null` value.


### `xjson.properties`

A userdata value used as a table key. The value associated with this key is an ordered list of JSON
object property names, each of which must be a string.

When decoding with the `o` flag, an `xjson.properties` field is added to each table representing a
decoded JSON object. Its value represents the decoded properties in input order, including
repetitions. When encoding a table with an `xjson.properties` field as a JSON object, JSON
properties are encoded in the specified order (also repeatedly), and unlisted or listed-but-absent
table keys are ignored.

> [!TIP]
> The idiom `{ [xjson.properties] = { } }` represents an empty JSON object, distinguished from `{ }`
> (the empty table) which represents an empty JSON array.


## Functions

### `xjson.decode (input [, flags])`

Decodes the input JSON document. The argument can be a Lua string or an open Lua file handle. In the
latter case, the file is read from its current position to the end and remains open.

The function returns the decoded Lua boolean, number, string, table, or `xjson.null`. Both JSON
arrays and objects are decoded as tables. JSON arrays use consecutive integer keys starting at 1;
JSON objects use string keys.

The optional `flags` string can contain the following letters:

* `b` accepts a UTF-8 byte order mark.
* `c` accepts comments.
* `n` accepts non-finite numeric literals (`NaN`, `Infinity`, and `-Infinity`).
* `o` adds an `xjson.properties` field (see above).
* `s` returns JSON numbers as strings, preserving their representation.
* `t` accepts trailing commas in arrays and objects.
* `v` validates strings as UTF-8.
* `i` allocates memory incrementally rather than speculatively (see below).

By default, when decoding the elements of an array, Lua xjson speculatively pre-allocates storage
for Lua tables using the size of the preceding array or object (respectively) within the same
containing array. The `i` flag disables speculative pre-allocation and uses incremental allocation.


### `xjson.encode ([file,] value [, flags])`

Encodes a Lua value as a JSON document. If `file` is omitted, the function returns the document as a
string. Otherwise, `file` must be an open Lua file handle. The function writes the document at the
current position, returns the file handle, and leaves it open. A write error returns `nil`, an error
message, and an error number.

The value must be a boolean, number, string, table, or `xjson.null`; this requirement applies
recursively to every table value selected for encoding.

Empty tables and tables with a positive raw length are encoded as JSON arrays. All other tables are
encoded as JSON objects. For a JSON array, the values at integer keys from 1 through the raw length
are encoded in order; a missing value in that range generates a Lua error, and all other keys are
ignored. For a JSON object without an `xjson.properties` field, only string keys are encoded, and
all other keys are ignored. See `xjson.properties` above for more information on controlling the
selection and order of properties and representing an empty JSON object.

The optional `flags` string can contain the following characters:

* `d1` through `df` write floating-point numbers in fixed-point notation with a precision from 1
  through 15.
* `n` writes infinity and NaN as literals.
* `N` writes infinity and NaN as `null`, overriding `n`.
* `p` indents output with two spaces and appends a newline.
* `p0` through `p8` indent output with the specified number of spaces and append a newline.
* `s` escapes forward slashes.
* `u` escapes non-ASCII Unicode and implies `v`.
* `v` validates strings as UTF-8.
* `b` buffers the complete document before writing it to `file` (see below).
* `c` writes JSON object properties in a canonical order (see below).
* `i` allocates memory incrementally rather than speculatively (see below).

Buffering the document before writing it to a file can improve performance at the price of higher
memory consumption.

Canonical order sorts property names by their raw bytes, independent of locale. The flag has no
effect on tables with the `xjson.properties` field.

By default, when encoding strings to memory, Lua xjson speculatively pre-allocates storage for
strings that require escaping based on their theoretical maximum encoded size. The `i` flag disables
speculative pre-allocation and uses incremental allocation.
