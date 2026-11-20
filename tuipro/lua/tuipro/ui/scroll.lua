-- Scrolling and scrollbar helpers shared by List, Menu and Table.
local hl = require("tuipro.hl")

local M = {}

function M.clamp_offset(offset, total, visible)
  if visible <= 0 then
    return 0
  end
  local max = math.max(0, total - visible)
  if offset < 0 then
    return 0
  end
  if offset > max then
    return max
  end
  return offset
end

-- Adjust an offset so that the 1-based index is visible.
function M.ensure_visible(offset, index, total, visible)
  if visible <= 0 then
    return 0
  end
  if index <= offset then
    return math.max(0, index - 1)
  end
  if index > offset + visible then
    return index - visible
  end
  return M.clamp_offset(offset, total, visible)
end

-- Geometry of the scrollbar thumb: returns start offset and size in cells.
function M.thumb(total, visible, offset, length)
  if length <= 0 or total <= 0 or visible <= 0 then
    return 0, 0
  end
  if total <= visible then
    return 0, length
  end
  local size = math.floor(length * visible / total + 0.5)
  if size < 1 then
    size = 1
  elseif size > length then
    size = length
  end
  local max_offset = total - visible
  local max_start = length - size
  local start = 0
  if max_offset > 0 then
    start = math.floor((offset / max_offset) * max_start + 0.5)
  end
  if start < 0 then
    start = 0
  elseif start > max_start then
    start = max_start
  end
  return start, size
end

-- Draw a vertical or horizontal scrollbar.
function M.draw(canvas, x, y, length, total, visible, offset, opts)
  opts = opts or {}
  if length <= 0 or total <= visible then
    return 0, 0
  end
  local vertical = opts.vertical ~= false
  local track_hl = opts.track_hl or "ScrollbarTrack"
  local thumb_hl = opts.thumb_hl or "ScrollbarThumb"
  if opts.focused then
    thumb_hl = opts.thumb_focus_hl or "ScrollbarThumbFocus"
  end
  local track_ch = opts.track or (vertical
      and hl.glyph("scrollbar", "vertical_track", "│")
      or hl.glyph("scrollbar", "horizontal_track", "─"))
  local thumb_ch = opts.thumb or (vertical
      and hl.glyph("scrollbar", "vertical_thumb", "┃")
      or hl.glyph("scrollbar", "horizontal_thumb", "━"))
  if vertical then
    canvas:vline(x, y, length, track_ch, track_hl)
  else
    canvas:hline(x, y, length, track_ch, track_hl)
  end
  local start, size = M.thumb(total, visible, offset, length)
  for i = 0, size - 1 do
    if vertical then
      canvas:put(x, y + start + i, thumb_ch, thumb_hl)
    else
      canvas:put(x + start + i, y, thumb_ch, thumb_hl)
    end
  end
  return start, size
end

return M
