-- A horizontal or vertical rule, optionally with a centered label.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")
local hl = require("tuipro.hl")

local Separator = Widget:extend("Separator")

function Separator.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Separator)
  Widget.init(self, opts)
  self.direction = opts.direction or opts.orientation or "horizontal"
  self.label = opts.label or opts.title
  self.hl = opts.hl or "Separator"
  self.label_hl = opts.label_hl or "Subtitle"
  self.style = opts.style or "single"
  self.char = opts.char
  self.vchar = opts.vchar
  self.focusable = false
  if self.direction == "vertical" then
    self.width = opts.width or 1
    self.height = opts.height
  else
    self.height = opts.height or 1
    self.width = opts.width
  end
  return self
end

function Separator:preferred_height()
  return 1
end

function Separator:preferred_width(avail)
  if self.direction == "vertical" then
    return 1
  end
  if self.width then
    return self.width
  end
  local label = rawget(self, "label")
  if label then
    return util.width(tostring(label)) + 4
  end
  return avail or 1
end

function Separator:render(canvas)
  local hchar = self.char or hl.glyph("separator", self.style, "─")
  local vchar = self.vchar or hl.glyph("separator", "vertical", "│")
  if self.direction == "vertical" then
    canvas:vline(0, 0, self.h, vchar, self.hl)
    return
  end
  if self.w <= 0 then
    return
  end
  local raw_label = rawget(self, "label")
  if not raw_label or raw_label == "" then
    canvas:hline(0, 0, self.w, hchar, self.hl)
    return
  end
  local label = tostring(raw_label)
  label = util.truncate(label, math.max(0, self.w - 4),
      hl.glyph("text", "ellipsis", "…"))
  local lw = util.width(label)
  local left = math.floor((self.w - lw - 2) / 2)
  if left < 0 then
    left = 0
  end
  local right = self.w - left - lw - 2
  if right < 0 then
    right = 0
  end
  if left > 0 then
    canvas:hline(0, 0, left, hchar, self.hl)
  end
  canvas:put(left, 0, " ", self.hl)
  canvas:put(left + 1, 0, label, self.label_hl)
  canvas:put(left + 1 + lw, 0, " ", self.hl)
  if right > 0 then
    canvas:hline(left + lw + 2, 0, right, hchar, self.hl)
  end
end

return Separator
