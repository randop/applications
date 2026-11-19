-- Scrollable viewport for arbitrary content.  The content child is laid out
-- at its natural height and clipped to the viewport rectangle; a scrollbar is
-- drawn on the right edge when the content does not fit.  Keys and the mouse
-- wheel scroll, and focused children are revealed automatically.
local container = require("tuipro.container")
local Container = container.Container
local scroll = require("tuipro.ui.scroll")
local keys = require("tuipro.keys")
local util = require("tuipro.util")

local Viewport = Container:extend("Viewport")

function Viewport.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Viewport)
  Container.init(self, opts)
  self.content = self.children[1]
  self.offset = opts.offset or 0
  self.total = 0
  self.visible_h = 0
  self.bar_w = 0
  self.content_height = opts.content_height
  self.max_height = opts.max_height
  self.show_scrollbar = opts.show_scrollbar ~= false
  self.focus_reveal = opts.focus_reveal ~= false
  self.wheel_step = opts.wheel_step or 3
  self.fill = opts.fill
  self.track_hl = opts.track_hl or "ScrollbarTrack"
  self.thumb_hl = opts.thumb_hl or "ScrollbarThumb"
  self.thumb_focus_hl = opts.thumb_focus_hl or "ScrollbarThumbFocus"
  self.on_change = opts.on_change
  self.focusable = opts.focusable ~= false
  return self
end

function Viewport:add(child, opts)
  local added = Container.add(self, child, opts)
  if added then
    self.content = self.children[1]
    self:invalidate_layout()
  end
  return added
end

function Viewport:set_content(widget)
  self._revealed_focus = nil
  self:clear()
  if widget then
    self:add(widget)
  end
  return widget
end

function Viewport:max_offset()
  return math.max(0, self.total - self.visible_h)
end

function Viewport:offset_visible(index)
  return index >= self.offset and index < self.offset + self.visible_h
end

function Viewport:set_offset(offset)
  local old = self.offset
  self.offset = scroll.clamp_offset(offset, self.total, self.visible_h)
  if self.offset ~= old then
    if self.content and self.content.w > 0 then
      self.content:resize(self.x, self.y - self.offset, self.content.w, self.total)
    end
    if self.on_change then
      self.on_change(self.offset, self)
    end
    self:invalidate()
  end
  return self.offset
end

function Viewport:scroll_by(delta)
  local old = self.offset
  self:set_offset(old + (delta or 0))
  return self.offset ~= old
end

function Viewport:scroll_page(dir)
  return self:scroll_by((dir or 1) * math.max(1, self.visible_h - 1))
end

function Viewport:layout(w, h)
  w = w or self.w
  h = h or self.h
  self.visible_h = h
  local content = self.content
  if not content then
    self.bar_w = 0
    return
  end
  local natural = self.content_height or content:preferred_height(w)
  natural = math.max(1, util.round(natural))
  self.total = natural
  self.bar_w = (self.show_scrollbar and natural > h) and 1 or 0
  self.offset = scroll.clamp_offset(self.offset, self.total, h)
  local inner_w = math.max(0, w - self.bar_w)
  content:resize(self.x, self.y - self.offset, inner_w, self.total)
  self:_reveal_focus(inner_w)
end

-- Scroll to reveal the focused descendant.  Only focus changes trigger the
-- reveal, so manual scrolling is not undone on every layout pass.
function Viewport:_reveal_focus(inner_w)
  if not self.focus_reveal then
    return
  end
  local f = self.app and self.app.focus
  if not f or f == self or not f:is_descendant_of(self) then
    self._revealed_focus = nil
    return
  end
  if f == self._revealed_focus or self.total <= self.visible_h then
    self._revealed_focus = f
    return
  end
  self._revealed_focus = f
  local rel = f.y - self.y
  local bottom = rel + math.max(1, f.h or 1)
  local new_offset = self.offset
  if rel < 0 then
    new_offset = self.offset + rel
  elseif bottom > self.visible_h then
    new_offset = self.offset + (bottom - self.visible_h)
  end
  new_offset = scroll.clamp_offset(new_offset, self.total, self.visible_h)
  if new_offset ~= self.offset then
    self.offset = new_offset
    if self.content then
      self.content:resize(self.x, self.y - self.offset, inner_w, self.total)
    end
    self:invalidate()
  end
end

function Viewport:preferred_width(avail)
  if self.width then
    return self.width
  end
  local inner = self.content and self.content:preferred_width(avail) or 0
  local total = inner + (self.show_scrollbar and 1 or 0)
  if avail and total > avail then
    return avail
  end
  return total
end

function Viewport:preferred_height(avail)
  if self.height then
    return self.height
  end
  local inner = self.content and self.content:preferred_height(avail) or 1
  if self.max_height then
    inner = math.min(inner, self.max_height)
  end
  return math.max(1, inner)
end

function Viewport:on_key(ev)
  local name = keys.name(ev)
  if name == "down" or name == "j" then
    self:scroll_by(1)
  elseif name == "up" or name == "k" then
    self:scroll_by(-1)
  elseif name == "pagedown" or name == "C-d" then
    self:scroll_page(1)
  elseif name == "pageup" or name == "C-u" then
    self:scroll_page(-1)
  elseif name == "home" or name == "g" then
    self:set_offset(0)
  elseif name == "end" or name == "G" or name == "S-g" then
    self:set_offset(self.total)
  else
    return false
  end
  return true
end

function Viewport:on_mouse(ev)
  if ev.action == "wheel" then
    local delta = 0
    if ev.button == "wheel-up" or ev.button == "wheel-left" then
      delta = -self.wheel_step
    elseif ev.button == "wheel-down" or ev.button == "wheel-right" then
      delta = self.wheel_step
    end
    if delta ~= 0 then
      self:scroll_by(delta)
      return true
    end
    return false
  end
  if ev.action == "press" and ev.button == "left" and self.bar_w > 0 then
    if ev.x - self.x >= self.w - self.bar_w then
      local max_offset = self:max_offset()
      if max_offset > 0 and self.visible_h > 1 then
        local ly = math.max(0, math.min(self.visible_h - 1, ev.y - self.y))
        self:set_offset(math.floor(ly * max_offset / (self.visible_h - 1) + 0.5))
      end
      return true
    end
  end
  return false
end

function Viewport:render(canvas)
  if self.fill then
    canvas:fill(0, 0, self.w, self.h, " ", self.fill)
  end
  local content = self.content
  if content and content.visible and self.w > 0 and self.h > 0 then
    local inner_w = math.max(0, self.w - self.bar_w)
    if inner_w > 0 then
      local cc = canvas:sub(self.x, self.y - self.offset, inner_w, self.total)
      content:render(cc)
    end
  end
  if self.bar_w > 0 and self.h > 0 then
    scroll.draw(canvas, self.w - 1, 0, self.h, self.total, self.visible_h, self.offset, {
      vertical = true,
      focused = self.focused or self.contains_focus,
      track_hl = self.track_hl,
      thumb_hl = self.thumb_hl,
      thumb_focus_hl = self.thumb_focus_hl,
    })
  end
end

return Viewport
