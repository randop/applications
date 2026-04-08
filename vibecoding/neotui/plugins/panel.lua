-- panel.lua - Full Neovim-like rendering API for Panel 3
-- Uses the basic API functions to implement advanced features like scrolling, flexboxes, sidebar, text editing

local panel = {}

-- Viewport and scrolling
local viewport = {
	x = 0,
	y = 0,
	width = 80,
	height = 24,
}

-- Text buffer for editing
local text_buffer = {
	lines = { "" },
	cursor_line = 1,
	cursor_col = 1,
	show_line_numbers = true,
}

-- Layout system (simple flexbox-like)
local layout_boxes = {}

-- Get reference to API
local api = _G.api

-- Use API functions for basic grid operations
panel.grid_resize = api.grid_resize
panel.grid_clear = api.grid_clear
panel.hl_attr_define = api.hl_attr_define

-- Enhanced grid_line with viewport support
function panel.grid_line(grid_id, row, col_start, cells, wrap)
	-- Apply viewport offset
	row = row - viewport.y
	col_start = col_start - viewport.x

	-- Only render if within viewport
	if row >= 0 and row < viewport.height then
		api.grid_line(grid_id, row, col_start, cells, wrap)
	end
end

-- Scroll viewport
function panel.scroll(dx, dy)
	viewport.x = math.max(0, viewport.x + dx)
	viewport.y = math.max(0, viewport.y + dy)
end

-- Create a flexbox container
function panel.flexbox(id, x, y, width, height, direction)
	layout_boxes[id] = {
		x = x,
		y = y,
		width = width,
		height = height,
		direction = direction or "vertical", -- "vertical" or "horizontal"
		children = {},
	}
	return id
end

-- Add child to flexbox
function panel.flexbox_add_child(parent_id, child_id, flex)
	if layout_boxes[parent_id] then
		table.insert(layout_boxes[parent_id].children, {
			id = child_id,
			flex = flex or 1,
		})
	end
end

-- Render flexbox layout
function panel.render_flexbox(box_id)
	local box = layout_boxes[box_id]
	if not box then
		return
	end

	local total_flex = 0
	for _, child in ipairs(box.children) do
		total_flex = total_flex + child.flex
	end

	local current_pos = box.direction == "vertical" and box.y or box.x
	local size_per_flex = box.direction == "vertical" and (box.height / total_flex) or (box.width / total_flex)

	for _, child in ipairs(box.children) do
		local child_size = child.flex * size_per_flex
		if box.direction == "vertical" then
			-- Render child vertically
			panel.grid_line(
				1,
				math.floor(current_pos),
				box.x,
				{ { "[", 0 }, { child.id, 1 }, { "]", 0 }, { string.rep(" ", child_size - 4), 0 } }
			)
			current_pos = current_pos + child_size
		else
			-- Render child horizontally
			panel.grid_line(1, box.y, math.floor(current_pos), { { "[", 0 }, { child.id, 1 }, { "]", 0 } })
			current_pos = current_pos + child_size
		end
	end
end

-- Sidebar rendering
function panel.render_sidebar(width, content)
	-- Render sidebar on the left
	local grid_width, grid_height = api.get_grid_size()
	for i, line in ipairs(content) do
		if i <= grid_height then
			panel.grid_line(1, i - 1, 0, { { line, 2 }, { string.rep(" ", width - #line), 2 } })
		end
	end
	-- Adjust main content area
	viewport.x = width
	viewport.width = grid_width - width
end

-- Text editing with line numbers
function panel.set_text_buffer(lines)
	text_buffer.lines = lines or { "" }
	text_buffer.cursor_line = 1
	text_buffer.cursor_col = 1
end

function panel.render_text_editor()
	local line_num_width = text_buffer.show_line_numbers and 4 or 0

	for i, line in ipairs(text_buffer.lines) do
		if i > viewport.y and i <= viewport.y + viewport.height then
			local display_line = i - viewport.y

			-- Render line number
			if text_buffer.show_line_numbers then
				local line_num = string.format("%3d ", i)
				panel.grid_line(1, display_line - 1, viewport.x, { { line_num, 3 } })
			end

			-- Render line content
			local content_start = viewport.x + line_num_width
			local content = line:sub(viewport.x + 1, viewport.x + viewport.width - line_num_width)
			panel.grid_line(1, display_line - 1, content_start, { { content, 0 } })

			-- Render cursor
			if i == text_buffer.cursor_line then
				local cursor_x = content_start + text_buffer.cursor_col - 1 - viewport.x
				if cursor_x >= content_start and cursor_x < content_start + viewport.width - line_num_width then
					-- Highlight cursor position (simple block cursor)
					grid[display_line][cursor_x] = { char = grid[display_line][cursor_x].char, hl_id = 4 }
				end
			end
		end
	end
end

-- Insert text at cursor
function panel.insert_text(text)
	local line = text_buffer.lines[text_buffer.cursor_line]
	local before = line:sub(1, text_buffer.cursor_col - 1)
	local after = line:sub(text_buffer.cursor_col)
	text_buffer.lines[text_buffer.cursor_line] = before .. text .. after
	text_buffer.cursor_col = text_buffer.cursor_col + #text
end

-- Delete character at cursor
function panel.delete_char()
	local line = text_buffer.lines[text_buffer.cursor_line]
	if text_buffer.cursor_col > 1 then
		local before = line:sub(1, text_buffer.cursor_col - 2)
		local after = line:sub(text_buffer.cursor_col)
		text_buffer.lines[text_buffer.cursor_line] = before .. after
		text_buffer.cursor_col = text_buffer.cursor_col - 1
	elseif text_buffer.cursor_line > 1 then
		-- Join with previous line
		local prev_line = text_buffer.lines[text_buffer.cursor_line - 1]
		text_buffer.lines[text_buffer.cursor_line - 1] = prev_line .. line
		table.remove(text_buffer.lines, text_buffer.cursor_line)
		text_buffer.cursor_line = text_buffer.cursor_line - 1
		text_buffer.cursor_col = #prev_line + 1
	end
end

-- Move cursor
function panel.move_cursor(dx, dy)
	text_buffer.cursor_col =
		math.max(1, math.min(#text_buffer.lines[text_buffer.cursor_line] + 1, text_buffer.cursor_col + dx))
	text_buffer.cursor_line = math.max(1, math.min(#text_buffer.lines, text_buffer.cursor_line + dy))
end

-- Render the complete grid to string
function panel.render_grid()
	return api.render_grid()
end

-- Text buffer for editing
local text_buffer = {
	lines = {
		"function hello_world()",
		"    print('Hello, Neovim!')",
		"end",
		"",
		"-- This is a Lua function",
		"-- demonstrating text editing",
		"-- with line numbers"
	},
	cursor_line = 1,
	cursor_col = 1,
	top_line = 1,  -- First visible line
	show_line_numbers = true
}

-- Render text editor
function panel.render_text_editor()
	local line_num_width = text_buffer.show_line_numbers and 4 or 0
	local content_start = 21 + line_num_width  -- Start after sidebar + line numbers
	local content_width = 80 - 21 - line_num_width  -- Available width for content

	-- Render visible lines
	for i = 0, 22 do  -- 23 visible lines (0-22)
		local buffer_line = text_buffer.top_line + i
		if buffer_line <= #text_buffer.lines then
			local line = text_buffer.lines[buffer_line]

			-- Render line number
			if text_buffer.show_line_numbers then
				local line_num = string.format("%3d ", buffer_line)
				api.grid_line(1, i + 1, 21, {{line_num, 3}})  -- Row i+1 to skip header
			end

			-- Render line content
			local visible_content = line:sub(1, content_width)
			api.grid_line(1, i + 1, content_start, {{visible_content, 0}})

			-- Render cursor if on this line
			if buffer_line == text_buffer.cursor_line then
				local cursor_x = content_start + text_buffer.cursor_col - 1
				if cursor_x >= content_start and cursor_x < content_start + content_width then
					-- Get current character at cursor
					local cursor_char = line:sub(text_buffer.cursor_col, text_buffer.cursor_col)
					if cursor_char == "" then cursor_char = " " end
					api.grid_line(1, i + 1, cursor_x, {{cursor_char, 4}})  -- Highlight with cursor color
				end
			end
		end
	end
end

-- Handle keyboard input for text editing
function panel.handle_key(key)
	if key == "h" then  -- Left
		text_buffer.cursor_col = math.max(1, text_buffer.cursor_col - 1)
	elseif key == "j" then  -- Down
		text_buffer.cursor_line = math.min(#text_buffer.lines, text_buffer.cursor_line + 1)
		text_buffer.cursor_col = math.min(#text_buffer.lines[text_buffer.cursor_line] + 1, text_buffer.cursor_col)
	elseif key == "k" then  -- Up
		text_buffer.cursor_line = math.max(1, text_buffer.cursor_line - 1)
		text_buffer.cursor_col = math.min(#text_buffer.lines[text_buffer.cursor_line] + 1, text_buffer.cursor_col)
	elseif key == "l" then  -- Right
		text_buffer.cursor_col = math.min(#text_buffer.lines[text_buffer.cursor_line] + 1, text_buffer.cursor_col + 1)
	elseif key == "i" then  -- Insert mode (just move cursor for demo)
		-- Could implement insert mode here
	elseif key == "x" then  -- Delete character
		local line = text_buffer.lines[text_buffer.cursor_line]
		if text_buffer.cursor_col <= #line then
			text_buffer.lines[text_buffer.cursor_line] = line:sub(1, text_buffer.cursor_col - 1) .. line:sub(text_buffer.cursor_col + 1)
		end
	elseif key == "a" then  -- Append (move cursor right)
		text_buffer.cursor_col = math.min(#text_buffer.lines[text_buffer.cursor_line] + 1, text_buffer.cursor_col + 1)
	end

	-- Update viewport if cursor goes out of view
	if text_buffer.cursor_line < text_buffer.top_line then
		text_buffer.top_line = text_buffer.cursor_line
	elseif text_buffer.cursor_line >= text_buffer.top_line + 23 then
		text_buffer.top_line = text_buffer.cursor_line - 22
	end
end

-- Demo function showcasing all features
function panel.demo()
	-- Initialize
	api.grid_resize(1, 80, 24)
	api.grid_clear(1)

	-- Define highlights
	api.hl_attr_define(0, { foreground = 0xFFFFFF, background = 0x000000 }) -- Normal
	api.hl_attr_define(1, { foreground = 0x00FF00, bold = true }) -- Header
	api.hl_attr_define(2, { foreground = 0x0088FF, background = 0x222244 }) -- Sidebar
	api.hl_attr_define(3, { foreground = 0x888888 }) -- Line numbers
	api.hl_attr_define(4, { foreground = 0x000000, background = 0xFFFFFF, reverse = true }) -- Cursor

	-- Sidebar (left side)
	api.grid_line(1, 0, 0, {{"Files", 2}})
	api.grid_line(1, 1, 0, {{"├── src/", 2}})
	api.grid_line(1, 2, 0, {{"│   ├── main.cpp", 2}})
	api.grid_line(1, 3, 0, {{"│   └── tui.cpp", 2}})
	api.grid_line(1, 4, 0, {{"├── include/", 2}})
	api.grid_line(1, 5, 0, {{"└── plugins/", 2}})

	-- Header
	api.grid_line(1, 0, 21, {{"Neovim-like Text Editor", 1}})

	-- Render text editor
	panel.render_text_editor()

	-- Add instructions
	api.grid_line(1, 22, 21, {{"Use h/j/k/l to move cursor, x to delete", 3}})
	api.grid_line(1, 23, 21, {{"Panel demonstrates neovim-like layout", 3}})

	return api.render_grid()
end

log("[INFO] Panel loaded.")

return panel

