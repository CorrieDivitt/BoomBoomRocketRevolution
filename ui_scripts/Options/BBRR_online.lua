-- Boom Boom Rocket Revolution: running the game's leaderboard screens without Xbox Live.
--
-- The leaderboard screens refuse to open unless the player is signed in to Xbox Live. The
-- port serves them from the scores kept on this PC (src/leaderboards.cpp), so the screens
-- that lead there load this file and run the game's own functions through
-- BBRR_AsIfOnline(fn, ...): while fn runs, every "is there an online profile" question
-- the scripts can ask is answered yes.

local function Log(text)
  pcall(gcinfo, "bbrr.log", text)
end

-- Tables of the game's script interface that have a HasOnlineProfile function. Which
-- table it lives in could not be read from the compiled scripts, so all are covered.
local cached_tables, cached_names = nil, ""

local function OnlineTables()
  if cached_tables ~= nil then
    return cached_tables, cached_names
  end
  local found = {}
  local names = ""
  for name, value in pairs(_G) do
    if type(value) == "table" then
      local ok, member = pcall(function()
        return value.HasOnlineProfile
      end)
      if ok and member ~= nil then
        table.insert(found, value)
        names = names .. tostring(name) .. " "
      end
    end
  end
  cached_tables, cached_names = found, names
  return found, names
end

local logged = false

function BBRR_AsIfOnline(fn, ...)
  if fn == nil then
    return
  end
  local tables, names = OnlineTables()
  if not logged then
    logged = true
    Log("leaderboards: HasOnlineProfile found in: " .. (names ~= "" and names or "(nowhere)") ..
      "; UIGlobals.online_profile is " .. tostring(UIGlobals and UIGlobals.online_profile))
  end
  local saved = {}
  for index, t in ipairs(tables) do
    saved[index] = rawget(t, "HasOnlineProfile")
    rawset(t, "HasOnlineProfile", function()
      return true
    end)
  end
  local was_online = UIGlobals and UIGlobals.online_profile
  if UIGlobals then
    UIGlobals.online_profile = true
  end
  local results = {pcall(fn, ...)}
  for index, t in ipairs(tables) do
    rawset(t, "HasOnlineProfile", saved[index])
  end
  if UIGlobals then
    UIGlobals.online_profile = was_online
  end
  if not results[1] then
    Log("leaderboards: a screen function failed: " .. tostring(results[2]))
    error(results[2])
  end
  return unpack(results, 2)
end

function BBRR_LeaderboardLog(text)
  Log(text)
end
