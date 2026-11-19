-- Horizontal tab strip.  Useful for a tabline or an in-body page switcher.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")
local keys = require("tuipro.keys")

local Tabs = Widget:extend("Tabs")

local function normalize(item)
  if type(item) == "string" then
    return { label = item }
  elseif type(item) == "number" then
    return { label = tostring(item) }
  end
  return item or {}
end

function Tabs.new(opts)
  if type(opts) == "table" and opts.items == nil and #opts > 0 then
    opts = { items = opts }
  end
  opts = opts or {}
  local self = setmetatable({}, Tabs)
  opts.focusable = opts.focusable ~= false
  Widget.init(self, opts)
  self.items = {}
  self.active = opts.active or opts.index or 1
  self.on_change = opts.on_change
  self.padding = opts.padding or 1
  self.gap = opts.gap or 0
  self.active_hl = opts.active_hl or "TabActive"
  self.inactive_hl = opts.inactive_hl or "TabInactive"
  self.badge_hl = opts.badge_hl or "TabBadge"
  self.fill = opts.fill or "Tabline"
  self.show_badges = opts.show_badges ~= false
  self.width = opts.width
  self.height = 1
  for i, item in ipairs(opts.items or {}) do
    self.items[i] = normalize(item)
  end
  if #self.items == 0 then
    self.active = 0
  elseif self.active < 1 or self.active > #self.items then
    self.active = 1
  end
  return self
end

function Tabs:count()
  return #self.items
end

function Tabs:item()
  return self.items[self.active]
end

function Tabs:set_items(items, active, silent)
  self.items = {}
  for i, item in ipairs(items or {}) do
    self.items[i] = normalize(item)
  end
  if #self.items == 0 then
    self.active = 0
  else
    active = active or self.active
    if active < 1 or active > #self.items then
      active = 1
    end
    self.active = active
  end
  self:invalidate()
  if not silent then
    self:_notify()
  end
  return self
end

function Tabs:_notify()
  if self.on_change and self.active > 0 then
    self.on_change(self.items[self.active], self.active, self)
  end
end

function Tabs:set_active(index, silent)
  if #self.items == 0 then
    return false
  end
  index = util.clamp(index, 1, #self.items)
  if index == self.active then
    return false
  end
  self.active = index
  self:invalidate()
  if not silent then
    self:_notify()
  end
  return true
end

function Tabs:next()
  return self:set_active((self.active % math.max(1, #self.items)) + 1)
end

function Tabs:prev()
  if self.active <= 1 then
    return self:set_active(#self.items)
  end
  return self:set_active(self.active - 1)
end

function Tabs:on_key(ev)
  local name = keys.name(ev)
  if name == "right" or name == "l" then
    return self:next()
  elseif name == "left" or name == "h" then
    return self:prev()
  elseif name == "home" then
    return self:set_active(1)
  elseif name == "end" then
    return self:set_active(#self.items)
  elseif name == "enter" or name == "space" then
    self:_notify()
    return true
  end
  return false
end

function Tabs:_segments()
  local segs = {}
  for i, item in ipairs(self.items) do
    local label = tostring(item.label or item.title or item.text or "")
    local w = util.width(label) + self.padding * 2
    local badge = item.badge
    if badge ~= nil and badge ~= false and self.show_badges then
      local bs = tostring(badge)
      if bs ~= "" then
        w = w + util.width(bs) + 1
      end
    end
    segs[i] = { label = label, width = w, item = item, badge = badge }
  end
  return segs
end

function Tabs:preferred_height()
  return 1
end

function Tabs:preferred_width(avail)
  if self.width then
    return self.width
  end
  local total = 0
  for _, seg in ipairs(self:_segments()) do
    total = total + seg.width + self.gap
  end
  if avail and total > avail then
    return avail
  end
  return total
end

function Tabs:on_mouse(ev)
  if ev.action ~= "press" or ev.button ~= "left" then
    return false
  end
  local segs = self:_segments()
  local x = 0
  local rel = ev.x - self.x
  for i, seg in ipairs(segs) do
    local right = x + seg.width
    if rel >= x and rel < right then
      self:set_active(i)
      return true
    end
    x = right + self.gap
  end
  return true
end

function Tabs:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  canvas:fill(0, 0, self.w, 1, " ", self.fill)
  if #self.items == 0 then
    return
  end
  local segs = self:_segments()
  -- Find the first tab to draw so that the active tab is visible.
  local start = 1
  local total = 0
  for _, seg in ipairs(segs) do
    total = total + seg.width + self.gap
  end
  local function width_from(from)
    local sum = 0
    for i = from, #segs do
      sum = sum + segs[i].width
      if i < #segs then
        sum = sum + self.gap
      end
    end
    return sum
  end
  while start < self.active and width_from(start) > self.w do
    start = start + 1
  end
  local x = 0
  for i = start, #segs do
    local seg = segs[i]
    if x >= self.w then
      break
    end
    local active = i == self.active
    local style = active and self.active_hl or self.inactive_hl
    local seg_w = math.min(seg.width, self.w - x)
    canvas:fill(x, 0, seg_w, 1, " ", style)
    local label_w = seg_w - self.padding * 2
    if seg.badge ~= nil and seg.badge ~= false and self.show_badges then
      label_w = label_w - util.width(tostring(seg.badge)) - 1
    end
    if label_w > 0 then
      canvas:text(x + self.padding, 0, seg.label, label_w, style)
    end
    if seg.badge ~= nil and seg.badge ~= false and self.show_badges then
      local bs = tostring(seg.badge)
      if bs ~= "" then
        local bx = x + seg_w - self.padding - util.width(bs)
        if bx > x + self.padding then
          canvas:put(bx, 0, bs, active and self.active_hl or self.badge_hl)
        end
      end
    end
    x = x + seg_w + self.gap
  end
end

return Tabs
