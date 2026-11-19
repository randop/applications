-- Built-in demo dashboard.  Run with: tuipro --demo
--
-- It exercises containers, boxes, menus, lists, tables, inputs, buttons,
-- progress bars, spinners, tabs, popups, focus movement, mouse input and the
-- command line.
local t = require("tuipro")
local ui = t.ui
local core = t.core

local M = {}

local pages = {
  { title = "Dashboard", badge = nil },
  { title = "Tasks" },
  { title = "Widgets" },
  { title = "Inputs" },
  { title = "Dialogs" },
}

local page_titles = { "Dashboard", "Tasks", "Widgets", "Inputs", "Dialogs" }

-- ---------------------------------------------------------------------------
-- Picture: a small half-block "photo" renderer used by the JPG dialog.
-- Terminals cannot decode JPEG data, so the dialog shows a deterministic
-- block-art preview plus file metadata.
-- ---------------------------------------------------------------------------

local function lerp(a, b, k)
  return a + (b - a) * k
end

local function clamp8(v)
  return math.max(0, math.min(255, math.floor(v + 0.5)))
end

local function mix(c1, c2, k)
  return { lerp(c1[1], c2[1], k), lerp(c1[2], c2[2], k), lerp(c1[3], c2[3], k) }
end

local function rgb(c)
  return clamp8(c[1]) * 65536 + clamp8(c[2]) * 256 + clamp8(c[3])
end

local function noise01(seed)
  local n = math.sin(seed * 12.9898 + 78.233) * 43758.5453
  return n - math.floor(n)
end

-- Deterministic sunset: sky gradient, sun with glow, mountain silhouettes and
-- a reflected path on the water.  Returns rows of 0xRRGGBB pixels.
local function build_example_photo(w, h)
  local pixels = {}
  local horizon = math.floor(h * 0.6 + 0.5)
  local sun_x, sun_y = w * 0.68, horizon * 0.45
  local sun_r = math.max(2, math.min(w, h) * 0.13)
  for y = 0, h - 1 do
    local row = {}
    for x = 0, w - 1 do
      local col
      if y < horizon then
        local k = y / math.max(1, horizon)
        col = mix({ 18, 16, 52 }, { 252, 148, 66 }, k ^ 1.7)
        local dx, dy = x - sun_x, y - sun_y
        local d = math.sqrt(dx * dx + dy * dy)
        local glow = math.max(0, 1 - d / (sun_r * 4.5))
        col[1] = col[1] + 210 * glow * glow
        col[2] = col[2] + 150 * glow * glow
        col[3] = col[3] + 40 * glow * glow
        if d < sun_r then
          col = mix(col, { 255, 238, 170 }, 0.85)
        end
        if noise01(x * 0.35 + y * 1.7) > 0.965 then
          col = mix(col, { 255, 210, 190 }, 0.35)
        end
        local ridge = 3 + 5 * noise01(x * 0.08) + 4 * noise01(x * 0.021 + 9)
        local ridge2 = 8 + 7 * noise01(x * 0.05 + 3)
        if y >= horizon - ridge2 then
          col = mix(col, { 48, 34, 76 }, 0.75)
        end
        if y >= horizon - ridge then
          col = mix(col, { 26, 20, 46 }, 0.85)
        end
      else
        local k = (y - horizon) / math.max(1, h - 1 - horizon)
        col = mix({ 92, 74, 128 }, { 14, 16, 38 }, k ^ 0.8)
        local band = math.max(0, 1 - math.abs(x - sun_x) / (sun_r * 2.2))
        local wobble = noise01(y * 3.1 + x * 0.7)
        if band > 0 and wobble > 0.35 then
          col = mix(col, { 255, 196, 120 }, band * (1 - k) * (0.5 + 0.5 * wobble) * 0.8)
        end
        if math.abs(x - sun_x) < sun_r * 3 and noise01(y * 9.7) > 0.8 then
          col = mix(col, { 220, 170, 160 }, 0.12)
        end
      end
      row[#row + 1] = rgb(col)
    end
    pixels[#pixels + 1] = row
  end
  return pixels
end

local EXAMPLE_PHOTO = build_example_photo(72, 24)

local Picture = t.Widget:extend("Picture")

function Picture.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Picture)
  t.Widget.init(self, opts)
  self.pixels = opts.pixels or EXAMPLE_PHOTO
  self.pixel_h = #self.pixels
  self.pixel_w = self.pixel_h > 0 and #self.pixels[1] or 0
  self.align = opts.align or "center"
  self.focusable = false
  return self
end

function Picture:preferred_width()
  return self.width or self.pixel_w
end

function Picture:preferred_height()
  return self.height or math.max(1, math.ceil(self.pixel_h / 2))
end

function Picture:render(canvas)
  if self.w <= 0 or self.h <= 0 or self.pixel_h == 0 then
    return
  end
  local cw = math.min(self.w, self.pixel_w)
  local ch = math.min(self.h, math.ceil(self.pixel_h / 2))
  local x0 = 0
  if self.align == "center" then
    x0 = math.floor((self.w - cw) / 2)
  elseif self.align == "right" or self.align == "end" then
    x0 = self.w - cw
  end
  canvas:clip()
  for cy = 0, ch - 1 do
    local top_row = self.pixels[cy * 2 + 1]
    local bottom_row = self.pixels[cy * 2 + 2] or top_row
    for cx = 0, cw - 1 do
      local top = top_row[cx + 1] or top_row[cw] or 0
      local bottom = bottom_row[cx + 1] or top
      core.put(canvas.x + x0 + cx, canvas.y + cy, "▀", top, bottom, 0)
    end
  end
end

local function card(title, value_text, value, sub, hl)
  local progress = ui.progress({
    value = value,
    max = 100,
    show_percent = true,
    height = 1,
  })
  local content = ui.vbox({
    gap = 0,
    children = {
      ui.text({ text = value_text, hl = hl or "Accent", height = 1 }),
      progress,
      ui.text({ text = sub, hl = "Muted", height = 1, wrap = false }),
    },
  })
  return ui.box({
    title = title,
    height = 7,
    flex = 1,
    padding = 1,
    fill = "NormalFloat",
    border_style = "rounded",
    content = content,
  }), progress
end

local function make_dashboard(app)
  local cpu = 34
  local cpu_text = ui.text({
    text = function()
      return string.format("%d%%", cpu)
    end,
    hl = "Success",
    height = 1,
  })
  local cpu_progress = ui.progress({ value = cpu, max = 100 })

  local cpu_card = ui.box({
    title = "CPU",
    height = 7,
    flex = 1,
    padding = 1,
    fill = "NormalFloat",
    content = ui.vbox({
      gap = 0,
      children = {
        cpu_text,
        cpu_progress,
        ui.text({ text = "12 threads", hl = "Muted", height = 1 }),
      },
    }),
  })

  local mem_progress
  local mem_card, mp = card("Memory", "68%", 68, "5.4 / 8 GiB", "Warning")
  mem_progress = mp
  local net_card = card("Network", "24%", 24, "1.2 MiB/s", "Info")

  local cards = ui.hbox({
    gap = 1,
    height = 7,
    children = { cpu_card, mem_card, net_card },
  })

  local spinner = ui.spinner({ label = "Indexing project files…", interval = 90 })
  local refresh = ui.button({
    label = "Refresh",
    on_press = function()
      app:success("Dashboard refreshed")
    end,
  })
  local toolbar = ui.hbox({
    gap = 1,
    height = 1,
    children = {
      spinner,
      ui.text({ text = "", flex = 1 }),
      refresh,
    },
  })

  local rows = {
    { time = "09:41", event = "Lua toolkit loaded", level = "OK" },
    { time = "09:42", event = "Grid renderer attached", level = "OK" },
    { time = "09:43", event = "Plugin scan completed", level = "INFO" },
    { time = "09:44", event = "Disk usage above 80%", level = "WARN" },
    { time = "09:45", event = "Demo session started", level = "OK" },
  }
  local level_hl = { OK = "Success", INFO = "Info", WARN = "Warning", ERR = "Error" }
  local table_widget = ui.table({
    columns = {
      { title = "Time", key = "time", width = 7, align = "center" },
      { title = "Event", key = "event", flex = 1 },
      {
        title = "Level",
        key = "level",
        width = 8,
        align = "center",
        format = function(v)
          return { text = tostring(v), hl = level_hl[v] or "Muted" }
        end,
      },
    },
    rows = rows,
    flex = 1,
    show_scrollbar = true,
    show_grid = false,
    header_separator = true,
    on_activate = function(row)
      app:info("Event: " .. tostring(row.event))
    end,
  })

  local page = ui.vbox({
    gap = 1,
    children = {
      ui.text({
        text = "A terminal UI container built on a C cell grid and Lua widgets.",
        hl = "Muted",
        height = 2,
        wrap = true,
      }),
      cards,
      toolbar,
      table_widget,
    },
  })
  return page, { cpu = cpu_progress, cpu_text = cpu_text, spinner = spinner,
                 table = table_widget }
end

local function make_tasks(app)
  local defaults = {
    { label = "Design the cell grid", checked = true, detail = "core" },
    { label = "Write the Lua widget toolkit", checked = true, detail = "lua" },
    { label = "Add mouse and paste support", detail = "core" },
    { label = "Document the plugin API", detail = "docs" },
    { label = "Ship tuipro 0.1", detail = "release" },
  }
  local items = {}
  for i, item in ipairs(defaults) do
    items[i] = { label = item.label, checked = item.checked, detail = item.detail }
  end
  local counter = #items

  local status_text = ui.text({ text = "", hl = "Muted", height = 1 })

  local list
  local function update_status()
    local done = list and list:checked_count() or 0
    status_text.text = string.format("%d of %d tasks complete", done, #list.items)
    status_text:invalidate()
  end

  local function add_task(label)
    counter = counter + 1
    if not label or label == "" then
      label = "New task " .. counter
    end
    list.items[#list.items + 1] = { label = label, detail = "new" }
    list:set_items(list.items, #list.items, true)
    list:set_index(#list.items)
    update_status()
    app:success("Added: " .. label)
  end

  list = ui.list({
    items = items,
    flex = 1,
    multiple = true,
    show_scrollbar = true,
    name = "Tasks",
    on_toggle = function()
      update_status()
    end,
    on_activate = function(item)
      app:info("Task: " .. tostring(item.label))
    end,
  })

  local input = ui.input({
    prompt = "+ ",
    placeholder = "New task name, then Enter",
    on_submit = function(value, self)
      add_task(value)
      self:set("", true)
    end,
  })

  local add_btn = ui.button({
    label = "Add",
    primary = true,
    hotkey = "a",
    on_press = function()
      add_task(input:get())
      input:set("", true)
    end,
  })
  local clear_btn = ui.button({
    label = "Clear done",
    danger = true,
    on_press = function()
      local kept = {}
      for _, item in ipairs(list.items) do
        if not item.checked then
          kept[#kept + 1] = item
        end
      end
      list:set_items(kept, 1, true)
      update_status()
      app:info("Removed completed tasks")
    end,
  })
  local reset_btn = ui.button({
    label = "Reset",
    on_press = function()
      local fresh = {}
      for i, item in ipairs(defaults) do
        fresh[i] = { label = item.label, checked = item.checked, detail = item.detail }
      end
      list:set_items(fresh, 1, true)
      update_status()
      app:info("Task list reset")
    end,
  })

  local buttons = ui.hbox({
    gap = 1,
    height = 1,
    children = { add_btn, clear_btn, reset_btn, ui.text({ text = "", flex = 1 }) },
  })

  local page = ui.vbox({
    gap = 1,
    children = {
      ui.text({
        text = "Space toggles a task, Enter opens it. Type below to add a task.",
        hl = "Muted",
        height = 2,
        wrap = true,
      }),
      input,
      status_text,
      list,
      buttons,
    },
  })
  update_status()
  return page, { list = list, input = input, status = status_text }
end

local function make_widgets(app)
  local progress_refs = {}
  local function add_progress(label, value, hl)
    local p = ui.progress({
      label = label,
      value = value,
      max = 100,
      height = 1,
      fill_hl = hl,
    })
    progress_refs[#progress_refs + 1] = p
    return p
  end

  local spinner = ui.spinner({ label = "Background sync", interval = 110 })
  local toggle = ui.button({
    label = "Pause spinner",
    on_press = function(button)
      if spinner.active then
        spinner:stop(true, "Sync paused")
        button.label = "Resume spinner"
      else
        spinner:start("Background sync")
        button.label = "Pause spinner"
      end
      app:info(spinner.active and "Spinner resumed" or "Spinner paused")
    end,
  })

  local inline_tabs = ui.tabs({
    items = { "Overview", "Metrics", "Logs" },
    on_change = function(item)
      app:info("Inline tab: " .. tostring(item.label))
    end,
  })

  local buttons = ui.hbox({
    gap = 1,
    height = 1,
    children = {
      ui.button({
        label = "Primary",
        primary = true,
        on_press = function()
          app:success("Primary action")
        end,
      }),
      ui.button({
        label = "Danger",
        danger = true,
        on_press = function()
          app:error("Danger action (demo)")
        end,
      }),
      ui.button({
        label = "Disabled",
        disabled = true,
        on_press = function()
          app:info("This will not run")
        end,
      }),
      ui.button({
        label = "Toast",
        on_press = function()
          app:info("Buttons call back into the application")
        end,
      }),
    },
  })

  local page = ui.vbox({
    gap = 1,
    children = {
      ui.text({
        text = "Buttons, progress bars, separators, spinners and inline tabs.",
        hl = "Muted",
        height = 2,
        wrap = true,
      }),
      buttons,
      ui.separator({ label = "Progress", height = 1 }),
      add_progress("Downloading", 37, "ProgressFill"),
      add_progress("Compiling", 72, "ProgressFillAlt"),
      add_progress("Waiting", 8, "ProgressFill"),
      ui.hbox({
        gap = 1,
        height = 1,
        children = {
          spinner,
          ui.text({ text = "", flex = 1 }),
          toggle,
        },
      }),
      ui.separator({ label = "Tabs", height = 1 }),
      inline_tabs,
      ui.text({
        text = "Focus moves with Tab / Shift-Tab and C-w h/j/k/l. Press ? for help.",
        hl = "Muted",
        flex = 1,
        wrap = true,
      }),
    },
  })
  return page, { progress = progress_refs, spinner = spinner, tabs = inline_tabs }
end

local function make_inputs(app)
  local name = ui.input({
    prompt = "Name:    ",
    placeholder = "Ada Lovelace",
    width = nil,
  })
  local email = ui.input({
    prompt = "Email:   ",
    placeholder = "ada@example.com",
  })
  local password = ui.input({
    prompt = "Password:",
    placeholder = "secret",
    mask = "•",
    history = {},
  })
  local result = ui.text({ text = "", wrap = true, height = 3, hl = "Muted" })

  local function submit()
    local n = name:get()
    local e = email:get()
    if n == "" then
      result.text = "Please enter a name."
      result.hl = "Error"
    else
      result.text = "Hello " .. n .. (e ~= "" and (" <" .. e .. ">") or "") .. "!"
      result.hl = "Success"
      app:success("Form submitted")
    end
    result:invalidate()
  end

  local buttons = ui.hbox({
    gap = 1,
    height = 1,
    children = {
      ui.button({ label = "Submit", primary = true, on_press = submit }),
      ui.button({
        label = "Clear",
        on_press = function()
          name:set("", true)
          email:set("", true)
          password:set("", true)
          result.text = ""
          result:invalidate()
          app:info("Form cleared")
        end,
      }),
      ui.button({
        label = "Focus name",
        on_press = function()
          name:focus()
        end,
      }),
      ui.text({ text = "", flex = 1 }),
    },
  })

  local page = ui.vbox({
    gap = 1,
    children = {
      ui.text({
        text = "Single-line UTF-8 inputs with prompts, masks and callbacks.",
        hl = "Muted",
        height = 2,
        wrap = true,
      }),
      name,
      email,
      password,
      ui.separator({ label = "Actions", height = 1 }),
      buttons,
      result,
      ui.text({
        text = "Use the mouse to place the cursor. Enter submits; Esc calls on_cancel.",
        hl = "Muted",
        flex = 1,
        wrap = true,
      }),
    },
  })
  return page, { name = name, email = email, password = password, result = result }
end

local function make_dialogs(app)
  local refs = {
    basic = function() return M.show_basic_dialog(app) end,
    scroll = function() return M.show_scroll_dialog(app) end,
    picture = function() return M.show_picture_dialog(app) end,
  }

  local function row(label, hint, open)
    return ui.hbox({
      gap = 1,
      height = 1,
      children = {
        ui.button({ label = label, width = 20, on_press = open }),
        ui.text({ text = hint, hl = "Muted", flex = 1 }),
      },
    })
  end

  local page = ui.vbox({
    gap = 1,
    children = {
      ui.text({
        text = "Modal dialog examples. Open one, then dismiss it with Esc or the mouse.",
        hl = "Muted",
        height = 2,
        wrap = true,
      }),
      ui.separator({ label = "Dialogs", height = 1 }),
      row("Basic", "Title header, text body, footer, OK and Cancel buttons.", refs.basic),
      row("Scrollable", "Long form with inputs, a check list and a scrollbar.", refs.scroll),
      row("Picture JPG", "Block-art preview of example.jpg with file metadata.", refs.picture),
      ui.text({
        text = "Esc closes the focused dialog; Tab moves between its buttons and fields. "
            .. "The scrollable dialog follows focus, or scroll it with PgUp/PgDn and the wheel.",
        hl = "Muted",
        flex = 1,
        wrap = true,
      }),
    },
  })
  return page, refs
end

function M.run(opts)
  if opts == M then
    opts = nil
  end
  opts = opts or {}
  t.hl.set_transparent(opts.transparent ~= false)
  local app = t.App.new({
    title = "tuipro demo",
    footer_hint = "1-5 pages   ? help   C-q quit",
    background = "Normal",
  })

  local stack = ui.stack({})
  local menu
  menu = ui.menu({
    items = {},
    flex = 1,
    name = "Navigate",
    on_select = function(item)
      if item.page then
        M._switch(app, stack, menu, nil, item.page, false)
      end
    end,
    on_activate = function(item)
      if item.action == "about" then
        M.show_about(app)
      elseif item.action == "help" then
        app:show_help()
      elseif item.action == "quit" then
        app:quit()
      elseif item.dialog then
        M.show_dialog(app, item.dialog)
      elseif item.page then
        M._switch(app, stack, menu, nil, item.page, true)
      end
    end,
  })

  local dashboard, dash_refs = make_dashboard(app)
  local tasks, task_refs = make_tasks(app)
  local widgets, widget_refs = make_widgets(app)
  local inputs, input_refs = make_inputs(app)
  local dialogs, dialog_refs = make_dialogs(app)
  local page_widgets = { dashboard, tasks, widgets, inputs, dialogs }
  for i, page in ipairs(page_widgets) do
    stack:add(page)
  end
  stack:show(1)

  local titles = { "Dashboard", "Tasks", "Widgets", "Inputs", "Dialogs" }
  local main = ui.box({
    flex = 1,
    title = "Dashboard",
    padding = { top = 1, bottom = 0, left = 1, right = 1 },
    fill = "NormalFloat",
    border_style = "rounded",
    content = stack,
  })

  local menu_items = {}
  for i, title in ipairs(titles) do
    menu_items[#menu_items + 1] = {
      label = title,
      shortcut = tostring(i),
      page = i,
    }
  end
  menu_items[#menu_items + 1] = { header = true, label = "Dialog demos" }
  menu_items[#menu_items + 1] = { label = "Basic dialog", shortcut = "b", dialog = "basic" }
  menu_items[#menu_items + 1] = { label = "Scrollable dialog", shortcut = "s", dialog = "scroll" }
  menu_items[#menu_items + 1] = { label = "Picture JPG", shortcut = "p", dialog = "picture" }
  menu_items[#menu_items + 1] = { header = true, label = "Actions" }
  menu_items[#menu_items + 1] = { label = "About", shortcut = "a", action = "about" }
  menu_items[#menu_items + 1] = { label = "Help", shortcut = "?", action = "help" }
  menu_items[#menu_items + 1] = { label = "Quit", shortcut = "C-q", action = "quit" }
  menu:set_items(menu_items, 1, true)

  local sidebar = ui.box({
    width = 21,
    flex = 0,
    title = "Navigate",
    fill = "Menu",
    padding = { top = 1, bottom = 1, left = 0, right = 0 },
    border_style = "rounded",
    content = menu,
  })

  local body = ui.hbox({
    gap = 1,
    children = { sidebar, main },
  })
  app:set_body(body)

  local tabline = ui.tabs({
    items = pages,
    focusable = false,
    on_change = function(_, index)
      M._switch(app, stack, menu, nil, index, true)
    end,
  })
  app:set_tabline(tabline)

  local clock_text = os.date("%H:%M:%S")
  app:set_statusline(ui.statusline({
    left = function(_, a)
      return a:status_sections().left
    end,
    center = function()
      return { text = " " .. clock_text .. " ", hl = "StatusLineInfo" }
    end,
    right = function(_, a)
      return a:status_sections().right
    end,
  }))

  M._switch(app, stack, menu, tabline, 1, false)
  app:set_focus(menu)

  -- Keep the demo alive and visibly animated.
  local tick = 0
  app:every(700, function()
    tick = tick + 1
    local cpu = 30 + math.floor(25 * (1 + math.sin(tick / 2.2)) / 2)
    dash_refs.cpu.value = cpu
    dash_refs.cpu:invalidate()
    local values = { 37, 72, 8 }
    for i, p in ipairs(widget_refs.progress) do
      p.value = (values[i] + tick * (i * 3)) % 101
      p:invalidate()
    end
    clock_text = os.date("%H:%M:%S")
    app:invalidate()
  end)

  -- Expose select bits for scripted use and testing.
  app.demo = {
    menu = menu,
    stack = stack,
    tabs = tabline,
    dashboard = dash_refs,
    tasks = task_refs,
    widgets = widget_refs,
    inputs = input_refs,
    dialogs = dialog_refs,
    switch_page = function(_, page, focus)
      M._switch(app, stack, menu, tabline, page, focus)
    end,
    show_about = function()
      M.show_about(app)
    end,
    show_dialog = function(kind)
      return M.show_dialog(app, kind)
    end,
  }

  app:command("about", function()
    M.show_about(app)
  end, "About tuipro")
  app:command("tasks", function()
    M._switch(app, stack, menu, tabline, 2, true)
  end, "Open the tasks page")
  app:command("widgets", function()
    M._switch(app, stack, menu, tabline, 3, true)
  end, "Open the widgets page")
  app:command("dialogs", function()
    M._switch(app, stack, menu, tabline, 5, true)
  end, "Open the dialogs page")
  app:command("dialog", function(arg)
    M.show_dialog(app, arg ~= "" and arg or "basic")
  end, "Open a dialog: basic, scroll or picture")

  app:bind("a", function()
    M.show_about(app)
  end, "About")
  app:bind("b", function()
    M.show_basic_dialog(app)
  end, "Basic dialog")
  app:bind("s", function()
    M.show_scroll_dialog(app)
  end, "Scrollable dialog")
  app:bind("p", function()
    M.show_picture_dialog(app)
  end, "Picture dialog")
  app:bind("1", function()
    M._switch(app, stack, menu, tabline, 1, true)
  end, "Dashboard page")
  app:bind("2", function()
    M._switch(app, stack, menu, tabline, 2, true)
  end, "Tasks page")
  app:bind("3", function()
    M._switch(app, stack, menu, tabline, 3, true)
  end, "Widgets page")
  app:bind("4", function()
    M._switch(app, stack, menu, tabline, 4, true)
  end, "Inputs page")
  app:bind("5", function()
    M._switch(app, stack, menu, tabline, 5, true)
  end, "Dialogs page")
  app:bind("C-t", function()
    local _, active = stack:active()
    local n = (active or 1) % #pages + 1
    M._switch(app, stack, menu, tabline, n, true)
  end, "Next page")

  if opts.setup then
    opts.setup(app)
  end
  if opts.once then
    app:layout()
    app:render()
    return app
  end
  app:run()
  return app
end

function M._switch(app, stack, menu, tabline, page, focus)
  if not page or page < 1 or page > #pages then
    return
  end
  stack:show(page)
  stack:invalidate()
  if tabline then
    tabline:set_active(page, true)
  end
  local menu_index = page
  if menu and menu_index and menu.items[menu_index] and menu.items[menu_index].page == page then
    menu:set_index(menu_index, true)
  end
  local main = stack.parent
  if main and main.title ~= nil then
    main.title = page_titles[page]
    main:invalidate()
  end
  if focus then
    local w = stack.children[page]
    local target = w and w:find_focusables()[1]
    if target then
      app:set_focus(target)
    end
  end
  app:layout_invalidate()
end

-- Options for the bundled dialog demos.  They all use close_on_esc, so Esc
-- dismisses the dialog that currently has focus.

function M.show_dialog(app, kind)
  if kind == "scroll" or kind == "scrollable" or kind == "form" then
    return M.show_scroll_dialog(app)
  elseif kind == "picture" or kind == "jpg" or kind == "image" then
    return M.show_picture_dialog(app)
  end
  return M.show_basic_dialog(app)
end

-- Basic dialog: title header, wrapped text body, footer and two buttons.
function M.show_basic_dialog(app)
  local popup = ui.popup({
    title = "Confirm action",
    width = 48,
    height = 12,
    footer = "This action cannot be undone.",
    padding = { top = 1, bottom = 0, left = 2, right = 2 },
    content = ui.text({
      text = "Apply the pending changes to the workspace?\n\n"
          .. "This dialog demonstrates a title header, a text body, a footer "
          .. "and two action buttons.",
      wrap = true,
      height = 5,
    }),
    buttons = {
      {
        label = "Cancel",
        on_press = function(p)
          p:close()
          app:info("Cancelled")
        end,
      },
      {
        label = "OK",
        primary = true,
        on_press = function(p)
          p:close()
          app:success("Confirmed")
        end,
      },
    },
  })
  app:open_popup(popup, {
    width = popup.width,
    height = popup.height,
    close_on_esc = true,
  })
  return popup
end

-- Scrollable dialog: a form taller than the dialog, inside a viewport with a
-- scrollbar.  Focus follows into the form and the viewport auto-scrolls.
function M.show_scroll_dialog(app)
  local notifications = ui.list({
    items = {
      { label = "Email notifications", checked = true, detail = "email" },
      { label = "Desktop alerts", checked = true, detail = "desktop" },
      { label = "Weekly digest", detail = "digest" },
      { label = "Product news", checked = true, detail = "news" },
      { label = "Beta features", detail = "beta" },
    },
    multiple = true,
    height = 5,
    show_scrollbar = false,
    name = "Notifications",
  })

  local form = ui.vbox({
    gap = 1,
    children = {
      ui.text({ text = "Profile", hl = "Title", height = 1 }),
      ui.text({
        text = "This form is taller than the dialog. Tab into a field and the "
            .. "viewport scrolls to keep it visible.",
        hl = "Muted",
        height = 2,
        wrap = true,
      }),
      ui.input({ prompt = "Name:    ", placeholder = "Grace Hopper" }),
      ui.input({ prompt = "Email:   ", placeholder = "grace@example.com" }),
      ui.input({ prompt = "Password:", placeholder = "secret", mask = "•" }),
      ui.separator({ label = "Notifications", height = 1 }),
      notifications,
      ui.input({ prompt = "Region:  ", placeholder = "eu-west-1" }),
      ui.separator({ label = "Notes", height = 1 }),
      ui.text({
        text = "Long-form content can be scrolled with the arrow keys, PgUp/PgDn, "
            .. "the mouse wheel or by clicking the scrollbar.",
        hl = "Muted",
        wrap = true,
        height = 3,
      }),
      ui.hbox({
        gap = 1,
        height = 1,
        children = {
          ui.button({
            label = "Apply",
            primary = true,
            on_press = function()
              app:success("Settings applied")
            end,
          }),
          ui.button({
            label = "Defaults",
            on_press = function()
              app:info("Defaults restored")
            end,
          }),
          ui.text({ text = "", flex = 1 }),
        },
      }),
      ui.text({
        text = "Esc closes the dialog. Space toggles a checkbox.",
        hl = "Muted",
        height = 1,
        wrap = true,
      }),
    },
  })

  local viewport = ui.viewport({
    content = form,
    flex = 1,
    name = "Settings form",
  })

  local popup = ui.popup({
    title = "Account settings",
    width = 54,
    height = 17,
    footer = "Esc closes · PgUp / PgDn scrolls",
    padding = { top = 1, bottom = 0, left = 2, right = 2 },
    content = viewport,
    buttons = {
      {
        label = "Cancel",
        on_press = function(p)
          p:close()
          app:info("Settings dialog closed")
        end,
      },
      {
        label = "Save",
        primary = true,
        on_press = function(p)
          p:close()
          app:success("Settings saved")
        end,
      },
    },
  })
  app:open_popup(popup, {
    width = popup.width,
    height = popup.height,
    close_on_esc = true,
    focus = viewport,
  })
  return popup, viewport
end

-- Picture dialog: half-block "preview" of an example JPEG plus metadata.
function M.show_picture_dialog(app)
  local picture = Picture.new({
    pixels = EXAMPLE_PHOTO,
    height = 12,
    name = "example.jpg",
  })
  local popup = ui.popup({
    title = "Picture — example.jpg",
    width = 78,
    height = 20,
    footer = "Terminal preview · Esc closes",
    padding = { top = 0, bottom = 0, left = 1, right = 1 },
    content = ui.vbox({
      gap = 0,
      children = {
        picture,
        ui.text({
          text = "example.jpg · JPEG · 1280×720 · 412 KiB",
          hl = "Muted",
          height = 1,
        }),
        ui.text({
          text = "Block-art preview drawn with truecolor half-blocks; "
              .. "256-color terminals fall back automatically.",
          hl = "Muted",
          height = 2,
          wrap = true,
        }),
      },
    }),
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
  app:open_popup(popup, {
    width = popup.width,
    height = popup.height,
    close_on_esc = true,
  })
  return popup
end

function M.show_about(app)
  local popup = ui.popup({
    title = "About tuipro",
    width = 54,
    height = 13,
    footer = "C grid + LuaJIT widgets",
    buttons = {
      {
        label = "Help",
        on_press = function(p)
          p:close()
          app:show_help()
        end,
      },
      {
        label = "Close",
        primary = true,
        on_press = function(p)
          p:close()
        end,
      },
    },
    content = ui.text({
      text = "tuipro is a terminal UI container. The C core owns the terminal, "
          .. "the diffing cell grid and input parsing. Lua builds layouts and "
          .. "widgets on top.\n\nPress Esc or click outside to close.",
      wrap = true,
      hl = "NormalFloat",
    }),
  })
  app:open_popup(popup, {
    width = popup.width,
    height = popup.height,
    close_on_esc = true,
  })
end

return M
