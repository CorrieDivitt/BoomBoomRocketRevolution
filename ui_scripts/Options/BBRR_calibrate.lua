-- Boom Boom Rocket Revolution: calibration screen.
--
-- Two tap tests, the way band games do it:
--   1. Sound:   clicks play at a steady beat, the player presses A on every click.
--   2. Picture: no sound; a ring slides to a target ring and the player presses A when
--               they line up.
-- Each test gives "how late the taps were" (sound or picture delay plus the player's
-- input delay). Their difference is how far sound and picture are apart, which is what
-- the Audio Offset setting corrects. The PC side does the arithmetic:
--   BBRR("bbrr.time")                      -> PC clock in milliseconds
--   BBRR("bbrr.calibrate", sound, picture) -> Audio Offset option number for those delays
--   BBRR("bbrr.set", "audio_offset", n), BBRR("bbrr.save")

local function BBRR(command, key, value)
  local ok, result = pcall(gcinfo, command, key, value)
  if ok then
    return result
  end
  return nil
end

local function Now()
  return tonumber(BBRR("bbrr.time")) or 0
end

local BEAT_MS = 800       -- time between clicks / ring passes
local WARM_UP = 2         -- first taps are ignored while the player finds the rhythm (shown as -2, -1)
local TAPS = 12           -- taps that count
local MAX_SPREAD_MS = 70  -- taps more uneven than this are not trusted
local TARGET_X = 320
local TRAVEL = 220        -- how far left of the target the ring starts

-- Click sounds to choose from (X on the sound-test intro steps through them).
local CLICKS = { "Select", "Countdown", "Back", "Error", "Move" }

local STATE_SOUND_INTRO, STATE_SOUND, STATE_PICTURE_INTRO, STATE_PICTURE, STATE_RESULT, STATE_FAILED = 1, 2, 3, 4, 5, 6

GUI = {
  finished = false,
  state = STATE_SOUND_INTRO,
  next_beat = 0,
  last_beat = 0,
  taps = {},
  tap_count = 0,
  sound_delay = 0,
  picture_delay = 0,
  option = nil,
  click = 1
}

local function PlayClick()
  local sound = UIEnums.SoundEffect[CLICKS[GUI.click]]
  if sound == nil then
    sound = UIEnums.SoundEffect.Select
  end
  Game.PlaySound(sound)
end

-- Counter text: -2, -1 while warming up, then 0 .. 12.
local function CounterText(counted)
  if counted < 0 then
    return UIText["BBRR_CAL_TAP_M" .. -counted] or UIText.IDS_NOWT
  end
  return UIText["BBRR_CAL_TAP_" .. counted] or UIText.IDS_NOWT
end

local function AddText(y, height, text, render_state)
  return Buttons.AddButton({
    type = UIEnums.ButtonTypes.TEXT,
    x = 320,
    y = y,
    width = SCREEN_WIDTH,
    height = height,
    justify = UIEnums.Justify.JUSTIFY_CENTRE,
    colour = "255 255 255 255",
    text = text,
    render_state = render_state,
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
end

local function ShowRings(visible)
  Buttons.SetActive(GUI.target_ID, visible)
  Buttons.SetActive(GUI.ring_ID, visible)
end

local function SetState(state)
  GUI.state = state
  if state == STATE_SOUND_INTRO then
    Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_SOUND_INTRO)
    Buttons.ChangeText(GUI.line2_ID, UIText.BBRR_CAL_BEGIN)
    Buttons.ChangeText(GUI.line3_ID, UIText.BBRR_CAL_CLICK_HINT)
    ShowRings(false)
  elseif state == STATE_PICTURE_INTRO then
    Buttons.ChangeText(GUI.line3_ID, UIText.IDS_NOWT)
    Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_PICTURE_INTRO)
    Buttons.ChangeText(GUI.line2_ID, UIText.BBRR_CAL_BEGIN)
    ShowRings(true)
    Buttons.ChangePosition(GUI.ring_ID, TARGET_X - TRAVEL, GUI.ring_y)
  elseif state == STATE_SOUND or state == STATE_PICTURE then
    if state == STATE_SOUND then
      Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_SOUND_RUN)
    else
      Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_PICTURE_RUN)
    end
    Buttons.ChangeText(GUI.line2_ID, CounterText(-WARM_UP))
    Buttons.ChangeText(GUI.line3_ID, UIText.IDS_NOWT)
    ShowRings(state == STATE_PICTURE)
    GUI.taps = {}
    GUI.tap_count = 0
    GUI.last_beat = Now()
    GUI.next_beat = GUI.last_beat + BEAT_MS
  elseif state == STATE_RESULT then
    Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_RESULT)
    Buttons.ChangeText(GUI.line2_ID, UIText["BBRR_C_audio_offset_" .. GUI.option] or UIText.IDS_NOWT)
    Buttons.ChangeText(GUI.line3_ID, UIText.IDS_NOWT)
    ShowRings(false)
  elseif state == STATE_FAILED then
    Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_FAILED)
    Buttons.ChangeText(GUI.line2_ID, UIText.BBRR_CAL_BEGIN)
    Buttons.ChangeText(GUI.line3_ID, UIText.BBRR_CAL_CLICK_HINT)
    ShowRings(false)
  end
end

function Init()
  SetupBackground()
  SetDefaultScreenTimers()
  SetupNextAndBack(true)
  SetupTitleText(UIText.BBRR_T_page_calibrate)
  GUI.ring_y = 270
  GUI.line1_ID = AddText(150, UIGlobals.font_size.small, UIText.BBRR_CAL_SOUND_INTRO, UIGlobals.font_render_state.new_filled_orange)
  GUI.line2_ID = AddText(190, UIGlobals.font_size.small, UIText.BBRR_CAL_BEGIN, UIGlobals.font_render_state.new_filled_blue)
  GUI.line3_ID = AddText(225, UIGlobals.font_size.very_small, UIText.BBRR_CAL_CLICK_HINT, UIGlobals.font_render_state.new_filled_blue)
  GUI.target_ID = AddText(GUI.ring_y, UIGlobals.font_size.large, UIText.BBRR_CAL_RING, UIGlobals.font_render_state.new_filled_blue)
  GUI.ring_ID = AddText(GUI.ring_y, UIGlobals.font_size.large, UIText.BBRR_CAL_RING, UIGlobals.font_render_state.new_filled_orange)
  -- The menu music would drown out the clicks; it comes back when the screen is left.
  Game.StopTitleMusic()
  SetState(STATE_SOUND_INTRO)
end

local function Leave()
  Game.StartTitleMusic(1)
  GUI.finished = true
  Screen.SetNextScreen("Options\\BBRR.lua")
end

-- Middle value and spread of the counted taps.
local function Summarise()
  local values = {}
  for i = WARM_UP + 1, GUI.tap_count do
    table.insert(values, GUI.taps[i])
  end
  table.sort(values)
  local n = table.getn(values)
  local median = (values[math.floor((n + 1) / 2)] + values[math.floor((n + 2) / 2)]) / 2
  -- Spread without the two most extreme taps on each side.
  local spread = values[n - 2] - values[3]
  return median, spread
end

local function FinishTest()
  local median, spread = Summarise()
  if spread > MAX_SPREAD_MS then
    SetState(STATE_FAILED)
    return
  end
  if GUI.state == STATE_SOUND then
    GUI.sound_delay = median
    SetState(STATE_PICTURE_INTRO)
  else
    GUI.picture_delay = median
    GUI.option = tonumber(BBRR("bbrr.calibrate", GUI.sound_delay, GUI.picture_delay))
    if GUI.option == nil then
      SetState(STATE_FAILED)
    else
      SetState(STATE_RESULT)
    end
  end
end

function Update()
  ContinueXBLSessionCreation()
  if GUI.finished == true then
    return
  end
  if GUI.state ~= STATE_SOUND and GUI.state ~= STATE_PICTURE then
    return
  end
  local now = Now()
  if now >= GUI.next_beat then
    if GUI.state == STATE_SOUND then
      PlayClick()
      -- The click is asked for now, not exactly on the beat: measure from this moment.
      GUI.last_beat = Now()
    else
      GUI.last_beat = GUI.next_beat
    end
    GUI.next_beat = GUI.next_beat + BEAT_MS
    if GUI.next_beat <= now then
      GUI.next_beat = now + BEAT_MS
    end
  end
  if GUI.state == STATE_PICTURE then
    -- The ring reaches the target exactly on the beat, then starts again from the left.
    local to_go = (GUI.next_beat - now) / BEAT_MS
    Buttons.ChangePosition(GUI.ring_ID, TARGET_X - TRAVEL * to_go, GUI.ring_y)
  end
end

local function Tap()
  local now = Now()
  -- Compare with the nearest beat: the one just gone or the one coming up.
  local late = now - GUI.last_beat
  local early = now - GUI.next_beat
  local offset = late
  if -early < late then
    offset = early  -- pressed a little ahead of the beat
  end
  GUI.tap_count = GUI.tap_count + 1
  GUI.taps[GUI.tap_count] = offset
  local counted = GUI.tap_count - WARM_UP
  Buttons.ChangeText(GUI.line2_ID, CounterText(counted))
  if counted >= TAPS then
    FinishTest()
  end
end

function Message(message, pad, data)
  if GUI.finished == true then
    return
  end
  if PadMessageAcceptable(pad) == false then
    return
  end
  if message == "BUTTON_NEXT" then
    if GUI.state == STATE_SOUND_INTRO or GUI.state == STATE_FAILED then
      SetState(STATE_SOUND)
    elseif GUI.state == STATE_PICTURE_INTRO then
      SetState(STATE_PICTURE)
    elseif GUI.state == STATE_SOUND or GUI.state == STATE_PICTURE then
      Tap()
    elseif GUI.state == STATE_RESULT then
      BBRR("bbrr.set", "audio_offset", GUI.option)
      BBRR("bbrr.save")
      PlayNextSound()
      Leave()
    end
  elseif message == "BUTTON_X" then
    -- Try the next click sound (only before the sound test starts).
    if GUI.state == STATE_SOUND_INTRO or GUI.state == STATE_FAILED then
      GUI.click = GUI.click + 1
      if GUI.click > table.getn(CLICKS) then
        GUI.click = 1
      end
      PlayClick()
    end
  elseif message == "BUTTON_BACK" then
    PlayBackSound()
    Leave()
  end
end

function Exit()
end
