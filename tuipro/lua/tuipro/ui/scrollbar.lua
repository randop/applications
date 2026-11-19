-- Standalone scrollbar widget.  List, Menu and Table draw scrollbars via
-- tuipro.ui.scroll, but this widget is handy for custom panes.
local Widget = require("tuipro.widget")
local scroll = require("tuipro.ui.scroll")
local util = require("tuipro.util")

local Scrollbar = Widget:extend("Scrollbar")

function Scrollbar.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Scrollbar)
  Widget.init(self, opts)
  self.total = opts.total or 0
  self.visible = opts.visible or 1
  self.offset = opts.offset or 0
  self.direction = opts.direction or "vertical"
  self.vertical = self.direction ~= "horizontal"
  self.track_hl = opts.track_hl or "ScrollbarTrack"
  self.thumb_hl = opts.thumb_hl or "ScrollbarThumb"
  self.thumb_focus_hl = opts.thumb_focus_hl or "ScrollbarThumbFocus"
  self.on_change = opts.on_change
  self.focusable = false
  if self.vertical then
    self.width = opts.width or 1
    self.height = opts.height
  else
    self.height = opts.height or 1
    self.width = opts.width
  end
  self.offset = scroll.clamp_offset(self.offset, self.total, self.visible)
  return self
end

function Scrollbar:set(total, visible, offset)
  self.total = total or self.total
  self.visible = visible or self.visible
  self.offset = scroll.clamp_offset(offset or self.offset, self.total, self.visible)
  self:invalidate()
  return self
end

function Scrollbar:set_offset(offset)
  local old = self.offset
  self.offset = scroll.clamp_offset(offset, self.total, self.visible)
  if old ~= self.offset then
    if self.on_change then
      self.on_change(self.offset, self)
    end
    self:invalidate()
  end
  return self.offset
end

function Scrollbar:preferred_width()
  return self.vertical and 1 or util.width("")
end

function Scrollbar:preferred_height()
  return self.vertical and 1 or 1
end

function Scrollbar:on_mouse(ev)
  local length = self.vertical and self.h or self.w
  if length <= 0 then
    return false
  end
  if ev.action == "wheel" then
    local delta = 0
    if ev.button == "wheel-up" or ev.button == "wheel-left" then
      delta = -1
    elseif ev.button == "wheel-down" or ev.button == "wheel-right" then
      delta = 1
    end
    return delta ~= 0 and (self:set_offset(self.offset + delta) ~= nil)
  elseif ev.action == "press" and ev.button == "left" then
    local local_pos
    if self.vertical then
      local_pos = ev.y - self.y
    else
      local_pos = ev.x - self.x
    end
    if local_pos < 0 or local_pos >= length then
      return false
    end
    local max_offset = math.max(0, self.total - self.visible)
    local target = 0
    if length > 1 then
      target = math.floor(local_pos * max_offset / (length - 1) + 0.5)
    end
    self:set_offset(target)
    return true
  end
  return false
end

function Scrollbar:render(canvas)
  if self.vertical then
    if self.h <= 0 or self.w <= 0 then
      return
    end
    scroll.draw(canvas, 0, 0, self.h, self.total, self.visible, self.offset, {
      vertical = true,
      focused = self.focused,
      track_hl = self.track_hl,
      thumb_hl = self.thumb_hl,
      thumb_focus_hl = self.thumb_focus_hl,
    })
  else
    if self.w <= 0 or self.h <= 0 then
      return
    end
    scroll.draw(canvas, 0, 0, self.w, self.total, self.visible, self.offset, {
      vertical = false,
      focused = self.focused,
      track_hl = self.track_hl,
      thumb_hl = self.thumb_hl,
      thumb_focus_hl = self.thumb_focus_hl,
    })
  end
end

return Scrollbar
