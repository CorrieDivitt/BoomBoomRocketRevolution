-- Boom Boom Rocket Revolution: replacement for the game's "Help & Options" menu.
-- Same as the original screen (rebuilt from the game's script) plus one extra entry
-- that opens the Revolution settings screen (Options\BBRR.lua).

GUI = {
  menu_ID = -1,
  finished = false,
  opt = { how_to_play = 1, controls = 2, options = 3, credits = 4, bbrr = 5 }
}

function Init()
  SetupBackground()
  GUI.logo_ID = SetupBoomBoomLogo()
  SetDefaultScreenTimers()
  SetupNextAndBack(true)
  local items = {
    { text = UIText.IDS_HOW_TO_PLAY, value = GUI.opt.how_to_play },
    { text = UIText.IDS_CONTROLS, value = GUI.opt.controls },
    { text = UIText.IDS_OPTIONS, value = GUI.opt.options },
    { text = UIText.IDS_CREDITS, value = GUI.opt.credits }
  }
  -- Only offer the entry when our strings made it into the game's text table.
  if UIText.BBRR_SETTINGS ~= nil then
    table.insert(items, 4, { text = UIText.BBRR_SETTINGS, value = GUI.opt.bbrr })
  end
  GUI.menu_ID = SetupMenu(items, UIEnums.Justify.JUSTIFY_RIGHT, UIText.IDS_HELP_N_OPTIONS)
  RestoreMenuSelection(GUI.menu_ID)
end

function Update()
  ContinueXBLSessionCreation()
  ShowSavePopup()
end

function Message(message, pad, data)
  CheckSaveMessage(message, pad)
  if GUI.finished == true then
    return
  end
  if PadMessageAcceptable(pad) == false then
    return
  end
  if message == "BUTTON_NEXT" then
    GUI.finished = true
    PlayNextSound()
    Buttons.AddEffect(GUI.logo_ID, GUIBank.fade_out_effect)
    StoreMenuSelection(GUI.menu_ID)
    local selection = Buttons.GetSelection(GUI.menu_ID)
    if selection == GUI.opt.how_to_play then
      UIGlobals.instructions_offset = 0
      Screen.SetNextScreen("Options\\HowToPlay.lua")
    elseif selection == GUI.opt.controls then
      Screen.SetNextScreen("Options\\Controls.lua")
    elseif selection == GUI.opt.options then
      UIGlobals.options_pad = pad
      Screen.SetNextScreen("Options\\Options.lua")
    elseif selection == GUI.opt.credits then
      Screen.SetNextScreen("Options\\Credits.lua")
    elseif selection == GUI.opt.bbrr then
      UIGlobals.options_pad = pad
      Screen.SetNextScreen("Options\\BBRR.lua")
    end
  elseif message == "BUTTON_BACK" then
    PlayBackSound()
    GUI.finished = true
    Screen.SetNextScreen(UIGlobals.screen_store.options_main_back)
  end
end

function Exit()
end
