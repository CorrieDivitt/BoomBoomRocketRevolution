-- Boom Boom Rocket Revolution: hit timing readout for the in-game HUD.
--
-- Loaded by the HUD scripts (Ingame\Hud.lua, Ingame\Hud_2p.lua) after the game's own
-- script. After each hit a small number appears next to the arrow that was pressed:
-- how far the press was from the rocket's time in milliseconds, + early, - late.
--
-- The PC side (src/input_offset.cpp) does the measuring and writes the text into one of
-- the game's stored strings (shown by a text item whose text is "STR<n>"):
--   BBRR("bbrr.hit.enabled") -> "1" when the readout is switched on
--   BBRR("bbrr.hit.poll")    -> "" or "slot,x,y": show STR<slot> at layout position x, y
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
local HIDDEN_Y = -200

local ids = {}
local shown_at = {}
local enabled = false
local game_Init = Init
local game_Update = Update

function Init(...)
  game_Init(...)
  enabled = BBRR("bbrr.hit.enabled") == "1"
  if not enabled then
    return
  end
  for i = 1, SLOTS do
    ids[i] = Buttons.AddButton({
      type = UIEnums.ButtonTypes.TEXT,
      x = 320,
      y = HIDDEN_Y,
      width = 200,
      height = 22,
      justify = UIEnums.Justify.JUSTIFY_CENTRE,
      colour = "255 255 255 255",
      text = "STR" .. (FIRST_SLOT + i - 1),
      render_state = UIGlobals.font_render_state.new_filled_orange
    })
    shown_at[i] = nil
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
    local slot, x, y = string.match(hit, "^(%d+),([%-%d%.]+),([%-%d%.]+)$")
    local i = (tonumber(slot) or 0) - FIRST_SLOT + 1
    if ids[i] ~= nil and x ~= nil then
      Buttons.ChangeText(ids[i], "STR" .. slot)
      Buttons.ChangePosition(ids[i], tonumber(x), tonumber(y))
      shown_at[i] = now
    end
  end
  for i = 1, SLOTS do
    if shown_at[i] ~= nil and now - shown_at[i] > LIFE_MS then
      Buttons.ChangePosition(ids[i], 320, HIDDEN_Y)
      shown_at[i] = nil
    end
  end
end
