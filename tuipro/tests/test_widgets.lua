-- Widget behaviour, callbacks, editing, input handling and rendering tests.
local here = (arg and arg[0] or "tests/test_widgets.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local ui = H.ui
local A = H.assert
local test = H.test

-- ---------------------------------------------------------------------------
-- Base widget and simple leaves
-- ---------------------------------------------------------------------------

test("widget base class helpers and labels", function()
  local w = H.t.Widget.new({ name = "Named", width = 5, height = 2 })
  A.eq(w:get_label(), "Named")
  A.eq(w:label(), "Named")
  A.eq(w.__class_name or w.__name, "Widget")
  local child = H.t.Widget.new({})
  w.children = { child }
  child.parent = w
  A.truthy(child:is_descendant_of(w))
  A.falsy(w:is_descendant_of(child))
  w:resize(0, 0, 5, 2)
  A.truthy(w:contains(2, 1))
  A.falsy(w:contains(5, 1))
  A.eq(w:find_widget_at(1, 1), w)
  A.eq(w:find_widget_at(5, 1), nil)
  local named = H.t.Widget.new({ name = "N" })
  A.eq(named:get_label(), "N")
  local plain = H.t.Widget.new()
  A.eq(plain:get_label(), "Widget")
  local Custom = H.t.Widget:extend("Custom")
  local c = Custom.new({ height = 1 })
  A.eq(c.__name, "Custom")
  A.eq(c:get_label(), "Custom")
end)

test("text value, wrapping and rendering", function()
  local text = ui.text("hello")
  A.eq(text:value(), "hello")
  text.text = function() return "dynamic" end
  A.eq(text:value(), "dynamic")
  local wrapped = ui.text({ text = "one two three", wrap = true })
  A.eq(wrapped:lines(7)[1], "one two")
  A.eq(wrapped:lines(7)[2], "three")
  A.eq(wrapped:preferred_height(7), 2)

  local aligned = ui.text({ text = "hello", align = "center" })
  local rows = H.render_widget(aligned, 11, 1)
  A.eq(rows[1], "   hello")
end)

test("box draws border, title and content", function()
  local box = ui.box({
    title = "Panel",
    padding = 1,
    content = ui.text("hi"),
  })
  local rows = H.render_widget(box, 20, 5)
  A.row(rows, "╭")
  A.row(rows, "Panel")
  A.row(rows, "╰")
  A.row(rows, "hi")
  A.eq(H.util.width(rows[1]), 20)
  A.eq(rows[1]:sub(-3), "╮")
  A.eq(rows[5]:sub(1, 3), "╰")
  A.eq(rows[5]:sub(-3), "╯")

  local bare = ui.box({ border = false, content = ui.text("plain") })
  local bare_rows = H.render_widget(bare, 10, 2)
  A.row(bare_rows, "plain")
  A.no_row(bare_rows, "╭")
end)

-- ---------------------------------------------------------------------------
-- List and Menu
-- ---------------------------------------------------------------------------

local function list_fixture()
  local selected, activated
  local list = ui.list({
    items = {
      "one",
      { label = "Header", header = true },
      { label = "two", disabled = true },
      "three",
      "four",
    },
    height = 3,
    on_select = function(item, index, self)
      selected = { item = item, index = index, self = self }
    end,
    on_activate = function(item, index, self)
      activated = { item = item, index = index, self = self }
    end,
  })
  return list, function() return selected, activated end
end

test("list selection skips headers and disabled items", function()
  local list, results = list_fixture()
  A.eq(list:count(), 5)
  A.eq(list:current_index(), 1)
  A.eq(list:current().label, "one")
  A.falsy(list:is_selectable(2))
  A.falsy(list:is_selectable(3))
  A.truthy(list:is_selectable(4))
  A.truthy(list:move(1))
  A.eq(list:current_index(), 4)
  local selected = results()
  A.eq(selected.item.label, "three")
  A.eq(selected.index, 4)
  A.eq(selected.self, list)
  A.truthy(list:move(-1))
  A.eq(list:current_index(), 1)
  A.truthy(list:move_last())
  A.eq(list:current_index(), 5)
  A.truthy(list:move_first())
  A.eq(list:current_index(), 1)
  A.falsy(list:move(-1))
  A.falsy(list:set_index(2))
  A.eq(list:current_index(), 1)
end)

test("list activation and multi-select toggling", function()
  local list, results = list_fixture()
  A.truthy(list:activate())
  local _, activated = results()
  A.eq(activated.item.label, "one")
  A.eq(activated.index, 1)
  A.eq(activated.self, list)

  local multiple = ui.list({ items = { "a", "b" }, multiple = true })
  A.eq(multiple:checked_count(), 0)
  A.truthy(multiple:on_key(H.key("space")))
  A.eq(multiple:checked_count(), 1)
  A.truthy(multiple.items[1].checked)
  A.truthy(multiple:on_key(H.key("space")))
  A.eq(multiple:checked_count(), 0)
  A.truthy(multiple:activate())
  A.eq(multiple:checked_count(), 1)
  A.eq(#multiple:checked_items(), 1)
end)

test("list wrapping, paging and scrolling", function()
  local list = ui.list({ items = { "a", "b", "c", "d" }, height = 2, wrap = true })
  A.truthy(list:move(-1))
  A.eq(list:current_index(), 4)
  A.truthy(list:move(1))
  A.eq(list:current_index(), 1)
  list:resize(0, 0, 20, 2)
  A.truthy(list:on_key(H.key("pagedown")))
  A.eq(list:current_index(), 3)
  A.truthy(list:on_key(H.key("pageup")))
  A.eq(list:current_index(), 1)
  A.eq(list.offset, 0)
  A.truthy(list:scroll_by(1))
  A.eq(list.offset, 1)
  A.truthy(list:scroll_by(-1))
  A.eq(list.offset, 0)
  A.falsy(list:scroll_by(-1))
end)

test("list mouse selection, activation and wheel", function()
  local activated
  local list = ui.list({
    items = { "a", "b", "c", "d", "e" },
    height = 3,
    on_activate = function(item) activated = item end,
  })
  list:resize(3, 4, 20, 3)
  list.offset = 0
  A.truthy(list:on_mouse(H.mouse("press", "left", 5, 5)))
  A.eq(list:current_index(), 2)
  A.truthy(list:on_mouse(H.mouse("release", "left", 5, 5)))
  A.eq(activated.label, "b")
  A.truthy(list:on_mouse(H.mouse("wheel", "wheel-down", 5, 5)))
  A.eq(list:current_index(), 5)
  A.truthy(list:on_mouse(H.mouse("wheel", "wheel-up", 5, 5)))
  A.eq(list:current_index(), 2)
end)

test("list empty state and rendering", function()
  local empty = ui.list({ height = 3 })
  local rows = H.render_widget(empty, 20, 3)
  A.row(rows, "No items")

  local list = ui.list({
    items = {
      "one",
      { label = "Header", header = true },
      { label = "two", disabled = true },
      "three",
    },
    height = 3,
    show_index = true,
  })
  list.focused = true
  local rendered = H.render_widget(list, 20, 3)
  A.row(rendered, "one")
  A.row(rendered, "Header")
  A.row(rendered, "two")
  A.contains(rendered[1], "›")
  A.eq(rendered[1]:sub(1, 3), "›")
end)

test("menu uses list navigation and shows shortcuts", function()
  local menu = ui.menu({
    items = {
      { label = "Open", shortcut = "o" },
      { label = "Save", key = "s" },
      { label = "Gone", detail = "g" },
      { header = true, label = "Actions" },
    },
  })
  A.eq(menu:get_label(), "Menu")
  A.eq(menu.items[1].label, "Open")
  A.eq(menu:_detail(menu.items[1]), "o")
  A.eq(menu:_detail(menu.items[2]), "s")
  A.eq(menu._detail(menu, menu.items[3]), "g")
  A.eq(menu:move(1), true)
  A.eq(menu:current().label, "Save")
  local rows = H.render_widget(menu, 24, 3)
  A.row(rows, "Open")
  A.row(rows, "Save")
end)

-- ---------------------------------------------------------------------------
-- Table
-- ---------------------------------------------------------------------------

local function table_fixture()
  local selected, activated
  local rows = {
    { name = "alpha", count = 1 },
    { name = "beta", count = 2 },
    { name = "gamma", count = 3 },
    { name = "delta", count = 4 },
  }
  local table_widget = ui.table({
    columns = {
      { title = "Name", key = "name" },
      { title = "Count", key = "count", width = 7, align = "right" },
    },
    rows = rows,
    height = 3,
    on_select = function(row, index, self)
      selected = { row = row, index = index, self = self }
    end,
    on_activate = function(row, index, self)
      activated = { row = row, index = index, self = self }
    end,
  })
  return table_widget, rows, function() return selected, activated end
end

test("table selection, paging and activation", function()
  local table_widget, rows, results = table_fixture()
  A.eq(table_widget:count(), 4)
  A.eq(table_widget:selected_row(), rows[1])
  A.truthy(table_widget:move(1))
  A.eq(table_widget:selected_row(), rows[2])
  local selected = results()
  A.eq(selected.index, 2)
  A.eq(selected.self, table_widget)
  A.truthy(table_widget:move(1))
  A.truthy(table_widget:on_key(H.key("end")))
  A.eq(table_widget.index, 4)
  A.truthy(table_widget:on_key(H.key("home")))
  A.eq(table_widget.index, 1)
  table_widget:resize(0, 0, 30, 4)
  A.truthy(table_widget:page(1))
  A.eq(table_widget.index, 3)
  A.truthy(table_widget:activate())
  local _, activated = results()
  A.eq(activated.row, rows[3])
  A.eq(activated.index, 3)
end)

test("table columns, formatting and inferred schemas", function()
  local table_widget, rows = table_fixture()
  A.eq(#table_widget.columns, 2)
  local text, hl, align = table_widget:_cell_value(rows[1], table_widget.columns[1])
  A.eq(text, "alpha")
  A.eq(align, "left")
  text, _, align = table_widget:_cell_value(rows[1], table_widget.columns[2])
  A.eq(text, "1")
  A.eq(align, "right")

  local formatted = ui.table({
    columns = {
      { title = "N", key = "n", format = function(v)
        return { text = "<" .. tostring(v) .. ">", hl = "Info", align = "center" }
      end },
    },
    rows = { { n = 7 } },
  })
  text, hl, align = formatted:_cell_value({ n = 7 }, formatted.columns[1])
  A.eq(text, "<7>")
  A.eq(hl, "Info")
  A.eq(align, "center")

  local array_rows = ui.table({ rows = { { 10, 20 } } })
  A.eq(#array_rows.columns, 2)
  A.eq(array_rows.columns[1].title, "Column 1")
  A.eq(array_rows:_cell_value(array_rows.rows[1], array_rows.columns[2]), "20")

  local keyed = ui.table({ rows = { { name = "x", value = 1 } } })
  A.eq(#keyed.columns, 2)
  A.eq(keyed.columns[1].title, "name")
  A.eq(keyed.columns[2].title, "value")
end)

test("table empty state, mouse and wheel", function()
  local empty = ui.table({ height = 3 })
  A.eq(empty:count(), 0)
  A.eq(empty.index, 0)
  A.eq(empty:selected_row(), nil)
  A.falsy(empty:move(1))
  A.falsy(empty:activate())
  local empty_rows = H.render_widget(empty, 20, 3)
  A.row(empty_rows, "No data")

  local activated
  local table_widget = ui.table({
    columns = { { title = "A", key = "a" }, { title = "B", key = "b" } },
    rows = {
      { a = "1", b = "x" }, { a = "2", b = "y" }, { a = "3", b = "z" },
      { a = "4", b = "w" }, { a = "5", b = "v" },
    },
    on_activate = function(row) activated = row end,
  })
  table_widget:resize(0, 4, 30, 3)
  table_widget.offset = 0
  A.truthy(table_widget:on_mouse(H.mouse("press", "left", 2, 5)))
  A.eq(table_widget.index, 1)
  A.truthy(table_widget:on_mouse(H.mouse("release", "left", 2, 5)))
  A.eq(activated.a, "1")
  A.truthy(table_widget:on_mouse(H.mouse("wheel", "wheel-down", 2, 5)))
  A.eq(table_widget.index, 4)
  A.truthy(table_widget:on_mouse(H.mouse("wheel", "wheel-up", 2, 5)))
  A.eq(table_widget.index, 1)
  A.falsy(table_widget:on_mouse(H.mouse("press", "left", 2, 4)))
end)

test("table rendering shows header, rows and grid", function()
  local table_widget = ui.table({
    columns = { "Name", "Value" },
    rows = { { "alpha", 1 }, { "beta", 2 } },
    show_grid = true,
    header_separator = true,
    height = 5,
  })
  table_widget.focused = true
  local rows = H.render_widget(table_widget, 30, 5)
  A.row(rows, "Name")
  A.row(rows, "Value")
  A.row(rows, "alpha")
  A.row(rows, "beta")
  A.contains(rows[2], "─")
  A.contains(rows[1], "│")
end)

test("table set_rows and callbacks reset selection", function()
  local selected
  local table_widget = ui.table({
    columns = { "A" },
    rows = { { "a" }, { "b" }, { "c" } },
    on_select = function(row, index) selected = { row = row, index = index } end,
  })
  table_widget:set_rows({ { "x" }, { "y" } }, 2)
  A.eq(table_widget:count(), 2)
  A.eq(table_widget.index, 2)
  A.eq(selected.index, 2)
  table_widget:set_rows({}, nil, true)
  A.eq(table_widget.index, 0)
end)

-- ---------------------------------------------------------------------------
-- Input
-- ---------------------------------------------------------------------------

test("input editing, cursor movement and change callbacks", function()
  local changes = 0
  local input = ui.input({ value = "abc", on_change = function() changes = changes + 1 end })
  A.eq(input:get(), "abc")
  A.eq(input.cursor, 4)
  A.truthy(input:backspace())
  A.eq(input:get(), "ab")
  A.eq(input.cursor, 3)
  A.truthy(input:insert("XY"))
  A.eq(input:get(), "abXY")
  A.eq(input.cursor, 5)
  A.truthy(input:on_key(H.key("left")))
  A.eq(input.cursor, 4)
  A.truthy(input:insert("z"))
  A.eq(input:get(), "abXzY")
  A.eq(input.cursor, 5)
  A.truthy(input:on_key(H.key("home")))
  A.eq(input.cursor, 1)
  A.truthy(input:insert(">"))
  A.eq(input:get(), ">abXzY")
  A.eq(input.cursor, 2)
  A.truthy(input:on_key(H.key("delete")))
  A.eq(input:get(), ">bXzY")
  A.eq(input.cursor, 2)
  A.truthy(input:on_key(H.key("end")))
  A.eq(input.cursor, #input:get() + 1)
  A.truthy(input:on_key(H.key("backspace")))
  A.eq(input:get(), ">bXz")
  A.eq(changes, 6)

  A.truthy(input:on_key(H.key("home")))
  A.truthy(input:on_key(H.key("right")))
  A.truthy(input:on_key(H.key("right")))
  A.truthy(input:on_key(H.key("C-u")))
  A.eq(input:get(), "Xz")
  A.eq(input.cursor, 1)
end)

test("input handles UTF-8 cursor movement", function()
  local input = ui.input({ value = "aé" })
  A.eq(#input:get(), 3)
  A.eq(input.cursor, 4)
  A.truthy(input:on_key(H.key("left")))
  A.eq(input.cursor, 2)
  A.truthy(input:backspace())
  A.eq(input:get(), "é")
  A.eq(input.cursor, 1)
  A.truthy(input:insert("a"))
  A.eq(input:get(), "aé")
  A.truthy(input:on_key(H.key("right")))
  A.eq(input.cursor, 4)
  A.truthy(input:on_key(H.key("backspace")))
  A.eq(input:get(), "a")
  A.eq(input.cursor, 2)
  A.falsy(input:delete())
end)

test("input max length, readonly and delete word", function()
  local input = ui.input({ max_length = 3 })
  A.truthy(input:insert("abcdef"))
  A.eq(input:get(), "abc")
  A.falsy(input:insert("d"))
  A.truthy(input:backspace())
  A.truthy(input:insert("cd"))
  A.eq(input:get(), "abc")

  local readonly = ui.input({ value = "fixed", readonly = true })
  A.falsy(readonly:insert("x"))
  A.falsy(readonly:backspace())
  A.falsy(readonly:delete())
  A.falsy(readonly:delete_word())
  A.falsy(readonly:on_paste(H.paste("nope")))
  A.eq(readonly:get(), "fixed")

  local words = ui.input({ value = "one two" })
  A.truthy(words:on_key(H.key("C-w")))
  A.eq(words:get(), "one ")
  A.eq(words.cursor, 5)
end)

test("input history navigation", function()
  local input = ui.input({ value = "draft", history = { "one", "two" } })
  A.truthy(input:on_key(H.key("up")))
  A.eq(input:get(), "two")
  A.truthy(input:on_key(H.key("up")))
  A.eq(input:get(), "one")
  A.falsy(input:on_key(H.key("up")))
  A.truthy(input:on_key(H.key("down")))
  A.eq(input:get(), "two")
  A.truthy(input:on_key(H.key("down")))
  A.eq(input:get(), "draft")
  A.falsy(input:on_key(H.key("down")))
end)

test("input masking and mouse cursor placement", function()
  local masked = ui.input({ value = "secret", mask = "•" })
  A.eq(masked:_display(), "••••••")
  masked.cursor = 1
  A.eq(masked:_cursor_display_col(), 0)
  masked.cursor = 4
  A.eq(masked:_cursor_display_col(), 3)
  A.eq(masked:_display_col_to_byte(0), 1)
  A.eq(masked:_display_col_to_byte(1), 2)
  A.eq(masked:_display_col_to_byte(6), 7)

  local input = ui.input({ value = "abcd" })
  input:resize(5, 6, 10, 1)
  A.truthy(input:on_mouse(H.mouse("press", "left", 7, 6)))
  A.eq(input.cursor, 3)
  A.truthy(input:on_mouse(H.mouse("press", "left", 5, 6)))
  A.eq(input.cursor, 1)

  local prompted = ui.input({ value = "abcd", prompt = "P: " })
  prompted:resize(0, 0, 10, 1)
  A.truthy(prompted:on_mouse(H.mouse("press", "left", 6, 0)))
  A.eq(prompted.cursor, 4)
end)

test("input submit, cancel, clear-on-submit and paste", function()
  local submitted, cancelled
  local input = ui.input({
    value = "hello",
    clear_on_submit = true,
    on_submit = function(text, self) submitted = { text = text, self = self } end,
    on_cancel = function(text, self) cancelled = { text = text, self = self } end,
  })
  A.truthy(input:on_key(H.key("enter")))
  A.eq(submitted.text, "hello")
  A.eq(submitted.self, input)
  A.eq(input:get(), "")
  input:set("bye")
  A.truthy(input:on_key(H.key("esc")))
  A.eq(cancelled.text, "bye")

  input:set("")
  A.truthy(input:on_paste(H.paste("a\nb\tc\1")))
  A.eq(input:get(), "a b c")
end)

test("input rendering: prompt, placeholder and mask", function()
  local prompt = ui.input({ value = "abc", prompt = "Name: " })
  local rows = H.render_widget(prompt, 20, 1)
  A.eq(rows[1], "Name: abc")

  local placeholder = ui.input({ placeholder = "type here" })
  local ph_rows = H.render_widget(placeholder, 20, 1)
  A.eq(ph_rows[1], "type here")

  local masked = ui.input({ value = "secret", mask = "•" })
  local mask_rows = H.render_widget(masked, 20, 1)
  A.contains(mask_rows[1], "••••••")
  A.not_contains(mask_rows[1], "secret")

  local disabled = ui.input({ value = "off", disabled = true })
  disabled.disabled = true
  A.eq(disabled:preferred_height(), 1)
end)

-- ---------------------------------------------------------------------------
-- Tabs
-- ---------------------------------------------------------------------------

test("tabs selection, wrapping and callbacks", function()
  local changes = {}
  local tabs = ui.tabs({ "One", "Two", "Three" }, {
    on_change = function(item, index, self)
      changes[#changes + 1] = { label = item.label, index = index, self = self }
    end,
  })
  A.eq(tabs:count(), 3)
  A.eq(tabs:item().label, "One")
  A.truthy(tabs:set_active(2))
  A.eq(#changes, 1)
  A.eq(changes[1].label, "Two")
  A.eq(changes[1].index, 2)
  A.eq(changes[1].self, tabs)
  A.falsy(tabs:set_active(2))
  A.truthy(tabs:next())
  A.eq(tabs.active, 3)
  A.truthy(tabs:next())
  A.eq(tabs.active, 1)
  A.truthy(tabs:prev())
  A.eq(tabs.active, 3)
  A.truthy(tabs:on_key(H.key("home")))
  A.eq(tabs.active, 1)
  A.truthy(tabs:on_key(H.key("end")))
  A.eq(tabs.active, 3)
  local before = #changes
  A.truthy(tabs:on_key(H.key("enter")))
  A.eq(#changes, before + 1)
end)

test("tabs mouse selection and rendering with badges", function()
  local tabs = ui.tabs({ "One", "Two", "Three" })
  tabs:resize(0, 0, 30, 1)
  A.truthy(tabs:on_mouse(H.mouse("press", "left", 6, 0)))
  A.eq(tabs.active, 2)
  A.truthy(tabs:on_mouse(H.mouse("press", "left", 1, 0)))
  A.eq(tabs.active, 1)
  A.truthy(tabs:on_mouse(H.mouse("press", "left", 29, 0)))

  local badged = ui.tabs({
    { label = "Alpha", badge = 3 },
    { label = "Beta", badge = false },
  })
  local rows = H.render_widget(badged, 30, 1)
  A.contains(rows[1], "Alpha")
  A.contains(rows[1], "3")
  A.contains(rows[1], "Beta")
  A.not_contains(rows[1], "false")
  A.ge(tabs:preferred_width(100), 1)
end)

-- ---------------------------------------------------------------------------
-- Progress and Spinner
-- ---------------------------------------------------------------------------

test("progress ratio, labels and rendering", function()
  local progress = ui.progress({ value = 50, max = 200 })
  A.near(progress:ratio(), 0.25)
  progress:set_value(300)
  A.near(progress:ratio(), 1)
  progress:set_value(-50)
  A.near(progress:ratio(), 0)
  progress.max = 0
  A.near(progress:ratio(), 0)
  progress.max = 100
  progress.value = function() return 25 end
  A.near(progress:ratio(), 0.25)
  progress.label = function() return "CPU" end
  A.eq(progress:label_text(), "CPU")
  A.eq(progress:get_label(), "CPU")

  local bar = ui.progress({ value = 50, max = 100, label = "Load" })
  local rows = H.render_widget(bar, 20, 1)
  A.contains(rows[1], "Load")
  A.contains(rows[1], "50%")
  A.contains(rows[1], "█")
  A.contains(rows[1], "░")
  A.eq(H.util.width(rows[1]), 20)
end)

test("spinner frames, states, timers and rendering", function()
  local spinner = ui.spinner({ frames = { "a", "b", "c" }, interval = 100, label = "work" })
  spinner.started = H.util.now() - 250
  A.eq(spinner:frame(), "c")
  local text, style, label = spinner:text_and_style()
  A.eq(text, "c")
  A.eq(style, "Spinner")
  A.eq(label, "work")
  spinner:stop(true, "done")
  A.falsy(spinner.active)
  A.eq(spinner.state, "done")
  text, style, label = spinner:text_and_style()
  A.eq(text, "✔")
  A.eq(style, "Success")
  A.eq(label, "done")
  spinner:start("again")
  A.truthy(spinner.active)
  A.eq(spinner.state, "running")
  A.eq(spinner.label, "again")
  spinner:stop(false, "failed")
  text = spinner:text_and_style()
  A.eq(text, "✖")
  A.eq(spinner.state, "error")
  A.eq(spinner:get_label(), "failed")

  local rows = H.render_widget(ui.spinner({ frames = { "x" }, label = "busy" }), 20, 1)
  A.contains(rows[1], "x")
  A.contains(rows[1], "busy")
end)

test("spinner attaches a timer through the app", function()
  H.with_app({ body = H.t.Widget.new({}) }, function(app)
    local spinner = ui.spinner({ label = "attached" })
    app:set_body(spinner)
    A.truthy(spinner.timer)
    A.eq(#app.timers, 1)
    spinner:stop(true)
    A.eq(spinner.timer, nil)
    spinner:start("again")
    A.truthy(spinner.timer)
    spinner:stop(true)
  end)
end)

-- ---------------------------------------------------------------------------
-- Buttons, popups, separators, scrollbars, statusline
-- ---------------------------------------------------------------------------

test("button hotkeys, keyboard and mouse activation", function()
  local pressed, pressed_with = 0, nil
  local button = ui.button("&Save", function(self, app)
    pressed = pressed + 1
    pressed_with = { self = self, app = app }
  end)
  A.eq(button.label, "Save")
  A.eq(button.hotkey, "s")
  A.truthy(button:press())
  A.eq(pressed, 1)
  A.eq(pressed_with.self, button)
  A.eq(pressed_with.app, nil)
  A.truthy(button:on_key(H.text_key("s")))
  A.eq(pressed, 2)
  A.truthy(button:on_key(H.key("enter")))
  A.truthy(button:on_key(H.key("space")))
  A.eq(pressed, 4)
  A.falsy(button:on_key(H.text_key("z")))

  button:resize(0, 0, 10, 1)
  pressed = 0
  A.truthy(button:on_mouse(H.mouse("press", "left", 1, 0)))
  A.truthy(button:on_mouse(H.mouse("release", "left", 1, 0)))
  A.eq(pressed, 1)
  A.falsy(button:on_mouse(H.mouse("release", "left", 1, 0)))
  A.eq(pressed, 1)

  local disabled = ui.button({ label = "No", disabled = true, on_press = function()
    error("disabled button pressed")
  end })
  A.falsy(disabled:press())
  A.falsy(disabled:on_key(H.key("enter")))
  A.falsy(disabled:on_mouse(H.mouse("press", "left", 0, 0)))
end)

test("button styles and rendering", function()
  local button = ui.button({ label = "OK", primary = true, focused = true })
  A.eq(button:style(), "ButtonPrimaryFocus")
  button.focused = false
  A.eq(button:style(), "ButtonPrimary")
  button.primary = false
  button.danger = true
  A.eq(button:style(), "ButtonDanger")
  button.danger = false
  A.eq(button:style(), "Button")
  button.focused = true
  A.eq(button:style(), "ButtonFocus")
  local rows = H.render_widget(button, 10, 1)
  A.contains(rows[1], "[ OK ]")
  A.eq(button:preferred_height(), 1)
end)

test("popup layout, content and buttons", function()
  local pressed
  local popup = ui.popup({
    title = "Confirm",
    content = "Delete this item?",
    footer = "footer",
    width = 30,
    height = 10,
    buttons = {
      {
        label = "OK",
        primary = true,
        on_press = function(p) pressed = p end,
      },
      { label = "Cancel" },
    },
  })
  A.eq(#popup.buttons, 2)
  A.contains(popup.content:value(), "Delete this item?")
  popup:resize(5, 5, 30, 10)
  A.eq(popup.content.x, 7)
  A.eq(popup.content.y, 7)
  A.eq(popup.content.w, 26)
  A.eq(popup.content.h, 3)
  A.eq(popup.button_box.y, 12)
  A.eq(popup.footer_y, 10)
  A.truthy(popup.buttons[1]:press())
  A.eq(pressed, popup)
  popup.buttons[2]:press()
  A.falsy(popup.visible)

  local rows = H.render_widget(popup, 30, 10)
  A.row(rows, "Confirm")
  A.row(rows, "Delete this item?")
  A.row(rows, "footer")
  A.row(rows, "OK")
  A.row(rows, "Cancel")
  A.contains(rows[1], "╭")
end)

test("popup content replacement, extra buttons and preferred size", function()
  local popup = ui.popup({
    title = "T",
    content = "first",
    min_width = 20,
    min_height = 5,
  })
  A.ge(popup:preferred_width(80), 20)
  A.ge(popup:preferred_height(40), 5)
  popup:set_content("second")
  A.contains(popup.content:value(), "second")
  local extra = popup:add_button({ label = "Extra" })
  A.eq(#popup.buttons, 1)
  A.eq(popup.buttons[1], extra)
  A.eq(popup.button_box.children[1], extra)
end)

test("separator renders horizontal labels and vertical rules", function()
  local separator = ui.separator("Hi")
  A.eq(separator.label, "Hi")
  local rows = H.render_widget(separator, 20, 1)
  A.contains(rows[1], " Hi ")
  A.eq(H.util.width(rows[1]), 20)

  local plain = ui.separator({ style = "ascii" })
  local plain_rows = H.render_widget(plain, 5, 1)
  A.eq(plain_rows[1], "-----")

  local vertical = ui.separator({ direction = "vertical" })
  local vertical_rows = H.render_widget(vertical, 1, 3)
  A.eq(vertical_rows[1], "│")
  A.eq(vertical_rows[2], "│")
  A.eq(vertical_rows[3], "│")
  A.eq(vertical:preferred_width(10), 1)
  A.eq(separator:preferred_width(100), 6)
end)

test("scrollbar clamps, callbacks and mouse input", function()
  local changes = 0
  local scrollbar = ui.scrollbar({
    total = 100,
    visible = 10,
    offset = 0,
    on_change = function() changes = changes + 1 end,
  })
  scrollbar:resize(0, 0, 1, 10)
  A.eq(scrollbar.offset, 0)
  A.eq(scrollbar:set_offset(50), 50)
  A.eq(changes, 1)
  A.eq(scrollbar:set_offset(200), 90)
  A.eq(changes, 2)
  scrollbar:set(100, 10, -5)
  A.eq(scrollbar.offset, 0)
  A.truthy(scrollbar:on_mouse(H.mouse("wheel", "wheel-down", 0, 0)))
  A.eq(scrollbar.offset, 1)
  scrollbar:set_offset(0)
  A.truthy(scrollbar:on_mouse(H.mouse("press", "left", 0, 9)))
  A.eq(scrollbar.offset, 90)
  A.falsy(scrollbar:on_mouse(H.mouse("press", "left", 0, 10)))

  local top = H.render_widget(ui.scrollbar({ total = 100, visible = 10 }), 1, 10)
  A.eq(top[1], "┃")
  A.eq(top[10], "│")
  local bottom = H.render_widget(ui.scrollbar({
    total = 100, visible = 10, offset = 90,
  }), 1, 10)
  A.eq(bottom[10], "┃")
end)

test("horizontal scrollbar rendering", function()
  local sb = ui.scrollbar({ total = 50, visible = 10, direction = "horizontal", width = 10 })
  local rows = H.render_widget(sb, 10, 1)
  A.eq(H.util.width(rows[1]), 10)
  A.eq(rows[1]:sub(1, 3), "━")
  A.eq(rows[1]:sub(4, 6), "━")
  A.eq(rows[1]:sub(7, 9), "─")
end)

test("viewport clips tall content and draws a scrollbar", function()
  local lines = {}
  for i = 1, 20 do
    lines[i] = ui.text({ text = string.format("row %02d", i), height = 1 })
  end
  local vp = ui.viewport({ content = ui.vbox({ gap = 0, children = lines }) })
  local rows = H.render_widget(vp, 12, 5)
  A.row(rows, "row 01")
  A.row(rows, "row 05")
  A.no_row(rows, "row 06")
  A.eq(vp.total, 20)
  A.eq(vp.visible_h, 5)
  A.eq(vp.bar_w, 1)
  A.eq(rows[1]:sub(-3), "┃")
  A.eq(rows[5]:sub(-3), "│")

  A.truthy(vp:scroll_by(3))
  A.eq(vp.offset, 3)
  rows = H.render_widget(vp, 12, 5)
  A.no_row(rows, "row 03")
  A.row(rows, "row 04")
  A.row(rows, "row 08")
  A.no_row(rows, "row 09")

  -- Content above the viewport must not leak into other rows.
  A.eq(vp.content.y, -3)
  A.truthy(vp:on_key(H.key("pagedown")))
  A.eq(vp.offset, 7)
  A.truthy(vp:on_key(H.key("home")))
  A.eq(vp.offset, 0)
  A.truthy(vp:on_key(H.key("end")))
  A.eq(vp.offset, 15)
  A.truthy(vp:on_mouse(H.mouse("wheel", "wheel-up", 0, 0)))
  A.eq(vp.offset, 12)
  A.eq(vp:set_offset(-10), 0)
  A.eq(vp:set_offset(999), 15)
end)

test("viewport reveals focused children inside an app", function()
  local buttons = {}
  for i = 1, 12 do
    buttons[i] = ui.button({ label = "B" .. i, height = 1 })
  end
  local vp = ui.viewport({ content = ui.vbox({ gap = 0, children = buttons }) })
  H.with_app({ width = 20, height = 8, body = vp }, function(app)
    app:layout()
    A.eq(vp.total, 12)
    A.truthy(vp.visible_h > 0 and vp.visible_h < 12)
    A.eq(vp.offset, 0)

    app:set_focus(buttons[12])
    app:layout()
    A.truthy(vp.offset > 0)
    local rel = buttons[12].y - vp.y
    A.truthy(rel >= 0 and rel + 1 <= vp.visible_h)

    -- Clicking the scrollbar jumps towards the bottom, and a later layout
    -- does not snap back to the focused child.
    vp:set_offset(0)
    A.truthy(vp:on_mouse(H.mouse("press", "left", vp.x + vp.w - 1, vp.y + vp.visible_h - 1)))
    A.eq(vp.offset, vp.total - vp.visible_h)
    app:layout()
    A.eq(vp.offset, vp.total - vp.visible_h)
  end)
end)

test("statusline left, center, right and dynamic sections", function()
  local sl = ui.statusline({ left = { "L" }, center = "C", right = "R" })
  local rows = H.render_widget(sl, 20, 1)
  A.contains(rows[1], "L")
  A.contains(rows[1], "C")
  A.eq(rows[1]:sub(-1), "R")
  A.le(#rows[1], 20)
  A.eq(rows[1]:sub(1, 1), " ")

  local dynamic = ui.statusline({
    dynamic = function()
      return { left = "A", right = "B" }
    end,
  })
  local dyn_rows = H.render_widget(dynamic, 20, 1)
  A.contains(dyn_rows[1], "A")
  A.contains(dyn_rows[1], "B")

  local long = ui.statusline({ left = "LEFTLEFTLEFT", right = "RIGHT" })
  local long_rows = H.render_widget(long, 20, 1)
  A.contains(long_rows[1], "LEFT")
  A.eq(long_rows[1]:sub(-5), "RIGHT")
  A.le(#long_rows[1], 20)

  local section = H.t.ui.Statusline.section("x", "Info")
  A.eq(section.text, "x")
  A.eq(section.hl, "Info")
  sl:set("center", "X")
  sl:set("right", "Y")
  A.eq(sl.center, "X")
  A.eq(sl.right, "Y")
end)

-- ---------------------------------------------------------------------------
-- Scroll helpers and constructors
-- ---------------------------------------------------------------------------

test("scroll helper arithmetic", function()
  local scroll = ui.scroll
  A.eq(scroll.clamp_offset(-5, 10, 3), 0)
  A.eq(scroll.clamp_offset(20, 10, 3), 7)
  A.eq(scroll.clamp_offset(5, 10, 3), 5)
  A.eq(scroll.clamp_offset(5, 10, 0), 0)
  A.eq(scroll.ensure_visible(0, 1, 10, 3), 0)
  A.eq(scroll.ensure_visible(0, 5, 10, 3), 2)
  A.eq(scroll.ensure_visible(5, 4, 10, 3), 3)
  A.eq(scroll.ensure_visible(2, 3, 10, 3), 2)
  local start, size = scroll.thumb(10, 3, 0, 5)
  A.eq(start, 0); A.eq(size, 2)
  start, size = scroll.thumb(10, 3, 7, 5)
  A.eq(start, 3); A.eq(size, 2)
  start, size = scroll.thumb(2, 3, 0, 5)
  A.eq(start, 0); A.eq(size, 5)

  H.with_core(6, 5, function()
    H.core.clear()
    local start2, size2 = scroll.draw(H.t.canvas.new(0, 0, 6, 5), 5, 0, 5, 10, 2, 8, {
      vertical = true,
    })
    A.eq(start2, 4)
    A.eq(size2, 1)
    H.core.flush()
    local rows = H.snapshot()
    A.eq(rows[5], "     ┃")
  end)
end)

test("ui constructors and shorthand forms", function()
  A.eq(ui.text(42).text, "42")
  A.eq(#ui.list({ "a", "b" }).items, 2)
  A.eq(#ui.menu({ "a", "b" }).items, 2)
  A.eq(ui.progress(42).value, 42)
  A.eq(ui.spinner("working").label, "working")
  A.eq(ui.button("Go", function() end).label, "Go")
  A.eq(ui.separator("Sec").label, "Sec")
  A.eq(ui.input("hint").placeholder, "hint")
  A.eq(#ui.tabs({ "A", "B" }).items, 2)
  A.eq(#ui.vbox({ ui.text("a"), ui.text("b") }).children, 2)
  A.eq(#ui.hbox({ ui.text("a") }).children, 1)
  A.truthy(ui.padding({ content = ui.text("x") }).children[1])
  A.truthy(ui.center({ content = ui.text("x") }).children[1])
  A.eq(#ui.stack({ ui.text("a"), ui.text("b") }).children, 2)
  A.truthy(ui.split({ ui.text("a") }).children[1])
  A.eq(ui.box({}).__name, "Box")
  A.eq(ui.scrollbar({}).__name, "Scrollbar")
  A.eq(ui.viewport({ content = ui.text("x") }).__name, "Viewport")
  A.truthy(ui.viewport({ ui.text("x") }).children[1])
  A.eq(ui.popup({}).__name, "Popup")
  A.eq(ui.table({}).__name, "Table")
  A.eq(ui.statusline({}).__name, "Statusline")
  A.eq(ui.spacer(2).height, 2)
  A.eq(ui.spacer().flex, 1)
end)

return H.finish()
