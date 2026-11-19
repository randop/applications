-- Example tuipro plugin.
--
-- Run standalone with:  build/tuipro --plugin plugins/hello
-- Or load it from a script with:
--   local plugin = require("tuipro.plugins").load("plugins/hello")
--   require("tuipro.plugins").install(plugin, app)
local M = {}

local function build(app, tuipro)
  local ui = tuipro.ui
  local status = ui.text({
    text = "This window is a plugin, not part of the host application.",
    hl = "Muted",
    wrap = true,
    height = 3,
  })
  local count = 0
  local button = ui.button({
    label = "Say hello",
    primary = true,
    on_press = function()
      count = count + 1
      status.text = string.format("Hello from the plugin! (%d)", count)
      status.hl = "Success"
      status:invalidate()
      app:success("Plugin action " .. count)
    end,
  })
  local close = ui.button({
    label = "Close",
    on_press = function()
      app:quit()
    end,
  })

  local content = ui.vbox({
    gap = 1,
    children = {
      ui.text({ text = "Hello, plugin!", hl = "Title", height = 1 }),
      ui.text({
        text = "A plugin returns a function or a table with run/setup. "
            .. "It gets the tuipro module and the CLI table.",
        hl = "Muted",
        wrap = true,
        height = 4,
      }),
      status,
      ui.hbox({
        gap = 1,
        height = 1,
        justify = "center",
        children = { button, close },
      }),
    },
  })

  local dialog = ui.box({
    width = 52,
    height = 14,
    title = "plugins/hello",
    padding = 1,
    fill = "NormalFloat",
    content = content,
  })

  return ui.center({ content = dialog })
end

function M.run()
  local tuipro = require("tuipro")
  local app = tuipro.App.new({
    title = "Hello plugin",
    footer_hint = "Enter activate   C-q quit",
    background = "Normal",
    body = nil,
  })
  app:set_body(build(app, tuipro))
  app:bind("C-q", function(a)
    a:quit()
  end, "Quit")
  app:bind("q", function(a)
    a:quit()
  end, "Quit")
  app:command("hello", function()
    app:success("Hello from :hello")
  end, "Say hello")
  app:run()
  return app
end

-- Used when the plugin is installed into an existing application.
function M.setup(_, _, app)
  if app then
    app:command("hello", function()
      app:success("Hello from the hello plugin")
    end, "Say hello from the hello plugin")
  end
end

return M
