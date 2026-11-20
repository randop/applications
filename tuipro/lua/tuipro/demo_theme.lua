-- Built-in theme showcase.  Run with: tuipro --demo-theme
--
-- The demo exercises the theme registry, live theme switching, transparent
-- surfaces and themed glyphs.  It registers the bundled theme plugins when it
-- can find them, so the list normally contains five themes: aurora, opencode,
-- tokyo-night, tuipro-dark and tuipro-light.
local t = require("tuipro")
local ui = t.ui
local util = t.util
local hl = t.hl
local theme = t.theme
local cli = t.cli
local Widget = t.Widget

local M = {}

local BORDER_STYLES = { "rounded", "single", "double", "heavy", "ascii" }

local PALETTE_ROLES = {
  "bg", "surface", "surface2", "panel",
  "border", "muted", "text", "accent",
  "accent2", "blue", "cyan", "green",
  "yellow", "orange", "red", "pink",
  "code", "selection", "add_bg", "del_bg",
}

-- ---------------------------------------------------------------------------
-- Palette swatches
-- ---------------------------------------------------------------------------

local Swatches = Widget:extend("ThemeSwatches")

function Swatches.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Swatches)
  Widget.init(self, opts)
  self.roles = opts.roles or PALETTE_ROLES
  self.height = opts.height or 4
  self.focusable = false
  self.name = opts.name or "Palette"
  return self
end

function Swatches:preferred_height()
  return self.height
end

function Swatches:render(canvas)
  if self.w <= 0 or self.h <= 0 then
    return
  end
  local cols = 5
  local cell = math.max(8, math.floor(self.w / cols))
  for i, role in ipairs(self.roles) do
    local row = math.floor((i - 1) / cols)
    if row >= self.h then
      break
    end
    local x = ((i - 1) % cols) * cell
    canvas:fill(x, row, 2, 1, " ", { bg = role })
    if cell > 4 then
      canvas:put(x + 3, row, util.truncate(tostring(role), cell - 4), "Faint")
    end
  end
end

-- ---------------------------------------------------------------------------
-- Theme discovery
-- ---------------------------------------------------------------------------

-- Bundled theme plugins worth loading into the showcase.  Add entries here
-- when another plugin ships a theme.
local PLUGIN_THEMES = {
  { plugin = "aurora", theme = "aurora" },
  { plugin = "opencode", theme = "opencode" },
  { plugin = "tokyo-night", theme = "tokyo-night" },
}

local function load_plugin_theme(plugin, theme_name)
  if theme.get(theme_name) then
    return true
  end
  local ok, entry = pcall(t.plugins.load, plugin, t)
  if not ok or type(entry) ~= "table" then
    return false
  end
  if entry.register then
    pcall(entry.register, t)
  elseif entry.theme then
    pcall(theme.register, entry.theme)
  end
  return theme.get(theme_name) ~= nil
end

local function ensure_plugin_themes()
  for _, item in ipairs(PLUGIN_THEMES) do
    load_plugin_theme(item.plugin, item.theme)
  end
end

local function theme_items(names)
  local items = {}
  for _, name in ipairs(names) do
    local builtin = name == "tuipro-dark" or name == "tuipro-light"
    items[#items + 1] = {
      label = name,
      detail = builtin and "built-in" or "plugin",
      theme_name = name,
      hl = "ListItem",
      detail_hl = builtin and "ListDetail" or "Accent",
    }
  end
  return items
end

-- ---------------------------------------------------------------------------
-- Demo app
-- ---------------------------------------------------------------------------

function M.run(opts)
  if opts == M then
    opts = nil
  end
  opts = opts or {}

  if opts.reset ~= false then
    hl.reset()
  end
  ensure_plugin_themes()

  local names = theme.names()
  if #names == 0 then
    names = { theme.default_name }
  end

  local state = {
    themes = names,
    index = 1,
    theme = theme.default_name,
    border_style = opts.border_style or "rounded",
    transparent = opts.transparent == true,
  }

  local start = opts.theme or cli.theme
  if start and not theme.get(start) then
    start = nil
  end
  start = start or (theme.get("aurora") and "aurora") or theme.default_name
  state.theme = start
  hl.set_theme(start, { transparent = state.transparent })
  for i, name in ipairs(state.themes) do
    if name == start then
      state.index = i
    end
  end

  -- Themes can ask the showcase to mimic an editor instead of a framed
  -- desktop: no drawn pane borders, plain titles and the Normal background
  -- for body panes (floats/statusline still use their own groups).
  local flat = function(name)
    local def = theme.get(name)
    return def and def.styling and def.styling.flat == true
  end
  local flat_start = flat(state.theme)

  local app = t.App.new({
    title = "tuipro themes",
    footer_hint = "t theme   x transparent   g glyphs   ? help   C-q quit",
    background = "Normal",
    width = opts.width,
    height = opts.height,
  })

  -- Header -----------------------------------------------------------------
  local theme_title = ui.text({ text = start, hl = "Title", flex = 1 })
  local theme_desc = ui.text({ text = "", hl = "Muted", height = 1 })
  local status_badge = ui.text({
    text = "",
    hl = "Badge",
    width = 26,
    align = "center",
    height = 1,
  })
  local heading = ui.vbox({
    gap = 0,
    height = 2,
    children = {
      ui.hbox({
        height = 1,
        gap = 1,
        children = {
          ui.text({ text = "Theme", hl = "Muted", width = 6, height = 1 }),
          theme_title,
          status_badge,
        },
      }),
      theme_desc,
    },
  })

  -- Theme list -------------------------------------------------------------
  local apply
  local refresh

  local theme_list = ui.list({
    items = theme_items(state.themes),
    selected = state.index,
    flex = 1,
    show_marker = true,
    show_index = false,
    detail_align = "right",
    name = "Themes",
    on_select = function(item)
      if item and item.theme_name then
        apply(item.theme_name, true)
      end
    end,
  })

  local theme_header = ui.text({
    text = "Themes",
    hl = "Title",
    height = 1,
    visible = flat_start,
  })
  local theme_box = ui.box({
    title = "Themes",
    width = 32,
    border = not flat_start,
    border_hl = "Border",
    title_hl = "Title",
    fill = flat_start and "Normal" or nil,
    padding = { top = 0, bottom = 0, left = 1, right = 1 },
    content = ui.vbox({
      gap = 0,
      children = { theme_header, theme_list },
    }),
  })

  -- Preview ----------------------------------------------------------------
  local role_line = ui.hbox({
    height = 1,
    gap = 1,
    children = {
      ui.text({ text = "Title", hl = "Title" }),
      ui.text({ text = "Accent", hl = "Accent" }),
      ui.text({ text = "Success", hl = "Success" }),
      ui.text({ text = "Warning", hl = "Warning" }),
      ui.text({ text = "Error", hl = "Error" }),
      ui.text({ text = "Info", hl = "Info" }),
      ui.text({ text = "Muted", hl = "Muted" }),
    },
  })

  local buttons = ui.hbox({
    height = 1,
    gap = 1,
    children = {
      ui.button({
        label = "Primary",
        primary = true,
        on_press = function()
          app:info("Primary buttons use ButtonPrimary")
        end,
      }),
      ui.button({
        label = "Danger",
        danger = true,
        on_press = function()
          app:warn("Danger buttons use ButtonDanger")
        end,
      }),
      ui.button({ label = "Disabled", disabled = true }),
    },
  })

  local input = ui.input({
    prompt = "Name: ",
    placeholder = "type here",
    width = 34,
  })

  local sample_list = ui.list({
    items = {
      { label = "Selected row", detail = "accent", checked = true },
      { label = "Checked row", detail = "green", checked = true },
      { label = "Plain row", detail = "muted" },
      { label = "Disabled row", detail = "faint", disabled = true },
    },
    selected = 1,
    flex = 1,
    multiple = true,
    show_marker = true,
  })

  local progress = ui.progress({
    label = "Build",
    value = 68,
    width = 26,
  })
  local spinner = ui.spinner({ label = "Loading", interval = 120 })
  local preview_tabs = ui.tabs({ items = { "Palette", "Widgets", "Glyphs" } })

  local preview_content = ui.vbox({
    gap = 0,
    children = {
      role_line,
      ui.separator({ label = "Widgets", height = 1 }),
      buttons,
      input,
      sample_list,
      ui.hbox({
        height = 1,
        gap = 1,
        children = {
          progress,
          ui.spacer(),
          spinner,
        },
      }),
      preview_tabs,
    },
  })

  local preview_header = ui.text({
    text = "Preview",
    hl = "Title",
    height = 1,
    visible = flat_start,
  })
  local preview_box = ui.box({
    title = "Preview",
    flex = 1,
    border = not flat_start,
    border_style = state.border_style,
    border_hl = "Border",
    title_hl = "Title",
    fill = flat_start and "Normal" or "NormalFloat",
    padding = { top = 0, bottom = 0, left = 1, right = 1 },
    content = ui.vbox({
      gap = 0,
      children = { preview_header, preview_content },
    }),
  })

  -- Palette ----------------------------------------------------------------
  local palette = Swatches.new({ roles = PALETTE_ROLES, height = 4 })
  local palette_header = ui.text({
    text = "Palette roles",
    hl = "Title",
    height = 1,
    visible = flat_start,
  })
  local palette_box = ui.box({
    title = "Palette roles",
    height = 6,
    border = not flat_start,
    border_hl = "Border",
    title_hl = "Title",
    fill = flat_start and "Normal" or nil,
    padding = { top = 0, bottom = 0, left = 1, right = 1 },
    content = ui.vbox({
      gap = 0,
      children = { palette_header, palette },
    }),
  })

  -- Body -------------------------------------------------------------------
  local body = ui.vbox({
    gap = 1,
    children = {
      heading,
      ui.hbox({
        gap = 1,
        flex = 1,
        children = { theme_box, preview_box },
      }),
      palette_box,
    },
  })
  app:set_body(ui.padding({ padding = 1, content = body }))

  -- State changes ----------------------------------------------------------
  refresh = function()
    local def = hl.current_theme() or theme.get(state.theme)
    theme_title.text = (def and def.name) or state.theme
    theme_desc.text = (def and def.description) or ""
    status_badge.text = string.format(" %s / %s ",
        state.border_style, state.transparent and "clear" or "solid")
    theme_title:invalidate()
    theme_desc:invalidate()
    status_badge:invalidate()
    theme_list:invalidate()
    app:invalidate()
  end

  apply = function(name, silent)
    local def, err = app:set_theme(name, { transparent = state.transparent })
    if not def then
      app:error(err or ("unknown theme: " .. tostring(name)))
      return false
    end
    state.theme = name
    state.transparent = hl.transparent
    for i, candidate in ipairs(state.themes) do
      if candidate == name then
        state.index = i
        break
      end
    end
    local flat_on = flat(name)
    theme_header.visible = flat_on
    preview_header.visible = flat_on
    palette_header.visible = flat_on
    theme_box.border = not flat_on
    preview_box.border = not flat_on
    palette_box.border = not flat_on
    theme_box.fill = flat_on and "Normal" or nil
    preview_box.fill = flat_on and "Normal" or "NormalFloat"
    palette_box.fill = flat_on and "Normal" or nil
    theme_header:invalidate()
    preview_header:invalidate()
    palette_header:invalidate()
    theme_box:invalidate()
    preview_box:invalidate()
    palette_box:invalidate()
    theme_list:set_index(state.index, true)
    refresh()
    app:layout_invalidate()
    if not silent then
      app:info("Theme: " .. name)
    end
    return true
  end

  local function next_theme()
    local index = (state.index % #state.themes) + 1
    apply(state.themes[index])
  end

  local function toggle_transparency()
    state.transparent = not state.transparent
    apply(state.theme, true)
    app:info(state.transparent and "Transparent surfaces" or "Opaque surfaces")
  end

  local function cycle_glyphs()
    local index = 1
    for i, style in ipairs(BORDER_STYLES) do
      if style == state.border_style then
        index = i
      end
    end
    state.border_style = BORDER_STYLES[(index % #BORDER_STYLES) + 1]
    preview_box.border_style = state.border_style
    preview_box:invalidate()
    refresh()
    app:info("Border glyphs: " .. state.border_style)
  end

  local function set_glyphs(style)
    for _, candidate in ipairs(BORDER_STYLES) do
      if candidate == style then
        state.border_style = style
        preview_box.border_style = style
        preview_box:invalidate()
        refresh()
        app:success("Border glyphs: " .. style)
        return true
      end
    end
    app:error("unknown glyph style: " .. tostring(style))
    return false
  end

  -- Bindings and commands ---------------------------------------------------
  app:bind("t", next_theme, "Next theme")
  app:bind("x", toggle_transparency, "Toggle transparency")
  app:bind("g", cycle_glyphs, "Cycle border glyphs")
  app:bind("q", function(a)
    a:quit()
  end, "Quit")

  for i, name in ipairs(state.themes) do
    if i <= 9 then
      local selected = name
      app:bind(tostring(i), function()
        apply(selected)
      end, "Theme " .. selected)
    end
  end

  app:command("theme", function(arg)
    arg = tostring(arg or ""):gsub("%s+$", "")
    if arg == "" or arg == "list" then
      app:info(table.concat(state.themes, ", "))
      return
    end
    apply(arg)
  end, "Switch theme by name")

  app:command("theme-next", next_theme, "Cycle to the next theme")
  app:command("transparency", toggle_transparency, "Toggle transparent surfaces")
  app:command("glyphs", function(arg)
    arg = tostring(arg or ""):gsub("%s+$", "")
    if arg == "" then
      cycle_glyphs()
    else
      set_glyphs(arg)
    end
  end, "Cycle or set border glyphs")

  refresh()

  app.theme_demo = {
    state = state,
    themes = state.themes,
    list = theme_list,
    sidebar = theme_box,
    preview = preview_box,
    palette = palette,
    palette_box = palette_box,
    apply = apply,
    next_theme = next_theme,
    toggle_transparency = toggle_transparency,
    cycle_glyphs = cycle_glyphs,
    set_glyphs = set_glyphs,
  }

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

return M
