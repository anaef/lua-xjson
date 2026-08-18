local xjson = require("xjson")

-- Helpers
local function expectError (func)
	local ok = pcall(func)
	assert(not ok)
end
local function nestedValue (depth)
	local value = true
	for _ = 1, depth do
		value = { value }
	end
	return value
end

-- Interface
local interface = {
	decode = "function",
	encode = "function",
	null = "userdata",
	properties = "userdata",
}
for key, value in pairs(xjson) do
	assert(type(value) == interface[key])
end
for key, valueType in pairs(interface) do
	assert(type(xjson[key]) == valueType)
end
assert(xjson.null ~= xjson.properties)

-- Scalar values
assert(xjson.encode(xjson.null) == "null")
assert(xjson.decode("null") == xjson.null)
assert(xjson.decode("true") == true)
assert(xjson.decode("false") == false)
assert(xjson.decode(" \n\ttrue\r") == true)
assert(#xjson.decode("[]") == 0)
assert(xjson.decode("42") == 42)
assert(math.type(xjson.decode("42")) == "integer")
assert(xjson.decode(tostring(math.maxinteger)) == math.maxinteger)
assert(xjson.decode(tostring(math.mininteger)) == math.mininteger)
assert(xjson.decode("-1.25e2") == -125)
assert(xjson.encode(math.maxinteger) == tostring(math.maxinteger))
assert(xjson.encode(math.mininteger) == tostring(math.mininteger))
assert(xjson.encode(1.25) == "1.25")
assert(xjson.decode('"a\\u0000b"') == "a\0b")
assert(xjson.decode('"\\b\\f\\n\\r\\t"') == "\b\f\n\r\t")
assert(xjson.decode('"\\\""') == '"')
assert(xjson.decode('"\\\\"') == "\\")
assert(xjson.decode('"\\/"') == "/")
assert(xjson.decode('"\\uD83D\\uDE00"') == "\240\159\152\128")
assert(xjson.encode("\b\f\n\r\t\"\\") == '"\\b\\f\\n\\r\\t\\"\\\\"')
assert(xjson.encode("\0\31"):lower() == '"\\u0000\\u001f"')
assert(xjson.encode("/") == '"/"')

-- Large strings
local largeString = string.rep("0123456789", 10000)
assert(xjson.decode(xjson.encode(largeString)) == largeString)
local largeEscapedString = string.rep("\n", 10000)
local largeValue = { largeEscapedString, largeString }
local largeDocument = '["' .. string.rep("\\n", 10000) .. '","' .. largeString .. '"]'

-- Numbers
assert(math.type(xjson.decode("0")) == "integer")
assert(math.type(xjson.decode("-0")) == "integer")
assert(math.type(xjson.decode("0.0")) == "float")
assert(math.type(xjson.decode("0e0")) == "float")
local negativeZero = xjson.decode("-0.0")
assert(negativeZero == 0)
assert(1 / negativeZero == -math.huge)
if math.maxinteger == 0x7fffffffffffffff then
	local positiveOverflow = xjson.decode("9223372036854775808")
	local negativeOverflow = xjson.decode("-9223372036854775809")
	local unsignedMaximum = xjson.decode("18446744073709551615")
	assert(math.type(positiveOverflow) == "float")
	assert(math.type(negativeOverflow) == "float")
	assert(math.type(unsignedMaximum) == "float")
	assert(positiveOverflow == 0x1p63)
	assert(negativeOverflow == -0x1p63)
	assert(unsignedMaximum == 0x1p64)
end
if string.packsize("n") == 8 then
	assert(xjson.decode("9007199254740993.0") == 9007199254740992.0)
	assert(xjson.decode("1.7976931348623157e308") == 0x1.fffffffffffffp1023)
	assert(xjson.decode("2.2250738585072014e-308") == 0x1p-1022)
	assert(xjson.decode("4.9406564584124654e-324") == 0x1p-1074)
	assert(xjson.decode("2.4703282292062327e-324") == 0)
	assert(xjson.decode("2.4703282292062328e-324") == 0x1p-1074)
	assert(xjson.decode(
			"1.00000000000000011102230246251565404236316680908203125") == 1.0)
	assert(xjson.decode(
			"1.00000000000000011102230246251565404236316680908203126")
			== 0x1.0000000000001p0)
else
	assert(xjson.decode("3.4028234663852886e38") == 0x1.fffffep127)
	assert(xjson.decode("1.1754943508222875e-38") == 0x1p-126)
	assert(xjson.decode("1.401298464324817e-45") == 0x1p-149)
end
assert(xjson.decode("1e999") == math.huge)
assert(xjson.decode("-1e999") == -math.huge)
assert(xjson.decode("1e-999") == 0)
assert(1 / xjson.decode("-1e-999") == -math.huge)
assert(xjson.decode("1" .. string.rep("0", 1000)) == math.huge)
assert(xjson.decode("1e" .. string.rep("9", 100)) == math.huge)
assert(xjson.decode("1e-" .. string.rep("9", 100)) == 0)
local invalidNumbers = {
	"-",
	"+1",
	".1",
	"00",
	"-01",
	"0.",
	"1.",
	"0e",
	"0e+",
	"0e-",
	"1e+",
	"1e-",
	"1e--1",
}
for _, number in ipairs(invalidNumbers) do
	expectError(function () xjson.decode(number) end)
	expectError(function () xjson.decode(number, "s") end)
end
expectError(function () xjson.encode(nil) end)
expectError(function () xjson.encode(function () end) end)
expectError(function () xjson.encode(coroutine.create(function () end)) end)
expectError(function () xjson.encode(xjson.properties) end)
expectError(function () xjson.decode(nil) end)
expectError(function () xjson.decode(true) end)

-- Strict decoding
local invalidDocuments = {
	"",
	" ",
	"[",
	"{",
	"[1,]",
	'{"a":1,}',
	"[1 2]",
	'{"a" 1}',
	'{"a":}',
	"tru",
	"01",
	"1.",
	"1e",
	"true false",
	'"\\x41"',
	'"\\uD800"',
	'"\\uDC00"',
	'"line\nbreak"',
}
for _, document in ipairs(invalidDocuments) do
	expectError(function () xjson.decode(document) end)
end
expectError(function () xjson.decode("\239\187\191true") end)
expectError(function () xjson.decode("/* comment */ true") end)
expectError(function () xjson.decode("Infinity") end)
expectError(function () xjson.decode("NaN") end)

-- Decode flags
assert(xjson.decode("\239\187\191true", "b") == true)
local commented = xjson.decode("/* first */ [1, // second\n2]", "c")
assert(commented[1] == 1)
assert(commented[2] == 2)
assert(xjson.decode("Infinity", "n") == math.huge)
assert(xjson.decode("-Infinity", "n") == -math.huge)
assert(xjson.decode("NaN", "n") ~= xjson.decode("NaN", "n"))
local rawNumbers = xjson.decode("[-0,1.20,1e+3,-4E-2]", "s")
assert(rawNumbers[1] == "-0")
assert(rawNumbers[2] == "1.20")
assert(rawNumbers[3] == "1e+3")
assert(rawNumbers[4] == "-4E-2")
local rawNonFinite = xjson.decode("[NaN,Infinity,-Infinity]", "ns")
assert(rawNonFinite[1] == "NaN")
assert(rawNonFinite[2] == "Infinity")
assert(rawNonFinite[3] == "-Infinity")
local trailing = xjson.decode('[1,2,]', "t")
assert(trailing[1] == 1)
assert(trailing[2] == 2)
assert(xjson.decode('{"value":true,}', "t").value == true)
assert(xjson.decode('"\195\164"', "v") == "\195\164")
assert(xjson.decode('"\255"') == "\255")
expectError(function () xjson.decode('"\255"', "v") end)
local containers = '[["clean","escaped\\n"],["next"],'
		.. '{"clean":"value","escaped":"line\\n"},{"next":"value"}]'
for _, flags in ipairs({ "", "i" }) do
	local value = xjson.decode(containers, flags)
	assert(#value == 4)
	assert(value[1][1] == "clean")
	assert(value[1][2] == "escaped\n")
	assert(value[2][1] == "next")
	assert(value[3].clean == "value")
	assert(value[3].escaped == "line\n")
	assert(value[4].next == "value")
end
local allDecodeFlags = xjson.decode(
		"\239\187\191/* comment */ {\"number\":Infinity,}", "bcnostvi")
assert(allDecodeFlags.number == "Infinity")
assert(allDecodeFlags[xjson.properties][1] == "number")
expectError(function () xjson.decode("true", "z") end)
expectError(function () xjson.decode("{}", "O") end)
expectError(function () xjson.decode("true", true) end)

-- Object properties
local emptyObject = {
	[xjson.properties] = {}
}
assert(xjson.encode(emptyObject) == "{}")
local emptyProjection = {
	ignored = true,
	[xjson.properties] = {}
}
assert(xjson.encode(emptyProjection) == "{}")
local projected = {
	first = 1,
	nullValue = xjson.null,
	second = 2,
	third = 3,
	[xjson.properties] = { "third", "missing", "nullValue", "first", "third" }
}
assert(xjson.encode(projected) ==
		'{"third":3,"nullValue":null,"first":1,"third":3}')
local decodedObject = xjson.decode('{"first":1,"second":2,"first":3}', "o")
assert(decodedObject.first == 3)
assert(decodedObject.second == 2)
assert(#decodedObject[xjson.properties] == 3)
assert(decodedObject[xjson.properties][1] == "first")
assert(decodedObject[xjson.properties][2] == "second")
assert(decodedObject[xjson.properties][3] == "first")
assert(xjson.encode(decodedObject) == '{"first":3,"second":2,"first":3}')
local decodedEmptyObject = xjson.decode("{}", "o")
assert(type(decodedEmptyObject[xjson.properties]) == "table")
assert(#decodedEmptyObject[xjson.properties] == 0)
assert(xjson.encode(decodedEmptyObject) == "{}")
assert(xjson.encode(xjson.decode("{}")) == "[]")
local nestedObject = xjson.decode('{"items":[{"enabled":true}]}', "o")
assert(nestedObject[xjson.properties][1] == "items")
assert(nestedObject.items[xjson.properties] == nil)
assert(nestedObject.items[1][xjson.properties][1] == "enabled")
local inherited = setmetatable({
	[xjson.properties] = { "name" }
}, {
	__index = { name = "inherited" }
})
assert(xjson.encode(inherited) == "{}")
expectError(function ()
	xjson.encode({ [xjson.properties] = true })
end)
expectError(function ()
	xjson.encode({ [xjson.properties] = { true } })
end)
expectError(function ()
	xjson.encode({ [xjson.properties] = { 1 } })
end)
expectError(function ()
	xjson.encode({ [xjson.properties] = { xjson.null } })
end)

-- Lua tables
assert(xjson.encode({}) == "[]")
assert(xjson.encode({ true, false, xjson.null, "four" }) ==
		'[true,false,null,"four"]')
local arrayWithProperties = {
	1,
	2,
	[xjson.properties] = { "ignored" }
}
assert(xjson.encode(arrayWithProperties) == "[1,2]")
local sequence = { 1, 2, 3 }
sequence.name = function () end
sequence[0] = xjson.properties
sequence[-1] = coroutine.create(function () end)
sequence[1.5] = {}
sequence[false] = true
assert(xjson.encode(sequence) == "[1,2,3]")
local hole = { 1, 2, 3 }
assert(rawlen(hole) == 3)
hole[2] = nil
expectError(function () xjson.encode(hole) end)
local ordinaryObject = xjson.decode(xjson.encode({ answer = 42, enabled = true }))
assert(ordinaryObject.answer == 42)
assert(ordinaryObject.enabled == true)
local ignoredObjectKeys = {
	name = "Lua",
	[-1] = "negative",
	[0] = "zero",
	[1.5] = "fraction",
	[{}] = true,
	[true] = true,
	[xjson.null] = true
}
assert(xjson.encode(ignoredObjectKeys) == '{"name":"Lua"}')
assert(xjson.encode({ [false] = true }) == "{}")
local rawObject = setmetatable({ name = "Lua" }, {
	__len = function () return 1 end,
	__pairs = function () error("pairs metamethod called") end
})
assert(xjson.encode(rawObject) == '{"name":"Lua"}')
expectError(function () xjson.encode({ value = function () end }) end)

-- Encode flags
assert(xjson.encode(1.2345, "d2") == "1.23")
for precision = 1, 15 do
	local flag = "d" .. string.format("%x", precision)
	local encoded = xjson.encode(1.234567890123456, flag)
	assert(tonumber(encoded) ~= nil)
	assert(not encoded:find("[eE]"))
end
assert(xjson.encode(math.huge, "n") == "Infinity")
assert(xjson.encode(-math.huge, "n") == "-Infinity")
assert(xjson.encode(0 / 0, "n") == "NaN")
assert(xjson.encode(math.huge, "N") == "null")
assert(xjson.encode(-math.huge, "N") == "null")
assert(xjson.encode(0 / 0, "N") == "null")
assert(xjson.encode(math.huge, "nN") == "null")
assert(xjson.encode(math.huge, "Nn") == "null")
expectError(function () xjson.encode(math.huge) end)
expectError(function () xjson.encode(-math.huge) end)
expectError(function () xjson.encode(0 / 0) end)
for width = 0, 8 do
	local indentation = string.rep(" ", width)
	local expected = "[\n" .. indentation .. "true\n]\n"
	assert(xjson.encode({ true }, "p" .. width) == expected)
end
assert(xjson.encode({ true }, "p") == xjson.encode({ true }, "p2"))
assert(xjson.encode(true, "p") == "true\n")
assert(xjson.encode({}, "p") == "[]\n")
assert(xjson.encode(emptyObject, "p") == "{}\n")
local prettyObject = {
	value = true,
	[xjson.properties] = { "value" }
}
assert(xjson.encode(prettyObject, "p0") == "{\n\"value\": true\n}\n")
assert(xjson.encode({ { true } }, "p") == "[\n  [\n    true\n  ]\n]\n")
assert(xjson.encode("</script>", "s") == '"<\\/script>"')
assert(xjson.encode("\195\164", "u"):lower() == '"\\u00e4"')
assert(xjson.encode("\240\159\152\128", "u"):lower() ==
		'"\\ud83d\\ude00"')
assert(xjson.encode("\195\164", "v") == '"\195\164"')
assert(xjson.encode("\255") == '"\255"')
expectError(function () xjson.encode("\255", "v") end)
expectError(function () xjson.encode("\255", "u") end)
expectError(function () xjson.encode(true, "z") end)
expectError(function () xjson.encode(true, "b") end)
expectError(function () xjson.encode(1.25, "d") end)
expectError(function () xjson.encode(1.25, "d0") end)
expectError(function () xjson.encode(1.25, "dg") end)
expectError(function () xjson.encode(true, "p9") end)
expectError(function () xjson.encode(true, "P") end)
expectError(function () xjson.encode(true, true) end)
local canonicalObject = {}
canonicalObject.b = 3
canonicalObject["\195\164"] = 4
canonicalObject.aa = 2
canonicalObject.a = 1
canonicalObject["a\0"] = 5
canonicalObject[""] = 0
local canonicalDocument = '{"":0,"a":1,"a\\u0000":5,"aa":2,"b":3,"\195\164":4}'
assert(xjson.encode(canonicalObject, "c") == canonicalDocument)
local canonicalNested = {
	z = { b = 2, a = 1 },
	a = { d = 4, c = 3 }
}
assert(xjson.encode(canonicalNested, "c") ==
		'{"a":{"c":3,"d":4},"z":{"a":1,"b":2}}')
assert(xjson.encode({ b = 2, a = 1, [-1] = "ignored" }, "c") == '{"a":1,"b":2}')
local canonicalMany = {}
for i = 10, 1, -1 do
	canonicalMany[string.char(96 + i)] = i
end
assert(xjson.encode(canonicalMany, "c") ==
		'{"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9,"j":10}')
local canonicalProperties = {
	a = 1,
	b = 2,
	[xjson.properties] = { "b", "a" }
}
assert(xjson.encode(canonicalProperties, "c") == '{"b":2,"a":1}')
assert(xjson.encode({ b = 2, a = 1 }, "cp0") == "{\n\"a\": 1,\n\"b\": 2\n}\n")
local canonicalLargeString = string.rep("x", 100000)
assert(xjson.encode({ b = true, a = canonicalLargeString }, "c") ==
		'{"a":"' .. canonicalLargeString .. '","b":true}')
expectError(function () xjson.encode({ value = function () end }, "c") end)
expectError(function () xjson.encode(true, "C") end)
assert(xjson.encode(largeValue) == largeDocument)
assert(xjson.encode(largeValue, "i") == largeDocument)

-- Recursion and nesting
local recursive = {}
recursive.self = recursive
expectError(function () xjson.encode(recursive) end)
local indirectA = {}
local indirectB = {}
indirectA.other = indirectB
indirectB.other = indirectA
expectError(function () xjson.encode(indirectA) end)
local recursiveArray = {}
recursiveArray[1] = recursiveArray
expectError(function () xjson.encode(recursiveArray) end)
local shared = { value = 1 }
local sharedDecoded = xjson.decode(xjson.encode({ shared, shared }))
assert(sharedDecoded[1].value == 1)
assert(sharedDecoded[2].value == 1)
assert(sharedDecoded[1] ~= sharedDecoded[2])
assert(type(xjson.decode(xjson.encode(nestedValue(128)))) == "table")
expectError(function () xjson.encode(nestedValue(129)) end)
local nestedDocument = string.rep("[", 128) .. "true" .. string.rep("]", 128)
assert(type(xjson.decode(nestedDocument)) == "table")
nestedDocument = "[" .. nestedDocument .. "]"
expectError(function () xjson.decode(nestedDocument) end)

-- File handles
local input = assert(io.tmpfile())
assert(input:write("prefix", "\239\187\191/* comment */ {\"value\":Infinity,}"))
assert(input:seek("set", 6))
local fileValue = xjson.decode(input, "bcnostv")
assert(fileValue.value == "Infinity")
assert(fileValue[xjson.properties][1] == "value")
assert(io.type(input) == "file")
input:close()
local incrementalInput = assert(io.tmpfile())
assert(incrementalInput:write(containers))
assert(incrementalInput:seek("set", 0))
local incrementalValue = xjson.decode(incrementalInput, "i")
assert(incrementalValue[1][2] == "escaped\n")
assert(incrementalValue[3].escaped == "line\n")
incrementalInput:close()
local output = assert(io.tmpfile())
assert(output:write("prefix"))
local outputValue = {
	path = "/",
	[xjson.properties] = { "path" }
}
assert(xjson.encode(output, outputValue, "p0s") == output)
assert(output:seek("set", 0))
assert(output:read("*a") == "prefix{\n\"path\": \"\\/\"\n}\n")
assert(io.type(output) == "file")
output:close()
local canonicalOutput = assert(io.tmpfile())
assert(xjson.encode(canonicalOutput, canonicalObject, "c") == canonicalOutput)
assert(canonicalOutput:seek("set", 0))
assert(canonicalOutput:read("*a") == canonicalDocument)
canonicalOutput:close()
local bufferedOutput = assert(io.tmpfile())
assert(bufferedOutput:write("prefix"))
assert(xjson.encode(bufferedOutput, outputValue, "p0sb") == bufferedOutput)
assert(bufferedOutput:seek("set", 0))
assert(bufferedOutput:read("*a") == "prefix{\n\"path\": \"\\/\"\n}\n")
assert(io.type(bufferedOutput) == "file")
bufferedOutput:close()
local largeOutput = assert(io.tmpfile())
assert(xjson.encode(largeOutput, largeString) == largeOutput)
assert(largeOutput:seek("set", 0))
assert(largeOutput:read("*a") == '"' .. largeString .. '"')
largeOutput:close()
local largeBufferedOutput = assert(io.tmpfile())
assert(xjson.encode(largeBufferedOutput, largeValue, "b") == largeBufferedOutput)
assert(largeBufferedOutput:seek("set", 0))
assert(largeBufferedOutput:read("*a") == largeDocument)
largeBufferedOutput:close()
local largeIncrementalOutput = assert(io.tmpfile())
assert(xjson.encode(largeIncrementalOutput, largeValue, "bi") == largeIncrementalOutput)
assert(largeIncrementalOutput:seek("set", 0))
assert(largeIncrementalOutput:read("*a") == largeDocument)
largeIncrementalOutput:close()
local invalidInput = assert(io.tmpfile())
assert(invalidInput:write("{"))
assert(invalidInput:seek("set", 0))
expectError(function () xjson.decode(invalidInput) end)
assert(io.type(invalidInput) == "file")
invalidInput:close()
local outputName = os.tmpname()
local writable = assert(io.open(outputName, "w"))
assert(writable:write("unchanged"))
writable:close()
local readOnly = assert(io.open(outputName, "r"))
local result, message, number = xjson.encode(readOnly, true)
assert(result == nil)
assert(type(message) == "string")
assert(type(number) == "number")
readOnly:close()
local bufferedReadOnly = assert(io.open(outputName, "r"))
result, message, number = xjson.encode(bufferedReadOnly, largeString, "b")
assert(result == nil)
assert(type(message) == "string")
assert(type(number) == "number")
bufferedReadOnly:close()
os.remove(outputName)
local closed = assert(io.tmpfile())
closed:close()
expectError(function () xjson.decode(closed) end)
expectError(function () xjson.encode(closed, true) end)

-- Arguments
expectError(function () xjson.decode() end)
expectError(function () xjson.encode() end)
local missingValueFile = assert(io.tmpfile())
expectError(function () xjson.encode(missingValueFile) end)
missingValueFile:close()
