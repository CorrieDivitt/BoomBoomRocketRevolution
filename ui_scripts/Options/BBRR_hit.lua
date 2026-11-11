-- Boom Boom Rocket Revolution: hit timing readout for the in-game HUD.
--
-- Loaded by the HUD scripts (Ingame\Hud.lua, Ingame\Hud_2p.lua) after the game's own
-- script. After each hit a small number appears a little above the target line, in line
-- with the arrow that was pressed: how far the press was from the rocket's time in
-- milliseconds, + early, - late, in the plain orange menu font (the judgement-coloured
-- fonts got lost in the fireworks, and a dark plate behind them spoiled the picture).
--
-- The PC side (src/input_offset.cpp) does the measuring and writes the text into one of
-- the game's stored strings (shown by a text item whose text is "STR<n>"):
--   BBRR("bbrr.hit.enabled") -> "1" when the readout is switched on
--   BBRR("bbrr.hit.poll")    -> "" or "slot,x,grade": show STR<slot> at layout position x
--   BBRR("bbrr.time")        -> PC clock in milliseconds

local function BBRR(command, key, value)
  local ok, result = pcall(gcinfo, command, key, value)
  if ok then
    return result
  end
  return nil
end

local FIRST_SLOT = 120   -- STR120 .. STR123
local SLOTS = 4
local LIFE_MS = 600      -- how long a number stays
local HEIGHT = 17        -- text size
local LINE_Y = 144       -- the target line in the HUD (Ingame\Hud.lua)
local GAP = 40           -- space between the target line and the number
local SHOWN_Y = LINE_Y - GAP - HEIGHT
local HIDDEN_Y = -200

-- One style for every judgement. (Kept per judgement so colours can come back later.)
local function GradeStyles()
  local plain = UIGlobals.font_render_state.new_filled_orange
  return { [1] = plain, [3] = plain, [5] = plain, [6] = plain }
end

local ids = {}       -- ids[grade][slot index]
local shown = {}     -- shown[slot index] = { id = ..., at = ... }
local enabled = false
local game_Init = Init
local game_Update = Update

function Init(...)
  game_Init(...)
  enabled = BBRR("bbrr.hit.enabled") == "1"
  if not enabled then
    return
  end
  for grade, style in pairs(GradeStyles()) do
    ids[grade] = {}
    for i = 1, SLOTS do
      ids[grade][i] = Buttons.AddButton({
        type = UIEnums.ButtonTypes.TEXT,
        x = 320,
        y = HIDDEN_Y,
        width = 200,
        height = HEIGHT,
        justify = UIEnums.Justify.JUSTIFY_CENTRE,
        colour = "255 255 255 255",
        text = "STR" .. (FIRST_SLOT + i - 1),
        render_state = style
      })
    end
  end
  shown = {}
end

local function Hide(i)
  if shown[i] ~= nil then
    Buttons.ChangePosition(shown[i].id, 320, HIDDEN_Y)
    shown[i] = nil
  end
end

function Update(...)
  game_Update(...)
  if not enabled then
    return
  end
  local now = tonumber(BBRR("bbrr.time")) or 0
  for _ = 1, 8 do
    local hit = BBRR("bbrr.hit.poll")
    if hit == nil or hit == "" then
      break
    end
    local slot, x, grade = string.match(hit, "^(%d+),([%-%d%.]+),(%d+)$")
    local i = (tonumber(slot) or 0) - FIRST_SLOT + 1
    local row = ids[tonumber(grade) or 0]
    if row ~= nil and row[i] ~= nil then
      Hide(i)
      Buttons.ChangeText(row[i], "STR" .. slot)
      Buttons.ChangePosition(row[i], tonumber(x), SHOWN_Y)
      shown[i] = { id = row[i], at = now }
    end
  end
  for i = 1, SLOTS do
    if shown[i] ~= nil and now - shown[i].at > LIFE_MS then
      Hide(i)
    end
  end
end
