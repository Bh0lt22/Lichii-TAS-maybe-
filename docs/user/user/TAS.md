# TAS (Tool-Assisted Speedrun) Mode

Lichii-TAS extends the inherited yuzu TAS subsystem with frame-accurate
recording, frame advance, a TAS control panel and a replay-based re-record
("seek") workflow. Since this emulator family has no save states, movies are
anchored to a **clean game boot**: going back to an earlier frame is
implemented as *restart + fast replay*, which gives the same editing workflow
without save states.

## Setup

1. `Tools` (or `Emulation`) → `TAS` → `Configure TAS`:
   - enable **Enable TAS functions**,
   - pick the TAS directory (default: `tas/` in your Lichii/Eden user folder).
2. Configure your real controller for Player 1 as usual
   (`Options → Controls`). You do **not** need to select the "tas" input
   device: TAS input is injected on dedicated per-controller channels, so your
   normal mapping keeps working both while recording and while playing back.
3. Recommended for deterministic movies:
   - play offline (e.g. Mario Kart World Time Trials);
   - disable motion/gyro controls in the *game* settings — motion is not
     recorded by the movie;
   - let the shader cache warm up with one normal boot before recording.

## Movie files

Movies are plain-text scripts (TAS-nx format) in the TAS directory:

```
0 KEY_A 0;0 0;0
6 KEY_ZL;KEY_A 32767;0 0;0
7 KEY_A -16384;8192 0;0
```

- One line per frame: `frame buttons leftstick rightstick`
- Buttons separated by `;`, stick values as `x;y` in the range
  `-32767..32767`. Full analog precision is recorded every frame, so quick
  stick flicks (e.g. trick inputs) are captured exactly.
- Player 1 script: `script0-1.txt`. Recordings are saved as `record.txt` and
  can be copied to `script0-1.txt` for playback (the recorder asks you).
- You can edit the file with any text editor, then use
  *TAS → Reset* (Ctrl+F6) to reload it.

## Menu & hotkeys (defaults)

| Action | Hotkey |
| --- | --- |
| TAS Start/Stop (playback) | Ctrl+F5 |
| TAS Record start/stop | Ctrl+F7 |
| TAS Reset (reload scripts) | Ctrl+F6 |
| **TAS Frame Advance** | **Ctrl+F8** |
| **TAS Seek & Record** (re-record) | **Ctrl+F11** |
| **TAS Toggle Panel** | **Ctrl+F10** |
| Continue/Pause emulation | F4 |
| Toggle Slow Speed (preset) | Ctrl+X |
| Toggle Turbo Speed | Ctrl+Z |

All hotkeys can be remapped in `Options → Hotkeys`.

## The TAS Panel

`TAS → TAS Panel` opens a floating panel showing, live and per frame:

- the current state (RECORDING / PLAYING / IDLE) and the frame counter,
- the inputs registered on the current frame: 18 buttons + both sticks,
- the emulation speed mode and a slow-motion preset selector (5/10/25/50/100%),
- quick buttons for every TAS command (record, playback, frame advance,
  seek & re-record, pause/resume).

Because recording is tied to emulated frames (not wall-clock time), running
the game at 10% speed while recording gives you more real time to press the
right inputs without affecting the movie.

## Workflows

### Record a new movie (anchored at boot)

1. Boot the game, then `TAS → Restart and Record`.
   The game restarts and recording starts automatically at frame 0, so the
   movie also contains the menu navigation. This is what makes replay and
   re-record deterministic.
2. Play (optionally at reduced speed, Ctrl+X or the panel presets).
3. Stop with `TAS → Record` (Ctrl+F7). Answer **Yes** to also write the
   movie to `script0-1.txt`.

### Play back a movie

- `TAS → Restart and Play`: restarts the game and plays `script0-1.txt`
  from frame 0 at console speed (this is the "run the recorded movie at
  60 fps" step).
- `TAS → Start` (Ctrl+F5) toggles playback from the current position of an
  already running game.

### Frame advance (precision editing)

- Pause the emulation (F4), then press **Ctrl+F8** repeatedly: the emulator
  advances exactly one frame per press and pauses again.
- The TAS Panel shows the inputs registered on the current frame.

### Go back and fix a mistake (seek & re-record)

This is the replay-based replacement for save-state rewinding:

1. Stop the emulation or keep it running (either works).
2. Edit `script0-1.txt` in a text editor if you want to change recorded
   inputs, then `TAS → Reset` (Ctrl+F6) so the edit is reloaded.
3. `TAS → Resume Recording at Frame...` (Ctrl+F11) and enter N.
4. Lichii restarts the game, replays frames `0..N-1` automatically at
   800% speed, then pauses at frame N and switches to recording, seeded with
   the replayed prefix.
5. Continue playing/ recording from there; stop with Ctrl+F7 to save.

No per-frame save files are needed: the movie itself is the state.

## Notes & limitations

- Determinism: playback reproduces a recording best when the emulator build,
  settings and shader cache are the same as during recording, and the movie
  is replayed from a clean boot (which Restart and Play / Seek always do).
- Motion controls (gyro) are not recorded: disable them in-game.
- Touch input is not recorded in this version.
- Multi-player movies (`script0-2.txt`, ...) are inherited from upstream and
  still play back, but recording captures Player 1 only.
- `Frame Advance` also works with TAS functions disabled (it is a general
  emulator feature).
