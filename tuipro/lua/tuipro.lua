-- tuipro: public Lua front-end.
--
-- The C core is embedded in the tuipro executable and exposes the terminal
-- grid through the "tuipro.core" module.  Everything in this file and below
-- it is plain Lua(JIT) 5.1.
local core = require("tuipro.core")

local M = {}

M._VERSION = "0.1.0"
M.version = M._VERSION
M.core = core
M.cli = require("tuipro.cli")
M.util = require("tuipro.util")
M.theme = require("tuipro.theme")
M.hl = require("tuipro.hl")
M.highlight = M.hl
M.keys = require("tuipro.keys")
M.canvas = require("tuipro.canvas")
M.widget = require("tuipro.widget")
M.Widget = M.widget
M.container = require("tuipro.container")
M.ui = require("tuipro.ui")
M.plugins = require("tuipro.plugins")
M.App = require("tuipro.app")
M.http = require("tuipro.http")
M.fetch = M.http.fetch
M.json = M.http.json

-- Fetch is also available as a global, mirroring browsers and Node.
_G.fetch = M.fetch

-- Create an application.  opts are passed to App.new; opts.setup, if present,
-- is called with the new app before it starts.
function M.app(opts)
  opts = opts or {}
  local app = M.App.new(opts)
  if opts.setup then
    opts.setup(app)
  end
  return app
end

M.application = M.app

-- Create and run an application.
function M.run(opts)
  local app = M.app(opts)
  app:run()
  return app
end

-- Run the bundled dashboard demo.
function M.demo(opts)
  local demo = require("tuipro.demo")
  if type(demo) == "table" and demo.run then
    return demo.run(opts)
  end
  if type(demo) == "function" then
    return demo(opts)
  end
  error("tuipro.demo has no run() entry point")
end

-- Run the bundled HTTP/fetch demo.
function M.demo_api(opts)
  local demo = require("tuipro.demo_api")
  if type(demo) == "table" and demo.run then
    return demo.run(opts)
  end
  if type(demo) == "function" then
    return demo(opts)
  end
  error("tuipro.demo_api has no run() entry point")
end

-- Run the bundled theme showcase demo.
function M.demo_theme(opts)
  local demo = require("tuipro.demo_theme")
  if type(demo) == "table" and demo.run then
    return demo.run(opts)
  end
  if type(demo) == "function" then
    return demo(opts)
  end
  error("tuipro.demo_theme has no run() entry point")
end

-- Run the bundled opencode-style coding demo.
function M.demo_coding(opts)
  local demo = require("tuipro.demo_coding")
  if type(demo) == "table" and demo.run then
    return demo.run(opts)
  end
  if type(demo) == "function" then
    return demo(opts)
  end
  error("tuipro.demo_coding has no run() entry point")
end

-- Small convenience helpers for one-off scripts.
function M.info()
  return core.initialized() and core.info() or {}
end

function M.quit()
  if core.initialized() then
    core.exit()
  end
end

return M
