-- Boom Boom Rocket Revolution: the game's own song-complete screen. After a song in which
-- Autoplay was on, the "new high score" screen is left out (its score is not kept).
System.LoadLuaScript("orig:Ingame\\LevelComplete.lua")

local game_Init, game_Update = Init, Update

local function NoHighScoreAfterAutoplay()
  local ok, used = pcall(gcinfo, "bbrr.autoplay.song")
  if ok and used == "1" and UIGlobals ~= nil then
    UIGlobals.show_highscore_achieved = false
  end
end

function Init(...)
  NoHighScoreAfterAutoplay()
  return game_Init(...)
end

function Update(...)
  NoHighScoreAfterAutoplay()
  return game_Update(...)
end
