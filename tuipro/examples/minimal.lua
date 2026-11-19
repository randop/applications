-- The smallest useful tuipro application.
--
--   build/tuipro examples/minimal.lua
--   build/tuipro --headless --frames 1 examples/minimal.lua
local tuipro = require("tuipro")
local ui = tuipro.ui

local app = tuipro.App.new({
  title = "tuipro minimal",
  footer_hint = "q quit   C-q quit",
  background = "Normal",
})

app:set_body(ui.center({
  content = ui.box({
    width = 52,
    height = 11,
    title = "Hello",
    padding = 1,
    fill = "NormalFloat",
    content = ui.vbox({
      gap = 1,
      children = {
        ui.text({ text = "Welcome to tuipro.", hl = "Title", height = 1 }),
        ui.separator(),
        ui.text({
          text = "A C cell grid renders the screen; Lua composes widgets, "
              .. "layout and input handling on top.",
          wrap = true,
          height = 3,
          hl = "Muted",
        }),
        ui.hbox({
          height = 1,
          justify = "center",
          children = {
            ui.button({
              label = "Quit",
              primary = true,
              on_press = function()
                app:quit()
              end,
            }),
          },
        }),
      },
    }),
  }),
}))

app:bind("q", function(a)
  a:quit()
end, "Quit")
app:bind("C-c", function(a)
  a:quit()
end, "Quit")

app:run()
