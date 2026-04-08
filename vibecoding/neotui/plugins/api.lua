-- api.lua - Basic Neovim-like rendering API
-- Provides core grid rendering functions for use by panel.lua

local api = {}

-- Grid storage - represents the terminal screen as a 2D array of cells
local grid = {}
local grid_width = 80
local grid_height = 24

-- Highlight attributes storage
local highlights = {}
local next_highlight_id = 1

-- Initialize grid with empty cells
local function init_grid(width, height)
	grid = {}
	for y = 1, height do
		grid[y] = {}
		for x = 1, width do
			grid[y][x] = { char = " ", hl_id = 0 }
		end
	end
	grid_width = width
	grid_height = height
end

-- Set highlight attribute
function api.hl_attr_define(id, rgb_attr, cterm_attr, info)
	highlights[id] = {
		foreground = rgb_attr.foreground or 0xFFFFFF,
		background = rgb_attr.background or 0x000000,
		bold = rgb_attr.bold or false,
		italic = rgb_attr.italic or false,
		underline = rgb_attr.underline or false,
		reverse = rgb_attr.reverse or false,
	}
	next_highlight_id = math.max(next_highlight_id, id + 1)
	return id
end

-- Resize grid
function api.grid_resize(grid_id, width, height)
	if grid_id ~= 1 then
		return
	end
	init_grid(width, height)
end

-- Clear grid
function api.grid_clear(grid_id)
	if grid_id ~= 1 then
		return
	end
	for y = 1, grid_height do
		for x = 1, grid_width do
			grid[y][x] = { char = " ", hl_id = 0 }
		end
	end
end

-- Set grid line
function api.grid_line(grid_id, row, col_start, cells, wrap)
	if grid_id ~= 1 then
		return
	end
	row = row + 1 -- Convert to 1-based indexing
	col_start = col_start + 1

	local col = col_start
	local current_hl_id = 0

	for _, cell in ipairs(cells) do
		local char, hl_id, repeat_count
		if type(cell) == "table" then
			char = cell[1] or ""
			hl_id = cell[2] or current_hl_id
			repeat_count = cell[3] or 1
		else
			char = cell
			hl_id = current_hl_id
			repeat_count = 1
		end

		current_hl_id = hl_id

		for i = 1, repeat_count do
			if row >= 1 and row <= grid_height and col >= 1 and col <= grid_width then
				if char ~= "" then
					grid[row][col] = { char = char, hl_id = hl_id }
				end
			end
			col = col + 1
		end
	end
end

-- Get grid cell
function api.get_grid_cell(row, col)
	row = row + 1
	col = col + 1
	if row >= 1 and row <= grid_height and col >= 1 and col <= grid_width then
		return grid[row][col]
	end
	return { char = " ", hl_id = 0 }
end

-- Set grid cell
function api.set_grid_cell(row, col, cell)
	row = row + 1
	col = col + 1
	if row >= 1 and row <= grid_height and col >= 1 and col <= grid_width then
		grid[row][col] = cell
	end
end

-- Get grid dimensions
function api.get_grid_size()
	return grid_width, grid_height
end

-- Render grid to string
function api.render_grid()
	local lines = {}

	for y = 1, grid_height do
		local line = {}
		for x = 1, grid_width do
			local cell = grid[y][x]
			table.insert(line, cell.char)
		end
		table.insert(lines, table.concat(line))
	end

	return table.concat(lines, "\n")
end

-- Set up global API table for use by panels
_G.api = {
	grid_resize = api.grid_resize,
	grid_clear = api.grid_clear,
	grid_line = api.grid_line,
	get_grid_cell = api.get_grid_cell,
	set_grid_cell = api.set_grid_cell,
	get_grid_size = api.get_grid_size,
	render_grid = api.render_grid,
	hl_attr_define = api.hl_attr_define,
}

-- Initialize
init_grid(80, 24)

log("[INFO] API loaded.")

return api

