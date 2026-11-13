-- Boom Boom Rocket Revolution: the game's own script with clearer text (see Options\BBRR_text.lua).
System.LoadLuaScript("Options\\BBRR_text.lua")
System.LoadLuaScript("orig:Ingame\\Pause.lua")

-- The pause menu's quit entry takes the player back to the menus, so it says "Exit to Menu".
-- Only while this screen builds its menu: the same text is used elsewhere in the game.
local game_Init = Init
function Init(...)
  local saved = UIText and UIText.IDS_QUIT
  if UIText ~= nil and UIText.BBRR_EXIT_TO_MENU ~= nil then
    UIText.IDS_QUIT = UIText.BBRR_EXIT_TO_MENU
  end
  local results = {pcall(game_Init, ...)}
  if UIText ~= nil then
    UIText.IDS_QUIT = saved
  end
  if not results[1] then
    error(results[2])
  end
  return unpack(results, 2)
end
