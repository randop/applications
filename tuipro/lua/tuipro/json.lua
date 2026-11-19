-- Minimal, dependency-free JSON encoder/decoder.
--
-- This exists so response:json() works without cjson being installed.  It is
-- intentionally small but strict: encoding rejects cycles, non-finite numbers
-- and non-string keys, while decoding rejects trailing garbage and malformed
-- escapes.
--
-- JSON null is represented by json.null.  Object members whose value is null
-- are stored as nil (so the key is absent); array elements keep the sentinel
-- so indices and lengths stay stable.
local M = {}

M.null = setmetatable({}, { __tostring = function() return "null" end })

local escapes = {
  ['"'] = '\\"',
  ["\\"] = "\\\\",
  ["\b"] = "\\b",
  ["\f"] = "\\f",
  ["\n"] = "\\n",
  ["\r"] = "\\r",
  ["\t"] = "\\t",
}

local function quote(s)
  return '"' .. s:gsub('[%z\1-\31\\"]', function(c)
    local e = escapes[c]
    if e then
      return e
    end
    return string.format("\\u%04x", string.byte(c))
  end) .. '"'
end

local function is_array(t)
  local n = #t
  local count = 0
  for k in pairs(t) do
    if type(k) ~= "number" or k < 1 or k > n or k % 1 ~= 0 then
      return false
    end
    count = count + 1
  end
  return count == n
end

local function encode_number(v)
  if v ~= v or v == math.huge or v == -math.huge then
    error("cannot encode non-finite number", 0)
  end
  if v == math.floor(v) and math.abs(v) < 1e15 then
    return string.format("%.0f", v)
  end
  return string.format("%.14g", v)
end

local function encode_value(v, out, seen, depth)
  local t = type(v)
  if v == M.null then
    out[#out + 1] = "null"
  elseif t == "nil" then
    out[#out + 1] = "null"
  elseif t == "boolean" then
    out[#out + 1] = v and "true" or "false"
  elseif t == "number" then
    out[#out + 1] = encode_number(v)
  elseif t == "string" then
    out[#out + 1] = quote(v)
  elseif t == "table" then
    if seen[v] then
      error("cannot encode circular table", 0)
    end
    if depth > 200 then
      error("cannot encode deeply nested table", 0)
    end
    seen[v] = true
    if is_array(v) then
      out[#out + 1] = "["
      for i = 1, #v do
        if i > 1 then
          out[#out + 1] = ","
        end
        encode_value(v[i], out, seen, depth + 1)
      end
      out[#out + 1] = "]"
    else
      local keys = {}
      for k in pairs(v) do
        if type(k) ~= "string" then
          error("JSON object keys must be strings, got " .. type(k), 0)
        end
        keys[#keys + 1] = k
      end
      table.sort(keys)
      out[#out + 1] = "{"
      for i = 1, #keys do
        if i > 1 then
          out[#out + 1] = ","
        end
        local value = v[keys[i]]
        if value ~= nil then
          out[#out + 1] = quote(keys[i])
          out[#out + 1] = ":"
          encode_value(value, out, seen, depth + 1)
        end
      end
      out[#out + 1] = "}"
    end
    seen[v] = nil
  else
    error("cannot encode " .. t, 0)
  end
end

function M.encode(value)
  local out = {}
  encode_value(value, out, {}, 0)
  return table.concat(out)
end

-- Decoder -------------------------------------------------------------------

local function utf8_encode(cp)
  if cp < 0x80 then
    return string.char(cp)
  elseif cp < 0x800 then
    return string.char(0xC0 + math.floor(cp / 0x40),
                       0x80 + cp % 0x40)
  elseif cp < 0x10000 then
    return string.char(0xE0 + math.floor(cp / 0x1000),
                       0x80 + math.floor(cp / 0x40) % 0x40,
                       0x80 + cp % 0x40)
  elseif cp <= 0x10FFFF then
    return string.char(0xF0 + math.floor(cp / 0x40000),
                       0x80 + math.floor(cp / 0x1000) % 0x40,
                       0x80 + math.floor(cp / 0x40) % 0x40,
                       0x80 + cp % 0x40)
  end
  error("invalid Unicode code point", 0)
end

local function decode_error(pos, msg)
  error(string.format("JSON error at byte %d: %s", pos, msg), 0)
end

local decode_value

local function skip_ws(s, pos)
  while true do
    local c = s:sub(pos, pos)
    if c == " " or c == "\t" or c == "\n" or c == "\r" then
      pos = pos + 1
    else
      return pos
    end
  end
end

local function decode_string(s, pos)
  -- s:sub(pos,pos) == '"'
  pos = pos + 1
  local out = {}
  while true do
    local c = s:sub(pos, pos)
    if c == "" then
      decode_error(pos, "unterminated string")
    elseif c == '"' then
      return table.concat(out), pos + 1
    elseif c == "\\" then
      local e = s:sub(pos + 1, pos + 1)
      if e == '"' or e == "\\" or e == "/" then
        out[#out + 1] = e
        pos = pos + 2
      elseif e == "b" then
        out[#out + 1] = "\b"
        pos = pos + 2
      elseif e == "f" then
        out[#out + 1] = "\f"
        pos = pos + 2
      elseif e == "n" then
        out[#out + 1] = "\n"
        pos = pos + 2
      elseif e == "r" then
        out[#out + 1] = "\r"
        pos = pos + 2
      elseif e == "t" then
        out[#out + 1] = "\t"
        pos = pos + 2
      elseif e == "u" then
        local hex = s:sub(pos + 2, pos + 5)
        if not hex:match("^%x%x%x%x$") then
          decode_error(pos, "invalid \\u escape")
        end
        local cp = tonumber(hex, 16)
        pos = pos + 6
        if cp >= 0xD800 and cp <= 0xDBFF then
          local low = s:sub(pos, pos + 5)
          local lo = low:match("^\\u(%x%x%x%x)$")
          if not lo then
            decode_error(pos, "missing low surrogate")
          end
          local lo_cp = tonumber(lo, 16)
          if lo_cp < 0xDC00 or lo_cp > 0xDFFF then
            decode_error(pos, "invalid low surrogate")
          end
          cp = 0x10000 + (cp - 0xD800) * 0x400 + (lo_cp - 0xDC00)
          pos = pos + 6
        elseif cp >= 0xDC00 and cp <= 0xDFFF then
          decode_error(pos, "unexpected low surrogate")
        end
        out[#out + 1] = utf8_encode(cp)
      else
        decode_error(pos, "invalid escape")
      end
    else
      local byte = string.byte(c)
      if byte < 0x20 then
        decode_error(pos, "control character in string")
      end
      -- Copy the whole run of plain bytes at once.
      local stop = s:find('[%z\1-\31\\"]', pos)
      if not stop then
        out[#out + 1] = s:sub(pos)
        return table.concat(out), #s + 1
      end
      out[#out + 1] = s:sub(pos, stop - 1)
      pos = stop
    end
  end
end

local function decode_number(s, pos)
  local start = pos
  local c = s:sub(pos, pos)
  if c == "-" then
    pos = pos + 1
  end
  local digits = s:match("^%d+", pos)
  if not digits then
    decode_error(pos, "invalid number")
  end
  if #digits > 1 and digits:sub(1, 1) == "0" then
    decode_error(pos, "leading zeros are not allowed")
  end
  pos = pos + #digits
  if s:sub(pos, pos) == "." then
    pos = pos + 1
    local frac = s:match("^%d+", pos)
    if not frac then
      decode_error(pos, "invalid fraction")
    end
    pos = pos + #frac
  end
  local exp = s:sub(pos, pos)
  if exp == "e" or exp == "E" then
    pos = pos + 1
    local sign = s:sub(pos, pos)
    if sign == "+" or sign == "-" then
      pos = pos + 1
    end
    local ed = s:match("^%d+", pos)
    if not ed then
      decode_error(pos, "invalid exponent")
    end
    pos = pos + #ed
  end
  local value = tonumber(s:sub(start, pos - 1))
  if value == nil then
    decode_error(start, "invalid number")
  end
  return value, pos
end

local function decode_array(s, pos)
  pos = pos + 1 -- '['
  local out = {}
  pos = skip_ws(s, pos)
  if s:sub(pos, pos) == "]" then
    return out, pos + 1
  end
  local index = 1
  while true do
    local value
    value, pos = decode_value(s, pos)
    out[index] = value
    index = index + 1
    pos = skip_ws(s, pos)
    local c = s:sub(pos, pos)
    if c == "]" then
      return out, pos + 1
    elseif c == "," then
      pos = skip_ws(s, pos + 1)
    else
      decode_error(pos, "expected ',' or ']'")
    end
  end
end

local function decode_object(s, pos)
  pos = pos + 1 -- '{'
  local out = {}
  pos = skip_ws(s, pos)
  if s:sub(pos, pos) == "}" then
    return out, pos + 1
  end
  while true do
    if s:sub(pos, pos) ~= '"' then
      decode_error(pos, "expected object key")
    end
    local key
    key, pos = decode_string(s, pos)
    pos = skip_ws(s, pos)
    if s:sub(pos, pos) ~= ":" then
      decode_error(pos, "expected ':'")
    end
    pos = skip_ws(s, pos + 1)
    local value
    value, pos = decode_value(s, pos)
    if value ~= M.null then
      out[key] = value
    end
    pos = skip_ws(s, pos)
    local c = s:sub(pos, pos)
    if c == "}" then
      return out, pos + 1
    elseif c == "," then
      pos = skip_ws(s, pos + 1)
    else
      decode_error(pos, "expected ',' or '}'")
    end
  end
end

decode_value = function(s, pos)
  pos = skip_ws(s, pos)
  local c = s:sub(pos, pos)
  if c == "" then
    decode_error(pos, "unexpected end of input")
  elseif c == '"' then
    return decode_string(s, pos)
  elseif c == "{" then
    return decode_object(s, pos)
  elseif c == "[" then
    return decode_array(s, pos)
  elseif c == "-" or c:match("%d") then
    return decode_number(s, pos)
  elseif s:sub(pos, pos + 3) == "true" then
    return true, pos + 4
  elseif s:sub(pos, pos + 4) == "false" then
    return false, pos + 5
  elseif s:sub(pos, pos + 3) == "null" then
    return M.null, pos + 4
  end
  decode_error(pos, "unexpected character")
end

function M.decode(text)
  if type(text) ~= "string" then
    error("json.decode expects a string", 2)
  end
  if text:sub(1, 3) == "\239\187\191" then
    text = text:sub(4)
  end
  local value, pos = decode_value(text, 1)
  pos = skip_ws(text, pos)
  if pos <= #text then
    decode_error(pos, "trailing characters")
  end
  return value
end

function M.is_null(v)
  return v == M.null
end

return M
