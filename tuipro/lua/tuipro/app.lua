-- Application container: owns the event loop, focus, chrome (tabline,
-- statusline, command line), popups, timers and global key bindings.
local core = require("tuipro.core")
local cli = require("tuipro.cli")
local hl = require("tuipro.hl")
local util = require("tuipro.util")
local keys = require("tuipro.keys")
local Canvas = require("tuipro.canvas")
local Container = require("tuipro.container")
local VBox = Container.VBox
local Statusline = require("tuipro.ui.statusline")

local App = {}
App.__index = App

local unpack = unpack or table.unpack

local function first_line(s)
  if not s then
    return "unknown error"
  end
  return tostring(s):match("([^\n]*)") or tostring(s)
end

function App.new(opts)
  opts = opts or {}
  local self = setmetatable({}, App)
  self.title = opts.title or "tuipro"
  self.width = opts.width or cli.width or 80
  self.height = opts.height or cli.height or 24
  self.fps = opts.fps or cli.fps or 60
  self.max_frames = opts.frames or (cli.frames and cli.frames > 0 and cli.frames or nil)
  self.headless = opts.headless
  self.background = opts.background
  if self.background == nil then
    self.background = "Normal"
  end
  -- Chrome styles.  Every group is visible here so applications and themes
  -- can restyle the statusline, mode badges and command line independently
  -- from the widgets in the body.
  self.styles = {
    status = {
      mode = "AppMode",
      command = "AppModeCommand",
      popup = "AppModePopup",
      title = "AppTitle",
      text = "StatusLine",
      info = "AppInfo",
    },
    cmdline = {
      fill = "AppCommandLine",
      prompt = "AppCommandPrompt",
      info = "AppCommandInfo",
      cursor = "AppCommandCursor",
    },
    messages = {
      info = "MessageInfo",
      warn = "MessageWarn",
      error = "MessageError",
      success = "MessageSuccess",
    },
  }
  for section, values in pairs(opts.styles or {}) do
    if self.styles[section] then
      for key, value in pairs(values) do
        self.styles[section][key] = value
      end
    end
  end
  self.show_cmdline = opts.cmdline ~= false
  self.show_statusline = opts.statusline ~= false
  self.footer_hint = opts.footer_hint or "? help   : command   C-q quit"
  self.command_prompt = opts.command_prompt or ":"
  self.cmd_cursor_shape = opts.cmd_cursor_shape
  self.mouse = opts.mouse
  if self.mouse == nil then
    self.mouse = cli.mouse ~= false
  end
  self.truecolor = opts.truecolor
  if self.truecolor == nil then
    self.truecolor = cli.truecolor
  end

  self.running = false
  self.should_quit = false
  self.dirty = true
  self.layout_dirty = true
  self.frames = 0
  self.focus = nil
  self.overlays = {}
  self.timers = {}
  self.bindings = {}
  self.commands = {}
  self.plugins = {}
  self.message = nil
  self.message_history = {}
  self.message_timer = nil
  self.cursor = nil
  self.mode = "normal"
  self.cmd_buffer = ""
  self.cmd_cursor = 1
  self.cmd_scroll = 0
  self.cmd_history = {}
  self.cmd_history_index = 1
  self.pending_prefix = nil
  self.prefix_timer = nil
  self.error_count = 0
  self.last_error = nil
  self._owned_core = false
  self._chrome = { tab_h = 0, body_h = 0, status_h = 0, cmd_h = 0, cmd_y = 0 }

  -- A theme selected with --theme NAME (or opts.theme) loads before the UI is
  -- built.  Highlight groups and glyphs resolve lazily from group names, so
  -- every widget, the chrome and the first frame already use it.
  local theme_name = opts.theme or cli.theme
  if theme_name then
    local _, theme_err = cli.load_theme(theme_name)
    if theme_err then
      io.stderr:write("[tuipro] " .. tostring(theme_err) .. "\n")
      self:error(theme_err)
    end
  end

  self.root = nil
  self:set_body(opts.body or VBox.new({ gap = 0, fill = nil }))
  if opts.children then
    for _, child in ipairs(opts.children) do
      self:add(child)
    end
  end

  self.statusline = nil
  self:set_statusline(opts.statusline)
  self.tabline = nil
  self:set_tabline(opts.tabline)

  self:register_default_commands()
  self:install_default_bindings()
  if opts.bindings then
    for lhs, def in pairs(opts.bindings) do
      if type(def) == "table" then
        self:bind(lhs, def.fn or def[1], def.desc or def[2])
      else
        self:bind(lhs, def)
      end
    end
  end
  if opts.commands then
    for name, def in pairs(opts.commands) do
      if type(def) == "table" then
        self:command(name, def.fn or def[1], def.desc or def[2])
      else
        self:command(name, def)
      end
    end
  end
  if opts.focus then
    self.focus = opts.focus
  end
  return self
end

-- ---------------------------------------------------------------------------
-- Structure
-- ---------------------------------------------------------------------------

function App:set_body(widget)
  if self.root and self.root ~= widget then
    self.root:detach()
  end
  self.root = widget
  widget.parent = nil
  widget:set_app(self)
  self:layout_invalidate()
  return widget
end

function App:add(child, opts)
  if not self.root or not self.root.add then
    error("App root does not accept children")
  end
  local c = self.root:add(child, opts)
  self:layout_invalidate()
  return c
end

function App:set_statusline(widget)
  if widget == false then
    self.statusline = nil
    self:layout_invalidate()
    return
  end
  if widget == nil then
    widget = Statusline.new({
      dynamic = function(_, app)
        return app:status_sections()
      end,
    })
  end
  self.statusline = widget
  widget:set_app(self)
  self:layout_invalidate()
  return widget
end

function App:set_tabline(widget)
  if widget == false then
    self.tabline = nil
    self:layout_invalidate()
    return
  end
  self.tabline = widget
  if widget then
    widget:set_app(self)
  end
  self:layout_invalidate()
  return widget
end

function App:set_title(title)
  self.title = title
  if core.initialized() then
    core.title(title)
  end
  self:invalidate()
end

-- Switch the global palette/groups and redraw.  Theme changes are applied to
-- every existing widget because styles resolve lazily from group names.
function App:set_theme(name, opts)
  local def, err = hl.set_theme(name, opts)
  if not def then
    return nil, err
  end
  self:layout_invalidate()
  return def
end

function App:status_sections()
  local style = self.styles.status
  local mode = "NORMAL"
  local mode_hl = style.mode
  if self.mode == "command" then
    mode = "COMMAND"
    mode_hl = style.command
  elseif #self.overlays > 0 then
    mode = "POPUP"
    mode_hl = style.popup
  end
  local left = {
    { text = " " .. mode .. " ", hl = mode_hl },
    { text = " tuipro ", hl = style.title },
    { text = self.title or "", hl = style.text },
  }
  if self.focus then
    local label = self.focus:get_label()
    if label then
      left[#left + 1] = { text = "  " .. label, hl = style.info }
    end
  end
  if #self.overlays > 0 then
    left[#left + 1] = { text = "  modal", hl = style.info }
  end
  local right = {
    { text = (self.footer_hint or "") .. " ", hl = style.info },
  }
  return { left = left, right = right }
end

-- ---------------------------------------------------------------------------
-- Invalidation, timers, errors
-- ---------------------------------------------------------------------------

function App:invalidate()
  self.dirty = true
end

function App:layout_invalidate()
  self.layout_dirty = true
  self.dirty = true
end

function App:set_cursor(x, y, shape)
  self.cursor = { x = x, y = y, shape = shape or "block" }
end

function App:after(ms, fn)
  local timer = {
    deadline = util.now() + math.max(0, ms or 0),
    fn = fn,
    active = true,
    repeat_ms = nil,
  }
  self.timers[#self.timers + 1] = timer
  return timer
end

function App:every(ms, fn)
  ms = math.max(1, ms or 1000)
  local timer = {
    deadline = util.now() + ms,
    fn = fn,
    active = true,
    repeat_ms = ms,
  }
  self.timers[#self.timers + 1] = timer
  return timer
end

function App:cancel(timer)
  if timer then
    timer.active = false
  end
end

function App:_next_timer()
  local best = nil
  local now = util.now()
  for _, t in ipairs(self.timers) do
    if t.active then
      local d = t.deadline - now
      if d < 0 then
        d = 0
      end
      if best == nil or d < best then
        best = d
      end
    end
  end
  return best
end

function App:_process_timers()
  local now = util.now()
  local due = {}
  for _, t in ipairs(self.timers) do
    if t.active and t.deadline <= now then
      due[#due + 1] = t
    end
  end
  for _, t in ipairs(due) do
    if t.active then
      if t.repeat_ms then
        t.deadline = now + t.repeat_ms
      else
        t.active = false
      end
      self:protect(t.fn)
    end
  end
  local live = {}
  for _, t in ipairs(self.timers) do
    if t.active then
      live[#live + 1] = t
    end
  end
  self.timers = live
end

function App:protect(fn, ...)
  if not fn then
    return true
  end
  local args = { ... }
  local ok, err = xpcall(function()
    return fn(unpack(args))
  end, function(e)
    return debug.traceback(tostring(e), 2)
  end)
  if not ok then
    self:report_error(err)
  end
  return ok
end

function App:report_error(err)
  self.error_count = self.error_count + 1
  self.last_error = tostring(err)
  io.stderr:write("[tuipro] " .. first_line(self.last_error) .. "\n")
  if self.on_error then
    pcall(self.on_error, self, err)
  end
  self:error("Error: " .. first_line(self.last_error), 8000)
  if self.error_count > 40 then
    self.should_quit = true
  end
end

-- ---------------------------------------------------------------------------
-- Messages
-- ---------------------------------------------------------------------------

function App:set_message(text, level, timeout)
  level = level or "info"
  text = tostring(text or "")
  self.message = {
    text = text,
    level = level,
    expires = timeout and (util.now() + timeout) or nil,
  }
  self.message_history[#self.message_history + 1] = {
    text = text,
    level = level,
    time = os.date("%H:%M:%S"),
  }
  if #self.message_history > 100 then
    table.remove(self.message_history, 1)
  end
  if self.message_timer then
    self.message_timer.active = false
    self.message_timer = nil
  end
  if timeout and timeout > 0 then
    self.message_timer = self:after(timeout, function()
      if self.message and self.message.expires and util.now() >= self.message.expires then
        self.message = nil
        self.message_timer = nil
        self:invalidate()
      end
    end)
  end
  self:invalidate()
end

function App:info(text, timeout)
  self:set_message(text, "info", timeout or 4000)
end

function App:warn(text, timeout)
  self:set_message(text, "warn", timeout or 6000)
end

function App:error(text, timeout)
  self:set_message(text, "error", timeout or 8000)
end

function App:success(text, timeout)
  self:set_message(text, "success", timeout or 4000)
end

function App:clear_message()
  self.message = nil
  if self.message_timer then
    self.message_timer.active = false
    self.message_timer = nil
  end
  self:invalidate()
end

function App:message_style()
  if not self.message then
    return self.styles.cmdline.info
  end
  return self.styles.messages[self.message.level] or self.styles.messages.info
end

-- ---------------------------------------------------------------------------
-- Bindings and commands
-- ---------------------------------------------------------------------------

function App:bind(lhs, fn, desc)
  local parsed = keys.parse(lhs)
  for _, b in ipairs(self.bindings) do
    if b.name == parsed.name then
      b.fn = fn
      b.desc = desc
      return b
    end
  end
  local b = { lhs = lhs, name = parsed.name, fn = fn, desc = desc }
  self.bindings[#self.bindings + 1] = b
  return b
end

function App:unbind(lhs)
  local name = keys.parse(lhs).name
  for i, b in ipairs(self.bindings) do
    if b.name == name then
      table.remove(self.bindings, i)
      return true
    end
  end
  return false
end

function App:command(name, fn, desc)
  self.commands[name] = { name = name, fn = fn, desc = desc }
  return self.commands[name]
end

function App:install_default_bindings()
  self:bind("C-q", function(app)
    app:quit()
  end, "Quit")
  self:bind("F1", function(app)
    app:show_help()
  end, "Help")
  self:bind("?", function(app)
    app:show_help()
  end, "Help")
  self:bind(":", function(app)
    app:enter_command(":")
  end, "Command line")
  self:bind("C-l", function(app)
    app:invalidate()
  end, "Redraw")
  self:bind("esc", function(app)
    if #app.overlays > 0 then
      app:close_popup()
    elseif app.message then
      app:clear_message()
    end
  end, "Close popup / clear message")
end

function App:register_default_commands()
  self:command("q", function(_, app)
    app:quit()
  end, "Quit")
  self:command("quit", function(_, app)
    app:quit()
  end, "Quit")
  self:command("qa", function(_, app)
    app:quit()
  end, "Quit")
  self:command("qall", function(_, app)
    app:quit()
  end, "Quit")
  self:command("help", function(_, app)
    app:show_help()
  end, "Show key bindings and commands")
  self:command("echo", function(arg, app)
    app:info(arg ~= "" and arg or " ")
  end, "Echo a message")
  self:command("redraw", function(_, app)
    app:layout_invalidate()
  end, "Redraw the screen")
  self:command("version", function(_, app)
    app:info("tuipro " .. tostring(core.version) .. " / LuaJIT")
  end, "Show version")
  self:command("messages", function(_, app)
    app:show_messages()
  end, "Show message history")
  self:command("commands", function(_, app)
    app:show_help("commands")
  end, "List commands")
end

function App:enter_command(prompt)
  self.mode = "command"
  self.command_prompt = prompt or ":"
  self.cmd_buffer = ""
  self.cmd_cursor = 1
  self.cmd_scroll = 0
  self.cmd_history_index = #self.cmd_history + 1
  self:invalidate()
end

function App:exit_command()
  self.mode = "normal"
  self.cmd_buffer = ""
  self.cmd_cursor = 1
  self.cmd_scroll = 0
  self:invalidate()
end

function App:execute_command(line)
  local name, arg = tostring(line or ""):match("^%s*(%S+)%s*(.*)$")
  if not name then
    return
  end
  local cmd = self.commands[name]
  if not cmd then
    self:error("E492: Not a command: " .. name)
    return
  end
  local ok, res = pcall(cmd.fn, arg, self)
  if not ok then
    self:error("Error: " .. first_line(res))
    io.stderr:write("[tuipro] command " .. name .. ": " .. tostring(res) .. "\n")
    return
  end
  if type(res) == "string" and res ~= "" then
    self:info(res)
  end
end

function App:_command_key(ev)
  local name = keys.name(ev)
  if name == "C-q" then
    self:quit()
    return
  end
  if name == "esc" then
    self:exit_command()
    return
  end
  if name == "enter" then
    local line = self.cmd_buffer
    if line ~= "" then
      self.cmd_history[#self.cmd_history + 1] = line
    end
    self:exit_command()
    self:execute_command(line)
    return
  end
  if name == "backspace" then
    if self.cmd_cursor > 1 then
      local p = util.prev_char_pos(self.cmd_buffer, self.cmd_cursor)
      self.cmd_buffer = self.cmd_buffer:sub(1, p - 1) .. self.cmd_buffer:sub(self.cmd_cursor)
      self.cmd_cursor = p
    end
    return
  end
  if name == "delete" or name == "C-d" then
    if self.cmd_cursor <= #self.cmd_buffer then
      local p = util.next_char_pos(self.cmd_buffer, self.cmd_cursor)
      self.cmd_buffer = self.cmd_buffer:sub(1, self.cmd_cursor - 1) .. self.cmd_buffer:sub(p)
    end
    return
  end
  if name == "left" or name == "C-b" then
    self.cmd_cursor = util.prev_char_pos(self.cmd_buffer, self.cmd_cursor)
    return
  elseif name == "right" or name == "C-f" then
    self.cmd_cursor = util.next_char_pos(self.cmd_buffer, self.cmd_cursor)
    return
  elseif name == "home" or name == "C-a" then
    self.cmd_cursor = 1
    return
  elseif name == "end" or name == "C-e" then
    self.cmd_cursor = #self.cmd_buffer + 1
    return
  elseif name == "C-u" then
    self.cmd_buffer = self.cmd_buffer:sub(self.cmd_cursor)
    self.cmd_cursor = 1
    return
  elseif name == "C-k" then
    self.cmd_buffer = self.cmd_buffer:sub(1, self.cmd_cursor - 1)
    return
  elseif name == "C-w" then
    local p = util.prev_word_pos(self.cmd_buffer, self.cmd_cursor)
    self.cmd_buffer = self.cmd_buffer:sub(1, p - 1) .. self.cmd_buffer:sub(self.cmd_cursor)
    self.cmd_cursor = p
    return
  elseif name == "up" then
    if self.cmd_history_index > 1 then
      self.cmd_history_index = self.cmd_history_index - 1
      self.cmd_buffer = self.cmd_history[self.cmd_history_index] or ""
      self.cmd_cursor = #self.cmd_buffer + 1
    end
    return
  elseif name == "down" then
    if self.cmd_history_index < #self.cmd_history then
      self.cmd_history_index = self.cmd_history_index + 1
      self.cmd_buffer = self.cmd_history[self.cmd_history_index] or ""
    else
      self.cmd_history_index = #self.cmd_history + 1
      self.cmd_buffer = ""
    end
    self.cmd_cursor = #self.cmd_buffer + 1
    return
  elseif name == "tab" then
    self:_complete_command()
    return
  end
  if ev.text and #ev.text > 0 then
    local mods = ev.mods or {}
    if mods.ctrl or mods.alt or mods.super then
      return
    end
    local text = ev.text:gsub("[%z\1-\31\127]", " ")
    self.cmd_buffer = self.cmd_buffer:sub(1, self.cmd_cursor - 1) .. text ..
        self.cmd_buffer:sub(self.cmd_cursor)
    self.cmd_cursor = self.cmd_cursor + #text
  end
end

function App:_complete_command()
  local prefix = self.cmd_buffer
  local matches = {}
  for name in pairs(self.commands) do
    if name:sub(1, #prefix) == prefix then
      matches[#matches + 1] = name
    end
  end
  table.sort(matches)
  if #matches == 1 then
    self.cmd_buffer = matches[1]
    self.cmd_cursor = #self.cmd_buffer + 1
  elseif #matches > 1 then
    self:info("Commands: " .. table.concat(matches, "  "), 5000)
  end
end

-- ---------------------------------------------------------------------------
-- Focus
-- ---------------------------------------------------------------------------

function App:current_scope()
  if #self.overlays > 0 then
    return self.overlays[#self.overlays].widget
  end
  return self.root
end

function App:focusables(scope)
  scope = scope or self:current_scope()
  if not scope then
    return {}
  end
  local list = scope:find_focusables()
  return list
end

function App:ensure_focus()
  local scope = self:current_scope()
  local list = self:focusables(scope)
  if #list == 0 then
    if self.focus then
      self.focus = nil
    end
    return
  end
  for _, w in ipairs(list) do
    if w == self.focus then
      return
    end
  end
  self:set_focus(list[1])
end

function App:set_focus(widget)
  if widget == self.focus then
    return
  end
  local old = self.focus
  if old then
    old.focused = false
    local p = old
    while p do
      p.contains_focus = false
      p = p.parent
    end
    if old.on_blur then
      old:on_blur()
    end
  end
  self.focus = widget
  if widget then
    widget.focused = true
    local p = widget
    while p do
      p.contains_focus = true
      p = p.parent
    end
    if widget.on_focus then
      widget:on_focus()
    end
  end
  self:invalidate()
end

function App:focus_next(dir)
  local list = self:focusables()
  if #list == 0 then
    return nil
  end
  local idx = nil
  for i, w in ipairs(list) do
    if w == self.focus then
      idx = i
      break
    end
  end
  local next_idx
  if idx then
    next_idx = ((idx - 1 + (dir or 1)) % #list) + 1
  else
    next_idx = dir and dir < 0 and #list or 1
  end
  self:set_focus(list[next_idx])
  return list[next_idx]
end

function App:focus_direction(dir)
  local list = self:focusables()
  if #list == 0 then
    return nil
  end
  local cur = self.focus
  if not cur then
    self:set_focus(list[1])
    return list[1]
  end
  local cx = cur.x + cur.w / 2
  local cy = cur.y + cur.h / 2
  local best, best_score = nil, nil
  for _, w in ipairs(list) do
    if w ~= cur then
      local wx = w.x + w.w / 2
      local wy = w.y + w.h / 2
      local dx, dy = wx - cx, wy - cy
      local primary, cross, ok = nil, nil, false
      if dir == "left" and dx < 0 then
        primary, cross, ok = -dx, math.abs(dy), true
      elseif dir == "right" and dx > 0 then
        primary, cross, ok = dx, math.abs(dy), true
      elseif dir == "up" and dy < 0 then
        primary, cross, ok = -dy, math.abs(dx), true
      elseif dir == "down" and dy > 0 then
        primary, cross, ok = dy, math.abs(dx), true
      end
      if ok then
        local score = primary + cross * 2
        if not best_score or score < best_score then
          best, best_score = w, score
        end
      end
    end
  end
  if best then
    self:set_focus(best)
  end
  return best
end

function App:clear_prefix()
  self.pending_prefix = nil
  if self.prefix_timer then
    self.prefix_timer.active = false
    self.prefix_timer = nil
  end
end

function App:start_window_prefix()
  self.pending_prefix = "C-w"
  if self.prefix_timer then
    self.prefix_timer.active = false
  end
  self.prefix_timer = self:after(900, function()
    self.pending_prefix = nil
    self.prefix_timer = nil
    self:invalidate()
  end)
  self:invalidate()
end

function App:_window_key(ev)
  local name = keys.name(ev)
  local dir = ({ h = "left", j = "down", k = "up", l = "right" })[name]
  if dir then
    self:focus_direction(dir)
    return true
  end
  if name == "w" then
    self:focus_next(1)
    return true
  end
  if name == "C-w" then
    self:clear_prefix()
    self:start_window_prefix()
    return true
  end
  return false
end

-- ---------------------------------------------------------------------------
-- Event dispatch
-- ---------------------------------------------------------------------------

function App:handle_event(ev)
  self:_dispatch(ev)
  self:invalidate()
  return true
end

function App:_dispatch(ev)
  if ev.type == "resize" then
    self.width = ev.width
    self.height = ev.height
    self:layout_invalidate()
    return
  elseif ev.type == "signal" then
    self.should_quit = true
    return
  elseif ev.type == "focus" then
    self:invalidate()
    return
  elseif ev.type == "key" then
    self:handle_key(ev)
  elseif ev.type == "mouse" then
    self:handle_mouse(ev)
  elseif ev.type == "paste" then
    self:handle_paste(ev)
  end
end

function App:_dispatch_to_focus(ev)
  local w = self.focus
  while w do
    if w:handle(ev) then
      return true
    end
    w = w.parent
  end
  return false
end

function App:handle_key(ev)
  if self.mode == "command" then
    self:_command_key(ev)
    self:invalidate()
    return true
  end

  if #self.overlays > 0 and keys.name(ev) == "esc" then
    local top = self.overlays[#self.overlays]
    if top.close_on_esc then
      self:close_popup(top.widget)
      return true
    end
  end

  if self.pending_prefix then
    if self:_window_key(ev) then
      self:clear_prefix()
      self:invalidate()
      return true
    end
    self:clear_prefix()
  end

  if self:_dispatch_to_focus(ev) then
    self:invalidate()
    return true
  end

  local name = keys.name(ev)
  if name == "C-w" then
    self:start_window_prefix()
    return true
  end

  for _, b in ipairs(self.bindings) do
    if b.name == name then
      if b.fn then
        self:protect(b.fn, self, ev)
      end
      self:invalidate()
      return true
    end
  end

  if name == "tab" then
    self:focus_next(1)
    self:invalidate()
    return true
  elseif name == "S-tab" then
    self:focus_next(-1)
    self:invalidate()
    return true
  end
  return false
end

function App:_focus_ancestor(widget)
  local w = widget
  while w and not w:is_focusable() do
    w = w.parent
  end
  return w
end

function App:_bubble(widget, ev, stop)
  local w = widget
  while w do
    if w:handle(ev) then
      return true
    end
    if w == stop then
      break
    end
    w = w.parent
  end
  return false
end

function App:handle_mouse(ev)
  local x, y = ev.x, ev.y
  if #self.overlays > 0 then
    local top = self.overlays[#self.overlays]
    local hit = top.widget:find_widget_at(x, y)
    if hit then
      local target = self:_focus_ancestor(hit)
      if target and ev.action == "press" then
        self:set_focus(target)
      end
      self:_bubble(hit, ev, top.widget)
    else
      if top.close_on_click_outside and ev.action == "press" then
        self:close_popup(top.widget)
      end
    end
    self:invalidate()
    return true
  end

  local tab_h = self._chrome.tab_h or 0
  local hit = nil
  if self.tabline and y < tab_h then
    hit = self.tabline:find_widget_at(x, y)
  elseif self.statusline and y == self._chrome.status_y then
    hit = self.statusline:find_widget_at(x, y)
  elseif y == self._chrome.cmd_y then
    if self.mode == "command" then
      local rel = math.max(0, x - 2)
      self.cmd_cursor = math.min(#self.cmd_buffer + 1, #self.cmd_buffer + 1)
      self:invalidate()
    end
    return true
  else
    hit = self.root:find_widget_at(x, y)
  end

  if hit then
    local target = self:_focus_ancestor(hit)
    if target and ev.action == "press" then
      self:set_focus(target)
    end
    self:_bubble(hit, ev)
  end
  self:invalidate()
  return true
end

function App:handle_paste(ev)
  if self.mode == "command" then
    local text = tostring(ev.text or ""):gsub("[\r\n\t]", " "):gsub("[%z\1-\31\127]", "")
    self.cmd_buffer = self.cmd_buffer:sub(1, self.cmd_cursor - 1) .. text ..
        self.cmd_buffer:sub(self.cmd_cursor)
    self.cmd_cursor = self.cmd_cursor + #text
    self:invalidate()
    return
  end
  if self:_dispatch_to_focus(ev) then
    self:invalidate()
  end
end

-- ---------------------------------------------------------------------------
-- Popups
-- ---------------------------------------------------------------------------

function App:open_popup(widget, opts)
  opts = opts or {}
  local overlay = {
    widget = widget,
    modal = opts.modal ~= false,
    close_on_esc = opts.close_on_esc ~= false,
    close_on_click_outside = opts.close_on_click_outside == true,
    width = opts.width or widget.width,
    height = opts.height or widget.height,
    x = opts.x,
    y = opts.y,
    focus_before = self.focus,
    on_close = opts.on_close,
    focus = opts.focus,
  }
  widget.parent = nil
  widget:set_app(self)
  self.overlays[#self.overlays + 1] = overlay
  self:layout_invalidate()
  -- Focus will be finalized during the next layout/render; choose a sensible
  -- widget now so key events work even before the first frame.
  local target = overlay.focus
  if not target then
    target = widget:find_focusables()[1]
  end
  if target then
    self:set_focus(target)
  end
  self:invalidate()
  return overlay
end

function App:close_popup(widget)
  if #self.overlays == 0 then
    return false
  end
  local idx = #self.overlays
  if widget then
    for i, o in ipairs(self.overlays) do
      if o.widget == widget then
        idx = i
        break
      end
    end
  end
  local overlay = table.remove(self.overlays, idx)
  if not overlay then
    return false
  end
  overlay.widget:detach()
  if overlay.on_close then
    self:protect(overlay.on_close, overlay.widget)
  end
  local restore = overlay.focus_before
  if restore and restore.app == self and restore.visible and restore:is_focusable() then
    self:set_focus(restore)
  elseif #self.overlays > 0 then
    local top = self.overlays[#self.overlays].widget
    local f = top:find_focusables()[1]
    self:set_focus(f)
  else
    self:ensure_focus()
  end
  self:layout_invalidate()
  self:invalidate()
  return true
end

function App:close_all_popups()
  while #self.overlays > 0 do
    self:close_popup()
  end
end

-- ---------------------------------------------------------------------------
-- Help and messages overlays
-- ---------------------------------------------------------------------------

function App:show_help(filter)
  local ui = require("tuipro.ui")
  local items = {}
  if not filter or filter == "keys" then
    items[#items + 1] = { header = true, label = "Key bindings" }
    for _, b in ipairs(self.bindings) do
      if b.desc then
        items[#items + 1] = { label = keys.pretty(b.name), detail = b.desc }
      end
    end
    items[#items + 1] = { label = "C-w h/j/k/l", detail = "Move focus between panes" }
    items[#items + 1] = { label = "Tab / S-Tab", detail = "Cycle focus" }
  end
  if not filter or filter == "commands" then
    items[#items + 1] = { header = true, label = "Commands" }
    local names = {}
    for name in pairs(self.commands) do
      names[#names + 1] = name
    end
    table.sort(names)
    for _, name in ipairs(names) do
      local c = self.commands[name]
      if c.desc then
        items[#items + 1] = { label = ":" .. name, detail = c.desc }
      end
    end
  end
  local list = ui.list({
    items = items,
    flex = 1,
    show_index = false,
    marker = " ",
    show_scrollbar = true,
    name = "Help",
  })
  local popup = ui.popup({
    title = "Help",
    content = list,
    width = math.min(math.max(46, self.width - 8), 84),
    height = math.min(math.max(10, self.height - 4), 26),
    buttons = {
      {
        label = "Close",
        primary = true,
        on_press = function(p)
          p:close()
        end,
      },
    },
  })
  self:open_popup(popup, { close_on_esc = true, width = popup.width, height = popup.height })
end

function App:show_messages()
  local ui = require("tuipro.ui")
  local items = {}
  for _, m in ipairs(self.message_history) do
    items[#items + 1] = { label = m.time, detail = m.text, hl = m.level == "error" and "Error" or nil }
  end
  if #items == 0 then
    items[1] = { label = "No messages", disabled = true }
  end
  local list = ui.list({
    items = items,
    flex = 1,
    show_index = false,
    marker = " ",
    detail_align = "left",
  })
  local popup = ui.popup({
    title = "Messages",
    content = list,
    width = math.min(72, self.width - 4),
    height = math.min(18, self.height - 4),
    buttons = {
      { label = "Close", primary = true, on_press = function(p) p:close() end },
    },
  })
  self:open_popup(popup, { close_on_esc = true })
end

function App:quit()
  self.should_quit = true
end

-- ---------------------------------------------------------------------------
-- Layout and rendering
-- ---------------------------------------------------------------------------

function App:layout(width, height)
  if width then
    self.width = width
  end
  if height then
    self.height = height
  end
  self:_layout_now()
end

function App:_layout_now()
  local W, H = self.width, self.height
  if W < 1 then W = 1 end
  if H < 1 then H = 1 end
  local tab_h = (self.tabline and self.tabline.visible and H >= 6) and 1 or 0
  local status_h = (self.statusline and H >= 4) and 1 or 0
  local cmd_h = (self.show_cmdline and H >= 2) and 1 or 0
  local body_h = H - tab_h - status_h - cmd_h
  if body_h < 1 then
    body_h = 1
    if tab_h > 0 and H - body_h - status_h - cmd_h < 0 then
      tab_h = 0
    end
    if status_h > 0 and H - body_h - cmd_h - tab_h < 0 then
      status_h = 0
    end
  end
  local y = tab_h

  if self.tabline and tab_h > 0 then
    self.tabline:resize(0, 0, W, tab_h)
  end
  self.root:resize(0, y, W, body_h)
  y = y + body_h
  local status_y = y
  if self.statusline and status_h > 0 then
    self.statusline:resize(0, y, W, status_h)
    y = y + status_h
  end
  local cmd_y = y
  self._chrome = {
    tab_h = tab_h,
    body_h = body_h,
    body_y = tab_h,
    status_h = status_h,
    status_y = status_y,
    cmd_h = cmd_h,
    cmd_y = cmd_y,
  }

  -- Overlays are centered on each layout pass.
  for _, o in ipairs(self.overlays) do
    local w = o.width
    local h = o.height
    if not w then
      w = math.min(W - 2, math.max(20, o.widget:preferred_width(W)))
    end
    if not h then
      h = math.min(H - 2, math.max(6, o.widget:preferred_height(W)))
    end
    w = util.clamp(w, 1, math.max(1, W - 2))
    h = util.clamp(h, 1, math.max(1, H - 2))
    local x = o.x or math.floor((W - w) / 2)
    local y2 = o.y or math.floor((H - h) / 2)
    o.widget:resize(x, y2, w, h)
  end
  self.layout_dirty = false
end

function App:_init_core()
  if core.initialized() then
    return
  end
  local headless = self.headless
  if headless == nil then
    headless = cli.headless
  end
  local init = {
    headless = headless,
    truecolor = self.truecolor,
    mouse = self.mouse,
    paste = true,
    title = self.title,
  }
  if headless then
    init.width = self.width
    init.height = self.height
  end
  core.init(init)
  self._owned_core = true
  local w, h = core.size()
  self.width, self.height = w, h
  self.layout_dirty = true
end

function App:render()
  self:_init_core()
  local w, h = core.size()
  if w ~= self.width or h ~= self.height then
    self.width, self.height = w, h
    self.layout_dirty = true
  end
  self:_layout_now()
  self:ensure_focus()
  self.cursor = nil

  core.clear()
  local main = Canvas.new(0, 0, self.width, self.height)
  if self.background then
    local _, bg = hl.get(self.background)
    if bg ~= hl.DEFAULT then
      main:fill(0, 0, self.width, self.height, " ", self.background)
    end
  end

  if self.tabline and self.tabline.visible and self._chrome.tab_h > 0 then
    local c = main:sub(self.tabline.x, self.tabline.y, self.tabline.w, self.tabline.h)
    self.tabline:render(c)
  end

  local root_canvas = main:sub(self.root.x, self.root.y, self.root.w, self.root.h)
  self.root:render(root_canvas)

  if self.statusline and self._chrome.status_h > 0 then
    local c = main:sub(self.statusline.x, self.statusline.y, self.statusline.w, self.statusline.h)
    self.statusline:render(c)
  end

  self:_render_commandline(main)

  for _, o in ipairs(self.overlays) do
    if o.widget.visible then
      local c = main:sub(o.widget.x, o.widget.y, o.widget.w, o.widget.h)
      o.widget:render(c)
    end
  end

  if self.cursor then
    core.cursor(self.cursor.x, self.cursor.y, true, self.cursor.shape or "block")
  else
    core.cursor(-1, -1, false)
  end
  core.flush()
  self.dirty = false
  self.frames = self.frames + 1
  return self
end

function App:_render_commandline(canvas)
  local row = self._chrome.cmd_y
  if self._chrome.cmd_h <= 0 then
    return
  end
  local styles = self.styles.cmdline
  canvas:fill(0, row, self.width, 1, " ", styles.fill)
  if self.mode == "command" then
    local prompt = self.command_prompt or ":"
    canvas:put(0, row, prompt, styles.prompt)
    local avail = math.max(1, self.width - 2)
    local prefix_w = util.display_width_of_prefix(self.cmd_buffer, self.cmd_cursor)
    if prefix_w - self.cmd_scroll >= avail then
      self.cmd_scroll = prefix_w - avail + 1
    end
    if prefix_w < self.cmd_scroll then
      self.cmd_scroll = 0
    end
    local visible = util.slice_display(self.cmd_buffer, self.cmd_scroll, avail)
    canvas:put(1, row, visible, styles.fill)
    local cx = 1 + prefix_w - self.cmd_scroll
    cx = util.clamp(cx, 0, self.width - 1)
    self:set_cursor(cx, row, self.cmd_cursor_shape or
        hl.glyph("input", "cursor", "bar"))
    return
  end
  if self.message and (not self.message.expires or util.now() < self.message.expires) then
    local text = self.message.text:gsub("\n.*$", "")
    canvas:put(1, row,
        util.truncate(text, self.width - 2, hl.glyph("text", "ellipsis", "…")),
        self:message_style())
  else
    canvas:put(1, row,
        util.truncate(self.footer_hint or "", self.width - 2,
            hl.glyph("text", "ellipsis", "…")),
        styles.info)
  end
end

-- ---------------------------------------------------------------------------
-- Main loop
-- ---------------------------------------------------------------------------

function App:_frame_limit_reached()
  return self.max_frames ~= nil and self.frames >= self.max_frames
end

function App:_loop()
  self:ensure_focus()
  local interval = 1000 / math.max(1, self.fps)
  self:render()
  if self:_frame_limit_reached() then
    return
  end
  local last_render = util.now()
  while not self.should_quit do
    local now = util.now()
    local timeout = 1000
    local next_timer = self:_next_timer()
    if next_timer and next_timer < timeout then
      timeout = next_timer
    end
    if self.dirty then
      local wait = interval - (now - last_render)
      if wait < 0 then
        wait = 0
      end
      if wait < timeout then
        timeout = wait
      end
    end
    local ev = core.poll(math.floor(timeout))
    if ev then
      self:_dispatch(ev)
    end
    self:_process_timers()
    now = util.now()
    if self.dirty and (now - last_render) >= interval then
      self:render()
      last_render = now
      if self:_frame_limit_reached() then
        return
      end
    end
    if core.should_exit() then
      self.should_quit = true
    end
  end
end

function App:run()
  if self.running then
    error("application is already running")
  end
  self.running = true
  self.should_quit = false
  self.frames = 0
  self:_init_core()
  local ok, err = xpcall(function()
    self:_loop()
  end, function(e)
    return debug.traceback(tostring(e), 2)
  end)
  self.running = false
  self:shutdown()
  if not ok then
    error(err, 0)
  end
  return self
end

function App:shutdown()
  for _, t in ipairs(self.timers) do
    t.active = false
  end
  if self._owned_core then
    core.shutdown()
    self._owned_core = false
  end
end

function App:resize(w, h)
  self.width = w
  self.height = h
  if core.initialized() then
    local info = core.info()
    if info.width ~= w or info.height ~= h then
      core.resize(w, h)
    end
  end
  self:layout_invalidate()
end

return App
