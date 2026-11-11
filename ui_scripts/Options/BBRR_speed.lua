-- Boom Boom Rocket Revolution: scroll speed panel on the song select screen.
--
-- Loaded by Main\\ChooseSong.lua after the game's own script. Shows the current scroll speed
-- to the right of the song list; LB lowers it and RB raises it. The tempo at the right end
-- of every song's bar (drawn by src/song_length.cpp) is the song's BPM times this speed,
-- so it changes as the buttons are pressed.
--
--   BBRR("bbrr.speed.step") -> applies LB / RB presses, puts the speed ("1.5x") into stored
--                              string STR119 and returns "1" when that text changed

local function BBRR(command, key, value)
  local ok, result = pcall(gcinfo, command, key, value)
  if ok then
    return result
  end
  return nil
end

local VALUE_SLOT = 119
-- Positions are in the menu layout: 640 wide with 320 the middle of the screen (a 16:9
-- screen runs from about -106 to 746), 480 high. The song bars end at about 555.
local PANEL_X = 640      -- middle of the panel, to the right of the song bars
local PANEL_Y = 194      -- middle of the song list
local BPM_X = 505        -- middle of the tempo column inside the bars
local BPM_Y = 30         -- above the first bar, beside the "Choose Song" title

local value_id = nil
local game_Init = Init
local game_Update = Update

local function Text(x, y, height, text, style)
  if text == nil then
    return nil
  end
  return Buttons.AddButton({
    type = UIEnums.ButtonTypes.TEXT,
    x = x,
    y = y,
    width = 200,
    height = height,
    justify = UIEnums.Justify.JUSTIFY_CENTRE,
    colour = "255 255 255 255",
    text = text,
    render_state = style
  })
end

function Init(...)
  game_Init(...)
  BBRR("bbrr.speed.step")
  local styles = UIGlobals.font_render_state
  -- A heading over the tempo column, in the style of the screen's title.
  Text(BPM_X, BPM_Y, 24, UIText.IDS_BPM or UIText.BBRR_BPM, styles.new_piped_orange)
  Text(PANEL_X, PANEL_Y - 40, 18, UIText.BBRR_SPEED_LABEL, styles.new_filled_blue)
  value_id = Text(PANEL_X, PANEL_Y - 18, 32, "STR" .. VALUE_SLOT, styles.new_filled_orange)
  Text(PANEL_X, PANEL_Y + 20, 18, UIText.BBRR_SPEED_HINT, styles.new_filled_orange)
end

function Update(...)
  game_Update(...)
  if value_id ~= nil and BBRR("bbrr.speed.step") == "1" then
    Buttons.ChangeText(value_id, "STR" .. VALUE_SLOT)
  end
end
