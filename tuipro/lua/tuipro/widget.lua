-- Base widget class.  Widgets have absolute geometry (x, y, w, h), a parent,
-- a reference to the application, and render through a clipped Canvas.
local util = require("tuipro.util")

local Widget = {}
Widget.__index = Widget
Widget.__name = "Widget"

local next_id = 0

-- Create a subclass.  Subclasses implement .new(opts) themselves, usually
-- calling the parent's .init method.
function Widget:extend(name)
  local cls = setmetatable({}, { __index = self })
  cls.__index = cls
  cls.__name = name
  cls.super = self
  -- Provide a default constructor so trivial subclasses can be used without
  -- writing their own .new().  Concrete widgets override this afterwards.
  cls.new = function(opts)
    local self = setmetatable({}, cls)
    cls.init(self, opts)
    return self
  end
  return cls
end

function Widget:init(opts)
  opts = opts or {}
  next_id = next_id + 1
  self.id = next_id
  self.x = 0
  self.y = 0
  self.w = 0
  self.h = 0
  self.visible = opts.visible ~= false
  self.focusable = opts.focusable == true
  self.focused = opts.focused == true
  self.contains_focus = false
  self.parent = nil
  self.app = nil
  -- Desired sizes.  A numeric width/height makes the widget fixed in the
  -- respective axis inside a box layout.
  self.width = opts.width or opts.w
  self.height = opts.height or opts.h
  self.flex = opts.flex
  self.name = opts.name
  self.hl = opts.hl
  self.tooltip = opts.tooltip
  self.padding = opts.padding
  return self
end

function Widget.new(opts)
  local self = setmetatable({}, Widget)
  Widget.init(self, opts)
  return self
end

function Widget:set_app(app)
  if self.app == app then
    return
  end
  self.app = app
  if self.on_attach then
    self:on_attach(app)
  end
  for _, child in ipairs(self.children or {}) do
    child:set_app(app)
  end
end

function Widget:detach()
  if self.on_detach then
    self:on_detach(self.app)
  end
  self.app = nil
  self.parent = nil
  for _, child in ipairs(self.children or {}) do
    child:detach()
  end
end

function Widget:invalidate()
  if self.app then
    self.app:invalidate()
  end
end

function Widget:invalidate_layout()
  if self.app then
    self.app:layout_invalidate()
  end
end

function Widget:resize(x, y, w, h)
  w = math.max(0, w or 0)
  h = math.max(0, h or 0)
  local changed = self.x ~= x or self.y ~= y or self.w ~= w or self.h ~= h
  self.x = x
  self.y = y
  self.w = w
  self.h = h
  if changed then
    self.dirty = true
  end
  self:layout(w, h)
  return changed
end

function Widget:layout(w, h)
  -- Leaf widgets need no child layout.
end

function Widget:preferred_width(avail)
  return self.width or math.min(avail or 10, 20)
end

function Widget:preferred_height(avail)
  return self.height or 1
end

function Widget:is_focusable()
  return self.focusable and self.visible and not self.disabled
end

function Widget:focus()
  if self.app then
    self.app:set_focus(self)
  end
end

function Widget:is_descendant_of(ancestor)
  local p = self.parent
  while p do
    if p == ancestor then
      return true
    end
    p = p.parent
  end
  return false
end

function Widget:contains(x, y)
  return self.visible and x >= self.x and y >= self.y and
      x < self.x + self.w and y < self.y + self.h
end

function Widget:hit(x, y)
  return self:contains(x, y)
end

function Widget:find_widget_at(x, y)
  if not self:contains(x, y) then
    return nil
  end
  local children = self.children
  if children then
    for i = #children, 1, -1 do
      local child = children[i]
      local found = child:find_widget_at(x, y)
      if found then
        return found
      end
    end
  end
  return self
end

function Widget:find_focusables(out)
  out = out or {}
  if self:is_focusable() then
    out[#out + 1] = self
  end
  for _, child in ipairs(self.children or {}) do
    if child.visible then
      child:find_focusables(out)
    end
  end
  return out
end

function Widget:handle(ev)
  if not self.visible then
    return false
  end
  if ev.type == "key" and self.on_key then
    return self:on_key(ev) == true
  elseif ev.type == "mouse" and self.on_mouse then
    return self:on_mouse(ev) == true
  elseif ev.type == "paste" and self.on_paste then
    return self:on_paste(ev) == true
  elseif ev.type == "resize" and self.on_resize then
    self:on_resize(ev)
    return true
  end
  return false
end

function Widget:render(canvas)
  -- Subclasses override.
end

function Widget:get_label()
  return rawget(self, "label") or self.name or self.__name
end

function Widget:label()
  return self:get_label()
end

function Widget:set_cursor(x, y, shape)
  if self.app then
    self.app:set_cursor(x, y, shape)
  end
end

function Widget:preferred_cursor()
  return nil
end

return Widget
