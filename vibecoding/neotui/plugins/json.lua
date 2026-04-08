-- json.lua
-- Minimal no dependency JSON parsing plugin
-- Use json.null instead of nil to get "null" in JSON

local json = {}

json.null = {} -- Special sentinel value (unique table) to represent JSON null

local function escape_str(s)
	s = tostring(s)
	s = s:gsub("\\", "\\\\")
		:gsub('"', '\\"')
		:gsub("\b", "\\b")
		:gsub("\f", "\\f")
		:gsub("\n", "\\n")
		:gsub("\r", "\\r")
		:gsub("\t", "\\t")
	s = s:gsub("[\x00-\x1F]", function(c)
		return string.format("\\u%04x", string.byte(c))
	end)
	return '"' .. s .. '"'
end

local function is_array(t)
	local max = 0
	for k in pairs(t) do
		if type(k) ~= "number" or k < 1 or k ~= math.floor(k) then
			return false
		end
		max = math.max(max, k)
	end
	return max == #t
end

function json.encode(v)
	local t = type(v)

	if v == json.null or t == "nil" then
		return "null"
	elseif t == "boolean" then
		return v and "true" or "false"
	elseif t == "number" then
		if v ~= v or v == math.huge or v == -math.huge then
			return "null"
		end
		return string.format("%.14g", v)
	elseif t == "string" then
		return escape_str(v)
	elseif t == "table" then
		if is_array(v) then
			-- Array
			local parts = {}
			for i = 1, #v do
				parts[i] = json.encode(v[i])
			end
			return "[" .. table.concat(parts, ",") .. "]"
		else
			-- Object
			local parts = {}
			for k, val in pairs(v) do
				local key_str = type(k) == "number" and tostring(k) or tostring(k)
				table.insert(parts, escape_str(key_str) .. ":" .. json.encode(val))
			end
			table.sort(parts)
			return "{" .. table.concat(parts, ",") .. "}"
		end
	else
		error("json.encode: unsupported type " .. t)
	end
end

-- Decoder: turns "null" into json.null (not Lua nil)
function json.decode(str)
	local pos = 1
	local len = #str

	local function skip_whitespace()
		while pos <= len do
			local c = str:sub(pos, pos)
			if not (c == " " or c == "\t" or c == "\n" or c == "\r") then
				break
			end
			pos = pos + 1
		end
	end

	local function parse_value()
		skip_whitespace()
		if pos > len then
			error("Unexpected end of JSON")
		end
		local c = str:sub(pos, pos)

		if c == "{" then
			pos = pos + 1
			local obj = {}
			skip_whitespace()
			if str:sub(pos, pos) == "}" then
				pos = pos + 1
				return obj
			end
			while true do
				skip_whitespace()
				if str:sub(pos, pos) ~= '"' then
					error("Expected string key")
				end
				local key = parse_value()
				skip_whitespace()
				if str:sub(pos, pos) ~= ":" then
					error("Expected ':'")
				end
				pos = pos + 1
				obj[key] = parse_value()
				skip_whitespace()
				if str:sub(pos, pos) == "}" then
					pos = pos + 1
					break
				end
				if str:sub(pos, pos) == "," then
					pos = pos + 1
				else
					error("Expected ',' or '}'")
				end
			end
			return obj
		elseif c == "[" then
			pos = pos + 1
			local arr = {}
			skip_whitespace()
			if str:sub(pos, pos) == "]" then
				pos = pos + 1
				return arr
			end
			while true do
				table.insert(arr, parse_value())
				skip_whitespace()
				if str:sub(pos, pos) == "]" then
					pos = pos + 1
					break
				end
				if str:sub(pos, pos) == "," then
					pos = pos + 1
				else
					error("Expected ',' or ']'")
				end
			end
			return arr
		elseif c == '"' then
			pos = pos + 1
			local start = pos
			local result = ""
			while pos <= len do
				local ch = str:sub(pos, pos)
				if ch == '"' then
					result = result .. str:sub(start, pos - 1)
					pos = pos + 1
					return result
				elseif ch == "\\" then
					result = result .. str:sub(start, pos - 1)
					pos = pos + 1
					local esc = str:sub(pos, pos)
					local map = {
						['"'] = '"',
						["\\"] = "\\",
						["b"] = "\b",
						["f"] = "\f",
						["n"] = "\n",
						["r"] = "\r",
						["t"] = "\t",
					}
					result = result .. (map[esc] or esc)
					pos = pos + 1
					start = pos
				else
					pos = pos + 1
				end
			end
			error("Unterminated string")
		elseif c == "t" and str:sub(pos, pos + 3) == "true" then
			pos = pos + 4
			return true
		elseif c == "f" and str:sub(pos, pos + 4) == "false" then
			pos = pos + 5
			return false
		elseif c == "n" and str:sub(pos, pos + 3) == "null" then
			pos = pos + 4
			return json.null
		elseif c == "-" or (c >= "0" and c <= "9") then
			local start = pos
			while pos <= len and str:find("^[%d%.eE+-]", pos) do
				pos = pos + 1
			end
			return tonumber(str:sub(start, pos - 1)) or error("Invalid number")
		end
		error("Invalid JSON at position " .. pos)
	end

	local result = parse_value()
	skip_whitespace()
	if pos <= len then
		error("Extra data after JSON")
	end
	return result
end

return json
