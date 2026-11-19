-- A compact dashboard: cards, progress bars, a live table and a timer.
--
--   build/tuipro examples/dashboard.lua
--   build/tuipro --headless --frames 1 examples/dashboard.lua
local tuipro = require("tuipro")
local ui = tuipro.ui

local app = tuipro.App.new({
  title = "tuipro dashboard",
  footer_hint = "r refresh   q quit",
  background = "Normal",
})

local cpu = ui.progress({ label = "CPU", value = 37, max = 100, width = 28 })
local memory = ui.progress({ label = "Memory", value = 62, max = 100, width = 28 })
local jobs = ui.progress({ label = "Jobs", value = 4, max = 10, width = 28 })

local function card(title, body)
  return ui.box({
    title = title,
    padding = { top = 0, bottom = 0, left = 1, right = 1 },
    fill = "NormalFloat",
    height = 4,
    content = body,
  })
end

local events = {
  { time = "09:41", event = "C core initialized", level = "OK" },
  { time = "09:42", event = "Grid renderer attached", level = "OK" },
  { time = "09:43", event = "Plugin scan completed", level = "INFO" },
  { time = "09:44", event = "Waiting for input", level = "INFO" },
}
local event_table = ui.table({
  columns = {
    { title = "Time", key = "time", width = 7, align = "center" },
    { title = "Event", key = "event", flex = 1 },
    { title = "Level", key = "level", width = 7, align = "center" },
  },
  rows = events,
  flex = 1,
})

local banner = ui.text({
  text = "A terminal UI container built on a C cell grid and LuaJIT widgets.",
  wrap = true,
  height = 2,
  hl = "Muted",
})

local body = ui.vbox({
  gap = 1,
  children = {
    banner,
    ui.hbox({
      gap = 1,
      height = 4,
      children = {
        card("CPU", cpu),
        card("Memory", memory),
        card("Jobs", jobs),
      },
    }),
    ui.separator({ label = "Recent events", height = 1 }),
    event_table,
  },
})

app:set_body(body)

local tick = 0
app:every(900, function()
  tick = tick + 1
  cpu.value = 25 + (tick * 7) % 60
  memory.value = 50 + (tick * 5) % 40
  jobs.value = 2 + tick % 8
  cpu:invalidate()
  memory:invalidate()
  jobs:invalidate()
  events[1].time = os.date("%H:%M")
  event_table:set_rows(events, event_table.index)
  app:invalidate()
end)

app:bind("r", function()
  app:success("Refreshed at " .. os.date("%H:%M:%S"))
end, "Refresh")
app:bind("q", function(a)
  a:quit()
end, "Quit")
app:bind("C-c", function(a)
  a:quit()
end, "Quit")

app:run()
