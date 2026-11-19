-- Scrollable table with fixed or flexible columns, header and selection.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")
local keys = require("tuipro.keys")
local scroll = require("tuipro.ui.scroll")

local Table = Widget:extend("Table")

local default_styles = {
  header = "TableHeader",
  border = "TableBorder",
  row = "TableRow",
  row_alt = "TableRowAlt",
  selected = "TableSelected",
  selected_inactive = "TableSelectedInactive",
  selected_muted = "TableSelectedMuted",
  empty = "TableEmpty",
  scrollbar_track = "ScrollbarTrack",
  scrollbar_thumb = "ScrollbarThumb",
  scrollbar_thumb_focus = "ScrollbarThumbFocus",
}

local function normalize_column(col, index)
  if type(col) == "string" then
    return { title = col, index = index }
  end
  col = col or {}
  return {
    title = col.title or col.label or col.header or tostring(index),
    key = col.key,
    index = col.index or index,
    width = col.width,
    flex = col.flex,
    min_width = col.min_width,
    align = col.align or "left",
    hl = col.hl,
    format = col.format,
    title_hl = col.title_hl,
  }
end

function Table.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Table)
  Widget.init(self, opts)
  self.focusable = opts.focusable ~= false
  self.rows = {}
  self.index = 0
  self.offset = 0
  self.columns = {}
  self.show_header = opts.show_header ~= false
  self.header_separator = opts.header_separator == true
  self.show_grid = opts.show_grid == true
  self.cell_padding = opts.cell_padding or opts.padding or 1
  self.empty_text = opts.empty_text or "No data"
  self.wheel_step = opts.wheel_step or 3
  self.selectable = opts.selectable ~= false
  self.on_select = opts.on_select
  self.on_activate = opts.on_activate
  self.styles = {}
  for k, v in pairs(default_styles) do
    self.styles[k] = v
  end
  for k, v in pairs(opts.styles or {}) do
    self.styles[k] = v
  end
  if opts.columns or opts.headers then
    local cols = opts.columns or opts.headers
    for i, col in ipairs(cols) do
      self.columns[i] = normalize_column(col, i)
    end
  end
  self:set_rows(opts.rows or {}, opts.selected or opts.index, true)
  if #self.columns == 0 then
    self:_infer_columns()
  end
  return self
end

function Table:_infer_columns()
  local sample = self.rows[1]
  if type(sample) ~= "table" then
    return
  end
  if #sample > 0 then
    for i = 1, #sample do
      self.columns[i] = normalize_column("Column " .. i, i)
    end
  else
    local keys_list = {}
    for k in pairs(sample) do
      if type(k) == "string" then
        keys_list[#keys_list + 1] = k
      end
    end
    table.sort(keys_list)
    for i, k in ipairs(keys_list) do
      self.columns[i] = normalize_column({ title = k, key = k }, i)
    end
  end
end

function Table:set_columns(columns)
  self.columns = {}
  for i, col in ipairs(columns or {}) do
    self.columns[i] = normalize_column(col, i)
  end
  self:invalidate()
  return self
end

function Table:count()
  return #self.rows
end

function Table:set_rows(rows, selected, silent)
  self.rows = type(rows) == "table" and rows or {}
  local target = selected or self.index
  if not target or target < 1 or target > #self.rows then
    target = #self.rows > 0 and 1 or 0
  end
  self.index = target
  self.offset = 0
  local visible = self:_row_area_height()
  self.offset = scroll.ensure_visible(0, self.index, #self.rows, visible)
  self:invalidate()
  if not silent and self.on_select and self.index > 0 then
    self.on_select(self.rows[self.index], self.index, self)
  end
  return self
end

function Table:_row_area_height()
  local h = self.h or 0
  if self.show_header and #self.columns > 0 then
    h = h - 1
    if self.header_separator then
      h = h - 1
    end
  end
  return math.max(0, h)
end

function Table:selected_row()
  return self.rows[self.index]
end

function Table:set_index(index, silent)
  if #self.rows == 0 then
    self.index = 0
    return false
  end
  index = util.clamp(index, 1, #self.rows)
  if index == self.index then
    return false
  end
  self.index = index
  self.offset = scroll.ensure_visible(self.offset, index, #self.rows, self:_row_area_height())
  self:invalidate()
  if not silent and self.on_select then
    self.on_select(self.rows[index], index, self)
  end
  return true
end

function Table:move(delta)
  if #self.rows == 0 then
    return false
  end
  local target = self.index + delta
  if target < 1 or target > #self.rows then
    return false
  end
  return self:set_index(target)
end

function Table:page(delta)
  local step = math.max(1, self:_row_area_height() - 1)
  return self:move((delta >= 0 and 1 or -1) * step)
end

function Table:activate()
  local row = self.rows[self.index]
  if not row then
    return false
  end
  if self.on_activate then
    self.on_activate(row, self.index, self)
    return true
  end
  return false
end

function Table:on_key(ev)
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
    return self:set_index(1)
  elseif name == "end" or name == "G" or name == "S-g" then
    return self:set_index(#self.rows)
  elseif name == "enter" then
    return self:activate()
  end
  return false
end

function Table:_row_at(y)
  local top = self.y
  if y < top or y >= top + self.h then
    return nil
  end
  local header_h = 0
  if self.show_header and #self.columns > 0 then
    header_h = 1
    if self.header_separator then
      header_h = header_h + 1
    end
  end
  local row = self.offset + (y - top - header_h) + 1
  if row >= 1 and row <= #self.rows then
    return row
  end
  return nil
end

function Table:on_mouse(ev)
  if ev.action == "wheel" then
    local delta = 0
    if ev.button == "wheel-up" then
      delta = -self.wheel_step
    elseif ev.button == "wheel-down" then
      delta = self.wheel_step
    end
    if delta ~= 0 then
      self:move(delta)
      return true
    end
    return false
  end
  local row = self:_row_at(ev.y)
  if not row then
    return false
  end
  if ev.action == "press" and ev.button == "left" then
    self.pressed_row = row
    self:set_index(row)
    return true
  elseif ev.action == "release" and ev.button == "left" then
    local pressed = self.pressed_row
    self.pressed_row = nil
    if pressed == row and row == self.index then
      return self:activate()
    end
    return true
  end
  return true
end

function Table:_cell_value(row, col)
  local value
  if col.key then
    value = row[col.key]
  else
    value = row[col.index]
  end
  if value == nil then
    value = ""
  end
  if col.format then
    value = col.format(value, row, col)
  end
  if type(value) == "table" then
    return tostring(value.text or value.value or ""),
        value.hl or col.hl, value.align or col.align
  end
  return tostring(value), col.hl, col.align
end

function Table:_intrinsic_widths()
  local pad = self.cell_padding
  local widths = {}
  for i, col in ipairs(self.columns) do
    local w = util.width(tostring(col.title or ""))
    for _, row in ipairs(self.rows) do
      local text = self:_cell_value(row, col)
      w = math.max(w, util.width(text))
    end
    widths[i] = w + pad * 2
  end
  return widths
end

function Table:_compute_widths(avail)
  local n = #self.columns
  local widths, mins = {}, {}
  local intrinsic = self:_intrinsic_widths()
  local total = 0
  for i, col in ipairs(self.columns) do
    local w = col.width or intrinsic[i]
    w = math.max(1, w)
    widths[i] = w
    mins[i] = math.min(w, col.min_width or 1)
    total = total + w
  end
  if n == 0 then
    return widths, 0
  end

  if total < avail then
    local extra = avail - total
    local flex_count = 0
    for _, col in ipairs(self.columns) do
      if col.flex and col.flex > 0 then
        flex_count = flex_count + 1
      end
    end
    if flex_count > 0 then
      local weight_sum = 0
      for _, col in ipairs(self.columns) do
        if col.flex and col.flex > 0 then
          weight_sum = weight_sum + col.flex
        end
      end
      local remaining = extra
      local seen = 0
      for i, col in ipairs(self.columns) do
        if col.flex and col.flex > 0 then
          seen = seen + 1
          local add
          if seen == flex_count then
            add = remaining
          else
            add = math.floor(extra * col.flex / weight_sum)
            remaining = remaining - add
          end
          widths[i] = widths[i] + add
        end
      end
    else
      widths[n] = widths[n] + extra
    end
  elseif total > avail then
    local excess = total - avail
    local guard = 0
    while excess > 0 and guard < 20000 do
      local changed = false
      for i, col in ipairs(self.columns) do
        if excess > 0 and widths[i] > mins[i] and col.width == nil then
          widths[i] = widths[i] - 1
          excess = excess - 1
          changed = true
        end
      end
      if not changed then
        for i = 1, n do
          if excess > 0 and widths[i] > 1 then
            widths[i] = widths[i] - 1
            excess = excess - 1
            changed = true
          end
        end
      end
      if not changed then
        break
      end
      guard = guard + 1
    end
  end
  return widths, total
end

function Table:preferred_height(avail)
  if self.height then
    return self.height
  end
  local h = #self.rows
  if self.show_header and #self.columns > 0 then
    h = h + 1
    if self.header_separator then
      h = h + 1
    end
  end
  return math.max(1, h)
end

function Table:preferred_width(avail)
  if self.width then
    return self.width
  end
  local total = 0
  for _, w in ipairs(self:_intrinsic_widths()) do
    total = total + w
  end
  if avail and total > avail then
    return avail
  end
  return math.max(1, total)
end

function Table:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  local n = #self.columns
  local pad = self.cell_padding
  if n == 0 then
    canvas:text(0, 0, self.empty_text, self.w, self.styles.empty, "center")
    return
  end
  local total = #self.rows
  local header_h = 0
  if self.show_header then
    header_h = 1
    if self.header_separator then
      header_h = header_h + 1
    end
  end
  local visible = math.max(0, self.h - header_h)
  local bar_w = (total > visible and self.h > header_h + 1) and 1 or 0
  local widths = self:_compute_widths(math.max(1, self.w - bar_w))
  local focused = self.focused
  local y = 0

  if self.show_header then
    canvas:fill(0, 0, self.w, 1, " ", self.styles.header)
    local x = 0
    for i, col in ipairs(self.columns) do
      local cw = widths[i]
      local title = util.truncate(tostring(col.title or ""), math.max(0, cw - pad * 2))
      local style = col.title_hl or self.styles.header
      if cw > pad * 2 then
        canvas:text(x + pad, 0, title, cw - pad * 2, style, col.align)
      end
      if self.show_grid and i < n and x + cw < self.w then
        canvas:put(x + cw, 0, "│", self.styles.border)
      end
      x = x + cw
    end
    y = 1
    if self.header_separator then
      if self.show_grid then
        local x = 0
        for i, cw in ipairs(widths) do
          canvas:hline(x, y, cw, "─", self.styles.border)
          if i < n and x + cw < self.w then
            canvas:put(x + cw, y, "┼", self.styles.border)
          end
          x = x + cw
        end
        if x < self.w then
          canvas:hline(x, y, self.w - x, "─", self.styles.border)
        end
      else
        canvas:hline(0, y, self.w, "─", self.styles.border)
      end
      y = y + 1
    end
  end

  if total == 0 then
    if visible > 0 then
      canvas:text(0, y, self.empty_text, self.w, self.styles.empty, "center")
    end
    return
  end

  self.offset = scroll.clamp_offset(self.offset, total, visible)
  for row = 0, visible - 1 do
    local idx = self.offset + row + 1
    local data = self.rows[idx]
    if not data then
      break
    end
    local selected = idx == self.index
    local active = selected and focused
    local base
    if active then
      base = self.styles.selected
    elseif selected then
      base = self.styles.selected_inactive
    else
      base = (idx % 2 == 0) and self.styles.row_alt or self.styles.row
    end
    canvas:fill(0, y + row, self.w, 1, " ", base)
    local x = 0
    for ci, col in ipairs(self.columns) do
      local cw = widths[ci]
      local text, chl, align = self:_cell_value(data, col)
      local style
      if active then
        style = self.styles.selected
      elseif selected then
        style = self.styles.selected_inactive
      else
        style = chl or base
      end
      if cw > pad * 2 then
        text = util.truncate(text, cw - pad * 2)
        canvas:text(x + pad, y + row, text, cw - pad * 2, style, align or "left")
      end
      if self.show_grid and ci < n and x + cw < self.w then
        local gstyle = selected and self.styles.selected_muted or self.styles.border
        canvas:put(x + cw, y + row, "│", gstyle)
      end
      x = x + cw
    end
  end

  if bar_w > 0 then
    scroll.draw(canvas, self.w - 1, header_h, math.max(1, self.h - header_h), total,
        visible, self.offset, {
          vertical = true,
          focused = focused,
          track_hl = self.styles.scrollbar_track,
          thumb_hl = self.styles.scrollbar_thumb,
          thumb_focus_hl = self.styles.scrollbar_thumb_focus,
        })
  end
end

return Table
