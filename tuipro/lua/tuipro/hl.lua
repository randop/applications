-- Highlight groups and style resolution.
--
-- A style can be:
--   * a group name:                "StatusLine"
--   * a table:                     { fg = "#7aa2f7", bold = true }
--   * a derived style:             hl.derive("ListSelected", { italic = true })
--   * nil:                         the Normal group
--
-- Resolved values are passed straight to the C core: 0xRRGGBB colors and an
-- attribute bitmask.  The value -1 means "terminal default color".
--
-- set_transparent(true) resolves neutral surface backgrounds to the terminal
-- default while leaving colored focus, selection and badge backgrounds alone.
local bit = require("bit")
local theme = require("tuipro.theme")

local M = {}

M.DEFAULT = -1
M.palette = theme.colors
M.theme = theme.name

M.attr = {
  bold = 1,
  dim = 2,
  italic = 4,
  underline = 8,
  blink = 16,
  reverse = 32,
  strike = 64,
  overline = 128,
}

M.attr_names = {
  "bold", "dim", "italic", "underline", "blink", "reverse", "strike", "overline",
}

-- When true, neutral panel/surface backgrounds resolve to the terminal's
-- default background so the terminal's transparency shows through.
M.transparent = false

local groups = {}
local string_cache = {}
local table_cache = setmetatable({}, { __mode = "k" })
local warned = {}

local function parse_color(v)
  if v == nil then
    return nil
  end
  if type(v) == "number" then
    return v
  end
  if v == false or v == "default" or v == "none" then
    return M.DEFAULT
  end
  if type(v) == "table" then
    return parse_color(v.color or v[1])
  end
  local p = theme.colors[v]
  if p ~= nil then
    v = p
  end
  if type(v) == "number" then
    return v
  end
  local hex = tostring(v):match("^#(%x+)$")
  if hex then
    if #hex == 3 then
      hex = hex:sub(1, 1):rep(2) .. hex:sub(2, 2):rep(2) .. hex:sub(3, 3):rep(2)
    end
    if #hex == 6 then
      return tonumber(hex, 16)
    end
  end
  local dec = tostring(v):match("^%d+$")
  if dec then
    return tonumber(dec)
  end
  return nil
end

-- Neutral surface colors.  In transparent mode these backgrounds resolve to
-- the terminal default, while accent backgrounds (focus, selection, badges)
-- keep their colors.
local neutral_names = {
  bg = true, surface = true, surface2 = true, panel = true, shadow = true,
}
local neutral_bg = {}
for name in pairs(neutral_names) do
  local color = parse_color(theme.colors[name])
  if color ~= nil then
    neutral_bg[color] = true
  end
end

local function is_neutral_bg(value)
  if type(value) == "string" and neutral_names[value] then
    return true
  end
  local color = parse_color(value)
  return color ~= nil and neutral_bg[color] == true
end

local function copy(t)
  local out = {}
  for k, v in pairs(t or {}) do
    out[k] = v
  end
  return out
end

local function normalize(spec)
  if type(spec) ~= "table" then
    return spec
  end
  local s = copy(spec)
  if s.fg == nil then
    s.fg = s.foreground
  end
  if s.bg == nil then
    s.bg = s.background
  end
  local raw_bg = s.bg
  s.fg = parse_color(s.fg)
  s.bg = parse_color(s.bg)
  s._neutral_bg = is_neutral_bg(raw_bg)
  if s.attrs then
    s._attrs = {}
    if type(s.attrs) == "number" then
      s._attrs_value = s.attrs
    else
      for _, name in ipairs(s.attrs) do
        s._attrs[name] = true
      end
    end
  end
  return s
end

function M.define(name, spec)
  groups[name] = normalize(spec or {})
  string_cache = {}
  return groups[name]
end

M.set = M.define

function M.get_group(name)
  return groups[name]
end

function M.names()
  local out = {}
  for name in pairs(groups) do
    out[#out + 1] = name
  end
  table.sort(out)
  return out
end

local function apply(res, spec, seen)
  if not spec then
    return res
  end
  if type(spec) == "string" then
    local g = groups[spec]
    if not g then
      if not warned[spec] then
        warned[spec] = true
        io.stderr:write(("[tuipro] unknown highlight group %q\n"):format(spec))
      end
      return res
    end
    if seen[spec] then
      return res
    end
    seen[spec] = true
    if g.link then
      apply(res, g.link, seen)
    end
    if g.fg ~= nil then res.fg = g.fg end
    if g.bg ~= nil then
      res.bg = g.bg
      res._neutral_bg = g._neutral_bg
    end
    for _, name in ipairs(M.attr_names) do
      if g[name] == true then
        res.attrs = bit.bor(res.attrs, M.attr[name])
      elseif g[name] == false then
        res.attrs = bit.band(res.attrs, bit.bnot(M.attr[name]))
      end
    end
    if g._attrs then
      for name in pairs(g._attrs) do
        if M.attr[name] then
          res.attrs = bit.bor(res.attrs, M.attr[name])
        end
      end
    end
    if g._attrs_value then
      res.attrs = bit.bor(res.attrs, g._attrs_value)
    end
    return res
  end
  if type(spec) == "table" then
    if spec.link then
      apply(res, spec.link, seen)
    end
    if spec.fg ~= nil then res.fg = parse_color(spec.fg) or res.fg end
    if spec.bg ~= nil then
      local bg = parse_color(spec.bg)
      if bg ~= nil then
        res.bg = bg
        res._neutral_bg = is_neutral_bg(spec.bg)
      end
    end
    for _, name in ipairs(M.attr_names) do
      if spec[name] == true then
        res.attrs = bit.bor(res.attrs, M.attr[name])
      elseif spec[name] == false then
        res.attrs = bit.band(res.attrs, bit.bnot(M.attr[name]))
      end
    end
    if spec.attrs then
      if type(spec.attrs) == "number" then
        res.attrs = bit.bor(res.attrs, spec.attrs)
      else
        for _, name in ipairs(spec.attrs) do
          if M.attr[name] then
            res.attrs = bit.bor(res.attrs, M.attr[name])
          end
        end
      end
    end
    return res
  end
  return res
end

local function resolve(spec)
  local res = { fg = M.DEFAULT, bg = M.DEFAULT, attrs = 0 }
  -- Normal is the implicit base for every style.
  local normal = groups.Normal
  if normal then
    res.fg = normal.fg ~= nil and normal.fg or M.DEFAULT
    res.bg = normal.bg ~= nil and normal.bg or M.DEFAULT
    res._neutral_bg = normal._neutral_bg
    for _, name in ipairs(M.attr_names) do
      if normal[name] then
        res.attrs = bit.bor(res.attrs, M.attr[name])
      end
    end
    if normal._attrs_value then
      res.attrs = bit.bor(res.attrs, normal._attrs_value)
    end
    if normal._attrs then
      for name in pairs(normal._attrs) do
        if M.attr[name] then
          res.attrs = bit.bor(res.attrs, M.attr[name])
        end
      end
    end
  end
  apply(res, spec, {})
  if M.transparent and (res._neutral_bg or neutral_bg[res.bg]) then
    res.bg = M.DEFAULT
  end
  return res
end

-- Enable or disable terminal-transparent neutral backgrounds.  Colored
-- backgrounds stay intact, so focus rings, selections and badges remain
-- visible.  Resolved-style caches are discarded.
function M.set_transparent(on)
  M.transparent = on and true or false
  string_cache = {}
  table_cache = setmetatable({}, { __mode = "k" })
  return M.transparent
end

-- Returns fg, bg, attrs for a style.
function M.get(spec)
  if spec == nil then
    local r = resolve("Normal")
    return r.fg, r.bg, r.attrs
  end
  if type(spec) == "string" then
    local cached = string_cache[spec]
    if not cached then
      cached = resolve(spec)
      string_cache[spec] = cached
    end
    return cached.fg, cached.bg, cached.attrs
  end
  if type(spec) == "table" then
    local cached = table_cache[spec]
    if not cached then
      cached = resolve(spec)
      table_cache[spec] = cached
    end
    return cached.fg, cached.bg, cached.attrs
  end
  local r = resolve(nil)
  return r.fg, r.bg, r.attrs
end

function M.attrs(spec)
  local _, _, attrs = M.get(spec)
  return attrs
end

function M.fg(spec)
  local fg = M.get(spec)
  return fg
end

function M.bg(spec)
  local _, bg = M.get(spec)
  return bg
end

function M.style(spec)
  local fg, bg, attrs = M.get(spec)
  return { fg = fg, bg = bg, attrs = attrs }
end

-- Build an inline style that inherits from base and overrides selected keys.
function M.derive(base, over)
  local s = { link = base }
  for k, v in pairs(over or {}) do
    s[k] = v
  end
  return s
end

M.inherit = M.derive

function M.reset()
  groups = {}
  string_cache = {}
  table_cache = setmetatable({}, { __mode = "k" })
  M.transparent = false
end

-- Load the default theme.
for name, spec in pairs(theme.groups) do
  groups[name] = normalize(spec)
end

return M
