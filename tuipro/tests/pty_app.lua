-- Small live application used by tests/pty_smoke.py.
--
-- It reports the current terminal size and an input counter so the Python
-- harness can verify resize handling, key input and clean shutdown through a
-- real pseudo-terminal.
local t = require("tuipro")
local ui = t.ui

local count = 0
local app

local size_text = ui.text({
  text = function()
    return string.format("size %dx%d", app.width, app.height)
  end,
  height = 1,
})

local count_text = ui.text({
  text = function()
    if count == 0 then
      return "waiting"
    end
    return "KEY-OK " .. tostring(count)
  end,
  height = 1,
})

app = t.App.new({
  title = "PTY smoke",
  statusline = false,
  cmdline = false,
  footer_hint = "",
  background = "Normal",
  body = ui.vbox({
    gap = 1,
    children = {
      ui.text({ text = "PTY-SMOKE", hl = "Title", height = 1 }),
      size_text,
      count_text,
    },
  }),
})

app:bind("x", function()
  count = count + 1
  app:invalidate()
end, "Increment")

app:bind("q", function(a)
  a:quit()
end, "Quit")

app:bind("C-c", function(a)
  a:quit()
end, "Quit")

app:run()
