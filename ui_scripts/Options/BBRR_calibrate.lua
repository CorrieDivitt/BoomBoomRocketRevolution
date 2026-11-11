-- Boom Boom Rocket Revolution: calibration screen.
--
-- Two tap tests, the way band games do it:
--   1. Sound:   a metronome plays in 4/4 (accent on the first beat of each bar); the
--               player presses A on every beat.
--   2. Picture: no sound; bars rise up the screen like the game's rockets and the player
--               presses A each time one reaches the line.
-- Both start with a four-beat count-in (shown as -3 .. 0) and then count four bars,
-- sixteen beats, 1 to 16. The player presses through the count-in too; only 1 to 16 count.
-- Each test gives "how late the taps were" (sound or picture delay plus the player's
-- input delay). Their difference is how far sound and picture are apart, which is what
-- the Audio Offset setting corrects.
--
-- The metronome runs on the PC side, mixed into the game's own sound output, so its
-- clicks take exactly the same path to the speakers as the music does:
--   BBRR("bbrr.metro.start", beat_ms, "audible,count_in,beats_per_bar,lead")
--                                               audible = 1 (sound test) or 0 (silent);
--                                               lead = beats of silence before the count-in
--   BBRR("bbrr.metro.pos")                      -> position in beats (0 = first count-in beat)
--   BBRR("bbrr.metro.stop")
--   BBRR("bbrr.calibrate", sound, picture)      -> Audio Offset option number for those delays
--   BBRR("bbrr.set", "audio_offset", n), BBRR("bbrr.save")

local function BBRR(command, key, value)
  local ok, result = pcall(gcinfo, command, key, value)
  if ok then
    return result
  end
  return nil
end

local BEAT_MS = 60000 / 115  -- 115 beats per minute
local COUNT_IN = 4        -- beats shown as -3 .. 0 before the counted ones
local BAR = 4             -- beats per bar: the first one of each bar is accented
local TAPS = 16           -- beats that count (shown as 1 .. 16): four bars
local MIN_TAPS = 10       -- fewer taps than this is not enough to go on
local MAX_SPREAD_MS = 70  -- taps more uneven than this are not trusted
local LINE_Y = 170        -- the line the bars rise to (like the game's target line)
local TRAVEL = 340        -- how far below the line a bar starts (below the bottom of the screen)
local LEAD_BEATS = 2      -- a bar is on its way for this many beats, so two are in view
local HIDDEN_Y = 900      -- where a bar waits when it has no beat to show
local BAR_WIDTH, BAR_HEIGHT = 56, 10

local STATE_SOUND_INTRO, STATE_SOUND, STATE_PICTURE_INTRO, STATE_PICTURE, STATE_RESULT, STATE_FAILED = 1, 2, 3, 4, 5, 6

GUI = {
  finished = false,
  state = STATE_SOUND_INTRO,
  taps = {},
  shown = nil,
  sound_delay = 0,
  picture_delay = 0,
  option = nil
}

local function Position()
  return tonumber(BBRR("bbrr.metro.pos")) or -100
end

-- Counter text for a beat index: -3 .. 0 during the count-in, then 1 .. 16.
local function CounterText(beat)
  local number = beat - (COUNT_IN - 1)
  if beat < 0 then
    return UIText.BBRR_CAL_READY
  elseif number < 0 then
    return UIText["BBRR_CAL_TAP_M" .. -number] or UIText.IDS_NOWT
  elseif number > TAPS then
    number = TAPS
  end
  return UIText["BBRR_CAL_TAP_" .. number] or UIText.IDS_NOWT
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

-- A plain coloured rectangle (x, y = top left corner; colour is "alpha red green blue").
local function AddBox(x, y, width, height, colour)
  return Buttons.AddButton({
    type = UIEnums.ButtonTypes.GRAPHIC,
    x = x,
    y = y,
    width = width,
    height = height,
    colour = colour,
    textures = {
      { name = UIEnums.Texture.null, u = 0, v = 0, usize = 1, vsize = 1 }
    },
    effects = {
      {
        when = UIEnums.EffectWhen.End,
        type = UIEnums.EffectType.Colour,
        from = colour,
        to = "0 0 0 0",
        duration = 0.1,
        timer = 0
      }
    }
  })
end

-- Where the two rising bars are for a metronome position: bar 1 carries the even beats,
-- bar 2 the odd ones; each reaches the line exactly on its beat.
local function PlaceBars(position)
  for m = 1, 2 do
    local beat = math.ceil(position)
    if beat - math.floor(beat / 2) * 2 ~= m - 1 then
      beat = beat + 1
    end
    local y = HIDDEN_Y
    -- Only real beats get a bar: the first one is the first count-in beat, rising from
    -- below the screen; nothing comes after the last counted beat.
    if beat >= 0 and beat < COUNT_IN + TAPS then
      y = LINE_Y - BAR_HEIGHT / 2 + TRAVEL * (beat - position) / LEAD_BEATS
    end
    Buttons.ChangePosition(GUI.bar_ID[m], 320 - BAR_WIDTH / 2, y)
  end
end

local function ShowBars(visible)
  Buttons.SetActive(GUI.line_ID, visible)
  Buttons.SetActive(GUI.bar_ID[1], visible)
  Buttons.SetActive(GUI.bar_ID[2], visible)
end

local function SetState(state)
  GUI.state = state
  if state == STATE_SOUND_INTRO then
    Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_SOUND_INTRO)
    Buttons.ChangeText(GUI.line2_ID, UIText.BBRR_CAL_BEGIN)
    ShowBars(false)
  elseif state == STATE_PICTURE_INTRO then
    Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_PICTURE_INTRO)
    Buttons.ChangeText(GUI.line2_ID, UIText.BBRR_CAL_BEGIN)
    ShowBars(true)
    PlaceBars(-100)
  elseif state == STATE_SOUND or state == STATE_PICTURE then
    if state == STATE_SOUND then
      Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_SOUND_RUN)
      BBRR("bbrr.metro.start", BEAT_MS, "1," .. COUNT_IN .. "," .. BAR .. "," .. LEAD_BEATS)
    else
      Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_PICTURE_RUN)
      BBRR("bbrr.metro.start", BEAT_MS, "0," .. COUNT_IN .. "," .. BAR .. "," .. LEAD_BEATS)
    end
    Buttons.ChangeText(GUI.line2_ID, UIText.BBRR_CAL_READY)
    ShowBars(state == STATE_PICTURE)
    GUI.taps = {}
    GUI.shown = nil
  elseif state == STATE_RESULT then
    Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_RESULT)
    Buttons.ChangeText(GUI.line2_ID, UIText["BBRR_C_audio_offset_" .. GUI.option] or UIText.IDS_NOWT)
    ShowBars(false)
  elseif state == STATE_FAILED then
    Buttons.ChangeText(GUI.line1_ID, UIText.BBRR_CAL_FAILED)
    Buttons.ChangeText(GUI.line2_ID, UIText.BBRR_CAL_BEGIN)
    ShowBars(false)
  end
end

function Init()
  SetupBackground()
  SetDefaultScreenTimers()
  SetupNextAndBack(true)
  SetupTitleText(UIText.BBRR_T_page_calibrate)
  GUI.line1_ID = AddText(100, UIGlobals.font_size.small, UIText.BBRR_CAL_SOUND_INTRO, UIGlobals.font_render_state.new_filled_orange)
  GUI.line2_ID = AddText(132, UIGlobals.font_size.small, UIText.BBRR_CAL_BEGIN, UIGlobals.font_render_state.new_filled_blue)
  GUI.line_ID = AddBox(170, LINE_Y - 2, 300, 4, "255 255 255 255")
  GUI.bar_ID = {
    AddBox(320 - BAR_WIDTH / 2, HIDDEN_Y, BAR_WIDTH, BAR_HEIGHT, "255 254 52 255"),
    AddBox(320 - BAR_WIDTH / 2, HIDDEN_Y, BAR_WIDTH, BAR_HEIGHT, "255 254 52 255")
  }
  -- The menu music would drown out the clicks; it comes back when the screen is left.
  Game.StopTitleMusic()
  SetState(STATE_SOUND_INTRO)
end

local function Leave()
  BBRR("bbrr.metro.stop")
  Game.StartTitleMusic(1)
  GUI.finished = true
  Screen.SetNextScreen("Options\\BBRR.lua")
end

-- Middle value and spread of the taps, or nil when there are too few.
local function Summarise()
  local values = {}
  for beat = COUNT_IN, COUNT_IN + TAPS - 1 do
    if GUI.taps[beat] ~= nil then
      table.insert(values, GUI.taps[beat])
    end
  end
  local n = table.getn(values)
  if n < MIN_TAPS then
    return nil
  end
  table.sort(values)
  local median = (values[math.floor((n + 1) / 2)] + values[math.floor((n + 2) / 2)]) / 2
  -- Spread without the two most extreme taps on each side.
  local spread = values[n - 2] - values[3]
  return median, spread
end

local function FinishTest()
  BBRR("bbrr.metro.stop")
  local median, spread = Summarise()
  if median == nil or spread > MAX_SPREAD_MS then
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
  local position = Position()
  local beat = math.floor(position)
  if beat ~= GUI.shown then
    GUI.shown = beat
    Buttons.ChangeText(GUI.line2_ID, CounterText(beat))
  end
  if GUI.state == STATE_PICTURE then
    PlaceBars(position)
  end
  -- Half a beat after the last counted beat the test is over.
  if position > COUNT_IN + TAPS - 0.5 then
    FinishTest()
  end
end

local function Tap()
  local position = Position()
  local beat = math.floor(position + 0.5)  -- the beat this press is closest to
  if beat >= COUNT_IN and beat < COUNT_IN + TAPS and GUI.taps[beat] == nil then
    GUI.taps[beat] = (position - beat) * BEAT_MS
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
  elseif message == "BUTTON_BACK" then
    PlayBackSound()
    Leave()
  end
end

function Exit()
end
