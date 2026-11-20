-- Theme definitions and the theme registry.
--
-- A theme is a plain table:
--
--   {
--     name = "my-theme",
--     base = "tuipro-dark",            -- optional parent theme
--     description = "one line",
--     colors = { accent = "#...", ... },   -- palette roles
--     groups = { Normal = { ... }, ... },  -- highlight groups
--     glyphs = { list = { marker = ">" } }, -- optional border/list glyphs
--   }
--
-- Colors are 0xRRGGBB integers or #rrggbb strings.  Group colors may refer to
-- palette role names (for example "accent") so a theme only needs to override
-- the roles it cares about.  Plugin themes are registered with
-- require("tuipro.theme").register() and activated with tuipro.hl.set_theme().
--
-- The neutral surface roles (bg, surface, surface2, panel, shadow) are the ones
-- hl.set_transparent() can turn into the terminal default at runtime.  This
-- also applies to custom themes: neutral roles are detected from their resolved
-- colors.
--
-- A theme may carry a `styling` table describing how the theme showcase should
-- render it.  It is optional metadata, not part of the highlight model:
--   { styling = { flat = true } }
-- Themes that want to mimic a borderless editor (normal panes share the Normal
-- background and splits use WinSeparator colors) set `flat`; the showcase then
-- drops the drawn pane borders and shows plain titles instead.
local M = {}

M.default_name = "tuipro-dark"
M.name = M.default_name

-- ---------------------------------------------------------------------------
-- Default glyphs (non-color UI aspects)
-- ---------------------------------------------------------------------------

M.glyphs = {
  border = {
    rounded = { tl = "╭", tr = "╮", bl = "╰", br = "╯", h = "─", v = "│" },
    single = { tl = "┌", tr = "┐", bl = "└", br = "┘", h = "─", v = "│" },
    double = { tl = "╔", tr = "╗", bl = "╚", br = "╝", h = "═", v = "║" },
    heavy = { tl = "┏", tr = "┓", bl = "┗", br = "┛", h = "━", v = "┃" },
    ascii = { tl = "+", tr = "+", bl = "+", br = "+", h = "-", v = "|" },
  },
  canvas = { horizontal = "─", vertical = "│" },
  button = { left = "[", right = "]" },
  list = {
    marker = "›",
    marker_unselected = " ",
    checked = "[x]",
    unchecked = "[ ]",
  },
  menu = { marker = "›", marker_unselected = " " },
  separator = {
    single = "─",
    double = "═",
    heavy = "━",
    dashed = "╌",
    dotted = "┄",
    ascii = "-",
    vertical = "│",
  },
  scrollbar = {
    vertical_track = "│",
    vertical_thumb = "┃",
    horizontal_track = "─",
    horizontal_thumb = "━",
  },
  progress = {
    fill = "█",
    track = "░",
    partials = { "", "▏", "▎", "▍", "▌", "▋", "▊", "▉" },
  },
  spinner = {
    frames = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" },
    idle = "◍",
    done = "✔",
    error = "✖",
  },
  input = { mask = "•", cursor = "bar" },
  text = { ellipsis = "…" },
  table = { vertical = "│", horizontal = "─", cross = "┼" },
}

-- ---------------------------------------------------------------------------
-- Palette roles
-- ---------------------------------------------------------------------------

M.colors = {
  bg = "#14161d",
  surface = "#1b1e27",
  surface2 = "#232735",
  panel = "#1f2330",
  raised = "#232735",
  border = "#333a4d",
  text = "#d5dae8",
  strong = "#ffffff",
  white = "#e7ebf7",
  black = "#0d0f14",
  muted = "#7a839d",
  faint = "#4d556b",
  accent = "#7aa2f7",
  accent2 = "#bb9af7",
  blue = "#5c9cf5",
  purple = "#bb9af7",
  cyan = "#7dcfff",
  green = "#9ece6a",
  yellow = "#e0af68",
  orange = "#ff9e64",
  red = "#f7768e",
  pink = "#ff7eb6",
  code = "#c0caf5",
  shadow = "#08090c",
  selection = "#2b211b",
  selection_detail = "#c98a58",
  add_bg = "#15251a",
  del_bg = "#2a1618",
  add_text = "#a8e6b4",
  add_num = "#5d9a6b",
  del_text = "#f0b0b5",
  del_num = "#a5656b",
  todo_done = "#6f6f6f",
  pill_text = "#1a1208",
}

-- ---------------------------------------------------------------------------
-- Core highlight groups
-- ---------------------------------------------------------------------------

M.groups = {
  Normal = { fg = "text", bg = "bg" },
  NormalFloat = { fg = "text", bg = "panel" },
  Dim = { fg = "muted" },
  Muted = { fg = "muted" },
  Faint = { fg = "faint" },
  Border = { fg = "border", bg = "surface" },
  BorderFocus = { fg = "accent", bg = "surface", bold = true },
  Title = { fg = "accent", bg = "surface", bold = true },
  Subtitle = { fg = "muted", bg = "surface", italic = true },
  Header = { fg = "accent", bold = true },
  Accent = { fg = "accent", bold = true },
  Accent2 = { fg = "accent2", bold = true },
  Success = { fg = "green" },
  Warning = { fg = "yellow" },
  Error = { fg = "red", bold = true },
  Info = { fg = "cyan" },
  Link = { fg = "cyan", underline = true },

  StatusLine = { fg = "text", bg = "surface2" },
  StatusLineMode = { fg = "bg", bg = "accent", bold = true },
  StatusLineAccent = { fg = "accent", bg = "surface2", bold = true },
  StatusLineInfo = { fg = "muted", bg = "surface2" },
  StatusLineError = { fg = "bg", bg = "red", bold = true },
  StatusLineWarning = { fg = "bg", bg = "yellow", bold = true },

  -- Application chrome.  These link to the statusline/command-line groups so
  -- themes can recolor the chrome independently without touching widgets.
  AppTitle = { link = "StatusLineAccent" },
  AppMode = { link = "StatusLineMode" },
  AppModeCommand = { link = "StatusLineWarning" },
  AppModePopup = { link = "StatusLineAccent" },
  AppInfo = { link = "StatusLineInfo" },
  AppCommandLine = { link = "CommandLine" },
  AppCommandPrompt = { link = "CommandLinePrompt" },
  AppCommandInfo = { link = "CommandLineInfo" },
  AppCommandCursor = { link = "CommandLineCursor" },

  Tabline = { fg = "muted", bg = "surface" },
  TabActive = { fg = "bg", bg = "accent", bold = true },
  TabInactive = { fg = "muted", bg = "surface" },
  TabBadge = { fg = "surface", bg = "accent2", bold = true },

  Menu = { fg = "text", bg = "surface" },
  MenuItem = { fg = "text", bg = "surface" },
  MenuSelected = { fg = "bg", bg = "accent", bold = true },
  MenuSelectedInactive = { fg = "white", bg = "surface2", bold = true },
  MenuHeader = { fg = "accent", bg = "surface", bold = true },
  MenuKey = { fg = "yellow", bg = "surface" },
  MenuKeySelected = { fg = "bg", bg = "accent", bold = true },
  MenuDetail = { fg = "muted", bg = "surface" },
  MenuDisabled = { fg = "faint", bg = "surface" },

  List = { fg = "text", bg = "surface" },
  ListItem = { fg = "text", bg = "surface" },
  ListSelected = { fg = "bg", bg = "accent", bold = true },
  ListSelectedInactive = { fg = "white", bg = "surface2", bold = true },
  ListSelectedInfo = { fg = "surface2", bg = "accent" },
  ListHeader = { fg = "accent", bg = "surface", bold = true },
  ListDetail = { fg = "muted", bg = "surface" },
  ListMarker = { fg = "accent", bg = "surface", bold = true },
  ListMarkerSelected = { fg = "bg", bg = "accent", bold = true },
  ListIndex = { fg = "faint", bg = "surface" },
  ListIndexSelected = { fg = "surface2", bg = "accent" },
  ListEmpty = { fg = "muted", bg = "surface", italic = true },
  ListDisabled = { fg = "faint", bg = "surface" },
  ListCheck = { fg = "green", bg = "surface" },
  ListCheckSelected = { fg = "bg", bg = "accent", bold = true },

  TableHeader = { fg = "accent", bg = "surface2", bold = true },
  TableBorder = { fg = "border", bg = "surface2" },
  TableRow = { fg = "text", bg = "surface" },
  TableRowAlt = { fg = "text", bg = "bg" },
  TableSelected = { fg = "bg", bg = "accent", bold = true },
  TableSelectedInactive = { fg = "white", bg = "surface2", bold = true },
  TableSelectedMuted = { fg = "surface2", bg = "accent" },
  TableEmpty = { fg = "muted", bg = "surface", italic = true },

  Popup = { fg = "text", bg = "panel" },
  PopupBorder = { fg = "accent", bg = "panel", bold = true },
  PopupTitle = { fg = "accent", bg = "panel", bold = true },
  PopupFooter = { fg = "muted", bg = "panel" },
  Shadow = { fg = "shadow", bg = "shadow" },

  Input = { fg = "text", bg = "surface2" },
  InputFocus = { fg = "text", bg = "surface2" },
  InputDisabled = { fg = "faint", bg = "surface" },
  InputPlaceholder = { fg = "muted", bg = "surface2", italic = true },
  InputPrompt = { fg = "accent", bg = "surface2", bold = true },
  InputPromptFocus = { fg = "accent", bg = "surface2", bold = true },
  InputCursor = { fg = "bg", bg = "accent" },
  InputBorder = { fg = "border", bg = "surface2" },
  InputBorderFocus = { fg = "accent", bg = "surface2" },

  ProgressLabel = { fg = "muted" },
  ProgressFill = { fg = "green", bold = true },
  ProgressFillAlt = { fg = "accent", bold = true },
  ProgressTrack = { fg = "border" },
  ProgressPercent = { fg = "text", bold = true },

  Spinner = { fg = "accent", bold = true },
  SpinnerLabel = { fg = "muted" },
  SpinnerDone = { fg = "green", bold = true },
  SpinnerError = { fg = "red", bold = true },

  Button = { fg = "text", bg = "surface2" },
  ButtonFocus = { fg = "bg", bg = "accent", bold = true },
  ButtonPrimary = { fg = "bg", bg = "green", bold = true },
  ButtonPrimaryFocus = { fg = "bg", bg = "green", bold = true, underline = true },
  ButtonDanger = { fg = "bg", bg = "red", bold = true },
  ButtonDangerFocus = { fg = "bg", bg = "red", bold = true, underline = true },
  ButtonDisabled = { fg = "faint", bg = "surface" },
  ButtonHotkey = { fg = "yellow", bg = "surface2", bold = true },
  ButtonHotkeyFocus = { fg = "bg", bg = "accent", bold = true },

  ScrollbarTrack = { fg = "border", bg = "surface" },
  ScrollbarThumb = { fg = "muted", bg = "surface" },
  ScrollbarThumbFocus = { fg = "accent", bg = "surface" },

  CommandLine = { fg = "text", bg = "surface" },
  CommandLinePrompt = { fg = "accent", bg = "surface", bold = true },
  CommandLineCursor = { fg = "bg", bg = "accent" },
  CommandLineInfo = { fg = "muted", bg = "surface" },
  Message = { fg = "cyan", bg = "surface" },
  MessageInfo = { fg = "cyan", bg = "surface" },
  MessageWarn = { fg = "yellow", bg = "surface" },
  MessageError = { fg = "red", bg = "surface", bold = true },
  MessageSuccess = { fg = "green", bg = "surface" },

  Separator = { fg = "border" },
  SeparatorFocus = { fg = "accent" },
  Key = { fg = "yellow", bold = true },
  KeyDesc = { fg = "muted" },
  Badge = { fg = "bg", bg = "accent", bold = true },
  BadgeAlt = { fg = "bg", bg = "accent2", bold = true },
  BadgeGreen = { fg = "bg", bg = "green", bold = true },
  Pill = { fg = "surface", bg = "accent2", bold = true },
  Breadcrumb = { fg = "muted" },
  BreadcrumbActive = { fg = "text", bold = true },
  Toast = { fg = "text", bg = "surface2" },
  ToastBorder = { fg = "accent", bg = "surface2" },
  ToastInfo = { fg = "cyan", bg = "surface2", bold = true },
  ToastWarn = { fg = "yellow", bg = "surface2", bold = true },
  ToastError = { fg = "red", bg = "surface2", bold = true },
  ToastSuccess = { fg = "green", bg = "surface2", bold = true },
}

-- ---------------------------------------------------------------------------
-- Registry
-- ---------------------------------------------------------------------------

local registry = {}
local order = {}

local function copy(t)
  local out = {}
  for k, v in pairs(t or {}) do
    out[k] = v
  end
  return out
end

local function deep_copy(t)
  local out = {}
  for k, v in pairs(t or {}) do
    if type(v) == "table" then
      out[k] = deep_copy(v)
    else
      out[k] = v
    end
  end
  return out
end

-- Arrays (spinner frames, progress partials, attribute lists) are replaced
-- wholesale; merging them positionally would mix a shorter override with
-- leftover entries from the base theme.
local function is_array(t)
  if type(t) ~= "table" then
    return false
  end
  local count = 0
  for k in pairs(t) do
    if type(k) ~= "number" or k < 1 or k % 1 ~= 0 then
      return false
    end
    count = count + 1
  end
  if count == 0 then
    return false
  end
  for i = 1, count do
    if t[i] == nil then
      return false
    end
  end
  return true
end

-- Merge b over a.  Nested tables are merged recursively so a theme can
-- override a single glyph or a single attribute of a group; arrays replace.
local function deep_merge(a, b)
  local out = deep_copy(a or {})
  for k, v in pairs(b or {}) do
    if type(v) == "table" and is_array(v) then
      out[k] = deep_copy(v)
    elseif type(v) == "table" and type(out[k]) == "table" then
      out[k] = deep_merge(out[k], v)
    elseif type(v) == "table" then
      out[k] = deep_copy(v)
    else
      out[k] = v
    end
  end
  return out
end

local function resolve(def)
  local out = {
    name = def.name,
    description = def.description,
    base = def.base,
    styling = deep_copy(def.styling or {}),
  }
  if def.base then
    local parent = registry[def.base]
    if not parent then
      error("theme " .. tostring(def.name) .. " has unknown base " ..
          tostring(def.base), 3)
    end
    out.colors = deep_merge(parent.colors, def.colors)
    out.groups = deep_merge(parent.groups, def.groups)
    out.glyphs = deep_merge(parent.glyphs, def.glyphs)
    out.styling = deep_merge(parent.styling or {}, out.styling)
  else
    -- No explicit parent: use the default theme as the implicit baseline so
    -- a theme can override only the roles and groups it cares about.
    out.colors = deep_merge(M.colors, def.colors)
    out.groups = deep_merge(M.groups, def.groups)
    out.glyphs = deep_merge(M.glyphs, def.glyphs)
  end
  out.glyphs = deep_merge(M.glyphs, out.glyphs)
  return out
end

-- Register (or replace) a theme.  Themes that specify `base` inherit its
-- colors, groups and glyphs before applying their own overrides.
function M.register(def)
  if type(def) ~= "table" or type(def.name) ~= "string" or def.name == "" then
    error("theme.register expects a table with a non-empty name", 2)
  end
  local resolved = resolve(def)
  if not registry[resolved.name] then
    order[#order + 1] = resolved.name
  end
  registry[resolved.name] = resolved
  return resolved
end

function M.get(name)
  if type(name) == "table" then
    return name
  end
  return registry[name or M.default_name]
end

function M.names()
  local out = copy(order)
  table.sort(out)
  return out
end

function M.list()
  local out = {}
  for _, name in ipairs(order) do
    out[#out + 1] = registry[name]
  end
  return out
end

function M.unregister(name)
  if not registry[name] then
    return false
  end
  registry[name] = nil
  for i, n in ipairs(order) do
    if n == name then
      table.remove(order, i)
      break
    end
  end
  return true
end

-- Register the built-in themes.
M.description = "The default tuipro dark theme."
M.register(M)

M.register({
  name = "tuipro-light",
  base = M.default_name,
  description = "A light palette with the same semantic highlight groups.",
  colors = {
    bg = "#f6f7fb",
    surface = "#eef1f8",
    surface2 = "#e3e8f4",
    panel = "#fbfcff",
    raised = "#e3e8f4",
    border = "#c2cbe0",
    text = "#1e2433",
    strong = "#0b0e16",
    white = "#111522",
    black = "#ffffff",
    muted = "#5c6883",
    faint = "#8d99b3",
    accent = "#2d5bd7",
    accent2 = "#7a3fd1",
    blue = "#2456c9",
    purple = "#7a3fd1",
    cyan = "#0e7490",
    green = "#2f8a46",
    yellow = "#9a6700",
    orange = "#b45309",
    red = "#c4384e",
    pink = "#c026d3",
    code = "#24304a",
    shadow = "#d6dbe8",
    selection = "#dbe6ff",
    selection_detail = "#2d5bd7",
    add_bg = "#dff3e4",
    del_bg = "#fbe2e5",
    add_text = "#1f6b35",
    add_num = "#3d8a51",
    del_text = "#9b2638",
    del_num = "#b34a5b",
    todo_done = "#8d99b3",
    pill_text = "#ffffff",
  },
  groups = {
    Normal = { fg = "text", bg = "bg" },
    NormalFloat = { fg = "text", bg = "panel" },
  },
})

return M
