-- Static (or dynamic) text block with optional word wrapping and alignment.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")

local Text = Widget:extend("Text")

function Text.new(opts)
  if type(opts) == "string" then
    opts = { text = opts }
  end
  opts = opts or {}
  local self = setmetatable({}, Text)
  Widget.init(self, opts)
  self.text = opts.text or opts.value or ""
  self.wrap = opts.wrap == true
  self.align = opts.align or "left"
  self.hl = opts.hl or opts.style
  self.padding = opts.padding or 0
  self.max_lines = opts.max_lines
  self.ellipsis = opts.ellipsis
  self.focusable = false
  self.height = opts.height or opts.h
  return self
end

function Text:value()
  local v = self.text
  if type(v) == "function" then
    return v(self, self.app)
  end
  return tostring(v == nil and "" or v)
end

function Text:lines(width)
  width = width or self.w
  local text = self:value()
  if not self.wrap then
    return util.lines(text)
  end
  return util.wrap(text, math.max(1, width), {})
end

function Text:preferred_width(avail)
  if self.width then
    return self.width
  end
  local maxw = 0
  for _, line in ipairs(util.lines(self:value())) do
    maxw = math.max(maxw, util.width(line))
  end
  if avail and maxw > avail then
    return avail
  end
  return maxw
end

function Text:preferred_height(avail)
  if self.height then
    return self.height
  end
  local lines = self:lines(avail or self.w)
  local count = #lines
  if self.max_lines and count > self.max_lines then
    count = self.max_lines
  end
  return math.max(1, count)
end

function Text:render(canvas)
  local pad = self.padding
  local width = math.max(0, self.w - pad * 2)
  if width <= 0 or self.h <= 0 then
    return
  end
  local lines = self:lines(width)
  local limit = self.h
  if self.max_lines then
    limit = math.min(limit, self.max_lines)
  end
  local y = 0
  for i, line in ipairs(lines) do
    if i > limit then
      break
    end
    if i == limit and i < #lines and self.ellipsis ~= false then
      line = util.truncate(line, width, self.ellipsis or "…")
    end
    canvas:text(pad, y, line, width, self.hl, self.align)
    y = y + 1
  end
end

return Text
