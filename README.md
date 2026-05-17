# split-monitor-workspaces

A lua package for Hyprland to provide `awesome`/`dwm`-like behavior with workspaces: split them between monitors and provide independent numbering.

The package features an easy to use API to manage workspaces on the currently focused monitor, without having to set up complicated rules yourself. It also provides functionality to create persistent workspaces on each monitor at Hyprland startup, as well as dynamically when new monitors are connected.

It's explicitly compatible with [the hy3 plugin](https://github.com/outfoxxed/hy3): we automatically detect if hy3 is installed, and if it is we'll use hy3's dispatchers instead of Hyprland's.

# Requirements

- Hyprland >= 0.55.0

If you're using an older version of Hyprland, or have not yet switched to the new Lua config, you can use the [cpp plugin](./docs/cpp-plugin.md) instead, which provides the same functionality. This will be deprecated soon in favour of the lua package.

# Installation

Simply clone the repository to a location of your choice, then add the following lines to your Hyprland config:

```lua
package.path = package.path .. ";/path/to/split-monitor-workspaces/lua/?.lua"
local smw = require("split-monitor-workspaces")
```

# Usage

Now that the `smw` package is imported, you can call the `setup()` function with your desired configuration.

Minimal example:

```lua
smw.setup({
    workspace_count = 5, -- This will create 5 persistent workspaces on each monitor at startup
})
```

You can set up keybinds like so:

```lua
local mainMod = "SUPER"
for i = 1, smw.get_amount_of_workspaces() do
    local n = tostring(i)
    if n == "10" then n = "0" end -- Optional if you configured 10 workspaces: bind workspace 10 to SUPER + 0
    -- Switch to the Nth workspace on the currently focused monitor.
    hl.bind(mainMod .. " +" .. n, smw.workspace(n))
    -- Move the active window to the Nth workspace on the currently focused monitor silently (no focus change).
    hl.bind(mainMod .. " + SHIFT +" .. n, smw.move_to_workspace_silent(n))
end
```

An example showing the full API can be found in [docs/example.lua](./docs/example.lua).

## Waybar integration

This plugin supports [waybar's](https://github.com/Alexays/Waybar) `hyprland/workspaces` module:

```json
"hyprland/workspaces": {
    "format": "{icon}",
    "format-icons": {
      "urgent": "",
      "active": "", // focused workspace on current monitor
      "visible": "", // focused workspace on other monitors
      "default": "",
      "empty": "" // persistent (created by this plugin)
    },
    "on-scroll-up": "hyprctl dispatch split-cycleworkspaces -1",
    "on-scroll-down": "hyprctl dispatch split-cycleworkspaces +1",
    "all-outputs": false // recommended
  },
```

# Special thanks

- [@Duckonaut](https://github.com/Duckonaut/): The original creator of this plugin
- [hyprsome](https://github.com/sopa0/hyprsome): An earlier project of similar nature
