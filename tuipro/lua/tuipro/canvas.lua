-- A clipped drawing surface.  Coordinates passed to Canvas methods are local
-- to the canvas; (0,0) is its top-left corner.  All widgets draw through a
-- canvas, which guarantees that they cannot paint outside their rectangle.
-- Child canvases created with sub() inherit the parent's clip rectangle, so
-- scrollable content can be offset and still be cut to the visible area.
local core = require("tuipro.core")
local hl = require("tuipro.hl")
local util = require("tuipro.util")

local Canvas = {}
Canvas.__index = Canvas

local function intersect(ax, ay, aw, ah, bx, by, bw, bh)
  local x1 = math.max(ax, bx)
  local y1 = math.max(ay, by)
  local x2 = math.min(ax + aw, bx + bw)
  local y2 = math.min(ay + ah, by + bh)
  if x2 <= x1 or y2 <= y1 then
    return x1, y1, 0, 0
  end
  return x1, y1, x2 - x1, y2 - y1
end

function Canvas.new(x, y, w, h)
  x, y = x or 0, y or 0
  w, h = math.max(0, w or 0), math.max(0, h or 0)
  return setmetatable({
    x = x, y = y, w = w, h = h,
    clip_x = x, clip_y = y, clip_w = w, clip_h = h,
  }, Canvas)
end

-- Create a child canvas.  Its drawing rectangle is (x, y, w, h) but the
-- visible region is intersected with the parent's clip rectangle.
function Canvas:sub(x, y, w, h)
  w, h = math.max(0, w or 0), math.max(0, h or 0)
  local cx, cy, cw, ch = intersect(
      self.clip_x, self.clip_y, self.clip_w, self.clip_h, x, y, w, h)
  return setmetatable({
    x = x, y = y, w = w, h = h,
    clip_x = cx, clip_y = cy, clip_w = cw, clip_h = ch,
  }, Canvas)
end

function Canvas:clip()
  core.clip(self.clip_x, self.clip_y, self.clip_w, self.clip_h)
end

function Canvas:contains(lx, ly)
  return lx >= 0 and ly >= 0 and lx < self.w and ly < self.h
end

function Canvas:put(lx, ly, text, style)
  if not text or text == "" or self.w <= 0 or self.h <= 0 then
    return
  end
  if self.clip_w <= 0 or self.clip_h <= 0 then
    return
  end
  if ly < 0 or ly >= self.h then
    return
  end
  local tw = util.width(text)
  if lx >= self.w or lx + tw <= 0 then
    return
  end
  self:clip()
  local fg, bg, attrs = hl.get(style)
  core.put(self.x + lx, self.y + ly, text, fg, bg, attrs)
end

function Canvas:put_right(ly, text, style, padding)
  local tw = util.width(text)
  self:put(self.w - tw - (padding or 0), ly, text, style)
end

function Canvas:fill(lx, ly, w, h, ch, style)
  if w <= 0 or h <= 0 or self.w <= 0 or self.h <= 0 then
    return
  end
  if self.clip_w <= 0 or self.clip_h <= 0 then
    return
  end
  self:clip()
  local fg, bg, attrs = hl.get(style)
  core.fill(self.x + lx, self.y + ly, w, h, ch or " ", fg, bg, attrs)
end

function Canvas:clear(style)
  self:fill(0, 0, self.w, self.h, " ", style)
end

function Canvas:text(lx, ly, text, maxw, style, align)
  maxw = maxw or self.w
  if maxw <= 0 or ly < 0 or ly >= self.h then
    return
  end
  local clipped = util.fit(text or "", maxw, align)
  self:put(lx, ly, clipped, style)
end

function Canvas:hline(lx, ly, w, ch, style)
  ch = ch or "─"
  if ch == "" then
    ch = "─"
  end
  self:fill(lx, ly, w, 1, ch, style)
end

function Canvas:vline(lx, ly, h, ch, style)
  ch = ch or "│"
  if ch == "" then
    ch = "│"
  end
  self:fill(lx, ly, 1, h, ch, style)
end

function Canvas:spans(lx, ly, spans, maxw, default_style)
  local x = 0
  local limit = maxw or self.w
  for _, span in ipairs(spans or {}) do
    local text = span.text or ""
    local w = util.width(text)
    if x + w > limit then
      text = util.truncate(text, limit - x)
      w = util.width(text)
    end
    if text ~= "" then
      self:put(lx + x, ly, text, span.hl or default_style)
    end
    x = x + w
    if x >= limit then
      break
    end
  end
end

function Canvas:label(x, y, label, style, width)
  self:text(x, y, label, width or (self.w - x), style)
end

return Canvas
