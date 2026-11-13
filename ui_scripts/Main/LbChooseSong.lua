-- Boom Boom Rocket Revolution: the game's own leaderboard screen, opened without Xbox Live.
-- The game's script does all the work ("orig:" asks the port for the one packed in the
-- game); its functions are only run as if the player were signed in
-- (see Options\\BBRR_online.lua).

System.LoadLuaScript("Options\\BBRR_online.lua")
System.LoadLuaScript("orig:Main\\LbChooseSong.lua")

local original_Init, original_Update, original_Message, original_Exit = Init, Update, Message, Exit

BBRR_LeaderboardLog("leaderboards: script Main\\LbChooseSong.lua loaded")

function Init(...)
  BBRR_LeaderboardLog("leaderboards: LbChooseSong Init")
  return BBRR_AsIfOnline(original_Init, ...)
end

function Update(...)
  return BBRR_AsIfOnline(original_Update, ...)
end

if original_Message ~= nil then
  function Message(...)
    return BBRR_AsIfOnline(original_Message, ...)
  end
end

if original_Exit ~= nil then
  function Exit(...)
    return BBRR_AsIfOnline(original_Exit, ...)
  end
end
