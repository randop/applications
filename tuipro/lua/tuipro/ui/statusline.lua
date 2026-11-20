-- Status line widget with left/center/right sections.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")
local hl = require("tuipro.hl")

local Statusline = Widget:extend("Statusline")

local function normalize_section(s, default_hl)
  if type(s) == "string" then
    return { text = s, hl = default_hl }
  end
  if type(s) == "table" then
    if s.text ~= nil then
      return { text = s.text, hl = s.hl or default_hl }
    end
    return s
  end
  return { text = tostring(s), hl = default_hl }
end

local function resolve(value, self, app, default_hl)
  if value == nil then
    return {}
  end
  if type(value) == "function" then
    value = value(self, app)
  end
  if value == nil then
    return {}
  end
  if type(value) == "table" and value.text ~= nil then
    return { normalize_section(value, default_hl) }
  end
  if type(value) == "string" then
    return { normalize_section(value, default_hl) }
  end
  local out = {}
  for _, s in ipairs(value) do
    if type(s) == "function" then
      s = s(self, app)
    end
    if s ~= nil then
      out[#out + 1] = normalize_section(s, default_hl)
    end
  end
  return out
end

function Statusline.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Statusline)
  Widget.init(self, opts)
  self.height = 1
  self.flex = 0
  self.left = opts.left
  self.center = opts.center
  self.right = opts.right
  self.dynamic = opts.dynamic
  self.background = opts.background or "StatusLine"
  self.default_hl = opts.hl or "StatusLine"
  self.padding = opts.padding or 1
  self.section_gap = opts.section_gap or ""
  self.focusable = false
  return self
end

function Statusline:set(side, value)
  self[side] = value
  self:invalidate()
end

function Statusline:render(canvas)
  canvas:fill(0, 0, self.w, 1, " ", self.background)
  local left = self.left
  local center = self.center
  local right = self.right
  if self.dynamic then
    local d = self.dynamic(self, self.app)
    if type(d) == "table" then
      left = d.left ~= nil and d.left or left
      center = d.center ~= nil and d.center or center
      right = d.right ~= nil and d.right or right
    end
  end
  left = resolve(left, self, self.app, self.default_hl)
  center = resolve(center, self, self.app, self.default_hl)
  right = resolve(right, self, self.app, self.default_hl)

  local function width_of(sections)
    local total = 0
    for i, s in ipairs(sections) do
      total = total + util.width(s.text or "")
      if i < #sections then
        total = total + util.width(self.section_gap)
      end
    end
    return total
  end

  local p = self.padding
  local right_w = width_of(right)
  local left_limit = math.max(0, self.w - p * 2 - (right_w > 0 and right_w + 1 or 0))
  local x = p
  local left_used = 0
  for i, s in ipairs(left) do
    if left_used >= left_limit then
      break
    end
    local text = s.text or ""
    text = util.truncate(text, left_limit - left_used,
        hl.glyph("text", "ellipsis", "…"))
    if text ~= "" then
      canvas:put(x, 0, text, s.hl)
      local tw = util.width(text)
      x = x + tw
      left_used = left_used + tw
    end
    if i < #left and left_used < left_limit then
      local gap = util.width(self.section_gap)
      x = x + gap
      left_used = left_used + gap
    end
  end

  local rx = math.max(p, self.w - p - right_w)
  for _, s in ipairs(right) do
    if rx >= self.w then
      break
    end
    local text = s.text or ""
    local maxw = self.w - p - rx
    text = util.truncate(text, maxw, hl.glyph("text", "ellipsis", "…"))
    canvas:put(rx, 0, text, s.hl)
    rx = rx + util.width(text) + util.width(self.section_gap)
  end

  local cw = width_of(center)
  if cw > 0 then
    local cx = math.floor((self.w - cw) / 2)
    local left_end = p + left_used
    local right_start = self.w - p - right_w
    local cx2 = math.max(cx, left_end + 1)
    local maxw = math.min(cw, math.max(0, right_start - 1 - cx2))
    if maxw > 0 then
      local used = 0
      for _, s in ipairs(center) do
        if used >= maxw then
          break
        end
        local t = util.truncate(s.text or "", maxw - used,
            hl.glyph("text", "ellipsis", "…"))
        canvas:put(cx2 + used, 0, t, s.hl)
        used = used + util.width(t)
      end
    end
  end
end

function Statusline.section(text, hl)
  return { text = text, hl = hl }
end

return Statusline
