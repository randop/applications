-- Animated spinner with an optional label.  Animation is driven by a timer
-- while the widget is attached to a running application.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")

local Spinner = Widget:extend("Spinner")

local default_frames = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" }

function Spinner.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Spinner)
  Widget.init(self, opts)
  self.frames = opts.frames or default_frames
  self.interval = opts.interval or 80
  self.label = opts.label
  self.active = opts.active ~= false
  self.style = opts.hl or "Spinner"
  self.label_hl = opts.label_hl or "SpinnerLabel"
  self.done_style = opts.done_hl or "Success"
  self.error_style = opts.error_hl or "Error"
  self.done_text = opts.done_text or "✔"
  self.error_text = opts.error_text or "✖"
  self.done_label = opts.done_label
  self.error_label = opts.error_label
  self.state = "running"
  self.started = util.now()
  self.timer = nil
  self.width = opts.width
  self.height = 1
  self.focusable = false
  return self
end

function Spinner:_ensure_timer()
  if self.timer or not self.app or not self.active then
    return
  end
  self.timer = self.app:every(self.interval, function()
    if self.active then
      self:invalidate()
    end
  end)
end

function Spinner:_stop_timer()
  if self.timer then
    self.timer.active = false
    self.timer = nil
  end
end

function Spinner:on_attach()
  self:_ensure_timer()
end

function Spinner:on_detach()
  self:_stop_timer()
end

function Spinner:start(label)
  self.active = true
  self.state = "running"
  self.started = util.now()
  if label ~= nil then
    self.label = label
  end
  self:_ensure_timer()
  self:invalidate()
end

function Spinner:stop(success, label)
  self.active = false
  self.state = success == false and "error" or "done"
  if label ~= nil then
    if success == false then
      self.error_label = label
    else
      self.done_label = label
    end
  end
  self:_stop_timer()
  self:invalidate()
end

function Spinner:frame()
  local count = #self.frames
  if count == 0 then
    return ""
  end
  local elapsed = math.max(0, util.now() - self.started)
  local idx = math.floor(elapsed / self.interval) % count + 1
  return self.frames[idx]
end

function Spinner:get_label()
  local _, _, label = self:text_and_style()
  return label
end

function Spinner:text_and_style()
  local label = rawget(self, "label")
  if not self.active then
    if self.state == "error" then
      return self.error_text, self.error_style,
          self.error_label or label or ""
    end
    return self.done_text, self.done_style,
        self.done_label or label or ""
  end
  return self:frame(), self.style, label or ""
end

function Spinner:preferred_width(avail)
  if self.width then
    return self.width
  end
  local text, _, label = self:text_and_style()
  local w = util.width(text)
  if label and label ~= "" then
    w = w + 1 + util.width(label)
  end
  return math.min(avail or w, math.max(1, w))
end

function Spinner:preferred_height()
  return 1
end

function Spinner:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  self:_ensure_timer()
  local text, style, label = self:text_and_style()
  if label and type(label) == "function" then
    label = tostring(label(self, self.app) or "")
  end
  local x = 0
  if text and text ~= "" then
    canvas:put(0, 0, text, style)
    x = util.width(text) + 1
  end
  if label and label ~= "" and x < self.w then
    canvas:text(x, 0, tostring(label), self.w - x, self.label_hl)
  end
end

return Spinner
