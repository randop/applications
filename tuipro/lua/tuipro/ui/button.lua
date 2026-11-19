-- Focusable button with optional primary/danger styling and hotkey.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")
local keys = require("tuipro.keys")

local Button = Widget:extend("Button")

local function parse_hotkey(label, hotkey)
  local hk = hotkey
  if not hk then
    local before, ch, after = label:match("^(.-)&(%w)(.*)$")
    if ch then
      label = before .. ch .. after
      hk = ch:lower()
    end
  end
  return label, hk
end

function Button.new(opts)
  if type(opts) == "string" then
    opts = { label = opts }
  end
  opts = opts or {}
  local self = setmetatable({}, Button)
  opts.focusable = opts.focusable ~= false
  Widget.init(self, opts)
  local label, hotkey = parse_hotkey(tostring(opts.label or opts.text or "Button"),
      opts.hotkey)
  self.label = label
  self.hotkey = hotkey
  self.on_press = opts.on_press
  self.primary = opts.primary == true
  self.danger = opts.danger == true
  self.brackets = opts.brackets ~= false
  self.padding = opts.padding or 1
  self.align = opts.align or "center"
  self.disabled = opts.disabled == true
  self.pressed = false
  self.width = opts.width
  self.height = 1
  self.focusable = not self.disabled and opts.focusable ~= false
  return self
end

function Button:preferred_width(avail)
  if self.width then
    return self.width
  end
  local w = util.width(self.label) + self.padding * 2
  if self.brackets then
    w = w + 2
  end
  return math.min(avail or w, math.max(1, w))
end

function Button:preferred_height()
  return 1
end

function Button:style()
  if self.disabled then
    return "ButtonDisabled"
  end
  if self.focused then
    if self.primary then
      return "ButtonPrimaryFocus"
    elseif self.danger then
      return "ButtonDangerFocus"
    end
    return "ButtonFocus"
  end
  if self.primary then
    return "ButtonPrimary"
  elseif self.danger then
    return "ButtonDanger"
  end
  return "Button"
end

function Button:press()
  if self.disabled then
    return false
  end
  if self.on_press then
    self.on_press(self, self.app)
  end
  self:invalidate()
  return true
end

function Button:on_key(ev)
  local name = keys.name(ev)
  if name == "enter" or name == "space" then
    return self:press()
  end
  if self.hotkey and not self.disabled then
    local key = (ev.key or ""):lower()
    local mods = ev.mods or {}
    if key == self.hotkey:lower() and not mods.ctrl and not mods.alt and not mods.super then
      return self:press()
    end
  end
  return false
end

function Button:on_mouse(ev)
  if self.disabled then
    return false
  end
  if ev.action == "press" and ev.button == "left" then
    self.pressed = true
    return true
  elseif ev.action == "release" and ev.button == "left" then
    local was = self.pressed
    self.pressed = false
    if was then
      return self:press()
    end
    return false
  end
  return false
end

function Button:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  local style = self:style()
  local hotkey_style = self.focused and "ButtonHotkeyFocus" or "ButtonHotkey"
  canvas:fill(0, 0, self.w, 1, " ", style)

  local text = self.label
  local tw = util.width(text) + self.padding * 2
  local total = tw + (self.brackets and 2 or 0)
  local x = 0
  if self.align == "center" then
    x = math.max(0, math.floor((self.w - total) / 2))
  elseif self.align == "right" then
    x = math.max(0, self.w - total)
  end
  if self.brackets then
    canvas:put(x, 0, "[", style)
    x = x + 1
  end
  x = x + self.padding
  local maxw = math.max(0, self.w - x - self.padding - (self.brackets and 1 or 0))
  local hotkey_lower = self.hotkey and self.hotkey:lower() or nil
  local placed = false
  for ch in util.chars(text) do
    if maxw <= 0 then
      break
    end
    local ch_style = style
    if hotkey_lower and not placed and ch:lower() == hotkey_lower then
      ch_style = hotkey_style
      placed = true
    end
    canvas:put(x, 0, ch, ch_style)
    x = x + util.char_width(ch)
    maxw = maxw - util.char_width(ch)
  end
  if self.brackets then
    local bx = math.min(self.w - 1, x + self.padding)
    canvas:put(bx, 0, "]", style)
  end
end

return Button
