-- workspace.lua - Lua FTXUI bridge for Panel 2 (Workspace)
-- This script builds UI elements using the 'workspace' global table

local function demo_workspace_ui()
	-- Button
	local btn = workspace.button("Demo Button", function()
		log("Button is clicked!")
	end)

	-- Spinner (element)
	local spinner = workspace.spinner(10, 0)

	-- Container for button and spinner
	local container = workspace.container_vert({
		btn,
	})

	-- Use renderer to include both
	local root = workspace.renderer(container, function()
		return workspace.vbox({
			btn,
			spinner,
		})
	end)

	workspace.set_root(root)
end

demo_workspace_ui()

log("[INFO] Workspace loaded.")

return workspace

