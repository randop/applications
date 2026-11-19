-- Small, dependency-free helpers shared by the widget library.
local core = require("tuipro.core")

local M = {}

local width_cache = {}

function M.char_width(ch)
  local w = width_cache[ch]
  if w == nil then
    w = core.text_width(ch)
    width_cache[ch] = w
  end
  return w
end

function M.width(s)
  if not s or s == "" then
    return 0
  end
  return core.text_width(s)
end

function M.chars(s)
  return (s or ""):gmatch("[%z\1-\127\194-\244][\128-\191]*")
end

function M.truncate(s, maxw, ellipsis)
  s = s or ""
  if maxw <= 0 then
    return ""
  end
  if M.width(s) <= maxw then
    return s
  end
  if ellipsis == nil then
    ellipsis = "…"
  end
  local ew = M.width(ellipsis)
  if ew > maxw then
    ellipsis = ""
    ew = 0
  end
  local limit = maxw - ew
  local out = {}
  local w = 0
  for ch in M.chars(s) do
    local cw = M.char_width(ch)
    if w + cw > limit then
      break
    end
    out[#out + 1] = ch
    w = w + cw
  end
  return table.concat(out) .. ellipsis
end

function M.pad(s, w, align)
  s = s or ""
  local cur = M.width(s)
  if cur >= w then
    return s
  end
  local space = w - cur
  if align == "right" then
    return string.rep(" ", space) .. s
  elseif align == "center" then
    local left = math.floor(space / 2)
    return string.rep(" ", left) .. s .. string.rep(" ", space - left)
  end
  return s .. string.rep(" ", space)
end

function M.fit(s, w, align, ellipsis)
  return M.pad(M.truncate(s, w, ellipsis), w, align)
end

function M.wrap_line(line, width)
  if width <= 0 then
    return { "" }
  end
  local lines = {}
  local current = {}
  local curw = 0
  local function flush()
    lines[#lines + 1] = table.concat(current)
    current = {}
    curw = 0
  end
  -- Tokenise into words while preserving a single separating space.
  local pos = 1
  local n = #line
  while pos <= n do
    local sp = line:find(" ", pos, true)
    local word = sp and line:sub(pos, sp - 1) or line:sub(pos)
    pos = sp and sp + 1 or n + 1
    local ww = M.width(word)
    if #current > 0 and curw + 1 + ww > width then
      flush()
    end
    if ww > width then
      -- Break a very long word character by character.
      for ch in M.chars(word) do
        local cw = M.char_width(ch)
        if curw + cw > width and #current > 0 then
          flush()
        end
        current[#current + 1] = ch
        curw = curw + cw
      end
    else
      if #current > 0 then
        current[#current + 1] = " "
        curw = curw + 1
      end
      current[#current + 1] = word
      curw = curw + ww
    end
  end
  if #current > 0 or #lines == 0 then
    flush()
  end
  return lines
end

function M.wrap(text, width, opts)
  opts = opts or {}
  text = tostring(text or "")
  local out = {}
  for line in (text .. "\n"):gmatch("(.-)\n") do
    line = line:gsub("\t", "    ")
    if line == "" then
      out[#out + 1] = ""
    else
      local wrapped = M.wrap_line(line, width)
      for _, l in ipairs(wrapped) do
        out[#out + 1] = l
      end
    end
  end
  -- Remove the final empty line produced by the trailing newline.
  if #out > 1 and out[#out] == "" then
    out[#out] = nil
  end
  return out
end

function M.split(text, sep)
  sep = sep or "\n"
  local out = {}
  local from = 1
  while true do
    local a, b = text:find(sep, from, true)
    if not a then
      out[#out + 1] = text:sub(from)
      break
    end
    out[#out + 1] = text:sub(from, a - 1)
    from = b + 1
  end
  return out
end

function M.lines(text)
  return M.split(tostring(text or ""), "\n")
end

function M.trim(s)
  return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

function M.clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

function M.round(v)
  if v >= 0 then
    return math.floor(v + 0.5)
  end
  return math.ceil(v - 0.5)
end

function M.align_offset(width, avail, align)
  if align == "center" then
    return math.max(0, math.floor((avail - width) / 2))
  elseif align == "right" or align == "end" or align == "bottom" then
    return math.max(0, avail - width)
  end
  return 0
end

-- UTF-8 editing helpers.  A cursor is a 1-based byte index into a string,
-- with #s+1 representing the position after the last character.
function M.prev_char_pos(s, i)
  if i <= 1 then
    return 1
  end
  local j = i - 1
  while j > 1 do
    local b = s:byte(j)
    if not b or b < 0x80 or b >= 0xC0 then
      break
    end
    j = j - 1
  end
  return j
end

function M.next_char_pos(s, i)
  local n = #s
  if i > n then
    return n + 1
  end
  local j = i + 1
  while j <= n do
    local b = s:byte(j)
    if not b or b < 0x80 or b >= 0xC0 then
      break
    end
    j = j + 1
  end
  return j
end

function M.char_before(s, i)
  if i <= 1 then
    return ""
  end
  local j = M.prev_char_pos(s, i)
  return s:sub(j, i - 1)
end

function M.char_at(s, i)
  if i > #s then
    return ""
  end
  local j = M.next_char_pos(s, i)
  return s:sub(i, j - 1)
end

function M.prev_word_pos(s, i)
  local j = i
  while j > 1 do
    local prev = M.prev_char_pos(s, j)
    local ch = s:sub(prev, j - 1)
    if not ch:match("%s") then
      break
    end
    j = prev
  end
  while j > 1 do
    local prev = M.prev_char_pos(s, j)
    local ch = s:sub(prev, j - 1)
    if ch:match("%s") then
      break
    end
    j = prev
  end
  return j
end

function M.next_word_pos(s, i)
  local j = i
  local n = #s
  while j <= n do
    local ch = M.char_at(s, j)
    if ch:match("%s") then
      break
    end
    j = M.next_char_pos(s, j)
  end
  while j <= n do
    local ch = M.char_at(s, j)
    if not ch:match("%s") then
      break
    end
    j = M.next_char_pos(s, j)
  end
  return j
end

-- Keep only the part of a string whose display cells fall in
-- [start_col, start_col + maxw).  Used for horizontally scrolled text.
function M.slice_display(s, start_col, maxw)
  if maxw <= 0 then
    return ""
  end
  local out = {}
  local col = 0
  local used = 0
  for ch in M.chars(s) do
    local cw = M.char_width(ch)
    if col + cw > start_col then
      if used + cw > maxw then
        break
      end
      out[#out + 1] = ch
      used = used + cw
    end
    col = col + cw
  end
  return table.concat(out)
end

function M.display_width_of_prefix(s, byte_pos)
  return M.width(s:sub(1, math.max(0, byte_pos - 1)))
end

function M.spans_width(spans)
  local w = 0
  for _, span in ipairs(spans or {}) do
    w = w + M.width(span.text or "")
  end
  return w
end

function M.deepcopy(v)
  if type(v) ~= "table" then
    return v
  end
  local out = {}
  for k, x in pairs(v) do
    out[k] = M.deepcopy(x)
  end
  return out
end

function M.merge(base, over)
  local out = {}
  for k, v in pairs(base or {}) do
    out[k] = v
  end
  for k, v in pairs(over or {}) do
    out[k] = v
  end
  return out
end

function M.human_bytes(n)
  if n < 1024 then
    return string.format("%d B", n)
  elseif n < 1024 * 1024 then
    return string.format("%.1f KiB", n / 1024)
  elseif n < 1024 * 1024 * 1024 then
    return string.format("%.1f MiB", n / (1024 * 1024))
  end
  return string.format("%.1f GiB", n / (1024 * 1024 * 1024))
end

function M.basename(path)
  local s = tostring(path or ""):gsub("/+$", "")
  return (s:match("([^/]+)$")) or s
end

function M.dirname(path)
  local s = tostring(path or ""):gsub("/+$", "")
  local d = s:match("^(.*)/[^/]*$")
  if not d or d == "" then
    return "/"
  end
  return d
end

function M.joinpath(a, b)
  if a == "/" then
    return "/" .. b
  end
  return a .. "/" .. b
end

function M.exec_lines(cmd)
  local f = io.popen(cmd)
  if not f then
    return {}
  end
  local out = {}
  for line in f:lines() do
    out[#out + 1] = line
  end
  f:close()
  return out
end

function M.now()
  return core.ticks and core.ticks() or (os.clock() * 1000)
end

function M.percent(v, max)
  if not max or max == 0 then
    return 0
  end
  return math.max(0, math.min(1, v / max))
end

function M.shorten_home(path)
  local home = os.getenv("HOME")
  if home and path:sub(1, #home) == home then
    return "~" .. path:sub(#home + 1)
  end
  return path
end

return M
