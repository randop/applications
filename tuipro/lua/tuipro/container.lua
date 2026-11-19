-- Layout containers: VBox, HBox, Padding, Center, Stack and Split.
local Widget = require("tuipro.widget")
local Canvas = require("tuipro.canvas")
local util = require("tuipro.util")

local Container = Widget:extend("Container")

function Container:init(opts)
  opts = opts or {}
  Widget.init(self, opts)
  self.children = {}
  self.gap = opts.gap or 0
  self.fill = opts.fill
  if opts.content then
    self:add(opts.content)
  end
  if opts.children then
    for _, child in ipairs(opts.children) do
      if type(child) == "table" and child.widget then
        self:add(child.widget, child)
      else
        self:add(child)
      end
    end
  end
  return self
end

function Container.new(opts)
  local self = setmetatable({}, Container)
  Container.init(self, opts)
  return self
end

function Container:add(child, opts)
  if not child then
    return nil
  end
  if child.parent and child.parent ~= self then
    child.parent:remove(child)
  end
  child.parent = self
  if self.app then
    child:set_app(self.app)
  end
  self.children[#self.children + 1] = child
  self:invalidate_layout()
  self:invalidate()
  return child
end

function Container:remove(child)
  for i, c in ipairs(self.children) do
    if c == child then
      table.remove(self.children, i)
      c.parent = nil
      if c.app then
        c:detach()
      end
      self:invalidate_layout()
      self:invalidate()
      return true
    end
  end
  return false
end

function Container:clear()
  for _, child in ipairs(self.children) do
    child.parent = nil
    child:detach()
  end
  self.children = {}
  self:invalidate_layout()
  self:invalidate()
end

function Container:visible_children()
  local out = {}
  for _, child in ipairs(self.children) do
    if child.visible then
      out[#out + 1] = child
    end
  end
  return out
end

function Container:render(canvas)
  if self.fill then
    canvas:fill(0, 0, self.w, self.h, " ", self.fill)
  end
  for _, child in ipairs(self.children) do
    if child.visible and child.w > 0 and child.h > 0 then
      local c = canvas:sub(child.x, child.y, child.w, child.h)
      child:render(c)
    end
  end
end

function Container:preferred_height(avail)
  if self.height then
    return self.height
  end
  local total = 0
  local n = 0
  for _, child in ipairs(self:visible_children()) do
    total = total + child:preferred_height(self.w)
    n = n + 1
  end
  if n > 1 then
    total = total + (n - 1) * self.gap
  end
  return total
end

function Container:preferred_width(avail)
  if self.width then
    return self.width
  end
  local maxw = 0
  for _, child in ipairs(self:visible_children()) do
    maxw = math.max(maxw, child:preferred_width(avail))
  end
  return maxw
end

-- ---------------------------------------------------------------------------
-- Vertical box
-- ---------------------------------------------------------------------------
local VBox = Container:extend("VBox")

function VBox.new(opts)
  local self = setmetatable({}, VBox)
  Container.init(self, opts)
  self.justify = opts.justify or "start"
  self.align = opts.align or "stretch"
  return self
end

function VBox:layout(w, h)
  local items = self:visible_children()
  local fixed_total = 0
  local flex_total = 0
  local flex_count = 0
  local meta = {}
  for _, child in ipairs(items) do
    local fx = child.flex or 0
    local size
    if fx > 0 then
      size = 0
      flex_total = flex_total + fx
      flex_count = flex_count + 1
    else
      size = child.height or child:preferred_height(w)
      size = math.max(0, util.round(size))
      fixed_total = fixed_total + size
    end
    meta[#meta + 1] = { child = child, fixed = size, flex = fx }
  end
  local gaps = self.gap * math.max(0, #items - 1)
  local remaining = math.max(0, (h or self.h) - fixed_total - gaps)
  local offset = 0
  if flex_count == 0 then
    local leftover = math.max(0, (h or self.h) - fixed_total - gaps)
    if self.justify == "center" then
      offset = math.floor(leftover / 2)
    elseif self.justify == "end" then
      offset = leftover
    end
  end

  local y = self.y + offset
  local used = 0
  local flex_seen = 0
  for _, m in ipairs(meta) do
    local size = m.fixed
    if m.flex > 0 then
      flex_seen = flex_seen + 1
      if flex_seen == flex_count then
        size = math.max(0, remaining - used)
      else
        size = math.floor(remaining * m.flex / flex_total)
      end
      used = used + size
    end
    local cw = m.child.width or w or self.w
    local cx = self.x
    if self.align ~= "stretch" then
      cx = self.x + util.align_offset(cw, w or self.w, self.align)
    end
    m.child:resize(cx, y, cw, size)
    y = y + size + self.gap
  end
end

-- ---------------------------------------------------------------------------
-- Horizontal box
-- ---------------------------------------------------------------------------
local HBox = Container:extend("HBox")

function HBox.new(opts)
  local self = setmetatable({}, HBox)
  Container.init(self, opts)
  self.justify = opts.justify or "start"
  self.valign = opts.valign or "stretch"
  return self
end

function HBox:layout(w, h)
  w = w or self.w
  h = h or self.h
  local items = self:visible_children()
  local fixed_total = 0
  local flex_total = 0
  local flex_count = 0
  local meta = {}
  for _, child in ipairs(items) do
    local fx = child.flex or 0
    local size
    if fx > 0 then
      size = 0
      flex_total = flex_total + fx
      flex_count = flex_count + 1
    else
      size = child.width or child:preferred_width(w)
      size = math.max(0, util.round(size))
      fixed_total = fixed_total + size
    end
    meta[#meta + 1] = { child = child, fixed = size, flex = fx }
  end
  local gaps = self.gap * math.max(0, #items - 1)
  local remaining = math.max(0, w - fixed_total - gaps)
  local offset = 0
  if flex_count == 0 then
    local leftover = math.max(0, w - fixed_total - gaps)
    if self.justify == "center" then
      offset = math.floor(leftover / 2)
    elseif self.justify == "end" then
      offset = leftover
    end
  end

  local x = self.x + offset
  local used = 0
  local flex_seen = 0
  for _, m in ipairs(meta) do
    local size = m.fixed
    if m.flex > 0 then
      flex_seen = flex_seen + 1
      if flex_seen == flex_count then
        size = math.max(0, remaining - used)
      else
        size = math.floor(remaining * m.flex / flex_total)
      end
      used = used + size
    end
    local ch = m.child.height or h
    local cy = self.y
    if self.valign ~= "stretch" then
      cy = self.y + util.align_offset(ch, h, self.valign)
    end
    m.child:resize(x, cy, size, ch)
    x = x + size + self.gap
  end
end

function HBox:preferred_width(avail)
  if self.width then
    return self.width
  end
  local total = 0
  local n = 0
  for _, child in ipairs(self:visible_children()) do
    total = total + child:preferred_width(avail)
    n = n + 1
  end
  if n > 1 then
    total = total + (n - 1) * self.gap
  end
  return total
end

function HBox:preferred_height(avail)
  if self.height then
    return self.height
  end
  local maxh = 0
  for _, child in ipairs(self:visible_children()) do
    maxh = math.max(maxh, child:preferred_height(avail))
  end
  return maxh
end

-- ---------------------------------------------------------------------------
-- Padding and Center
-- ---------------------------------------------------------------------------
local function parse_padding(p)
  if type(p) == "number" then
    return p, p, p, p
  end
  if type(p) == "table" then
    return p.top or p[1] or 0, p.right or p[2] or p[1] or 0,
        p.bottom or p[3] or p[1] or 0, p.left or p[4] or p[2] or p[1] or 0
  end
  return 0, 0, 0, 0
end

local Padding = Container:extend("Padding")

function Padding.new(opts)
  local self = setmetatable({}, Padding)
  Container.init(self, opts)
  self.pad_top, self.pad_right, self.pad_bottom, self.pad_left = parse_padding(opts.padding or 1)
  self.fill = opts.fill
  return self
end

function Padding:layout(w, h)
  local child = self.children[1]
  if not child then
    return
  end
  local x = self.x + self.pad_left
  local y = self.y + self.pad_top
  local cw = math.max(0, (w or self.w) - self.pad_left - self.pad_right)
  local ch = math.max(0, (h or self.h) - self.pad_top - self.pad_bottom)
  child:resize(x, y, cw, ch)
end

local Center = Container:extend("Center")

function Center.new(opts)
  local self = setmetatable({}, Center)
  Container.init(self, opts)
  self.fill = opts.fill
  return self
end

function Center:layout(w, h)
  local child = self.children[1]
  if not child then
    return
  end
  w = w or self.w
  h = h or self.h
  local cw = child.width or child:preferred_width(w)
  local ch = child.height or child:preferred_height(w)
  cw = math.min(cw, w)
  ch = math.min(ch, h)
  child:resize(self.x + math.floor((w - cw) / 2), self.y + math.floor((h - ch) / 2), cw, ch)
end

-- ---------------------------------------------------------------------------
-- Stack: all children share the same rectangle.  Useful for pages/overlays.
-- ---------------------------------------------------------------------------
local Stack = Container:extend("Stack")

function Stack.new(opts)
  local self = setmetatable({}, Stack)
  Container.init(self, opts)
  self.fill = opts.fill
  return self
end

function Stack:layout(w, h)
  for _, child in ipairs(self.children) do
    if child.visible then
      child:resize(self.x, self.y, w or self.w, h or self.h)
    end
  end
end

function Stack:show(index)
  for i, child in ipairs(self.children) do
    child.visible = i == index
  end
  self:invalidate_layout()
  self:invalidate()
end

function Stack:active()
  for i, child in ipairs(self.children) do
    if child.visible then
      return child, i
    end
  end
end

-- ---------------------------------------------------------------------------
-- Split: neovim-style panes with a one-cell separator.
-- ---------------------------------------------------------------------------
local Split = Container:extend("Split")

function Split.new(opts)
  local self = setmetatable({}, Split)
  Container.init(self, opts)
  self.direction = opts.direction or opts.orientation or "vertical"
  self.show_separator = opts.show_separator ~= false
  self.separator_style = opts.separator_style or "Separator"
  self.separator_focus_style = opts.separator_focus_style or "SeparatorFocus"
  self.separators = {}
  return self
end

function Split:add(child, opts)
  opts = opts or {}
  local c = Container.add(self, child)
  if c then
    c.split_size = opts.size
    c.split_weight = opts.weight or (opts.size and 0 or 1)
  end
  return c
end

function Split:layout(w, h)
  w = w or self.w
  h = h or self.h
  local vertical = self.direction == "vertical"
  local items = self:visible_children()
  local n = #items
  local sep_total = (self.show_separator and n > 1) and (n - 1) or 0
  local avail = (vertical and w or h) - sep_total
  if avail < 0 then
    avail = 0
  end
  local fixed_total = 0
  local weight_total = 0
  local flex_count = 0
  local meta = {}
  for _, child in ipairs(items) do
    local size = child.split_size
    local weight = child.split_weight or 1
    if size then
      size = math.max(0, util.round(size))
      fixed_total = fixed_total + size
    else
      weight_total = weight_total + weight
      flex_count = flex_count + 1
    end
    meta[#meta + 1] = { child = child, size = size, weight = weight }
  end
  local remaining = math.max(0, avail - fixed_total)
  local flex_used = 0
  local flex_seen = 0
  local pos = vertical and self.x or self.y
  self.separators = {}
  for i, m in ipairs(meta) do
    local size
    if m.size then
      size = m.size
    elseif weight_total > 0 then
      flex_seen = flex_seen + 1
      if flex_seen == flex_count then
        size = math.max(0, remaining - flex_used)
      else
        size = math.floor(remaining * m.weight / weight_total)
      end
      flex_used = flex_used + size
    else
      size = 0
    end
    if vertical then
      m.child:resize(pos, self.y, size, h)
    else
      m.child:resize(self.x, pos, w, size)
    end
    pos = pos + size
    if self.show_separator and i < #meta then
      self.separators[#self.separators + 1] = {
        x = vertical and pos or self.x,
        y = vertical and self.y or pos,
        vertical = vertical,
      }
      pos = pos + 1
    end
  end
end

function Split:render(canvas)
  Container.render(self, canvas)
  for _, s in ipairs(self.separators) do
    local lx = s.x - self.x
    local ly = s.y - self.y
    if s.vertical then
      canvas:vline(lx, 0, self.h, "│", self.separator_style)
    else
      canvas:hline(0, ly, self.w, "─", self.separator_style)
    end
  end
end

return {
  Container = Container,
  VBox = VBox,
  HBox = HBox,
  Padding = Padding,
  Center = Center,
  Stack = Stack,
  Split = Split,
  parse_padding = parse_padding,
}
