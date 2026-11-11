-- Boom Boom Rocket Revolution: clearer wording for some of the game's own text.
-- "Restart Game" / "Replay Game" restart the song, so they say so. The screens that
-- show them load this file before the game's own script.
if UIText ~= nil and UIText.BBRR_RESTART_SONG ~= nil then
  pcall(function()
    UIText.IDS_RESTART = UIText.BBRR_RESTART_SONG
    UIText.IDS_REPLAY = UIText.BBRR_REPLAY_SONG
    UIText.IDS_RESTART_GAME_INFO = UIText.BBRR_RESTART_SONG_INFO
  end)
end
