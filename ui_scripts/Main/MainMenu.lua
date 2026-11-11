-- Boom Boom Rocket Revolution: main menu tweaks.
--
-- The game's own main menu script does all the work; it is loaded here under its
-- original name ("orig:" asks the port for the script packed in the game). We only step
-- in while it builds the menu:
--   * "Downloadable Content" is left out (packs are picked up from the DLC folder).
--   * "Return to Arcade" is shown as "Exit Game".

System.LoadLuaScript("orig:Main\\MainMenu.lua")

local original_Init = Init

function Init()
  local real_SetupMenu = SetupMenu
  SetupMenu = function(items, ...)
    local kept = {}
    for index, item in ipairs(items) do
      if item.value ~= GUI.dlc then
        if item.value == GUI.return_to_arcade and UIText.BBRR_EXIT_GAME ~= nil then
          item.text = UIText.BBRR_EXIT_GAME
        end
        table.insert(kept, item)
      end
    end
    return real_SetupMenu(kept, ...)
  end
  local ok, problem = pcall(original_Init)
  SetupMenu = real_SetupMenu
  if not ok then
    error(problem)
  end
  -- Help line shown under the menu for the renamed entry.
  if GUI.text_table ~= nil and UIText.BBRR_EXIT_GAME_INFO ~= nil then
    GUI.text_table[GUI.return_to_arcade] = UIText.BBRR_EXIT_GAME_INFO
  end
end
