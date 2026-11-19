-- One-line progress bar with label, track and percentage.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")

local Progress = Widget:extend("Progress")

local partials = { "", "▏", "▎", "▍", "▌", "▋", "▊", "▉" }

function Progress.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Progress)
  Widget.init(self, opts)
  self.value = opts.value or 0
  self.max = opts.max or 100
  self.label = opts.label
  self.show_label = opts.show_label ~= false
  self.show_percent = opts.show_percent ~= false
  self.percent_format = opts.percent_format or "%3d%%"
  self.fill_hl = opts.fill_hl or "ProgressFill"
  self.track_hl = opts.track_hl or "ProgressTrack"
  self.label_hl = opts.label_hl or "ProgressLabel"
  self.percent_hl = opts.percent_hl or "ProgressPercent"
  self.fill_char = opts.fill_char or "█"
  self.track_char = opts.track_char or "░"
  self.smooth = opts.smooth ~= false
  self.width = opts.width
  self.height = 1
  self.focusable = false
  return self
end

function Progress:ratio()
  local value = self.value
  if type(value) == "function" then
    value = value(self, self.app)
  end
  value = tonumber(value) or 0
  local max = tonumber(self.max) or 0
  if max <= 0 then
    return 0
  end
  return util.clamp(value / max, 0, 1)
end

function Progress:set_value(value)
  self.value = value
  self:invalidate()
end

function Progress:get_label()
  return self:label_text()
end

function Progress:label_text()
  local label = rawget(self, "label")
  if type(label) == "function" then
    return tostring(label(self, self.app) or "")
  end
  return label and tostring(label) or ""
end

function Progress:preferred_width(avail)
  if self.width then
    return self.width
  end
  return math.min(avail or 24, 24)
end

function Progress:preferred_height()
  return 1
end

function Progress:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  local ratio = self:ratio()
  local label = self.show_label and self:label_text() or ""
  local lw = util.width(label)
  local percent = ""
  if self.show_percent then
    percent = string.format(self.percent_format, math.floor(ratio * 100 + 0.5))
  end
  local pw = util.width(percent)
  local x = 0
  if lw > 0 then
    label = util.truncate(label, math.max(0, self.w - pw - 2))
    lw = util.width(label)
    canvas:put(0, 0, label, self.label_hl)
    x = lw + 1
  end
  local bar_w = self.w - x - pw - (pw > 0 and 1 or 0)
  if bar_w > 0 then
    local filled = ratio * bar_w
    local full = math.floor(filled)
    local frac = filled - full
    if full > bar_w then
      full = bar_w
    end
    local partial = 0
    if self.smooth and frac > 0 and full < bar_w then
      partial = math.floor(frac * 8 + 0.5)
      if partial >= 8 then
        full = full + 1
        partial = 0
      end
    end
    for i = 0, full - 1 do
      canvas:put(x + i, 0, self.fill_char, self.fill_hl)
    end
    local used = full
    if partial > 0 then
      canvas:put(x + full, 0, partials[partial], self.fill_hl)
      used = used + 1
    end
    for i = used, bar_w - 1 do
      canvas:put(x + i, 0, self.track_char, self.track_hl)
    end
  end
  if pw > 0 then
    canvas:put(self.w - pw, 0, percent, self.percent_hl)
  end
end

return Progress
