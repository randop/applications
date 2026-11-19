-- Key notation parsing and matching.
--
-- Canonical names look like "a", "A", "S-a", "C-x", "M-x", "C-S-left",
-- "f1", "space", "enter", "tab", "backspace", "esc", "pageup".
-- Bindings accept vim-style <C-w>, neovim-style modifiers and plain names.
local M = {}

local named = {
  esc = "esc", escape = "esc",
  cr = "enter", ["return"] = "enter", enter = "enter",
  tab = "tab", backtab = "tab",
  bs = "backspace", backspace = "backspace",
  del = "delete", delete = "delete",
  ins = "insert", insert = "insert",
  space = "space", spc = "space",
  up = "up", down = "down", left = "left", right = "right",
  home = "home", ["end"] = "end",
  pageup = "pageup", pgup = "pageup", page_up = "pageup",
  pagedown = "pagedown", pgdn = "pagedown", page_down = "pagedown",
  begin = "begin",
}

-- Aliases for keys with punctuation names.
local symbol_names = {
  ["<"] = "<",
  [">"] = ">",
  ["-"] = "-",
  ["="] = "=",
  ["+"] = "+",
  ["_"] = "_",
  ["."] = ".",
  [","] = ",",
  [";"] = ";",
  [":"] = ":",
  ["/"] = "/",
  ["\\"] = "\\",
  ["|"] = "|",
  ["?"] = "?",
  ["!"] = "!",
  ["@"] = "@",
  ["#"] = "#",
  ["$"] = "$",
  ["%"] = "%",
  ["^"] = "^",
  ["&"] = "&",
  ["*"] = "*",
  ["("] = "(",
  [")"] = ")",
  ["["] = "[",
  ["]"] = "]",
  ["{"] = "{",
  ["}"] = "}",
  ["'"] = "'",
  ['"'] = '"',
  ["`"] = "`",
  ["~"] = "~",
}

local mod_prefixes = {
  { "ctrl-", "C" }, { "control-", "C" }, { "c-", "C" },
  { "alt-", "M" }, { "meta-", "M" }, { "m-", "M" }, { "a-", "M" },
  { "shift-", "S" }, { "s-", "S" },
  { "super-", "D" }, { "cmd-", "D" }, { "d-", "D" },
}

local function lower(s)
  return (s or ""):lower()
end

local function canonical_parts(key, mods)
  mods = mods or {}
  key = key or ""
  local shift = mods.shift
  if #key == 1 and key:match("%u") then
    key = key:lower()
    shift = true
  end
  local out = {}
  if mods.ctrl then out[#out + 1] = "C" end
  if mods.alt then out[#out + 1] = "M" end
  if shift then out[#out + 1] = "S" end
  if mods.super then out[#out + 1] = "D" end
  if #out == 0 then
    return key
  end
  return table.concat(out, "-") .. "-" .. key
end

function M.parse(lhs)
  if type(lhs) == "table" then
    if lhs.name then
      return M.parse(lhs.name)
    end
    lhs = lhs[1]
  end
  local s = tostring(lhs or "")
  s = s:gsub("^%s+", ""):gsub("%s+$", "")
  if s:sub(1, 1) == "<" and s:sub(-1) == ">" then
    s = s:sub(2, -2)
  end
  -- Normalize <C-w> style names written with brackets inside.
  local mods = { ctrl = false, alt = false, shift = false, super = false }
  local rest = s
  local changed = true
  while changed do
    changed = false
    local low = rest:lower()
    for _, p in ipairs(mod_prefixes) do
      if low:sub(1, #p[1]) == p[1] and #rest > #p[1] then
        if p[2] == "C" then mods.ctrl = true
        elseif p[2] == "M" then mods.alt = true
        elseif p[2] == "S" then mods.shift = true
        elseif p[2] == "D" then mods.super = true end
        rest = rest:sub(#p[1] + 1)
        changed = true
        break
      end
    end
  end

  local key
  local low_rest = rest:lower()
  if named[low_rest] then
    key = named[low_rest]
    if low_rest == "backtab" then
      mods.shift = true
    end
  elseif #rest == 1 then
    key = rest
  elseif symbol_names[rest] then
    key = rest
  elseif low_rest:match("^f%d+$") then
    key = low_rest
  elseif low_rest == "space" then
    key = "space"
  else
    -- Unknown name: keep it as-is (lowercased for stability).
    key = low_rest
  end
  return {
    key = key,
    ctrl = mods.ctrl,
    alt = mods.alt,
    shift = mods.shift,
    super = mods.super,
    name = canonical_parts(key, mods),
  }
end

local function event_mods(ev)
  local m = ev.mods or {}
  return {
    ctrl = m.ctrl or ev.ctrl or false,
    alt = m.alt or ev.alt or false,
    shift = m.shift or ev.shift or false,
    super = m.super or ev.super or false,
  }
end

-- Canonical name for a key event from the core.
function M.name(ev)
  if type(ev) == "string" then
    return M.parse(ev).name
  end
  local key = ev.key or ""
  return canonical_parts(key, event_mods(ev))
end

function M.match(lhs, ev)
  local parsed = type(lhs) == "table" and lhs.name and lhs or M.parse(lhs)
  return parsed.name == M.name(ev)
end

-- Human-readable form for hints and help screens.
function M.pretty(name)
  if type(name) == "table" then
    name = M.name(name)
  end
  local s = tostring(name or "")
  local out = {}
  local rest = s
  while true do
    local prefix, remainder = rest:match("^([CMDS])%-(.+)$")
    if not prefix then
      break
    end
    local label = ({ C = "Ctrl", M = "Alt", S = "Shift", D = "Super" })[prefix]
    out[#out + 1] = label
    rest = remainder
  end
  local key = rest
  if #key == 1 then
    key = key:upper()
  elseif key == "space" then
    key = "Space"
  elseif key == "esc" then
    key = "Esc"
  elseif key == "enter" then
    key = "Enter"
  elseif key == "tab" then
    key = "Tab"
  elseif key == "backspace" then
    key = "Backspace"
  elseif key == "delete" then
    key = "Del"
  elseif key == "pageup" then
    key = "PgUp"
  elseif key == "pagedown" then
    key = "PgDn"
  elseif key:match("^f%d+$") then
    key = key:upper()
  else
    key = key:sub(1, 1):upper() .. key:sub(2)
  end
  out[#out + 1] = key
  return table.concat(out, "-")
end

return M
