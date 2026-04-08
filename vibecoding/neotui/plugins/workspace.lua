-- workspace.lua - Lua FTXUI bridge for Panel 2 (Workspace)
-- This script builds UI elements using the 'workspace' global table

log("Workspace loaded!")

local function demo_workspace_ui()
    -- Button
    local btn = workspace.button("Click Me", function()
        log("Clicked!")
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

return workspace