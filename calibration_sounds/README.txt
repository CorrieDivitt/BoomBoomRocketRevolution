Calibration metronome sounds (BBRR Settings > Calibration)

Put two WAV files in this folder to replace the built-in clicks:

  beat.wav      the accent: first beat of every bar
  subbeat.wav   beats 2, 3 and 4 of every bar, and the four count-in beats

Format: ordinary WAV - 16-bit, 24-bit or 32-bit PCM, or 32-bit float; any sample rate
(48000 Hz is used as-is, others are converted); mono or stereo (stereo is mixed to mono).
Keep them short and sharp: anything longer than a quarter of a second is cut off, and
the sound should start right at the beginning of the file with no silence before it.

If a file is missing or cannot be read, the built-in click is used for that one.
The files are read when the first test of a session starts.
