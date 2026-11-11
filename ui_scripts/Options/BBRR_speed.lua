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
local PANEL_X = 566      -- to the right of the song bars (layout is 640 x 480)
local PANEL_Y = 194      -- middle of the song list

local value_id = nil
local game_Init = Init
local game_Update = Update

local function Text(y, height, text)
  return Buttons.AddButton({
    type = UIEnums.ButtonTypes.TEXT,
    x = PANEL_X,
    y = y,
    width = 140,
    height = height,
    justify = UIEnums.Justify.JUSTIFY_CENTRE,
    colour = "255 255 255 255",
    text = text,
    render_state = UIGlobals.font_render_state.new_filled_orange
  })
end

function Init(...)
  game_Init(...)
  BBRR("bbrr.speed.step")
  Text(PANEL_Y - 30, 14, "BBRR_SPEED_LABEL")
  value_id = Text(PANEL_Y - 13, 26, "STR" .. VALUE_SLOT)
  Text(PANEL_Y + 18, 12, "BBRR_SPEED_HINT")
end

function Update(...)
  game_Update(...)
  if value_id ~= nil and BBRR("bbrr.speed.step") == "1" then
    Buttons.ChangeText(value_id, "STR" .. VALUE_SLOT)
  end
end
