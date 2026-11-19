-- Three-pane layout using Split, with focus movement between the panes.
--
--   build/tuipro examples/split_panes.lua
--   build/tuipro --headless --frames 1 examples/split_panes.lua
local tuipro = require("tuipro")
local ui = tuipro.ui

local app = tuipro.App.new({
  title = "tuipro split panes",
  footer_hint = "Tab focus   C-w h/l panes   q quit",
  background = "Normal",
})

local projects = {
  { label = "tuipro", detail = "Lua/C" },
  { label = "neovim", detail = "C" },
  { label = "LuaJIT", detail = "C" },
  { label = "docs", detail = "md" },
}

local selected_name = ui.text({ text = "tuipro", hl = "Title", height = 1 })
local details = ui.text({
  text = "Select a project on the left.",
  wrap = true,
  hl = "Muted",
})

local nav = ui.list({
  items = projects,
  flex = 1,
  on_select = function(item)
    selected_name.text = item.label
    details.text = string.format(
        "%s\n\nLanguage: %s\n\nThis pane keeps its own scroll position and "
            .. "selection, like a Neovim window.", item.label, item.detail)
    selected_name:invalidate()
    details:invalidate()
  end,
})

local files = ui.table({
  columns = {
    { title = "Path", key = "path", flex = 1 },
    { title = "Lines", key = "lines", width = 7, align = "right" },
  },
  rows = {
    { path = "src/main.c", lines = 430 },
    { path = "src/grid.c", lines = 610 },
    { path = "src/input.c", lines = 520 },
    { path = "lua/tuipro/app.lua", lines = 1440 },
    { path = "lua/tuipro/container.lua", lines = 550 },
  },
  flex = 1,
  show_grid = true,
  header_separator = true,
})
files:set_index(1, true)

local activity = ui.progress({ label = "Build", value = 72, max = 100 })
local spinner = ui.spinner({ label = "Watching files…" })
local filter = ui.input({
  placeholder = "Filter files…",
  on_submit = function(text)
    app:info("Filter: " .. (text ~= "" and text or "(clear)"))
  end,
})

local split = ui.split({ direction = "vertical" })
split:add(ui.box({
  title = "Projects",
  fill = "NormalFloat",
  padding = 0,
  content = nav,
}), { size = 24 })
split:add(ui.box({
  title = "Files",
  fill = "NormalFloat",
  padding = 0,
  content = files,
}), { weight = 1 })
split:add(ui.box({
  title = "Inspector",
  fill = "NormalFloat",
  padding = 1,
  content = ui.vbox({
    gap = 1,
    children = {
      selected_name,
      ui.separator(),
      details,
      activity,
      spinner,
      filter,
    },
  }),
}), { size = 34 })

app:set_body(split)

app:bind("q", function(a)
  a:quit()
end, "Quit")
app:bind("C-c", function(a)
  a:quit()
end, "Quit")
app:run()
