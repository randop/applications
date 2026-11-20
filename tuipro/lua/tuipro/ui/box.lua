-- A bordered container with a title, padding and a single content area.
local container = require("tuipro.container")
local Container = container.Container
local parse_padding = container.parse_padding
local Canvas = require("tuipro.canvas")
local util = require("tuipro.util")
local hl = require("tuipro.hl")

local Box = Container:extend("Box")

local fallback_border = { tl = "╭", tr = "╮", bl = "╰", br = "╯", h = "─", v = "│" }

function Box.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Box)
  Container.init(self, opts)
  self.border = opts.border ~= false
  self.border_style = opts.border_style or "rounded"
  self.chars = opts.chars
  self.border_hl = opts.border_hl or opts.hl or "Border"
  self.border_focus_hl = opts.border_focus_hl or "BorderFocus"
  self.title = opts.title
  self.title_align = opts.title_align or "left"
  self.title_hl = opts.title_hl or "Title"
  self.title_focus_hl = opts.title_focus_hl or self.title_hl
  self.fill = opts.fill
  self.pad_top, self.pad_right, self.pad_bottom, self.pad_left =
      parse_padding(opts.padding or 0)
  self.content_align = opts.content_align or "stretch"
  self.content_x = 0
  self.content_y = 0
  self.content_w = 0
  self.content_h = 0
  self.focusable = false
  return self
end

function Box:layout(w, h)
  w = w or self.w
  h = h or self.h
  local b = self.border and 1 or 0
  local x = self.x + b + self.pad_left
  local y = self.y + b + self.pad_top
  local cw = math.max(0, w - b * 2 - self.pad_left - self.pad_right)
  local ch = math.max(0, h - b * 2 - self.pad_top - self.pad_bottom)
  self.content_x, self.content_y = x, y
  self.content_w, self.content_h = cw, ch
  for _, child in ipairs(self.children) do
    local child_w = cw
    local child_h = ch
    local cx, cy = x, y
    if self.content_align == "center" and child.width then
      child_w = math.min(cw, child.width)
      cx = x + math.floor((cw - child_w) / 2)
    elseif self.content_align == "right" and child.width then
      child_w = math.min(cw, child.width)
      cx = x + cw - child_w
    end
    if child.height then
      local want = math.min(ch, child.height)
      if self.content_align == "center" or self.content_align == "right" then
        -- keep the child at the top for vertical alignment
      end
      child_h = want
    end
    child:resize(cx, cy, child_w, child_h)
  end
end

function Box:preferred_width(avail)
  if self.width then
    return self.width
  end
  local b = self.border and 2 or 0
  local inner = 0
  for _, child in ipairs(self.children) do
    inner = math.max(inner, child:preferred_width(avail))
  end
  if self.title then
    inner = math.max(inner, util.width(tostring(self.title)) + 4)
  end
  return math.min(avail or inner + b + self.pad_left + self.pad_right,
      inner + b + self.pad_left + self.pad_right)
end

function Box:preferred_height(avail)
  if self.height then
    return self.height
  end
  local b = self.border and 2 or 0
  local inner = 0
  for _, child in ipairs(self.children) do
    inner = inner + child:preferred_height(avail)
  end
  if inner == 0 then
    inner = 1
  end
  return inner + b + self.pad_top + self.pad_bottom
end

function Box:render(canvas)
  if self.fill then
    canvas:fill(0, 0, self.w, self.h, " ", self.fill)
  end
  local focused = self.focused or self.contains_focus
  local bhl = focused and self.border_focus_hl or self.border_hl
  local title_hl = focused and self.title_focus_hl or self.title_hl
  local c = self.chars or hl.glyph("border", self.border_style,
      hl.glyph("border", "rounded", fallback_border))

  if self.border and self.w >= 2 and self.h >= 2 then
    canvas:put(0, 0, c.tl, bhl)
    canvas:put(self.w - 1, 0, c.tr, bhl)
    canvas:put(0, self.h - 1, c.bl, bhl)
    canvas:put(self.w - 1, self.h - 1, c.br, bhl)
    if self.w > 2 then
      canvas:hline(1, 0, self.w - 2, c.h, bhl)
      canvas:hline(1, self.h - 1, self.w - 2, c.h, bhl)
    end
    if self.h > 2 then
      canvas:vline(0, 1, self.h - 2, c.v, bhl)
      canvas:vline(self.w - 1, 1, self.h - 2, c.v, bhl)
    end
    if self.title and self.title ~= "" and self.w > 4 then
      local title = " " .. tostring(self.title) .. " "
      title = util.truncate(title, self.w - 4,
          hl.glyph("text", "ellipsis", "…"))
      local tw = util.width(title)
      local tx = 2
      if self.title_align == "center" then
        tx = math.max(2, math.floor((self.w - tw) / 2))
      elseif self.title_align == "right" then
        tx = math.max(2, self.w - 2 - tw)
      end
      canvas:put(tx, 0, title, title_hl)
    end
  end

  for _, child in ipairs(self.children) do
    if child.visible and child.w > 0 and child.h > 0 then
      local cc = canvas:sub(child.x, child.y, child.w, child.h)
      child:render(cc)
    end
  end
end

return Box
