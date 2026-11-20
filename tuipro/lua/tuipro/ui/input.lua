-- Single-line text input with UTF-8 editing, horizontal scrolling,
-- placeholder/masking and history support.
local Widget = require("tuipro.widget")
local util = require("tuipro.util")
local keys = require("tuipro.keys")
local hl = require("tuipro.hl")

local Input = Widget:extend("Input")

local function filter_text(s)
  s = tostring(s or "")
  s = s:gsub("[\r\n\t]", " ")
  return (s:gsub("[%z\1-\31\127]", ""))
end

local function clip_utf8(s, max_bytes)
  if #s <= max_bytes then
    return s
  end
  local out = {}
  local used = 0
  for ch in util.chars(s) do
    if used + #ch > max_bytes then
      break
    end
    out[#out + 1] = ch
    used = used + #ch
  end
  return table.concat(out)
end

function Input.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Input)
  opts.focusable = opts.focusable ~= false
  Widget.init(self, opts)
  self.text = tostring(opts.value or opts.text or "")
  self.cursor = #self.text + 1
  self.scroll = 0
  self.prompt = opts.prompt or ""
  self.placeholder = opts.placeholder or ""
  self.mask = opts.mask or opts.password
  self.cursor_shape = opts.cursor_shape or opts.cursor
  self.readonly = opts.readonly == true
  self.max_length = opts.max_length
  self.on_change = opts.on_change
  self.on_submit = opts.on_submit
  self.on_cancel = opts.on_cancel
  self.clear_on_submit = opts.clear_on_submit == true
  self.history = opts.history
  self.width = opts.width
  self.height = 1
  self.hl = opts.hl or "Input"
  self.focus_hl = opts.focus_hl or "InputFocus"
  self.placeholder_hl = opts.placeholder_hl or "InputPlaceholder"
  self.prompt_hl = opts.prompt_hl or "InputPrompt"
  self.prompt_focus_hl = opts.prompt_focus_hl or "InputPromptFocus"
  self.disabled_hl = opts.disabled_hl or "InputDisabled"
  self.history_index = self.history and (#self.history + 1) or 1
  self.draft = ""
  self.name = opts.name or "Input"
  return self
end

function Input:get()
  return self.text
end

function Input:set(text, silent)
  self.text = tostring(text or "")
  self.cursor = self.cursor or (#self.text + 1)
  if self.cursor > #self.text + 1 then
    self.cursor = #self.text + 1
  end
  if not silent then
    self:_changed()
  end
  return self
end

function Input:_changed()
  if self.on_change then
    self.on_change(self.text, self)
  end
  self:invalidate()
end

function Input:submit()
  if self.on_submit then
    self.on_submit(self.text, self, self.app)
  end
  if self.clear_on_submit then
    self:set("")
  end
  return true
end

function Input:cancel()
  if self.on_cancel then
    self.on_cancel(self.text, self, self.app)
  end
  return true
end

function Input:_mask_char()
  local mask = self.mask
  if type(mask) == "string" and mask ~= "" then
    return mask
  end
  return hl.glyph("input", "mask", "•")
end

function Input:_display()
  if not self.mask then
    return self.text
  end
  local mask = self:_mask_char()
  local out = {}
  for _ in util.chars(self.text) do
    out[#out + 1] = mask
  end
  return table.concat(out)
end

function Input:_cursor_display_col()
  if self.mask then
    local n = 0
    local i = 1
    while i < self.cursor do
      i = util.next_char_pos(self.text, i)
      n = n + 1
    end
    return n
  end
  return util.display_width_of_prefix(self.text, self.cursor)
end

function Input:_display_col_to_byte(col)
  if col <= 0 then
    return 1
  end
  local i = 1
  local used = 0
  while i <= #self.text do
    local ch = util.char_at(self.text, i)
    local w = self.mask and 1 or util.char_width(ch)
    if used + w > col then
      return i
    end
    used = used + w
    i = util.next_char_pos(self.text, i)
  end
  return #self.text + 1
end

function Input:insert(text)
  if self.readonly then
    return false
  end
  text = filter_text(text)
  if text == "" then
    return false
  end
  if self.max_length then
    local room = self.max_length - #self.text
    if room <= 0 then
      return false
    end
    if #text > room then
      text = clip_utf8(text, room)
    end
  end
  self.text = self.text:sub(1, self.cursor - 1) .. text .. self.text:sub(self.cursor)
  self.cursor = self.cursor + #text
  self:_changed()
  return true
end

function Input:backspace()
  if self.readonly or self.cursor <= 1 then
    return false
  end
  local p = util.prev_char_pos(self.text, self.cursor)
  self.text = self.text:sub(1, p - 1) .. self.text:sub(self.cursor)
  self.cursor = p
  self:_changed()
  return true
end

function Input:delete()
  if self.readonly or self.cursor > #self.text then
    return false
  end
  local p = util.next_char_pos(self.text, self.cursor)
  self.text = self.text:sub(1, self.cursor - 1) .. self.text:sub(p)
  self:_changed()
  return true
end

function Input:delete_word()
  if self.readonly or self.cursor <= 1 then
    return false
  end
  local p = util.prev_word_pos(self.text, self.cursor)
  self.text = self.text:sub(1, p - 1) .. self.text:sub(self.cursor)
  self.cursor = p
  self:_changed()
  return true
end

function Input:_history_step(dir)
  if not self.history or #self.history == 0 then
    return false
  end
  if dir < 0 then
    if self.history_index > 1 then
      if self.history_index > #self.history then
        self.draft = self.text
      end
      self.history_index = self.history_index - 1
      self.text = tostring(self.history[self.history_index] or "")
      self.cursor = #self.text + 1
      self:_changed()
      return true
    end
  else
    if self.history_index <= #self.history then
      self.history_index = self.history_index + 1
      if self.history_index > #self.history then
        self.text = self.draft
      else
        self.text = tostring(self.history[self.history_index] or "")
      end
      self.cursor = #self.text + 1
      self:_changed()
      return true
    end
  end
  return false
end

function Input:on_key(ev)
  local name = keys.name(ev)
  if name == "enter" then
    return self:submit()
  elseif name == "esc" then
    return self:cancel()
  elseif name == "backspace" then
    return self:backspace()
  elseif name == "delete" or name == "C-d" then
    return self:delete()
  elseif name == "left" or name == "C-b" then
    self.cursor = util.prev_char_pos(self.text, self.cursor)
    return true
  elseif name == "right" or name == "C-f" then
    self.cursor = util.next_char_pos(self.text, self.cursor)
    return true
  elseif name == "home" or name == "C-a" then
    self.cursor = 1
    return true
  elseif name == "end" or name == "C-e" then
    self.cursor = #self.text + 1
    return true
  elseif name == "C-u" then
    self.text = self.text:sub(self.cursor)
    self.cursor = 1
    self:_changed()
    return true
  elseif name == "C-k" then
    self.text = self.text:sub(1, self.cursor - 1)
    self:_changed()
    return true
  elseif name == "C-w" then
    return self:delete_word()
  elseif name == "up" then
    return self:_history_step(-1)
  elseif name == "down" then
    return self:_history_step(1)
  elseif name == "tab" then
    return false
  end
  if ev.text and #ev.text > 0 then
    local mods = ev.mods or {}
    if mods.ctrl or mods.alt or mods.super then
      return false
    end
    return self:insert(ev.text)
  end
  return false
end

function Input:on_paste(ev)
  return self:insert(ev.text or "")
end

function Input:on_mouse(ev)
  if ev.action == "press" and ev.button == "left" then
    local prompt_w = util.width(self.prompt)
    local col = ev.x - self.x - prompt_w + self.scroll
    if ev.x - self.x < prompt_w then
      col = 0
    end
    self.cursor = self:_display_col_to_byte(col)
    self:invalidate()
    return true
  end
  return false
end

function Input:preferred_width(avail)
  if self.width then
    return self.width
  end
  local w = util.width(self.prompt) + math.max(12, util.width(self.text) + 1)
  if avail and w > avail then
    w = avail
  end
  return math.max(3, w)
end

function Input:preferred_height()
  return 1
end

function Input:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  local style
  if self.disabled then
    style = self.disabled_hl
  elseif self.focused then
    style = self.focus_hl
  else
    style = self.hl
  end
  canvas:fill(0, 0, self.w, 1, " ", style)
  local prompt = self.prompt or ""
  local pw = util.width(prompt)
  if pw > 0 then
    prompt = util.truncate(prompt, self.w, hl.glyph("text", "ellipsis", "…"))
    pw = util.width(prompt)
    canvas:put(0, 0, prompt, self.focused and self.prompt_focus_hl or self.prompt_hl)
  end
  local avail = math.max(0, self.w - pw)
  if avail <= 0 then
    return
  end
  local display = self:_display()
  local cursor_col = self:_cursor_display_col()
  if cursor_col < self.scroll then
    self.scroll = cursor_col
  end
  if cursor_col - self.scroll >= avail then
    self.scroll = cursor_col - avail + 1
  end
  if self.scroll < 0 then
    self.scroll = 0
  end
  local visible = util.slice_display(display, self.scroll, avail)
  if display == "" and self.placeholder ~= "" then
    canvas:text(pw, 0, self.placeholder, avail, self.placeholder_hl)
    visible = ""
  end
  if visible ~= "" then
    canvas:put(pw, 0, visible, style)
  end
  if self.focused and not self.disabled then
    local cx = pw + cursor_col - self.scroll
    if cx < pw then
      cx = pw
    end
    if cx >= self.w then
      cx = self.w - 1
    end
    self:set_cursor(self.x + cx, self.y, self.cursor_shape or
        hl.glyph("input", "cursor", "bar"))
  end
end

return Input
