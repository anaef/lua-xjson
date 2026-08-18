# Lua xjson

## Introduction

Lua xjson provides JSON encoding and decoding for Lua. It was created to combine performance with a
broad JSON processing interface. Documents are converted directly between their textual
representation and Lua values; input can be a string or a Lua file, and encoded output can be
returned as a string or written directly to a Lua file.

Flags select encoding and decoding behavior independently for each request. They control accepted
input extensions, numeric representations, UTF-8 validation, property tracking and ordering, output
formatting, escaping, buffering, and memory allocation.

Lua xjson provides explicit representations for JSON null and objects, preserving the distinction
between an empty JSON object and an empty JSON array. The input order of object properties can be
retained when decoding. When encoding, properties can be selected and ordered explicitly or sorted
by raw bytes into a canonical property order for deterministic output.

Here is a quick example:

```lua
local xjson = require("xjson")

local value = xjson.decode('{"name":"Lua","versions":[5.4,5.5]}')
print(value.name)         -- Lua
print(value.versions[2])  -- 5.5

local request = {
	model = "gpt-5",
	tools = {
		{ type = "web_search" }
	},
	[xjson.properties] = { "model", "tools" }
}

print(xjson.encode(request))
-- {"model":"gpt-5","tools":[{"type":"web_search"}]}

xjson.encode(io.stdout, request, "p")
--[[
{
  "model": "gpt-5",
  "tools": [
    {
      "type": "web_search"
    }
  ]
}
]]
```


## Release Notes

Please see the [release notes](NEWS.md) document.


## Documentation

Please see the [documentation](doc/) folder.


## Benchmarks

Please see the [benchmarks](benchmarks/) folder.

## Credits

Number conversion is adapted from [yyjson](https://github.com/ibireme/yyjson) by Yao Yuan. The
distinction between JSON arrays and objects is adapted from David Kolf's
[array detection proposal](https://dkolf.de/dkjson-lua/roadmap-3.0#array-detection) for
[dkjson](https://dkolf.de/dkjson-lua/).


## Limitations

Lua xjson is in beta.

Lua xjson supports Lua 5.4 and Lua 5.5.

Lua xjson has been built and tested on Ubuntu Linux (64-bit).

JSON documents are limited to 128 nested arrays and objects.


## License

Lua xjson is released under the MIT license. See LICENSE for license terms.
