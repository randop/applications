-- Lua widget toolkit.  Constructors accept either an options table or, where
-- it is unambiguous, a shorthand value such as a label or item list.
local container = require("tuipro.container")

local M = {}

M.Text = require("tuipro.ui.text")
M.Box = require("tuipro.ui.box")
M.List = require("tuipro.ui.list")
M.Menu = require("tuipro.ui.menu")
M.Table = require("tuipro.ui.table")
M.Input = require("tuipro.ui.input")
M.Tabs = require("tuipro.ui.tabs")
M.Progress = require("tuipro.ui.progress")
M.Spinner = require("tuipro.ui.spinner")
M.Button = require("tuipro.ui.button")
M.Popup = require("tuipro.ui.popup")
M.Separator = require("tuipro.ui.separator")
M.Scrollbar = require("tuipro.ui.scrollbar")
M.Viewport = require("tuipro.ui.viewport")
M.Statusline = require("tuipro.ui.statusline")
M.scroll = require("tuipro.ui.scroll")

M.Container = container.Container
M.VBox = container.VBox
M.HBox = container.HBox
M.Padding = container.Padding
M.Center = container.Center
M.Stack = container.Stack
M.Split = container.Split

function M.text(opts)
  if type(opts) == "string" or type(opts) == "number" then
    opts = { text = tostring(opts) }
  end
  return M.Text.new(opts)
end

function M.box(opts)
  return M.Box.new(opts)
end

function M.list(opts)
  if type(opts) == "table" and opts.items == nil and #opts > 0 then
    opts = { items = opts }
  end
  return M.List.new(opts)
end

function M.menu(opts)
  if type(opts) == "table" and opts.items == nil and #opts > 0 then
    opts = { items = opts }
  end
  return M.Menu.new(opts)
end

function M.table(opts)
  return M.Table.new(opts)
end

function M.input(opts)
  if type(opts) == "string" then
    opts = { placeholder = opts }
  end
  return M.Input.new(opts)
end

function M.tabs(items, opts)
  if items == nil then
    return M.Tabs.new(opts)
  end
  if type(items) == "table" and items.items then
    return M.Tabs.new(items)
  end
  opts = opts or {}
  opts.items = items
  return M.Tabs.new(opts)
end

function M.progress(opts)
  if type(opts) == "number" then
    opts = { value = opts }
  end
  return M.Progress.new(opts)
end

function M.spinner(opts)
  if type(opts) == "string" then
    opts = { label = opts }
  end
  return M.Spinner.new(opts)
end

function M.button(opts, on_press, extra)
  if type(opts) == "string" then
    local o = extra or {}
    o.label = opts
    if on_press then
      o.on_press = on_press
    end
    opts = o
  end
  return M.Button.new(opts)
end

function M.separator(opts, extra)
  if type(opts) == "string" then
    local o = extra or {}
    o.label = opts
    opts = o
  end
  return M.Separator.new(opts)
end

function M.scrollbar(opts)
  return M.Scrollbar.new(opts)
end

function M.popup(opts)
  return M.Popup.new(opts)
end

local function container_opts(opts)
  if type(opts) == "table" and #opts > 0 and opts.children == nil and opts.content == nil then
    return { children = opts }
  end
  return opts or {}
end

function M.vbox(opts)
  return M.VBox.new(container_opts(opts))
end

function M.hbox(opts)
  return M.HBox.new(container_opts(opts))
end

function M.padding(opts)
  return M.Padding.new(opts)
end

function M.center(opts)
  return M.Center.new(opts)
end

function M.stack(opts)
  return M.Stack.new(container_opts(opts))
end

function M.split(opts)
  return M.Split.new(container_opts(opts))
end

function M.viewport(opts)
  if type(opts) == "table" and #opts > 0 and opts.children == nil and opts.content == nil then
    opts = { children = opts }
  end
  return M.Viewport.new(opts)
end

function M.statusline(opts)
  return M.Statusline.new(opts)
end

-- An empty area for boxes and toolbars.  With no argument it is flexible on
-- the main axis of the surrounding layout; with a height it is a fixed-height
-- vertical gap (the common case inside a VBox).
function M.spacer(height)
  if height ~= nil then
    return M.Box.new({ border = false, height = height })
  end
  return M.Box.new({ border = false, flex = 1 })
end

return M
