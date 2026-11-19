-- Layout engine tests: VBox, HBox, Padding, Center, Stack, Split, Box.
local here = (arg and arg[0] or "tests/test_layout.lua"):match("^(.*)/") or "."
local H = dofile(here .. "/helper.lua")
local ui = H.ui
local container = H.t.container
local Widget = H.t.widget
local A = H.assert
local test = H.test

local function leaf(opts)
  return Widget.new(opts or {})
end

local function geom(widget)
  return widget.x, widget.y, widget.w, widget.h
end

test("vbox stacks fixed-height children with gaps", function()
  local a, b, c = leaf({ height = 1 }), leaf({ height = 2 }), leaf({ height = 3 })
  local box = ui.VBox.new({ gap = 1, children = { a, b, c } })
  box:resize(2, 3, 10, 20)
  A.eq(a.x, 2); A.eq(a.y, 3); A.eq(a.w, 10); A.eq(a.h, 1)
  A.eq(b.x, 2); A.eq(b.y, 5); A.eq(b.w, 10); A.eq(b.h, 2)
  A.eq(c.x, 2); A.eq(c.y, 8); A.eq(c.w, 10); A.eq(c.h, 3)
  A.eq(box:preferred_height(10), 1 + 2 + 3 + 2)
end)

test("vbox distributes remaining height to flex children", function()
  local fixed1, flex, fixed2 = leaf({ height = 2 }), leaf({ flex = 1 }), leaf({ height = 1 })
  local box = ui.VBox.new({ children = { fixed1, flex, fixed2 } })
  box:resize(0, 3, 12, 12)
  A.eq(fixed1.y, 3); A.eq(fixed1.h, 2)
  A.eq(flex.y, 5); A.eq(flex.h, 9)
  A.eq(fixed2.y, 14); A.eq(fixed2.h, 1)
end)

test("vbox honors flex weights and gives remainder to the last", function()
  local a, b = leaf({ flex = 1 }), leaf({ flex = 2 })
  local box = ui.VBox.new({ children = { a, b } })
  box:resize(0, 0, 10, 10)
  A.eq(a.y, 0); A.eq(a.h, 3)
  A.eq(b.y, 3); A.eq(b.h, 7)
end)

test("vbox justify start, center and end", function()
  local function build(justify)
    local a, b, c = leaf({ height = 1 }), leaf({ height = 1 }), leaf({ height = 1 })
    local box = ui.VBox.new({ gap = 1, justify = justify, children = { a, b, c } })
    box:resize(0, 10, 10, 15)
    return a.y, b.y, c.y
  end
  local ay, by, cy = build("start")
  A.eq(ay, 10); A.eq(by, 12); A.eq(cy, 14)
  ay, by, cy = build("center")
  A.eq(ay, 15); A.eq(by, 17); A.eq(cy, 19)
  ay, by, cy = build("end")
  A.eq(ay, 20); A.eq(by, 22); A.eq(cy, 24)
end)

test("vbox horizontal alignment", function()
  local centered = leaf({ width = 4, height = 1 })
  local right = leaf({ width = 4, height = 1 })
  local box = ui.VBox.new({
    align = "center",
    children = { centered },
  })
  box:resize(2, 0, 10, 4)
  A.eq(centered.x, 5)
  box.align = "right"
  box:resize(2, 0, 10, 4)
  A.eq(centered.x, 8)
  box.align = "start"
  box:resize(2, 0, 10, 4)
  A.eq(centered.x, 2)

  local box2 = ui.VBox.new({ align = "right", children = { right } })
  box2:resize(1, 0, 20, 2)
  A.eq(right.x, 17)
end)

test("hbox stacks fixed-width children with gaps", function()
  local a, b, c = leaf({ width = 2 }), leaf({ width = 3 }), leaf({ width = 4 })
  local box = ui.HBox.new({ gap = 1, children = { a, b, c } })
  box:resize(2, 3, 20, 6)
  A.eq(a.x, 2); A.eq(a.y, 3); A.eq(a.w, 2); A.eq(a.h, 6)
  A.eq(b.x, 5); A.eq(b.w, 3)
  A.eq(c.x, 9); A.eq(c.w, 4)
  A.eq(box:preferred_width(20), 11)
end)

test("hbox distributes remaining width to flex children", function()
  local fixed1, flex, fixed2 = leaf({ width = 4 }), leaf({ flex = 1 }), leaf({ width = 3 })
  local box = ui.HBox.new({ children = { fixed1, flex, fixed2 } })
  box:resize(2, 0, 20, 4)
  A.eq(fixed1.x, 2); A.eq(fixed1.w, 4)
  A.eq(flex.x, 6); A.eq(flex.w, 13)
  A.eq(fixed2.x, 19); A.eq(fixed2.w, 3)
end)

test("hbox valign center and end", function()
  local a = leaf({ width = 2, height = 2 })
  local box = ui.HBox.new({ valign = "center", children = { a } })
  box:resize(0, 3, 10, 8)
  A.eq(a.y, 6)
  box.valign = "end"
  box:resize(0, 3, 10, 8)
  A.eq(a.y, 9)
  box.valign = "start"
  box:resize(0, 3, 10, 8)
  A.eq(a.y, 3)
end)

test("hbox justify and gap", function()
  local a, b = leaf({ width = 2 }), leaf({ width = 3 })
  local box = ui.HBox.new({ gap = 1, justify = "center", children = { a, b } })
  box:resize(0, 0, 20, 2)
  A.eq(a.x, 7)
  A.eq(b.x, 10)
  box.justify = "end"
  box:resize(0, 0, 20, 2)
  A.eq(a.x, 14)
  A.eq(b.x, 17)
end)

test("padding layout and parse_padding", function()
  local top, right, bottom, left = container.parse_padding(2)
  A.eq(top, 2); A.eq(right, 2); A.eq(bottom, 2); A.eq(left, 2)
  top, right, bottom, left = container.parse_padding({ 1, 2, 3, 4 })
  A.eq(top, 1); A.eq(right, 2); A.eq(bottom, 3); A.eq(left, 4)
  top, right, bottom, left = container.parse_padding({ top = 1, bottom = 2 })
  A.eq(top, 1); A.eq(right, 0); A.eq(bottom, 2); A.eq(left, 0)
  top, right, bottom, left = container.parse_padding(nil)
  A.eq(top, 0); A.eq(right, 0); A.eq(bottom, 0); A.eq(left, 0)

  local child = leaf()
  local pad = ui.Padding.new({
    padding = { top = 1, right = 2, bottom = 3, left = 4 },
    content = child,
  })
  pad:resize(5, 5, 20, 12)
  A.eq(child.x, 9); A.eq(child.y, 6)
  A.eq(child.w, 14); A.eq(child.h, 8)
end)

test("center positions and clamps its child", function()
  local child = leaf({ width = 4, height = 2 })
  local center = ui.Center.new({ content = child })
  center:resize(5, 5, 10, 10)
  A.eq(child.x, 8); A.eq(child.y, 9)
  A.eq(child.w, 4); A.eq(child.h, 2)

  local huge = leaf({ width = 30, height = 20 })
  local center2 = ui.Center.new({ content = huge })
  center2:resize(0, 0, 10, 8)
  A.eq(huge.x, 0); A.eq(huge.y, 0)
  A.eq(huge.w, 10); A.eq(huge.h, 8)
end)

test("stack shares geometry and switches visibility", function()
  local a, b = leaf(), leaf()
  local stack = ui.Stack.new({ children = { a, b } })
  stack:resize(3, 4, 10, 5)
  A.eq(a.x, 3); A.eq(a.y, 4); A.eq(a.w, 10); A.eq(a.h, 5)
  A.eq(b.x, 3); A.eq(b.y, 4); A.eq(b.w, 10); A.eq(b.h, 5)
  A.truthy(a.visible); A.truthy(b.visible)

  stack:show(2)
  A.falsy(a.visible); A.truthy(b.visible)
  local active, index = stack:active()
  A.eq(active, b); A.eq(index, 2)
  stack:show(1)
  active, index = stack:active()
  A.eq(active, a); A.eq(index, 1)
end)

test("vertical split sizes panes and separators", function()
  local a, b, c = leaf(), leaf(), leaf()
  local split = ui.Split.new({ direction = "vertical" })
  split:add(a)
  split:add(b, { weight = 2 })
  split:add(c, { weight = 1 })
  split:resize(0, 0, 24, 6)
  A.eq(a.x, 0); A.eq(a.w, 5)
  A.eq(b.x, 6); A.eq(b.w, 11)
  A.eq(c.x, 18); A.eq(c.w, 6)
  A.eq(a.h, 6); A.eq(b.h, 6); A.eq(c.h, 6)
  A.eq(#split.separators, 2)
  A.eq(split.separators[1].x, 5)
  A.eq(split.separators[2].x, 17)
  A.truthy(split.separators[1].vertical)
end)

test("split supports fixed sizes and hiding separators", function()
  local a, b = leaf(), leaf()
  local split = ui.Split.new({ direction = "vertical" })
  split:add(a, { size = 6 })
  split:add(b)
  split:resize(0, 0, 20, 4)
  A.eq(a.w, 6)
  A.eq(b.x, 7); A.eq(b.w, 13)
  A.eq(#split.separators, 1)

  local c, d, e = leaf(), leaf(), leaf()
  local bare = ui.Split.new({ direction = "vertical", show_separator = false })
  bare:add(c); bare:add(d); bare:add(e)
  bare:resize(0, 0, 20, 4)
  A.eq(c.w, 6); A.eq(d.x, 6); A.eq(d.w, 6); A.eq(e.x, 12); A.eq(e.w, 8)
  A.eq(#bare.separators, 0)
end)

test("horizontal split sizes panes vertically", function()
  local a, b = leaf(), leaf()
  local split = ui.Split.new({ direction = "horizontal" })
  split:add(a)
  split:add(b)
  split:resize(0, 0, 10, 20)
  A.eq(a.y, 0); A.eq(a.h, 9)
  A.eq(b.y, 10); A.eq(b.h, 10)
  A.eq(a.w, 10); A.eq(b.w, 10)
  A.eq(split.separators[1].y, 9)
  A.falsy(split.separators[1].vertical)
end)

test("box lays out a padded content area", function()
  local child = leaf()
  local box = ui.box({
    title = "Panel",
    padding = 1,
    content = child,
  })
  box:resize(0, 0, 12, 8)
  A.eq(box.content_x, 2); A.eq(box.content_y, 2)
  A.eq(box.content_w, 8); A.eq(box.content_h, 4)
  A.eq(child.x, 2); A.eq(child.y, 2)
  A.eq(child.w, 8); A.eq(child.h, 4)

  local bare_child = leaf()
  local bare = ui.box({ border = false, padding = 1, content = bare_child })
  bare:resize(0, 0, 12, 8)
  A.eq(bare.content_x, 1); A.eq(bare.content_y, 1)
  A.eq(bare.content_w, 10); A.eq(bare.content_h, 6)
  A.eq(bare_child.w, 10); A.eq(bare_child.h, 6)

  local fixed_child = leaf({ height = 2 })
  local sized = ui.box({ border = false, content = fixed_child })
  sized:resize(0, 0, 10, 8)
  A.eq(fixed_child.h, 2)
end)

test("container add, remove, clear and visible children", function()
  local box = ui.vbox({})
  local a, b = leaf(), leaf()
  A.eq(box:add(a), a)
  box:add(b)
  A.eq(#box.children, 2)
  A.eq(#box:visible_children(), 2)
  b.visible = false
  A.eq(#box:visible_children(), 1)
  A.truthy(box:remove(a))
  A.eq(a.parent, nil)
  A.eq(#box.children, 1)
  A.falsy(box:remove(a))
  box:clear()
  A.eq(#box.children, 0)
  A.eq(b.parent, nil)
end)

test("widget resize clamps negative sizes and reports change", function()
  local w = leaf()
  A.truthy(w:resize(1, 2, 3, 4))
  A.falsy(w:resize(1, 2, 3, 4))
  A.eq(w.x, 1); A.eq(w.y, 2); A.eq(w.w, 3); A.eq(w.h, 4)
  w:resize(0, 0, -5, -6)
  A.eq(w.w, 0); A.eq(w.h, 0)
end)

test("spacer is flexible without an argument and fixed with a height", function()
  local a, spacer, b = leaf({ width = 4 }), ui.spacer(), leaf({ width = 4 })
  local hbox = ui.hbox({ children = { a, spacer, b } })
  hbox:resize(0, 0, 20, 2)
  A.eq(a.w, 4)
  A.eq(spacer.w, 12)
  A.eq(b.x, 16)

  local top, gap, bottom = leaf({ height = 1 }), ui.spacer(2), leaf({ height = 1 })
  local vbox = ui.vbox({ children = { top, gap, bottom } })
  vbox:resize(0, 0, 10, 10)
  A.eq(top.y, 0)
  A.eq(gap.y, 1); A.eq(gap.h, 2)
  A.eq(bottom.y, 3)
end)

return H.finish()
