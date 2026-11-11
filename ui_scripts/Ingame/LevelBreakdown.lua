-- Boom Boom Rocket Revolution: the game's own results screen plus one line about timing:
-- the song's average hit timing, its spread, and how many hits were early and late.
-- The PC side (src/input_offset.cpp) keeps the numbers and writes the line into stored
-- string STR126 when asked; it answers "0" when Hit Timing is off or nothing was hit.

System.LoadLuaScript("orig:Ingame\\LevelBreakdown.lua")

local function BBRR(command, key, value)
  local ok, result = pcall(gcinfo, command, key, value)
  if ok then
    return result
  end
  return nil
end

local HEIGHT = 18
local BOX_BOTTOM = 336  -- lower edge of the results box, in layout units
local game_Init = Init

function Init(...)
  game_Init(...)
  if BBRR("bbrr.hit.summary") ~= "1" then
    return
  end
  -- Centred, just below the results box (the box ends at about y = 336).
  Buttons.AddButton({
    type = UIEnums.ButtonTypes.TEXT,
    x = 320,
    y = BOX_BOTTOM + 8,
    width = SCREEN_WIDTH,
    height = HEIGHT,
    justify = UIEnums.Justify.JUSTIFY_CENTRE,
    colour = "255 255 255 255",
    text = "STR126",
    render_state = UIGlobals.font_render_state.new_filled_orange,
    effects = {
      {
        when = UIEnums.EffectWhen.End,
        type = UIEnums.EffectType.Colour,
        from = "255 255 255 255",
        to = "0 0 0 0",
        duration = 0.1,
        timer = 0
      }
    }
  })
end
