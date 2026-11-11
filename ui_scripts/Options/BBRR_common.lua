-- Boom Boom Rocket Revolution settings screen (shared by every settings page).
--
-- A page script (BBRR.lua, BBRR_video.lua, ...) sets BBRR_PAGE and loads this file.
-- Built the same way as the game's own Options screen (Options\Options.lua): a column
-- of "variable spinner" rows, A saves, B cancels, X resets to defaults. A row can also
-- be a link that opens another page.
--
-- Settings live on the PC side. BBRR(command, a, b) talks to it (the port answers calls
-- to the otherwise unused gcinfo() whose first argument starts with "bbrr."):
--   BBRR("bbrr.count", page)       -> number of rows on a page
--   BBRR("bbrr.key", page, n)      -> key of row n ("page:<name>" = link to that page)
--   BBRR("bbrr.choices", key)      -> how many options a setting has
--   BBRR("bbrr.get", key)          -> current option number (1-based) as a string
--   BBRR("bbrr.default", key)      -> default option number
--   BBRR("bbrr.set", key, n)       -> choose option n (applied immediately where possible)
--   BBRR("bbrr.save")              -> write the settings file
--   BBRR("bbrr.note", page)        -> "1" if the page has settings that need a restart
--   BBRR("bbrr.restart_needed")    -> "1" if a saved change needs a restart
--   BBRR("bbrr.restart")           -> restart the game now
-- Text ids: BBRR_T_<key> title, BBRR_C_<key>_<n> option n, BBRR_T_page_<name> link title.

local PAGE = BBRR_PAGE or "main"

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

local function PageScript(page)
  if page == "main" then
    return "Options\\BBRR.lua"
  end
  return "Options\\BBRR_" .. page .. ".lua"
end

-- Where B (or a finished save) goes: sub-pages return to the main page.
local function ParentScript()
  if PAGE == "main" then
    return "Options\\Main.lua"
  end
  return PageScript("main")
end

local function BuildRows()
  local rows = {}
  local count = tonumber(BBRR("bbrr.count", PAGE)) or 0
  for i = 1, count do
    local key = BBRR("bbrr.key", PAGE, i)
    if key ~= nil and string.sub(key, 1, 5) == "page:" then
      local target = string.sub(key, 6)
      local title = UIText["BBRR_T_page_" .. target]
      if title ~= nil then
        table.insert(rows, { link = target, title = title, items = { { text = UIText.BBRR_OPEN, value = 1 } } })
      end
    elseif key ~= nil then
      local choices = tonumber(BBRR("bbrr.choices", key)) or 0
      local items = {}
      for n = 1, choices do
        items[n] = { text = UIText["BBRR_C_" .. key .. "_" .. n], value = n }
      end
      if choices > 0 and UIText["BBRR_T_" .. key] ~= nil then
        table.insert(rows, { key = key, title = UIText["BBRR_T_" .. key], items = items })
      end
    end
  end
  return rows
end

GUI = {
  current_selection = -1,
  finished = false,
  rows = nil,
  old_values = {},
  spinner_menu_ID = {},
  note_ID = nil
}

function Init()
  SetupBackground()
  GUI.padtitle_ID = SetupPrimaryPadTitle(UIGlobals.options_pad)
  SetDefaultScreenTimers()
  SetupNextAndBack(false, UIText.IDS_A_SAVE, UIText.IDS_B_CANCEL)
  SetupNextAndBack(true, UIText.IDS_DP_SELECT, UIText.IDS_X_RESET_TO_DEFAULTS)
  SetupTitleText(UIText["BBRR_T_page_" .. PAGE] or UIText.BBRR_SETTINGS)
  Buttons.AddEffect(GUI.padtitle_ID, GUIBank.fade_out_effect)

  GUI.rows = BuildRows()
  GUI.num_items = table.getn(GUI.rows)

  local spinner = {
    type = UIEnums.ButtonTypes.VARIABLE_SPINNER,
    x = 320,
    y = 100,
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

    local current = 1
    if row.key ~= nil then
      current = OptionNumber("bbrr.get", row.key, table.getn(row.items))
    end
    GUI.old_values[i] = current
    Buttons.SetSelection(GUI.spinner_menu_ID[i], current)
  end

  -- Footnote under the rows: pages with settings that only take effect on the next start
  -- say so here.
  if BBRR("bbrr.note", PAGE) == "1" and UIText.BBRR_RESTART_NOTE ~= nil then
    local ok, id = pcall(Buttons.AddButton, {
      type = UIEnums.ButtonTypes.TEXT,
      x = 320,
      y = spinner.y + 4,
      width = SCREEN_WIDTH,
      height = UIGlobals.font_size.very_small,
      justify = UIEnums.Justify.JUSTIFY_CENTRE,
      colour = "255 255 255 255",
      text = UIText.BBRR_RESTART_NOTE,
      render_state = UIGlobals.font_render_state.new_filled_blue,
      effects = {
        {
          when = UIEnums.EffectWhen.End,
          type = UIEnums.EffectType.Colour,
          from = "255 255 255 255",
          to = "255 0 0 0",
          duration = 0.1,
          timer = 0
        }
      }
    })
    if ok then
      GUI.note_ID = id
    end
  end

  GUI.current_selection = -1
  if GUI.num_items > 0 then
    SetSelection(1)
  end
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
    if GUI.rows[i].key ~= nil then
      BBRR("bbrr.set", GUI.rows[i].key, Buttons.GetSelection(GUI.spinner_menu_ID[i]))
    end
  end
end

local function Leave(script)
  GUI.finished = true
  Screen.SetNextScreen(script)
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
    local row = GUI.rows[GUI.current_selection]
    if row ~= nil and row.link ~= nil then
      Leave(PageScript(row.link))
    elseif BBRR("bbrr.restart_needed") == "1" and UIText.BBRR_RESTART_TITLE ~= nil then
      -- Ask with the game's own pop-up box: X restarts now, B carries on without restarting.
      -- Either way this page is done; the pop-up moves on to the parent page when B is pressed.
      GUI.finished = true
      CustomPopup.Data.BBRR_RESTART = {
        darken = true,
        no_next = true,
        message_text_ID = UIText.BBRR_RESTART_TITLE,
        info_text_ID = UIText.BBRR_RESTART_INFO,
        left_text_ID = UIText.BBRR_B_LATER,
        right_text_ID = UIText.BBRR_X_RESTART,
        prev_screen = ParentScript(),
        message_CB = function(popup_message, popup_pad, popup_data)
          if popup_message == "BUTTON_X" then
            PlayNextSound()
            BBRR("bbrr.restart")
            return true
          end
          return false
        end
      }
      SetupCustomPopup("BBRR_RESTART")
    else
      Leave(ParentScript())
    end
  elseif message == "BUTTON_BACK" then
    for i = 1, GUI.num_items do
      if GUI.rows[i].key ~= nil then
        BBRR("bbrr.set", GUI.rows[i].key, GUI.old_values[i])
      end
    end
    PlayBackSound()
    Leave(ParentScript())
  elseif message == "BUTTON_X" then
    for i = 1, GUI.num_items do
      local row = GUI.rows[i]
      if row.key ~= nil then
        Buttons.SetSelection(GUI.spinner_menu_ID[i], OptionNumber("bbrr.default", row.key, table.getn(row.items)))
      end
    end
    ApplyAll()
  end
end

function Exit()
end
