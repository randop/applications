-- Aurora theme plugin.
--
-- Run standalone with:  build/tuipro --plugin plugins/aurora
-- Or register it from a script:
--   local plugin = require("tuipro.plugins").load("aurora")
--   plugin.register(require("tuipro"))
--   require("tuipro.hl").set_theme("aurora")
--
-- The plugin is a normal tuipro plugin.  It registers a complete theme
-- (palette, highlight groups and glyphs) and, when run, opens the theme
-- showcase demo.  Themes are inherited from "tuipro-dark", so Aurora only
-- overrides the roles and groups it wants to change.
local M = {}

M.theme = {
  name = "aurora",
  base = "tuipro-dark",
  description = "Northern-lights palette with mint, ice and violet accents.",
  colors = {
    bg = "#0b1020",
    surface = "#121a2e",
    surface2 = "#1a2540",
    panel = "#101728",
    raised = "#1a2540",
    border = "#2b3b5e",
    text = "#d7e3f4",
    strong = "#f4f9ff",
    white = "#e8f1ff",
    black = "#070b14",
    muted = "#8395b5",
    faint = "#5a6a8a",
    accent = "#63f0c8",
    accent2 = "#b794ff",
    blue = "#6cb6ff",
    purple = "#b794ff",
    cyan = "#65e0e8",
    green = "#7ee787",
    yellow = "#e9d38a",
    orange = "#ffb86b",
    red = "#ff7b8a",
    pink = "#ff8fd0",
    code = "#cfe6ff",
    shadow = "#050811",
    selection = "#173a3b",
    selection_detail = "#63f0c8",
    add_bg = "#103125",
    del_bg = "#351827",
    add_text = "#a8f0c0",
    add_num = "#5fbf85",
    del_text = "#ffb3bd",
    del_num = "#c76b7a",
    todo_done = "#68789b",
    pill_text = "#05201a",
  },
  groups = {
    Normal = { fg = "text", bg = "bg" },
    NormalFloat = { fg = "text", bg = "panel" },
    Border = { fg = "border", bg = "surface" },
    BorderFocus = { fg = "accent", bg = "surface", bold = true },
    Title = { fg = "accent", bg = "surface", bold = true },
    StatusLineMode = { fg = "pill_text", bg = "accent", bold = true },
    StatusLineAccent = { fg = "accent", bg = "surface2", bold = true },
    ListSelected = { fg = "pill_text", bg = "accent", bold = true },
    MenuSelected = { fg = "pill_text", bg = "accent", bold = true },
    TableSelected = { fg = "pill_text", bg = "accent", bold = true },
    ButtonPrimary = { fg = "pill_text", bg = "green", bold = true },
    ButtonPrimaryFocus = {
      fg = "pill_text", bg = "green", bold = true, underline = true,
    },
    TabActive = { fg = "pill_text", bg = "accent", bold = true },
    PopupTitle = { fg = "accent", bg = "panel", bold = true },
  },
  glyphs = {
    button = { left = "«", right = "»" },
    list = { marker = "❯" },
    menu = { marker = "❯" },
    spinner = {
      frames = { "⣾", "⣽", "⣻", "⢿", "⡿", "⣟", "⣯", "⣷" },
      idle = "◉",
    },
    progress = { fill = "▓", track = "░" },
    scrollbar = { vertical_thumb = "█" },
    input = { mask = "*" },
    text = { ellipsis = "→" },
  },
}

-- Register (or refresh) the theme in the global registry.
function M.register(tuipro)
  local t = tuipro or require("tuipro")
  return t.theme.register(M.theme)
end

-- Register and activate the theme.
function M.apply(tuipro, opts)
  local t = tuipro or require("tuipro")
  M.register(t)
  return t.hl.set_theme(M.theme.name, opts)
end

-- Plugins installed into an existing app register the theme and add a
-- command for re-applying it later.
function M.setup(tuipro, _, app)
  local t = tuipro or require("tuipro")
  M.register(t)
  if app then
    app:set_theme(M.theme.name)
    app:command("aurora", function(_, a)
      a:set_theme(M.theme.name)
      a:success("Aurora theme applied")
    end, "Apply the aurora theme")
  end
end

-- Standalone entry point used by `tuipro --plugin plugins/aurora`.
function M.run()
  local t = require("tuipro")
  M.register(t)
  return require("tuipro.demo_theme").run({ theme = M.theme.name })
end

return M
