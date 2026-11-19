-- Widget gallery: lists, tabs, inputs, buttons, progress and a spinner.
--
--   build/tuipro examples/widgets.lua
--   build/tuipro --headless --frames 1 examples/widgets.lua
local tuipro = require("tuipro")
local ui = tuipro.ui

local app = tuipro.App.new({
  title = "tuipro widget gallery",
  footer_hint = "Tab focus   q quit",
  background = "Normal",
})

local status = ui.text({ text = "Try the controls.", hl = "Muted", height = 1 })

local items = {
  { label = "Cell grid", detail = "C" },
  { label = "Diff renderer", detail = "C" },
  { label = "Lua widgets", detail = "Lua" },
  { label = "Plugin loader", detail = "Lua" },
}

local list = ui.list({
  items = items,
  flex = 1,
  show_scrollbar = true,
  on_select = function(item)
    status.text = "Selected: " .. item.label
    status:invalidate()
  end,
  on_activate = function(item)
    status.text = "Activated: " .. item.label
    status:invalidate()
  end,
})

local value = 35
local progress = ui.progress({ label = "Progress", value = value, max = 100 })

local spinner = ui.spinner({ label = "Working…" })

local function bump(delta)
  value = math.max(0, math.min(100, value + delta))
  progress.value = value
  progress:invalidate()
end

local tabs = ui.tabs({
  { label = "Overview", badge = 4 },
  { label = "Details" },
  { label = "Log", badge = "!" },
}, {
  on_change = function(item, index)
    status.text = "Tab " .. index .. ": " .. item.label
    status:invalidate()
  end,
})

local input = ui.input({
  placeholder = "Type a label and press Enter",
  width = 40,
  on_submit = function(text)
    if text ~= "" then
      list:set_items(list.items)
      table.insert(list.items, { label = text, detail = "new" })
      list:set_items(list.items, #list.items)
      input:set("")
      app:success("Added " .. text)
    end
  end,
})

local body = ui.vbox({
  gap = 1,
  children = {
    ui.text({ text = "Widget gallery", hl = "Title", height = 1 }),
    tabs,
    ui.hbox({
      gap = 1,
      flex = 1,
      children = {
        ui.box({ title = "List", flex = 1, padding = 0, content = list }),
        ui.box({
          title = "Progress",
          width = 34,
          padding = 1,
          content = ui.vbox({
            gap = 1,
            children = {
              progress,
              spinner,
              ui.text({ text = "Space/Enter activates list items.", hl = "Muted", height = 1 }),
            },
          }),
        }),
      },
    }),
    ui.separator({ label = "New item" }),
    input,
    ui.hbox({
      gap = 1,
      height = 1,
      children = {
        ui.button({ label = "−10", on_press = function() bump(-10) end }),
        ui.button({ label = "+10", primary = true, on_press = function() bump(10) end }),
        ui.button({ label = "Clear", danger = true, on_press = function()
          list:set_items({}, nil, true)
          status.text = "List cleared."
        end }),
        ui.text({ text = "", flex = 1 }),
        ui.button({ label = "Quit", on_press = function() app:quit() end }),
      },
    }),
    status,
  },
})

app:set_body(body)
app:bind("q", function(a)
  a:quit()
end, "Quit")
app:bind("C-c", function(a)
  a:quit()
end, "Quit")

app:run()
