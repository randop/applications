# Widgets and layout

All constructors live in `tuipro.ui`. Most accept an options table; a few accept
a convenient shorthand:

```lua
local ui = require("tuipro").ui

ui.text("hello")                 -- { text = "hello" }
ui.list({ "One", "Two" })        -- { items = { "One", "Two" } }
ui.menu({ "Open", "Quit" })
ui.progress(42)                  -- { value = 42 }
ui.spinner("Working…")           -- { label = "Working…" }
ui.button("Quit", function() end)
ui.separator("Section")
ui.tabs({ "One", "Two" }, { on_change = fn })
ui.vbox({ a, b })                -- { children = { a, b } }
ui.hbox({ a, b })
ui.stack({ a, b })
ui.split({ a, b })
```

`ui.Input.new` and the exported classes are also available (`ui.Text`,
`ui.Box`, `ui.List`, `ui.Menu`, `ui.Table`, `ui.Input`, `ui.Tabs`,
`ui.Progress`, `ui.Spinner`, `ui.Button`, `ui.Popup`, `ui.Separator`,
`ui.Scrollbar`, `ui.Viewport`, `ui.Statusline`).

## Common options

Every widget inherits these fields:

| Option | Meaning |
| --- | --- |
| `width`, `height` (`w`, `h`) | Requested size. A fixed size is respected in a box layout. |
| `flex` | Share of the remaining main-axis space. Without `flex`, a child keeps its fixed/preferred size. |
| `visible` | Default `true`; hidden widgets skip layout, rendering and input. |
| `focusable` | Some widgets default to `true` (List, Menu, Table, Input, Tabs, Button). |
| `name` | Label used by the default statusline and help screens. |
| `hl` | Default highlight group. |
| `Tooltip`/`tooltip` | Metadata only; no built-in tooltip renderer. |

A widget gets absolute 0-based `x`, `y`, `w`, `h` fields after layout. Call
`widget:invalidate()` after changing data that affects rendering, and
`widget:invalidate_layout()` after changing the tree or requested sizes.

## Containers

All containers derive from `Container`, which supports:

| Method | Description |
| --- | --- |
| `container:add(child[, opts])` | Append a child, detaching it from a previous parent. |
| `container:remove(child)` | Remove a child and detach it. |
| `container:clear()` | Remove and detach all children. |
| `container:visible_children()` | Array of visible children. |
| `container.children` | Raw child array. |

`Container` itself behaves like a VBox for preferred sizes and lays children out
without arranging them. Use the concrete classes.

### VBox

Stacks children vertically.

```lua
ui.vbox({
  gap = 1,
  justify = "start",     -- start | center | end (when no child flexes)
  align = "stretch",     -- stretch | start | center | right | end
  fill = "Normal",       -- optional background
  padding = nil,         -- metadata only; use ui.padding for real padding
  children = { ... },
})
```

- Fixed children use `height` or `preferred_height()`.
- Children with `flex > 0` share the remaining height by weight; the last flex
  child receives the rounding remainder.
- Children without an explicit `width` stretch to the container width.
- `align` positions fixed-width children horizontally; `start`, `center`,
  `right`/`end` are accepted.

### HBox

Stacks children horizontally.

```lua
ui.hbox({
  gap = 1,
  justify = "start",     -- start | center | end (when no child flexes)
  valign = "stretch",    -- stretch | start | center | end | bottom
  fill = "Normal",
  children = { ... },
})
```

- Fixed children use `width` or `preferred_width()`.
- Flexible children share leftover width by `flex` weight.
- Fixed-height children align vertically with `valign`.

### Padding

Adds interior space around a single child.

```lua
ui.padding({
  padding = 1,                                  -- all sides
  -- padding = { left = 2, right = 2 },         -- named
  -- padding = { 1, 2, 1, 2 },                  -- top, right, bottom, left
  content = widget,
  fill = "Normal",
})
```

The numeric/table parser is exported as `tuipro.container.parse_padding` and
returns `top, right, bottom, left`.

### Center

Centers one child using its fixed or preferred size, clamped to the available
space.

```lua
ui.center({ content = widget, fill = "Normal" })
```

### Stack

Gives every visible child the same rectangle. This is the easiest way to build
pages, layers or tabs.

```lua
local stack = ui.stack({ page1, page2, page3 })

stack:show(2)             -- only page2 is visible
local widget, index = stack:active()
```

`Stack:show(index)` toggles `visible`; `Stack:active()` returns the first
visible child and its 1-based index. `ui.stack` accepts an array of children or
`{ children = ... }`.

### Split

Neovim-style panes with a one-cell separator.

```lua
local split = ui.split({
  direction = "vertical",        -- vertical = side by side, horizontal = stacked
  show_separator = true,
  separator_style = "Separator",
  separator_focus_style = "SeparatorFocus",
})

split:add(left, { size = 24 })    -- fixed 24 cells
split:add(main, { weight = 1 })   -- takes the rest
split:add(inspector, { weight = 2 })
```

`direction` also accepts `orientation`. Panes with `size` are fixed; panes
without one share the remaining axis by `weight` (default 1). The separator is
drawn by `Split:render` using `│` or `─`. `split:add(child)` also accepts these
options on the child itself (`child.split_size`, `child.split_weight`).

### Spacer

`ui.spacer()` is an empty flexible box (`flex = 1`). `ui.spacer(height)` is an
empty fixed-height box, the common vertical gap. For a horizontal fixed gap,
use `ui.box({ border = false, width = n })`.

## Box

A bordered container with a title, optional padding and one content area.

```lua
ui.box({
  title = "Panel",
  title_align = "left",         -- left | center | right
  title_hl = "Title",
  title_focus_hl = "Title",
  border = true,
  border_style = "rounded",     -- rounded | single | double | heavy | ascii
  chars = nil,                  -- custom { tl, tr, bl, br, h, v }
  border_hl = "Border",
  border_focus_hl = "BorderFocus",
  fill = "NormalFloat",
  padding = 1,                  -- number or { top, right, bottom, left }
  content_align = "stretch",    -- stretch | center | right (horizontal)
  content = widget,
})
```

`Box` is not focusable. It highlights its border and title when it contains
focus (`focused` or `contains_focus`). The laid-out content rectangle is
available as `content_x`, `content_y`, `content_w`, `content_h`. Content height
is reduced when the child has a fixed `height`. `Box:preferred_width()` also
accounts for the title width.

## Text

Static or dynamic text with optional wrapping and alignment.

```lua
ui.text({
  text = "hello",               -- string, number or function(self, app)
  wrap = false,
  align = "left",               -- left | center | right
  hl = "Normal",
  padding = 0,
  max_lines = nil,
  ellipsis = nil,               -- default "…"; false disables the cut marker
  height = nil,
})
```

- A function `text` is evaluated at render/preferred-size time as
  `text(self, app)`.
- `wrap = true` uses word wrapping that breaks overlong words and expands tabs
  to four spaces.
- `max_lines` limits visible lines; when content is cut the last line receives
  the ellipsis.
- `width`/`height` may be fixed. Without them, preferred width is the longest
  line and preferred height is the number of resulting lines.

Useful methods: `text:value()`, `text:lines(width)`.

## List

A scrollable, selectable list and the base class for Menu.

```lua
local list = ui.list({
  items = {
    { label = "First", detail = "one" },
    { label = "Second", hl = "Accent", detail_hl = "Info" },
    { header = true, label = "Group" },
    { label = "Disabled", disabled = true },
    { label = "Toggle", checked = true },
  },
  selected = 1,
  marker = "›",
  marker_unselected = " ",
  show_marker = true,
  show_index = false,
  index_width = nil,            -- default width of the item count
  show_scrollbar = false,
  multiple = false,
  wrap = false,
  empty_text = "No items",
  detail_align = "right",       -- right | left
  wheel_step = 3,
  select_disabled = false,
  styles = { ... },             -- override any style name below
  on_select = function(item, index, self) end,
  on_activate = function(item, index, self) end,
  on_toggle = function(item, index, self) end,
  on_change = function(item, index, self) end,
})
```

Item fields:

| Field | Meaning |
| --- | --- |
| `label`, `text`, `name`, `value` | Display text, in that fallback order. Strings and numbers are converted automatically. |
| `detail`, `shortcut`, `hint` | Secondary text shown on the right or after the label. |
| `hl`, `detail_hl` | Per-item highlight groups. |
| `header` | Non-selectable group heading. |
| `disabled` | Non-selectable unless `select_disabled` is set. |
| `selectable = false` | Explicitly non-selectable. |
| `checked` | Checkbox state when `multiple` is enabled. |

Methods:

| Method | Description |
| --- | --- |
| `list:count()` / `list:item_at(i)` | Item count / lookup. |
| `list:current()` / `current_index()` | Selected item / 1-based index. |
| `list:set_items(items[, selected, silent])` | Replace items and select an index. |
| `list:set_index(index[, silent])` | Select a selectable index; returns whether it changed. |
| `list:move(delta)` | Move by `delta` selectable items, honoring `wrap`. |
| `list:page(delta)` | Move by one visible page (the widget height). |
| `list:move_first()` / `move_last()` | Jump to the first/last selectable item. |
| `list:scroll_by(delta)` | Scroll the viewport without changing selection. |
| `list:activate()` | Invoke `on_activate`, or toggle in multiple mode. |
| `list:toggle([index])` | Flip `checked` and invoke `on_toggle`. |
| `list:checked_items()` / `checked_count()` | Selection helpers. |
| `list:is_selectable(index)` | Header/disabled/selectable checks. |
| `first_selectable(dir)` / `last_selectable()` | Find edge selectable items. |

Keys: `Up`/`k`/`C-p`, `Down`/`j`/`C-n`, `PageUp`/`C-u`, `PageDown`/`C-d`,
`Home`/`g`, `End`/`G`/`S-g`, `Enter`, and `Space` (activate, or toggle when
`multiple`). The mouse moves the selection on press and activates on release
over the same row; the wheel moves by `wheel_step` items.

`on_select` and `on_change` fire together whenever the selection changes;
`set_items`/`set_index` accept `silent = true` to suppress callbacks.

Style overrides: `item`, `selected`, `selected_inactive`, `detail`,
`detail_selected`, `disabled`, `marker`, `marker_selected`, `index`,
`index_selected`, `empty`, `check`, `check_selected`, `header`,
`scrollbar_track`, `scrollbar_thumb`, `scrollbar_thumb_focus`.

## Menu

A List subclass with menu-oriented styles and a shortcut column.

```lua
ui.menu({
  items = {
    { label = "Open", shortcut = "C-o" },
    { label = "Quit", shortcut = "C-q", action = "quit" },
  },
  name = "Menu",
  on_select = function(item, index, self) end,
  on_activate = function(item, index, self) end,
})
```

`Menu` displays `item.shortcut` (or `item.key`) as the detail, hides indices,
and defaults to the menu style groups: `MenuItem`, `MenuSelected`,
`MenuSelectedInactive`, `MenuHeader`, `MenuKey`, `MenuKeySelected`,
`MenuDetail`, `MenuDisabled`. Everything else comes from List.

## Table

A scrollable table with fixed, flexible and inferred columns.

```lua
local table_widget = ui.table({
  columns = {
    { title = "Time", key = "time", width = 7, align = "center" },
    { title = "Event", key = "event", flex = 1 },
    {
      title = "Level", key = "level", width = 8, align = "center",
      format = function(value, row, col)
        return { text = tostring(value), hl = "Success" }
      end,
    },
  },
  rows = {
    { time = "09:41", event = "Started", level = "OK" },
  },
  show_header = true,
  header_separator = true,
  show_grid = false,
  cell_padding = 1,             -- alias: padding
  empty_text = "No data",
  wheel_step = 3,
  selectable = true,
  selected = 1,
  styles = { ... },
  on_select = function(row, index, self) end,
  on_activate = function(row, index, self) end,
})
```

Column fields:

| Field | Meaning |
| --- | --- |
| `title`, `label`, `header` | Header text. |
| `key` | Row table key. Without `key`, `index` is used. |
| `index` | 1-based numeric column default. |
| `width` | Fixed width in cells. |
| `flex` | Share of leftover width when the table is wider than intrinsic. |
| `min_width` | Minimum width when shrinking. |
| `align` | `left`, `center` or `right`. |
| `hl`, `title_hl` | Cell/header highlight groups. |
| `format(value, row, col)` | Return a string or `{ text, hl, align }`. |

Rows can be arrays (`row[1]`, `row[2]`, ...) or keyed tables. If no columns are
provided, the first row is inspected: array rows get `Column 1..n`, keyed rows
get alphabetically sorted string keys.

Methods: `count()`, `selected_row()`, `set_rows(rows[, selected, silent])`,
`set_columns(columns)`, `set_index(index[, silent])`, `move(delta)`,
`page(delta)`, `activate()`.

Keys: `Up`/`k`/`C-p`, `Down`/`j`/`C-n`, `PageUp`/`C-u`, `PageDown`/`C-d`,
`Home`/`g`, `End`/`G`/`S-g`, `Enter`. Mouse press selects a row and release
activates it; the wheel moves the selection.

Style overrides: `header`, `border`, `row`, `row_alt`, `selected`,
`selected_inactive`, `selected_muted`, `empty`, `scrollbar_track`,
`scrollbar_thumb`, `scrollbar_thumb_focus`.

Column widths are computed as follows: fixed widths are honored; intrinsic
width includes the header, all cell text and `2 * cell_padding`; when space is
left over it is split by `flex` (or given to the last column); when space is
short, flexible columns shrink toward `min_width` before all columns shrink
toward 1.

## Input

A single-line editable text field with UTF-8 support.

```lua
local input = ui.input({
  value = "",
  prompt = "Name: ",
  placeholder = "Ada Lovelace",
  mask = nil,                   -- e.g. "•" or password = true for the default
  readonly = false,
  max_length = nil,
  width = nil,
  history = nil,                -- array; Up/Down navigate it
  clear_on_submit = false,
  hl = "Input",
  focus_hl = "InputFocus",
  placeholder_hl = "InputPlaceholder",
  prompt_hl = "InputPrompt",
  prompt_focus_hl = "InputPromptFocus",
  disabled_hl = "InputDisabled",
  on_change = function(text, self) end,
  on_submit = function(text, self, app) end,
  on_cancel = function(text, self, app) end,
})
```

Methods:

| Method | Description |
| --- | --- |
| `input:get()` / `set(text[, silent])` | Read or replace the value. |
| `input:submit()` / `cancel()` | Invoke the corresponding callback. |
| `input:insert(text)` | Insert filtered text at the cursor. |
| `input:backspace()` / `delete()` | Delete around the cursor. |
| `input:delete_word()` | Delete the previous word. |

Keys: printable text inserts; `Enter` submits; `Esc` cancels; `Backspace`,
`Delete`/`C-d`; `Left`/`C-b`, `Right`/`C-f`, `Home`/`C-a`, `End`/`C-e`;
`C-u`/`C-k` kill to start/end; `C-w` deletes a word; `Up`/`Down` walk history.
`Tab` is not consumed, so focus moves. Control characters are stripped and
newlines/tabs become spaces. Masking uses one mask character per input
character. The widget is one row high, scrolls horizontally to keep the cursor
visible, and shows a `bar` cursor when focused.

`on_change` receives `(text, self)`; `on_submit` and `on_cancel` receive
`(text, self, app)`. The mouse places the cursor. Bracketed paste inserts into
the focused input with control characters removed.

## Tabs

A one-row horizontal tab strip, usable as an app tabline or inside the body.

```lua
local tabs = ui.tabs({
  items = {
    { label = "One" },
    { title = "Two", badge = 3 },
    { text = "Three", badge = "!" },
  },
  active = 1,
  padding = 1,
  gap = 0,
  show_badges = true,
  active_hl = "TabActive",
  inactive_hl = "TabInactive",
  badge_hl = "TabBadge",
  fill = "Tabline",
  on_change = function(item, index, self) end,
})

-- shorthand
local tabs2 = ui.tabs({ "One", "Two" }, { on_change = fn })
```

Items may be strings, numbers or tables with `label`/`title`/`text` and an
optional `badge`. The strip scrolls its starting tab so the active tab stays
visible. Methods: `count()`, `item()`, `set_items(items[, active, silent])`,
`set_active(index[, silent])`, `next()`, `prev()`.

Keys: `Right`/`l`, `Left`/`h`, `Home`, `End`, `Enter`/`Space` (re-notifies).
A left click selects the tab under the pointer. `Tabs:preferred_width()`
returns the total segment width. Use `focusable = false` for passive tablines.

## Button

A focusable one-row button.

```lua
ui.button({
  label = "Save",               -- "&Save" marks S as the hotkey
  hotkey = "s",                 -- explicit hotkey (case-insensitive)
  on_press = function(button, app) end,
  primary = false,
  danger = false,
  brackets = true,              -- draw [ label ]
  padding = 1,
  align = "center",             -- left | center | right
  disabled = false,
  width = nil,
})
```

`Enter`/`Space` press the focused button. A hotkey also works while the button
is focused. The mouse presses on `press` and triggers on `release` over the
same widget. `button:press()` invokes the callback directly. Disabled buttons
are not focusable and do not react. Style groups: `Button`, `ButtonFocus`,
`ButtonPrimary`, `ButtonPrimaryFocus`, `ButtonDanger`, `ButtonDangerFocus`,
`ButtonDisabled`, `ButtonHotkey`, `ButtonHotkeyFocus`.

`ui.button("Quit", on_press, { primary = true })` is the shorthand form.

## Popup

A bordered floating container with a title, content, optional footer and a
right-aligned button row. Popups are opened through `App:open_popup`.

```lua
local popup = ui.popup({
  title = "Confirm",
  border = true,
  border_style = "rounded",     -- rounded | single | double | heavy
  border_hl = "PopupBorder",
  title_hl = "PopupTitle",
  fill = "Popup",
  footer = "optional footer",
  footer_hl = "PopupFooter",
  footer_align = "left",        -- left | center | right
  padding = 1,
  min_width = 30,
  min_height = 5,
  width = nil,
  height = nil,
  content = "Delete the file?", -- string/number becomes wrapped Text
  message = nil,                -- used when content is nil
  buttons = {
    { label = "Cancel" },
    { label = "Delete", danger = true,
      on_press = function(popup) ... end },
  },
})
```

Button definitions are converted to `Button` widgets. Their `on_press`
callback receives the popup, not the button. If a button has no callback, it
closes the popup. Entire pre-built button widgets may also be supplied.

Methods: `popup:close()` (through the app), `popup:set_content(widget_or_text)`,
`popup:add_button(def)`. The popup lays content above the footer and button
row. Preferred size accounts for content, title and buttons, then is clamped by
the app to the screen.

## Progress

A one-row progress bar with label, smooth partial cells and percentage.

```lua
ui.progress({
  value = 42,                   -- number or function(self, app)
  max = 100,
  label = "Downloading",        -- string or function(self, app)
  show_label = true,
  show_percent = true,
  percent_format = "%3d%%",
  fill_hl = "ProgressFill",
  track_hl = "ProgressTrack",
  label_hl = "ProgressLabel",
  percent_hl = "ProgressPercent",
  fill_char = "█",
  track_char = "░",
  smooth = true,
  width = nil,
})
```

Methods: `ratio()` (clamped 0–1), `set_value(value)`. Colors on the fill are
configurable, so each bar can use `ProgressFill` or `ProgressFillAlt`. With
`smooth`, fractional cells use `▏▎▍▌▋▊▉`. Percent is rounded to the nearest
integer.

## Spinner

An animated one-row spinner with an optional label.

```lua
local spinner = ui.spinner({
  frames = { "⠋", "⠙", ... },
  interval = 80,                -- milliseconds
  label = "Indexing…",          -- string or function(self, app)
  active = true,
  hl = "Spinner",
  label_hl = "SpinnerLabel",
  done_hl = "Success",
  error_hl = "Error",
  done_text = "✔",
  error_text = "✖",
  done_label = nil,
  error_label = nil,
  width = nil,
})

spinner:stop(true, "Done")      -- success state and optional final label
spinner:start("Working…")       -- resume
```

A spinner attaches a repeating timer through the app while `active`; detaching
stops it. When stopped, it draws a success/error glyph instead of a frame.
Methods: `start([label])`, `stop([success, label])`, `frame()`,
`text_and_style()`. `get_label()` returns the displayed label, which makes it
suitable in the default statusline.

## Separator

A horizontal or vertical rule, optionally with a centered label.

```lua
ui.separator()                                  -- horizontal line
ui.separator("Section")                         -- labeled line
ui.separator({ label = "Rules", height = 1 })
ui.separator({ direction = "vertical", height = 10 })
```

Options: `direction`/`orientation` (`"horizontal"` or `"vertical"`), `label` or
`title`, `hl` (default `"Separator"`), `label_hl` (default `"Subtitle"`),
`char`, `vchar`, `style` (`single`, `double`, `heavy`, `dashed`, `dotted`,
`ascii`), and fixed `width`/`height`. A vertical separator defaults to one cell
wide and draws `│`; a horizontal one defaults to one row high.

## Scrollbar

A standalone scrollbar for custom panes. List, Menu and Table draw their own
scrollbars through the internal `tuipro.ui.scroll` helper.

```lua
local bar = ui.scrollbar({
  total = 100,
  visible = 10,
  offset = 0,
  direction = "vertical",      -- vertical | horizontal
  track_hl = "ScrollbarTrack",
  thumb_hl = "ScrollbarThumb",
  thumb_focus_hl = "ScrollbarThumbFocus",
  on_change = function(offset, self) end,
  width = nil, height = nil,
})

bar:set(total, visible, offset)
bar:set_offset(offset)
```

`set_offset` clamps to `[0, total - visible]`, invokes `on_change` and returns
the new offset. Clicking maps the pointer position to an offset; the wheel
moves one step. The standalone widget is not focusable.

## Viewport

A scrollable pane for arbitrary content. The content child is laid out at its
natural height, clipped to the viewport rectangle, and a scrollbar is drawn on
the right edge when it does not fit.

```lua
local viewport = ui.viewport({
  content = ui.vbox({ gap = 1, children = form_widgets }),
  flex = 1,
  name = "Settings form",
  show_scrollbar = true,
  focus_reveal = true,     -- scroll focused children into view on focus change
  wheel_step = 3,
  content_height = nil,    -- override the child's preferred height
  max_height = nil,        -- cap preferred_height()
  fill = nil,              -- optional background
  track_hl = "ScrollbarTrack",
  thumb_hl = "ScrollbarThumb",
  thumb_focus_hl = "ScrollbarThumbFocus",
  on_change = function(offset, self) end,
})

viewport:set_offset(10)
viewport:scroll_by(3)
viewport:scroll_page(1)
viewport:set_content(widget)
```

Up/Down, PgUp/PgDn, Home/End and the mouse wheel scroll when the viewport or a
descendant has focus; clicking the scrollbar jumps to that position. `offset`
is clamped to `[0, total - visible_h]`. When focus moves to a child that is
outside the viewport, the viewport scrolls it into view; manual scrolling is
kept until the next focus change. Viewports are focusable by default so
keyboard scrolling works as soon as they are focused, and Tab traversal still
reaches the form widgets inside.

## Statusline

A one-row bar with left, center and right sections.

```lua
app:set_statusline(ui.statusline({
  left = "NORMAL",
  center = function(self, app) return " " .. os.date("%H:%M:%S") .. " " end,
  right = {
    { text = " q quit ", hl = "StatusLineInfo" },
  },
  background = "StatusLine",
  hl = "StatusLine",
  padding = 1,
  section_gap = "",
}))
```

Each side accepts a string, a section table `{ text, hl }`, an array of
sections/functions, or a function `(self, app)` returning any of those. The
`dynamic` option can return `{ left = ..., center = ..., right = ... }` and
takes precedence per side. Center sections are clipped between the left and
right content. `ui.statusline.section(text, hl)` builds a section table.

The default app statusline uses `app:status_sections()`, which shows mode
(`NORMAL`, `COMMAND`, `POPUP`), the title, the focused widget label and
`footer_hint`. `app:status_sections()` returns `{ left = ..., right = ... }`
and is useful in custom statuslines.

## Styling widget internals

Widgets normally take named highlight groups. List and Table also accept a
`styles` table that renames their internal groups, which is handy for embedding
several visual variants without redefining global groups:

```lua
ui.list({
  items = items,
  styles = {
    selected = "AccentSelected",
    marker = "Faint",
  },
})
```

All other widgets accept the specific `*_hl` options listed above (for example
`border_hl`, `title_hl`, `fill_hl`, `track_hl`). Style values may be group
names or inline tables; see [lua-api.md](lua-api.md#highlights).

`fill` and other background styles can use `false`, `"default"` or `"none"` to
show the terminal background, or the whole theme can be switched to transparent
surfaces with `hl.set_transparent(true)`.

## Focus defaults

| Widget | Focusable by default |
| --- | --- |
| List, Menu, Table, Input, Tabs, Button, Viewport | Yes (unless disabled or `focusable = false`) |
| Text, Box, Padding, Center, Stack, Split, Popup, Progress, Spinner, Separator, Scrollbar, Statusline | No |

Layout containers are not focusable themselves; `find_focusables()` walks their
visible children depth-first, so a List inside a Box inside a VBox participates
normally. Viewport is the exception: it is focusable so it can handle scrolling
keys, and focus still descends into its content.

## A custom widget

```lua
local tuipro = require("tuipro")
local Widget = tuipro.Widget

local Gauge = Widget:extend("Gauge")

function Gauge.new(opts)
  opts = opts or {}
  local self = setmetatable({}, Gauge)
  Widget.init(self, opts)
  self.value = opts.value or 0
  self.height = 1
  return self
end

function Gauge:render(canvas)
  local width = self.w
  local filled = math.floor(width * math.max(0, math.min(1, self.value)))
  canvas:fill(0, 0, width, 1, "░", "ProgressTrack")
  if filled > 0 then
    canvas:fill(0, 0, filled, 1, "█", "ProgressFill")
  end
end

local gauge = Gauge.new({ value = 0.5 })
```

Because `Widget:extend` installs a default constructor, the explicit
`Gauge.new` is optional when `Gauge:init(opts)` is defined instead.
