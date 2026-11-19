-- Modal-style popup: bordered window with title, content, optional footer and
-- a right-aligned button row.  App:open_popup() manages stacking and focus.
local container = require("tuipro.container")
local Container = container.Container
local parse_padding = container.parse_padding
local HBox = container.HBox
local Canvas = require("tuipro.canvas")
local Widget = require("tuipro.widget")
local util = require("tuipro.util")
local Button = require("tuipro.ui.button")
local Text = require("tuipro.ui.text")

local Popup = Container:extend("Popup")

local border_chars = {
  rounded = { tl = "╭", tr = "╮", bl = "╰", br = "╯", h = "─", v = "│" },
  single = { tl = "┌", tr = "┐", bl = "└", br = "┘", h = "─", v = "│" },
  double = { tl = "╔", tr = "╗", bl = "╚", br = "╝", h = "═", v = "║" },
  heavy = { tl = "┏", tr = "┓", bl = "┗", br = "┛", h = "━", v = "┃" },
}

function Popup.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Popup)
  Widget.init(self, opts)
  self.children = {}
  self.title = opts.title
  self.border = opts.border ~= false
  self.border_style = opts.border_style or "rounded"
  self.chars = opts.chars or border_chars[self.border_style] or border_chars.rounded
  self.border_hl = opts.border_hl or "PopupBorder"
  self.title_hl = opts.title_hl or "PopupTitle"
  self.fill = opts.fill or "Popup"
  self.footer = opts.footer
  self.footer_hl = opts.footer_hl or "PopupFooter"
  self.footer_align = opts.footer_align or "left"
  self.pad_top, self.pad_right, self.pad_bottom, self.pad_left =
      parse_padding(opts.padding or 1)
  self.min_width = opts.min_width or 30
  self.min_height = opts.min_height or 5
  self.width = opts.width
  self.height = opts.height
  self.focusable = false

  local content = opts.content
  if type(content) == "string" or type(content) == "number" then
    content = Text.new({ text = tostring(content), wrap = true })
  elseif content == nil then
    content = Text.new({ text = opts.message or "" })
  end
  self.content = content
  Container.add(self, content)

  self.buttons = {}
  self.button_box = nil
  if opts.buttons and #opts.buttons > 0 then
    local box = HBox.new({ justify = "end", gap = 1, height = 1 })
    for _, def in ipairs(opts.buttons) do
      local button
      if type(def) == "table" and def.render and not def.label then
        button = def
      else
        if type(def) == "string" then
          def = { label = def }
        end
        local callback = def.on_press
        button = Button.new({
          label = def.label or def.text or "Button",
          primary = def.primary,
          danger = def.danger,
          disabled = def.disabled,
          hotkey = def.hotkey,
          width = def.width,
          brackets = def.brackets,
          focusable = def.focusable,
          on_press = function()
            if callback then
              callback(self)
            else
              self:close()
            end
          end,
        })
      end
      box:add(button)
      self.buttons[#self.buttons + 1] = button
    end
    self.button_box = box
    Container.add(self, box)
  end
  return self
end

function Popup:close()
  if self.app then
    return self.app:close_popup(self)
  end
  self.visible = false
  return false
end

function Popup:set_content(widget)
  if self.content then
    Container.remove(self, self.content)
  end
  if type(widget) == "string" then
    widget = Text.new({ text = widget, wrap = true })
  end
  self.content = widget
  if widget then
    self:add(widget)
  end
  self:invalidate_layout()
  return widget
end

function Popup:add_button(def)
  if not self.button_box then
    self.button_box = HBox.new({ justify = "end", gap = 1, height = 1 })
    Container.add(self, self.button_box)
  end
  local button = Button.new(def)
  self.button_box:add(button)
  self.buttons[#self.buttons + 1] = button
  self:invalidate_layout()
  return button
end

function Popup:layout(w, h)
  w = w or self.w
  h = h or self.h
  local b = self.border and 1 or 0
  local x = self.x + b + self.pad_left
  local y = self.y + b + self.pad_top
  local cw = math.max(0, w - b * 2 - self.pad_left - self.pad_right)
  local ch = math.max(0, h - b * 2 - self.pad_top - self.pad_bottom)
  local content_bottom = y + ch
  local button_y = nil
  if self.button_box then
    button_y = content_bottom - 1
    content_bottom = button_y - 1
  end
  local footer_y = nil
  if self.footer and self.footer ~= "" then
    footer_y = content_bottom - 1
    content_bottom = footer_y
  end
  local content_h = math.max(0, content_bottom - y)
  self.footer_y = footer_y
  if self.content then
    self.content:resize(x, y, cw, content_h)
  end
  if self.button_box then
    self.button_box:resize(x, button_y, cw, 1)
  end
end

function Popup:preferred_width(avail)
  if self.width then
    return self.width
  end
  local b = self.border and 2 or 0
  local inner = self.min_width - b - self.pad_left - self.pad_right
  if self.content then
    inner = math.max(inner, self.content:preferred_width(avail))
  end
  if self.title then
    inner = math.max(inner, util.width(tostring(self.title)) + 4)
  end
  local bw = 0
  for i, button in ipairs(self.buttons) do
    bw = bw + button:preferred_width()
    if i > 1 then
      bw = bw + 1
    end
  end
  inner = math.max(inner, bw)
  local total = inner + b + self.pad_left + self.pad_right
  if avail and total > avail then
    total = avail
  end
  return total
end

function Popup:preferred_height(avail)
  if self.height then
    return self.height
  end
  local b = self.border and 2 or 0
  local inner = self.min_height - b - self.pad_top - self.pad_bottom
  if self.content then
    inner = math.max(inner, self.content:preferred_height(avail))
  end
  if #self.buttons > 0 then
    inner = inner + 2
  end
  if self.footer and self.footer ~= "" then
    inner = inner + 1
  end
  return inner + b + self.pad_top + self.pad_bottom
end

function Popup:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  canvas:fill(0, 0, self.w, self.h, " ", self.fill)
  local c = self.chars
  if self.border and self.w >= 2 and self.h >= 2 then
    canvas:put(0, 0, c.tl, self.border_hl)
    canvas:put(self.w - 1, 0, c.tr, self.border_hl)
    canvas:put(0, self.h - 1, c.bl, self.border_hl)
    canvas:put(self.w - 1, self.h - 1, c.br, self.border_hl)
    if self.w > 2 then
      canvas:hline(1, 0, self.w - 2, c.h, self.border_hl)
      canvas:hline(1, self.h - 1, self.w - 2, c.h, self.border_hl)
    end
    if self.h > 2 then
      canvas:vline(0, 1, self.h - 2, c.v, self.border_hl)
      canvas:vline(self.w - 1, 1, self.h - 2, c.v, self.border_hl)
    end
    if self.title and self.title ~= "" and self.w > 4 then
      local title = util.truncate(" " .. tostring(self.title) .. " ", self.w - 4)
      canvas:put(2, 0, title, self.title_hl)
    end
  end

  for _, child in ipairs(self.children) do
    if child.visible and child.w > 0 and child.h > 0 then
      local cc = canvas:sub(child.x, child.y, child.w, child.h)
      child:render(cc)
    end
  end

  if self.footer and self.footer ~= "" and self.footer_y then
    local text = tostring(self.footer)
    local x = (self.border and 1 or 0) + self.pad_left
    local maxw = math.max(0, self.w - x - (self.border and 1 or 0) - self.pad_right)
    canvas:text(x, self.footer_y - self.y, text, maxw, self.footer_hl, self.footer_align)
  end
end

return Popup
