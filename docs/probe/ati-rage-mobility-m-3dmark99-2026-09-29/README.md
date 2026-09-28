# Rage Mobility-M: first 3DMark 99 Max run on hardware Direct3D

Gateway Solo 2150, Velocity9x bound, HAL from `e0b04db` (textures 8 to 256,
mipmaps with level selection), boot 30. 3DMark 99 Max, default test
selection, 640x480x16, 16-bit Z, triple buffering (`SETTINGS-640X480.png`).
Started and driven through the remote agent.

## Result

The run completed with all 26 tests, and without a lock.

- Score: 457 3DMarks, 6542 CPU 3DMarks (`SCORE-457.png`). Recorded, not
  compared with anything.
- HAL counters across the run (`V9XSNA-BEFORE.INI`, `V9XSNA-AFTER.INI`):
  - 162,902 batches reached the engine, carrying 5,871,389 triangles
    (41,349 of them zero-area);
  - 147,124 batches were textured, 152,800 depth-tested and 31,633
    blended;
  - 11,479 draws were refused: about 7% of those submitted. The last
    refusal was a vertex refusal (engine reason 6), and the last policy
    refusal was fog with texture (policy reason 16);
  - 36,160 textures or chains were placed by the HAL;
  - zero FIFO timeouts, zero idle timeouts and zero resets.

## Screenshots

The agent captures through GDI. During page flipping that can be a buffer
other than the one on the panel, and three of the eleven captures were
black. What is black on the panel is not established.

- `ROCK-TEXTURE.png`: a scene, probably the bump-mapping test, textured and
  filtered correctly.
- `TEXTURE-TUNNEL-WRONG.png`: the texture rendering speed tunnel. Walls are
  flat averaged colours with radial streaks, not texture detail. This fits
  a very small mip level being sampled, or texture coordinates wrong under
  perspective. Not investigated.
- `LOADING-GAME2.png`: a between-test loading screen.

## Open

- The texture tunnel's wrong texturing.
- The two game tests finished within about 25 seconds each. Whether
  refused or wrongly drawn batches shortened them is not established.
- Which draws the 11,479 refusals were.
