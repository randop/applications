-- json.lua
-- Minimal but complete JSON encode/decode in pure Lua 5 (no dependencies)

local json = {}

-- Helper: escape string for JSON
local function escape_str(s)
	s = tostring(s)
	s = s:gsub("\\", "\\\\")
		:gsub('"', '\\"')
		:gsub("\b", "\\b")
		:gsub("\f", "\\f")
		:gsub("\n", "\\n")
		:gsub("\r", "\\r")
		:gsub("\t", "\\t")
	-- Escape control characters
	s = s:gsub("[\x00-\x1F]", function(c)
		return string.format("\\u%04x", string.byte(c))
	end)
	return '"' .. s .. '"'
end

-- Encode any Lua value to JSON string
function json.encode(v)
	local t = type(v)

	if t == "nil" then
		return "null"
	elseif t == "boolean" then
		return v and "true" or "false"
	elseif t == "number" then
		if v ~= v then
			return "null"
		end -- NaN
		if v == math.huge or v == -math.huge then
			return "null"
		end
		return string.format("%.14g", v) -- clean number
	elseif t == "string" then
		return escape_str(v)
	elseif t == "table" then
		-- Check if it's an array
		local is_array = true
		local max_idx = 0
		for k in pairs(v) do
			if type(k) ~= "number" or k < 1 or k ~= math.floor(k) then
				is_array = false
				break
			end
			if k > max_idx then
				max_idx = k
			end
		end

		if is_array and max_idx == #v then
			-- Encode as JSON array
			local parts = {}
			for i = 1, #v do
				table.insert(parts, json.encode(v[i]))
			end
			return "[" .. table.concat(parts, ",") .. "]"
		else
			-- Encode as JSON object
			local parts = {}
			for k, val in pairs(v) do
				table.insert(parts, escape_str(k) .. ":" .. json.encode(val))
			end
			return "{" .. table.concat(parts, ",") .. "}"
		end
	else
		error("json.encode: unsupported type " .. t)
	end
end

-- Decode JSON string to Lua value
function json.decode(str)
	local pos = 1
	local len = #str

	local function skip_whitespace()
		while pos <= len do
			local c = str:sub(pos, pos)
			if c ~= " " and c ~= "\t" and c ~= "\n" and c ~= "\r" then
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
			return parse_object()
		elseif c == "[" then
			return parse_array()
		elseif c == '"' then
			return parse_string()
		elseif c == "t" and str:sub(pos, pos + 3) == "true" then
			pos = pos + 4
			return true
		elseif c == "f" and str:sub(pos, pos + 4) == "false" then
			pos = pos + 5
			return false
		elseif c == "n" and str:sub(pos, pos + 3) == "null" then
			pos = pos + 4
			return nil
		elseif c == "-" or (c >= "0" and c <= "9") then
			return parse_number()
		else
			error("Invalid JSON character at position " .. pos .. ": " .. c)
		end
	end

	function parse_object()
		pos = pos + 1 -- skip '{'
		local obj = {}
		skip_whitespace()

		if str:sub(pos, pos) == "}" then
			pos = pos + 1
			return obj
		end

		while true do
			skip_whitespace()
			if str:sub(pos, pos) ~= '"' then
				error("Expected string key at " .. pos)
			end
			local key = parse_string()

			skip_whitespace()
			if str:sub(pos, pos) ~= ":" then
				error("Expected ':' after key at " .. pos)
			end
			pos = pos + 1 -- skip ':'

			obj[key] = parse_value()

			skip_whitespace()
			if str:sub(pos, pos) == "}" then
				pos = pos + 1
				break
			elseif str:sub(pos, pos) == "," then
				pos = pos + 1
			else
				error("Expected ',' or '}' at " .. pos)
			end
		end
		return obj
	end

	function parse_array()
		pos = pos + 1 -- skip '['
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
			elseif str:sub(pos, pos) == "," then
				pos = pos + 1
			else
				error("Expected ',' or ']' at " .. pos)
			end
		end
		return arr
	end

	function parse_string()
		pos = pos + 1 -- skip opening "
		local start = pos
		local result = ""

		while pos <= len do
			local c = str:sub(pos, pos)
			if c == '"' then
				result = result .. str:sub(start, pos - 1)
				pos = pos + 1
				return result
			elseif c == "\\" then
				result = result .. str:sub(start, pos - 1)
				pos = pos + 1
				if pos > len then
					error("Unterminated escape")
				end
				local esc = str:sub(pos, pos)
				if esc == '"' or esc == "\\" or esc == "/" then
					result = result .. esc
				elseif esc == "b" then
					result = result .. "\b"
				elseif esc == "f" then
					result = result .. "\f"
				elseif esc == "n" then
					result = result .. "\n"
				elseif esc == "r" then
					result = result .. "\r"
				elseif esc == "t" then
					result = result .. "\t"
				elseif esc == "u" then
					-- Simple \uXXXX support (basic)
					pos = pos + 1
					local hex = str:sub(pos, pos + 3)
					if #hex == 4 then
						local code = tonumber(hex, 16)
						if code then
							result = result .. string.char(code)
						end
					end
					pos = pos + 3
				else
					result = result .. esc
				end
				pos = pos + 1
				start = pos
			else
				pos = pos + 1
			end
		end
		error("Unterminated string")
	end

	function parse_number()
		local start = pos
		while pos <= len do
			local c = str:sub(pos, pos)
			if not (c == "-" or c == "." or c == "e" or c == "E" or (c >= "0" and c <= "9")) then
				break
			end
			pos = pos + 1
		end
		local num_str = str:sub(start, pos - 1)
		local num = tonumber(num_str)
		if not num then
			error("Invalid number: " .. num_str)
		end
		return num
	end

	local result = parse_value()
	skip_whitespace()
	if pos <= len then
		error("Extra data after JSON value at position " .. pos)
	end
	return result
end

-- For easy use in scripts:
-- json = (loadfile("barebones_json.lua"))()   or just paste the code

return json
