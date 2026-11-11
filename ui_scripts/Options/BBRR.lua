-- Boom Boom Rocket Revolution settings screen.
--
-- Built the same way as the game's own Options screen (Options\Options.lua): a column
-- of "variable spinner" rows, A saves, B cancels, X resets to defaults.
--
-- Settings live on the PC side. BBRR(command, key, value) talks to it (the port answers
-- calls to the otherwise unused gcinfo() whose first argument starts with "bbrr."):
--   BBRR("bbrr.get", key)        -> current option number (1-based) as a string
--   BBRR("bbrr.default", key)    -> default option number as a string
--   BBRR("bbrr.set", key, n)     -> choose option n (applied immediately)
--   BBRR("bbrr.save")            -> write the settings file

local function BBRR(command, key, value)
  local ok, result = pcall(gcinfo, command, key, value)
  if ok then
    return result
  end
  return nil
end

local function OptionNumber(command, key, count)
  local n = tonumber(BBRR(command, key))
  if n == nil or n < 1 or n > count then
    return 1
  end
  return n
end

-- One row per setting. "items" follow the game's spinner format: text id + value.
local function BuildRows()
  local rows = {}

  rows[1] = {
    key = "aspect",
    title = UIText.BBRR_ASPECT,
    items = {
      { text = UIText.BBRR_AR_AUTO, value = 1 },
      { text = UIText.BBRR_AR_169, value = 2 },
      { text = UIText.BBRR_AR_219, value = 3 },
      { text = UIText.BBRR_AR_329, value = 4 }
    }
  }

  rows[2] = {
    key = "layout",
    title = UIText.BBRR_LAYOUT,
    items = {
      { text = UIText.BBRR_LAYOUT_EDGES, value = 1 },
      { text = UIText.BBRR_LAYOUT_CENTRE, value = 2 }
    }
  }

  local offsets = {}
  for i = 1, 41 do
    offsets[i] = { text = UIText["BBRR_MS_" .. i], value = i }
  end
  rows[3] = { key = "audio_offset", title = UIText.BBRR_AUDIO_OFFSET, items = offsets }

  rows[4] = {
    key = "skip_intro",
    title = UIText.BBRR_SKIP_INTRO,
    items = {
      { text = UIText.IDS_ON, value = 1 },
      { text = UIText.IDS_OFF, value = 2 }
    }
  }
  return rows
end

GUI = {
  current_selection = -1,
  finished = false,
  rows = nil,
  old_values = {},
  spinner_menu_ID = {}
}

function Init()
  SetupBackground()
  GUI.padtitle_ID = SetupPrimaryPadTitle(UIGlobals.options_pad)
  SetDefaultScreenTimers()
  SetupNextAndBack(false, UIText.IDS_A_SAVE, UIText.IDS_B_CANCEL)
  SetupNextAndBack(true, UIText.IDS_DP_SELECT, UIText.IDS_X_RESET_TO_DEFAULTS)
  SetupTitleText(UIText.BBRR_SETTINGS)
  Buttons.AddEffect(GUI.padtitle_ID, GUIBank.fade_out_effect)

  GUI.rows = BuildRows()
  GUI.num_items = table.getn(GUI.rows)

  local spinner = {
    type = UIEnums.ButtonTypes.VARIABLE_SPINNER,
    x = 320,
    y = 130,
    width = 384,
    height = 32,
    justify = UIEnums.Justify.JUSTIFY_CENTRE,
    player = UIGlobals.options_pad,
    text_info = {
      font = 0,
      height = UIGlobals.font_size.small,
      offset = -4,
      colour = "255 255 255 255",
      text = UIText.IDS_OPTIONS
    },
    render_state = {
      blend_mode = UIEnums.BlendMode.Additive,
      shader = UIEnums.Shader.MultiTexture
    },
    title_selected = {
      colour = "255 255 255 255",
      render_state = UIGlobals.font_render_state.new_filled_orange
    },
    title_deselected = {
      colour = "255 255 255 255",
      render_state = UIGlobals.font_render_state.new_filled_blue
    },
    variable_selected = {
      colour = "255 255 255 255",
      render_state = UIGlobals.font_render_state.new_filled_orange
    },
    variable_deselected = {
      colour = "255 255 255 255",
      render_state = UIGlobals.font_render_state.new_filled_blue
    },
    end_size = 50,
    menu_end_l = UIGlobals.gfx.menu_end_l,
    menu_end_r = UIGlobals.gfx.menu_end_r,
    menu_line = UIGlobals.gfx.menu_line,
    menu_double_line = UIGlobals.gfx.menu_double_line,
    selected_colour = UIGlobals.colour.orange,
    colour = UIGlobals.colour.blue,
    effects = {}
  }
  local slide_in = {
    when = UIEnums.EffectWhen.Start,
    type = UIEnums.EffectType.Translate,
    fromx = 0, tox = 0, fromy = 0, toy = 0, fromz = 0, toz = 0,
    timer = 0,
    duration = UIGlobals.screen_timers.default_start
  }
  local slide_out = {
    when = UIEnums.EffectWhen.End,
    type = UIEnums.EffectType.Translate,
    fromx = 0, tox = 0, fromy = 0, toy = 0, fromz = 0, toz = 0,
    timer = 0,
    duration = UIGlobals.screen_timers.default_end
  }

  local from_left = true
  for i = 1, GUI.num_items do
    local row = GUI.rows[i]
    spinner.text_info.text = row.title
    spinner.items = row.items
    if from_left == true then
      slide_in.fromx = -640
      slide_out.tox = 640
      from_left = false
    else
      slide_in.fromx = 640
      slide_out.tox = -640
      from_left = true
    end
    spinner.effects[1] = slide_in
    spinner.effects[2] = slide_out
    spinner.width = 550
    GUI.spinner_menu_ID[i] = Buttons.AddButton(spinner)
    spinner.y = spinner.y + spinner.height
    Buttons.SetSelected(GUI.spinner_menu_ID[i], false)

    local current = OptionNumber("bbrr.get", row.key, table.getn(row.items))
    GUI.old_values[i] = current
    Buttons.SetSelection(GUI.spinner_menu_ID[i], current)
  end

  GUI.current_selection = -1
  SetSelection(1)
end

function SetSelection(index)
  if index == GUI.current_selection then
    return
  end
  if GUI.current_selection ~= -1 then
    Game.PlaySound(UIEnums.SoundEffect.Move)
    Buttons.SetSelected(GUI.spinner_menu_ID[GUI.current_selection], false)
  end
  GUI.current_selection = index
  Buttons.SetSelected(GUI.spinner_menu_ID[GUI.current_selection], true)
end

-- Apply what the spinners show right now (so changes can be seen and heard at once).
local function ApplyAll()
  for i = 1, GUI.num_items do
    BBRR("bbrr.set", GUI.rows[i].key, Buttons.GetSelection(GUI.spinner_menu_ID[i]))
  end
end

function Update()
  ContinueXBLSessionCreation()
end

function Message(message, pad, data)
  if GUI.finished == true then
    return
  end
  if pad ~= UIGlobals.options_pad then
    return
  end
  if message == "BUTTON_UP" then
    if GUI.current_selection > 1 then
      SetSelection(GUI.current_selection - 1)
    end
  elseif message == "BUTTON_DOWN" then
    if GUI.current_selection < GUI.num_items then
      SetSelection(GUI.current_selection + 1)
    end
  elseif message == "BUTTON_LEFT" or message == "BUTTON_RIGHT" then
    ApplyAll()
  elseif message == "BUTTON_NEXT" then
    ApplyAll()
    BBRR("bbrr.save")
    PlayNextSound()
    GUI.finished = true
    Screen.SetNextScreen("Options\\Main.lua")
  elseif message == "BUTTON_BACK" then
    for i = 1, GUI.num_items do
      BBRR("bbrr.set", GUI.rows[i].key, GUI.old_values[i])
    end
    PlayBackSound()
    GUI.finished = true
    Screen.SetNextScreen("Options\\Main.lua")
  elseif message == "BUTTON_X" then
    for i = 1, GUI.num_items do
      local row = GUI.rows[i]
      Buttons.SetSelection(GUI.spinner_menu_ID[i], OptionNumber("bbrr.default", row.key, table.getn(row.items)))
    end
    ApplyAll()
  end
end

function Exit()
end
