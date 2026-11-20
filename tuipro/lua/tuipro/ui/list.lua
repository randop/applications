-- Scrollable selectable list.  Used directly and as the base for Menu.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")
local keys = require("tuipro.keys")
local scroll = require("tuipro.ui.scroll")
local hl = require("tuipro.hl")

local List = Widget:extend("List")

local base_styles = {
  item = "ListItem",
  selected = "ListSelected",
  selected_inactive = "ListSelectedInactive",
  detail = "ListDetail",
  detail_selected = "ListSelectedInfo",
  disabled = "ListDisabled",
  marker = "ListMarker",
  marker_selected = "ListMarkerSelected",
  index = "ListIndex",
  index_selected = "ListIndexSelected",
  empty = "ListEmpty",
  check = "ListCheck",
  check_selected = "ListCheckSelected",
  header = "ListHeader",
  scrollbar_track = "ScrollbarTrack",
  scrollbar_thumb = "ScrollbarThumb",
  scrollbar_thumb_focus = "ScrollbarThumbFocus",
}

function List.new(opts)
  if type(opts) == "table" and opts.items == nil and #opts > 0 then
    opts = { items = opts }
  end
  opts = opts or {}
  local self = setmetatable({}, List)
  opts.focusable = opts.focusable ~= false
  Widget.init(self, opts)
  self.items = {}
  self.index = 0
  self.offset = 0
  self.marker = opts.marker
  self.marker_unselected = opts.marker_unselected
  self.glyph_section = opts.glyph_section or "list"
  self.show_marker = opts.show_marker ~= false
  self.show_index = opts.show_index == true
  self.index_width = opts.index_width
  self.show_scrollbar = opts.show_scrollbar == true
  self.multiple = opts.multiple == true
  self.wrap = opts.wrap == true
  self.empty_text = opts.empty_text or "No items"
  self.detail_align = opts.detail_align or "right"
  self.wheel_step = opts.wheel_step or 3
  self.select_disabled = opts.select_disabled == true
  self.styles = {}
  for k, v in pairs(base_styles) do
    self.styles[k] = v
  end
  for k, v in pairs(opts.styles or {}) do
    self.styles[k] = v
  end
  self.on_select = opts.on_select
  self.on_activate = opts.on_activate
  self.on_toggle = opts.on_toggle
  self.on_change = opts.on_change
  self.multiple = opts.multiple == true
  self:set_items(opts.items or {}, opts.selected or opts.index, true)
  return self
end

function List:count()
  return #self.items
end

function List:item_at(index)
  return self.items[index]
end

function List:current()
  return self.items[self.index]
end

function List:current_index()
  return self.index
end

function List:is_selectable(index)
  local item = self.items[index]
  if not item or item.header then
    return false
  end
  if item.selectable == false then
    return false
  end
  if item.disabled and not self.select_disabled then
    return false
  end
  return true
end

function List:first_selectable(dir)
  if #self.items == 0 then
    return 0
  end
  dir = dir or 1
  local i = dir > 0 and 1 or #self.items
  while i >= 1 and i <= #self.items do
    if self:is_selectable(i) then
      return i
    end
    i = i + dir
  end
  return 0
end

function List:last_selectable()
  local i = #self.items
  while i >= 1 do
    if self:is_selectable(i) then
      return i
    end
    i = i - 1
  end
  return 0
end

function List:set_items(items, selected, silent)
  if type(items) == "table" then
    self.items = items
  else
    self.items = {}
  end
  for i, item in ipairs(self.items) do
    if type(item) == "string" then
      self.items[i] = { label = item }
    elseif type(item) == "number" then
      self.items[i] = { label = tostring(item) }
    end
  end
  local target = selected or self.index
  if not target or target < 1 or target > #self.items then
    target = self:first_selectable(1)
  elseif not self:is_selectable(target) then
    local next_ok = nil
    for i = target + 1, #self.items do
      if self:is_selectable(i) then
        next_ok = i
        break
      end
    end
    target = next_ok or self:last_selectable()
  end
  self.index = target or 0
  self.offset = scroll.ensure_visible(0, self.index, #self.items, self.h or 1)
  self:invalidate()
  if not silent then
    self:_notify_select()
  end
  return self
end

function List:_notify_select()
  if self.on_select and self.index > 0 then
    self.on_select(self.items[self.index], self.index, self)
  end
  if self.on_change then
    self.on_change(self.items[self.index], self.index, self)
  end
end

function List:set_index(index, silent)
  if #self.items == 0 then
    self.index = 0
    self.offset = 0
    return
  end
  index = util.clamp(index or 1, 1, #self.items)
  if not self:is_selectable(index) and not silent then
    return false
  end
  if index == self.index then
    self.offset = scroll.ensure_visible(self.offset, index, #self.items, self.h or 1)
    return false
  end
  self.index = index
  self.offset = scroll.ensure_visible(self.offset, index, #self.items, self.h or 1)
  self:invalidate()
  if not silent then
    self:_notify_select()
  end
  return true
end

function List:find_selectable(from, dir)
  dir = dir or 1
  local i = from
  while i >= 1 and i <= #self.items do
    if self:is_selectable(i) then
      return i
    end
    i = i + dir
  end
  return 0
end

function List:move(delta)
  if #self.items == 0 then
    return false
  end
  if self.index == 0 then
    return self:set_index(self:first_selectable(delta >= 0 and 1 or -1))
  end
  local target = self:find_selectable(self.index + delta, delta >= 0 and 1 or -1)
  if target == 0 and self.wrap then
    target = self:find_selectable(delta >= 0 and 1 or #self.items, delta >= 0 and 1 or -1)
  end
  if target == 0 or target == self.index then
    return false
  end
  return self:set_index(target)
end

function List:page(delta)
  local step = math.max(1, self.h or 1)
  return self:move((delta >= 0 and 1 or -1) * step)
end

function List:move_first()
  local i = self:first_selectable(1)
  if i > 0 then
    return self:set_index(i)
  end
  return false
end

function List:move_last()
  local i = self:last_selectable()
  if i > 0 then
    return self:set_index(i)
  end
  return false
end

function List:scroll_by(delta)
  local before = self.offset
  self.offset = scroll.clamp_offset(self.offset + delta, #self.items, self.h or 1)
  if before ~= self.offset then
    self:invalidate()
    return true
  end
  return false
end

function List:activate()
  local item = self.items[self.index]
  if not item or item.disabled or item.header then
    return false
  end
  if self.multiple then
    return self:toggle()
  end
  if self.on_activate then
    self.on_activate(item, self.index, self)
    return true
  end
  return false
end

function List:toggle(index)
  index = index or self.index
  local item = self.items[index]
  if not item or item.disabled or item.header then
    return false
  end
  item.checked = not item.checked
  if self.on_toggle then
    self.on_toggle(item, index, self)
  end
  self:invalidate()
  return true
end

function List:checked_items()
  local out = {}
  for _, item in ipairs(self.items) do
    if item.checked then
      out[#out + 1] = item
    end
  end
  return out
end

function List:checked_count()
  local n = 0
  for _, item in ipairs(self.items) do
    if item.checked then
      n = n + 1
    end
  end
  return n
end

function List:on_key(ev)
  local name = keys.name(ev)
  if name == "down" or name == "j" or name == "C-n" then
    return self:move(1)
  elseif name == "up" or name == "k" or name == "C-p" then
    return self:move(-1)
  elseif name == "pagedown" or name == "C-d" then
    return self:page(1)
  elseif name == "pageup" or name == "C-u" then
    return self:page(-1)
  elseif name == "home" or name == "g" then
    return self:move_first()
  elseif name == "end" or name == "G" or name == "S-g" then
    return self:move_last()
  elseif name == "enter" then
    return self:activate()
  elseif name == "space" then
    if self.multiple then
      return self:toggle()
    end
    return self:activate()
  end
  return false
end

function List:_row_at(y)
  if y < self.y or y >= self.y + self.h then
    return nil
  end
  local row = self.offset + (y - self.y) + 1
  if row >= 1 and row <= #self.items then
    return row
  end
  return nil
end

function List:on_mouse(ev)
  if ev.action == "wheel" then
    local delta = 0
    if ev.button == "wheel-up" or ev.button == "wheel-left" then
      delta = -self.wheel_step
    elseif ev.button == "wheel-down" or ev.button == "wheel-right" then
      delta = self.wheel_step
    end
    if delta ~= 0 then
      self:move(delta)
      return true
    end
    return false
  end
  local idx = self:_row_at(ev.y)
  if not idx then
    return false
  end
  if ev.action == "press" and ev.button == "left" then
    if self:is_selectable(idx) then
      self.pressed_index = idx
      self:set_index(idx)
    end
    return true
  elseif ev.action == "release" and ev.button == "left" then
    local pressed = self.pressed_index
    self.pressed_index = nil
    if pressed == idx and idx == self.index and self:is_selectable(idx) then
      return self:activate()
    end
    return true
  end
  return true
end

function List:preferred_height(avail)
  if self.height then
    return self.height
  end
  return math.max(1, #self.items)
end

function List:preferred_width(avail)
  if self.width then
    return self.width
  end
  local maxw = 0
  for _, item in ipairs(self.items) do
    local label = tostring(item.label or item.text or item.name or "")
    local detail = tostring(item.detail or item.shortcut or "")
    maxw = math.max(maxw, util.width(label) + util.width(detail) + 4)
  end
  if maxw == 0 then
    maxw = util.width(self.empty_text)
  end
  if avail and maxw > avail then
    maxw = avail
  end
  return maxw
end

function List:_label(item)
  return tostring(item.label or item.text or item.name or item.value or "")
end

function List:_detail(item)
  return tostring(item.detail or item.shortcut or item.hint or "")
end

function List:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  local total = #self.items
  if total == 0 then
    local y = math.floor(self.h / 2)
    local text = self.empty_text
    canvas:text(0, y, text, self.w, self.styles.empty, "center")
    return
  end
  self.offset = scroll.clamp_offset(self.offset, total, self.h)
  local bar_w = 0
  if self.show_scrollbar and total > self.h then
    bar_w = 1
  end
  local width = math.max(0, self.w - bar_w)
  local index_w = self.index_width or math.max(2, #tostring(total))
  local focused = self.focused
  local marker_on = self.marker or hl.glyph(self.glyph_section, "marker", "›")
  local marker_off = self.marker_unselected or
      hl.glyph(self.glyph_section, "marker_unselected", " ")
  local checked_ch = hl.glyph("list", "checked", "[x]")
  local unchecked_ch = hl.glyph("list", "unchecked", "[ ]")

  for row = 0, self.h - 1 do
    local i = self.offset + row + 1
    local item = self.items[i]
    if not item then
      break
    end
    local selected = i == self.index
    local active = selected and focused
    local style
    local detail_style
    if item.header then
      style = item.hl or self.styles.header
      detail_style = style
      canvas:fill(0, row, width, 1, " ", style)
    elseif item.disabled then
      style = self.styles.disabled
      detail_style = self.styles.disabled
      canvas:fill(0, row, width, 1, " ", style)
    else
      if active then
        style = self.styles.selected
        detail_style = self.styles.detail_selected
      elseif selected then
        style = self.styles.selected_inactive
        detail_style = self.styles.detail
      else
        style = item.hl or self.styles.item
        detail_style = item.detail_hl or self.styles.detail
      end
      canvas:fill(0, row, width, 1, " ", style)
    end

    local x = 0
    if self.show_marker then
      local marker = selected and marker_on or marker_off
      local marker_w = util.width(marker)
      if marker ~= "" then
        local mstyle = active and self.styles.marker_selected or
            (selected and style or self.styles.marker)
        if item.header or item.disabled then
          mstyle = style
        end
        canvas:put(x, row, marker, mstyle)
      end
      x = x + (marker ~= "" and marker_w or 1)
    end

    if self.multiple and not item.header then
      local check = item.checked and checked_ch or unchecked_ch
      local check_w = util.width(check)
      local cstyle
      if active and item.checked then
        cstyle = self.styles.check_selected
      elseif active then
        cstyle = self.styles.check_selected
      elseif item.checked then
        cstyle = self.styles.check
      else
        cstyle = style
      end
      canvas:put(x, row, check, cstyle)
      x = x + check_w + 1
    end

    if self.show_index and not item.header then
      local idx_text = string.format("%" .. index_w .. "d", i)
      local ist = active and self.styles.index_selected or self.styles.index
      canvas:put(x, row, idx_text, ist)
      x = x + index_w + 1
    end

    if item.header then
      local label = self:_label(item)
      if label == "" then
        label = item.title or ""
      end
      canvas:text(x, row, label, width - x, style)
    else
      local label = self:_label(item)
      local detail = self:_detail(item)
      local avail = math.max(0, width - x)
      if avail > 0 then
        if detail ~= "" and self.detail_align == "left" then
          local label_w = util.width(label)
          local max_label = math.max(0, avail - 4)
          label = util.truncate(label, max_label,
              hl.glyph("text", "ellipsis", "…"))
          label_w = util.width(label)
          canvas:put(x, row, label, style)
          local dx = x + label_w + 2
          if dx < width then
            canvas:text(dx, row, detail, width - dx, detail_style)
          end
        else
          local dw = 0
          if detail ~= "" then
            dw = util.width(detail)
          end
          local label_w = avail
          if dw > 0 then
            local max_detail = math.max(0, math.floor(avail * 0.6))
            detail = util.truncate(detail, max_detail,
                hl.glyph("text", "ellipsis", "…"))
            dw = util.width(detail)
            label_w = avail - dw - 1
            if label_w < 4 then
              dw = math.max(0, avail - 5)
              detail = util.truncate(detail, dw,
                  hl.glyph("text", "ellipsis", "…"))
              dw = util.width(detail)
              label_w = math.max(0, avail - dw - 1)
            end
          end
          canvas:text(x, row, label, label_w, style)
          if dw > 0 then
            local dx = width - dw
            if dx < x + label_w + 1 then
              dx = x + label_w + 1
            end
            if dx < width then
              canvas:put(dx, row, detail, detail_style)
            end
          end
        end
      end
    end
  end

  if bar_w > 0 then
    scroll.draw(canvas, self.w - 1, 0, self.h, total, self.h, self.offset, {
      vertical = true,
      focused = focused,
      track_hl = self.styles.scrollbar_track,
      thumb_hl = self.styles.scrollbar_thumb,
      thumb_focus_hl = self.styles.scrollbar_thumb_focus,
    })
  end
end

return List
