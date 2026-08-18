local candidates = {
	cjson = function ()
		local json = require("cjson")
		json.decode_invalid_numbers(false)
		json.encode_empty_table_as_object(false)
		return {
			decode = function (document) return json.decode(document) end,
			encode = function (value) return json.encode(value) end,
		}
	end,
	dkjson = function ()
		local json = require("dkjson").use_lpeg()
		return {
			decode = function (document) return json.decode(document, 1, json.null, nil) end,
			encode = function (value) return json.encode(value) end,
		}
	end,
	xjson = function ()
		local json = require("xjson")
		return {
			decode = function (document) return json.decode(document) end,
			encode = function (value) return json.encode(value, "s") end,
		}
	end,
}

local name, operation, path = arg[1], arg[2], arg[3]
local repeatCount = math.tointeger(arg[4])
local initialize = assert(candidates[name], "unknown JSON library")
assert(operation == "decode" or operation == "encode", "unknown benchmark operation")
assert(repeatCount ~= nil and repeatCount > 0, "invalid repeat count")
local candidate = initialize()

local file = assert(io.open(path, "rb"))
local document = assert(file:read("*a"))
assert(file:close())

local result
local value
if operation == "encode" then
	value = candidate.decode(document)
	if value == nil then
		io.write("-\n")
		return
	end
end

local cpuStart = os.clock()
if operation == "decode" then
	for _ = 1, repeatCount do
		result = candidate.decode(document)
	end
else
	for _ = 1, repeatCount do
		result = candidate.encode(value)
	end
end
local cpu = os.clock() - cpuStart
if result == nil then
	io.write("-\n")
else
	io.write(string.format("%.3f\n", cpu))
end
