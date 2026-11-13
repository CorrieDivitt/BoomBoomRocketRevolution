-- Boom Boom Rocket Revolution: the game's own results screen plus one line about timing:
-- the song's average hit timing, its spread, and how many hits were early and late.
-- The PC side (src/input_offset.cpp) keeps the numbers and writes the line into stored
-- string STR126 when asked; it answers "0" when Show Accuracy is off or nothing was hit.
-- Below it, when the hits were clearly early or late, a suggested Input Offset (STR118).

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
local game_Update = Update
-- Autoplay (src/autoplay.cpp): once it has been used in this run, the game's profile save is
-- skipped, so no autoplayed score can reach the save file.
local no_save = false

-- One centred orange line below the results box, fading out with the screen.
local function AddLine(y, text)
  Buttons.AddButton({
    type = UIEnums.ButtonTypes.TEXT,
    x = 320,
    y = y,
    width = SCREEN_WIDTH,
    height = HEIGHT,
    justify = UIEnums.Justify.JUSTIFY_CENTRE,
    colour = "255 255 255 255",
    text = text,
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

-- After an Autoplay song the game's profile save is skipped. The screen also waits for that
-- save before it lets go of the controller when the game ends an Xbox Live session (with
-- the leaderboards on), so it is told the save has started - nothing is saved, as
-- save_needed is false - or the results screen ignores every button for 60 seconds.
function SkipSave()
  if no_save and GUI ~= nil then
    GUI.save_needed = false
    GUI.started_save = true
  end
end

function Init(...)
  game_Init(...)
  no_save = BBRR("bbrr.autoplay.nosave") == "1"
  SkipSave()
  -- On a wide screen the results box, its labels and values and the Rating beside it stay
  -- together in the middle; only the button prompts below (about y = 420) follow the edges.
  BBRR("bbrr.ui.keep", "-300,0,940," .. (BOX_BOTTOM + 64))
  local y = BOX_BOTTOM + 8
  -- Timing line (average, spread, early, late): only with Show Accuracy on.
  if BBRR("bbrr.hit.summary") == "1" then
    AddLine(y, "STR126")
    y = y + HEIGHT + 4
  end
  -- Offset suggestion (R-807): only when the song's hits were clearly early or late.
  if BBRR("bbrr.hit.advice") == "1" then
    AddLine(y, "STR118")
  end
end

function Update(...)
  SkipSave()
  return game_Update(...)
end
