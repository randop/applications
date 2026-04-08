-- scripts/ui.lua ── Basic Input Text Box Demo (Fixed)

-------------------------------------------------------------------------------
-- Shared state
-------------------------------------------------------------------------------
local log_lines = { "Ready. Type something and press Submit." }
local MAX_LOG = 10

local function log(msg)
	table.insert(log_lines, 1, msg)
	while #log_lines > MAX_LOG do
		table.remove(log_lines)
	end
end

-------------------------------------------------------------------------------
-- Widgets
-------------------------------------------------------------------------------
local name_input = ftxui.input("Enter your name...")

local submit_btn = ftxui.button("[ Submit ]", function()
	local name = ftxui.get_input_value(name_input)
	if name == "" then
		log("❌ Please enter something!")
	else
		log("✅ Submitted: " .. name)
	end
end)

local clear_btn = ftxui.button("[ Clear Log ]", function()
	log_lines = {}
	log("Log cleared.")
end)

local quit_btn = ftxui.button("[ Quit ]", function()
	ftxui.quit()
end)

-------------------------------------------------------------------------------
-- Layout
-------------------------------------------------------------------------------
local form = ftxui.container_vert({
	name_input,
	ftxui.container_horiz({ submit_btn, clear_btn, quit_btn }),
})

-------------------------------------------------------------------------------
-- Renderer
-------------------------------------------------------------------------------
local root = ftxui.renderer(form, function()
	local input_value = ftxui.get_input_value(name_input)

	local preview = ftxui.vbox({
		ftxui.text(" Current Input: "),
		ftxui.text("   " .. (input_value ~= "" and input_value or "(empty)")),
		ftxui.separator(),
	})

	-- Log items
	local log_items = {}
	for _, line in ipairs(log_lines) do
		table.insert(log_items, ftxui.text("  " .. line))
	end
	if #log_items == 0 then
		log_items[1] = ftxui.text("  (no entries yet)")
	end

	return ftxui.border(ftxui.vbox({
		ftxui.text(" Basic Input Text Box Demo "), -- Fixed here
		ftxui.separator(),
		preview,
		ftxui.border(ftxui.vbox(log_items)),
		ftxui.separator(),
		ftxui.text(" Tab/Arrows: navigate   Enter: activate   Esc: quit"),
	}))
end)

ftxui.set_root(root)
