-- Boom Boom Rocket Revolution: main menu tweaks.
--
-- The game's own main menu script does all the work; it is loaded here under its
-- original name ("orig:" asks the port for the script packed in the game). We only step
-- in while it builds the menu:
--   * "Downloadable Content" is left out (packs are picked up from the DLC folder).
--   * "Return to Arcade" is shown as "Exit Game".
--   * "Leaderboards" is no longer greyed out (see Init).
--   * "Exit Game" asks first ("Close Boom Boom Rocket and return to Windows?").

System.LoadLuaScript("Options\\BBRR_online.lua")
System.LoadLuaScript("orig:Main\\MainMenu.lua")

-- Exit Game: on the Xbox, "Return to Arcade" went straight back to the dashboard. On a PC that
-- closes the game, so it is easy to do by accident with one press too many: the game's own
-- pop-up box asks first. A quits, B goes back to the menu.
if Game ~= nil and Game.ReturnToArcade ~= nil and BBRR_RealReturnToArcade == nil then
  BBRR_RealReturnToArcade = Game.ReturnToArcade
end
if BBRR_RealReturnToArcade ~= nil and UIText.BBRR_QUIT_TITLE ~= nil and CustomPopup ~= nil then
  -- pcall: should Game not take new fields, the menu keeps working without the question.
  pcall(function() Game.ReturnToArcade = function(...)
    CustomPopup.Data.BBRR_QUIT = {
      darken = true,
      message_text_ID = UIText.BBRR_QUIT_TITLE,
      info_text_ID = UIText.BBRR_QUIT_INFO,
      left_text_ID = UIText.BBRR_QUIT_CANCEL,
      right_text_ID = UIText.BBRR_QUIT_CONFIRM,
      message_CB = function(popup_message, popup_pad, popup_data)
        if popup_message == "BUTTON_NEXT" then
          PlayNextSound()
          pcall(gcinfo, "bbrr.log", "Exit Game confirmed")
          BBRR_RealReturnToArcade()
          return true
        elseif popup_message == "BUTTON_BACK" then
          PlayBackSound()
          Screen.CancelPopup()
          return true
        end
        return false
      end
    }
    SetupCustomPopup("BBRR_QUIT")
  end end)
end

local original_Init = Init
local original_Update = Update
local original_Message = Message

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
  -- Leaderboards: the game greys the entry out unless the player is signed in to Xbox Live.
  -- The port keeps its own leaderboards on this PC (every profile's best scores), so the
  -- entry is switched back on: any call that turns a menu item off while naming the
  -- leaderboards entry is turned into one that leaves it on.
  local real_SetItemActive = Buttons.SetItemActive
  Buttons.SetItemActive = function(...)
    local args = {...}
    local count = select("#", ...)
    local names_leaderboards = false
    local described = ""
    for i = 1, count do
      described = described .. tostring(args[i]) .. " "
      if args[i] == GUI.leaderboards then names_leaderboards = true end
    end
    if names_leaderboards then
      for i = 1, count do
        if args[i] == false then args[i] = true end
      end
    end
    pcall(gcinfo, "bbrr.log", "main menu SetItemActive(" .. described .. "), leaderboards entry is " ..
      tostring(GUI.leaderboards) .. (names_leaderboards and ": kept on" or ""))
    return real_SetItemActive(unpack(args, 1, count))
  end
  local ok, problem = pcall(original_Init)
  Buttons.SetItemActive = real_SetItemActive
  SetupMenu = real_SetupMenu
  if not ok then
    error(problem)
  end
  -- Help line shown under the menu for the renamed entry.
  if GUI.text_table ~= nil and UIText.BBRR_EXIT_GAME_INFO ~= nil then
    GUI.text_table[GUI.return_to_arcade] = UIText.BBRR_EXIT_GAME_INFO
  end
end

-- Choosing Leaderboards: the game checks for an Xbox Live profile again at that moment and
-- shows "not signed in" instead of opening the screen. The game's own functions are run
-- as if signed in; should one still ask for that message, the message is left out and the
-- leaderboard screen is opened the way the game's script does it. The check may sit in
-- Update or in Message (button presses arrive there), so both are covered.
local function WithLeaderboards(name, fn, ...)
  if fn == nil then
    return
  end
  local real_SetupCustomPopup = SetupCustomPopup
  local refused = false
  if real_SetupCustomPopup ~= nil and UIEnums ~= nil and UIEnums.CustomPopups ~= nil then
    SetupCustomPopup = function(which, ...)
      if which == UIEnums.CustomPopups.ONLINE_PROFILE_NA then
        refused = true
        return
      end
      return real_SetupCustomPopup(which, ...)
    end
  end
  local results = {pcall(BBRR_AsIfOnline, fn, ...)}
  SetupCustomPopup = real_SetupCustomPopup
  if not results[1] then
    error(results[2])
  end
  if refused then
    BBRR_LeaderboardLog("leaderboards: the main menu's " .. name .. " still refused; opening the screen directly")
    pcall(function()
      UIGlobals.scoreboard_player = UIGlobals.primary_pad
      if UIGlobals.screen_store ~= nil then
        UIGlobals.screen_store.lb_choosegametype_back = Screen.GetCurrentScreen()
      end
      Screen.SetNextScreen("Main\\LbChooseGameType.lua")
    end)
  end
  return unpack(results, 2)
end

function Update(...)
  return WithLeaderboards("Update", original_Update, ...)
end

if original_Message ~= nil then
  function Message(...)
    return WithLeaderboards("Message", original_Message, ...)
  end
end
