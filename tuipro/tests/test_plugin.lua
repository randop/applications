-- Tests for plugin resolution, loading and installation.
local here = (arg and arg[0] or "tests/test_plugin.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local t = H.t
local ui = H.ui
local A = H.assert
local test = H.test
local plugins = t.plugins

test("plugin resolver and discovery", function()
  local path = plugins.resolve("plugins/hello")
  A.truthy(path)
  A.contains(path, "plugins/hello/init.lua")
  A.eq(plugins.resolve("definitely-not-a-real-plugin-xyz"), nil)

  local found = plugins.discover("plugins")
  local hit = false
  for _, p in ipairs(found) do
    if p:find("plugins/hello/init.lua", 1, true) then
      hit = true
    end
  end
  A.truthy(hit, "discover should find the hello plugin")
end)

test("plugin load errors are returned", function()
  local entry, err = plugins.load("definitely-not-a-real-plugin-xyz", t)
  A.eq(entry, nil)
  A.truthy(err)
end)

test("hello plugin loads and installs a command", function()
  local entry, path = plugins.load("plugins/hello", t)
  A.eq(type(entry), "table")
  A.truthy(path)
  A.eq(type(entry.run), "function")
  A.eq(type(entry.setup), "function")

  H.with_app({ body = ui.text("host") }, function(app)
    local installed = plugins.install(entry, app, t)
    A.eq(installed, entry)
    A.truthy(app.commands.hello)
    app:execute_command("hello")
    A.contains(app.message.text, "Hello from the hello plugin")
  end)
end)

test("plugin tables can mount widgets, commands and bindings", function()
  local mounted, bound = false, 0
  local entry = {
    mount = function(app)
      mounted = true
      app:set_body(ui.text("mounted"))
    end,
    commands = {
      ping = function(arg, app)
        app:info("pong " .. arg)
      end,
    },
    bindings = {
      z = function(app)
        bound = bound + 1
      end,
    },
  }

  H.with_app({ body = ui.text("before") }, function(app)
    plugins.install(entry, app, t)
    A.truthy(mounted)
    A.truthy(app.commands.ping)
    app:execute_command("ping 1")
    A.eq(app.message.text, "pong 1")

    app:layout()
    local rows = H.render_app(app)
    A.row(rows, "mounted")
    app:handle_event(H.text_key("z"))
    A.eq(bound, 1)
  end)
end)

test("function plugin entries receive tuipro, cli and app", function()
  local seen = nil
  local entry = function(tuipro, cli, app)
    seen = { tuipro = tuipro, cli = cli, app = app }
    return "installed"
  end

  H.with_app({ body = ui.text("host") }, function(app)
    local result = plugins.install(entry, app, t)
    A.eq(result, "installed")
    A.eq(seen.tuipro, t)
    A.truthy(seen.cli)
    A.eq(seen.app, app)
  end)
end)

return H.finish()
