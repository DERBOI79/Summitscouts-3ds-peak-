# Summit Scouts (3DS homebrew prototype)

First-person mountain-climbing survival for Old/New 3DS. Single player vertical slice; **not yet compiled on real 3DS tooling** (see Status).

## Build (produces summit_scouts.3dsx)
Option A, Docker (no toolchain install):
    docker run --rm -v "$PWD":/project -w /project devkitpro/devkitarm make
Option B: install devkitPro (devkitARM, libctru, citro3d, picasso, 3dstools), then `make`.

Copy `summit_scouts.3dsx` to `sdmc:/3ds/` and launch from the Homebrew Launcher.
To get a .cia, convert later with makerom/bannertool (needs a banner + icon).

## Controls
Circle Pad move | D-pad or touch-drag look | L / R = left / right hand grip
A jump / loot / camp | B place ice screw | X grapple | Y pack + crafting | START save + quit

Climbing: walk into a steep face and hold L or R. Hold ONE hand to climb (the other reaches),
hold BOTH to rest and slowly recover stamina. Each hand tires; swap hands before it slips.
Slick pale patches tire hands 3x faster. Release both hands = fall.

## Character customiser
Title screen -> SELECT. Up/Down pick a row. Colour rows: A types a hex code (e.g. FF8800 or #f80),
X randomises, or touch the R/G/B bars. Piece rows: Left/Right cycle outfits. L/R spin the preview. B saves.
Pieces: tops (T-shirt, oversized tee, hoodie, puffer), bottoms (shorts, baggy jeans, cargo, joggers),
shoes (hiking boots, chunky sneakers, high-tops), hats (none, beanie, bucket, cap), 3 eye styles.
Warmth gear you craft (windbreaker/jacket/parka) is worn OVER your outfit and changes its colour.
In game: SELECT switches 3rd/1st person.

## Co-op status
Skins are 30 bytes (skin_pack/skin_unpack in source/skin.c) so friends' looks can be sent over local wireless.
The wireless networking itself is NOT written yet.

## Logic test on a PC (no 3DS needed)
    sh test/run.sh 12345 4      # seed, max hours; a bot climbs and prints checkpoint times

## Tuning knobs
source/game.c top: CLIMB_SPEED, drain rates, GRAPPLE_GAIN. source/world.c: mountain size (radius, 3000 m), terrace step.

## Rockfall
On steep faces a boulder is announced **10 seconds** ahead (message + bottom-screen countdown + a flashing red lane on the slope). It then rolls down that lane at 14 m/s. Move ~4 m sideways and it misses; a hit costs 35 HP and knocks you off the wall. Tuning: `ROCK_WARN_SECS`, `ROCK_SPEED`, `ROCK_RADIUS` in `game.h`, and the 150-300 s spacing in `rock_update` (`game.c`). Test: `gcc -Isource test/rock_test.c source/*.c ...` (see test/rock_test.c).

## Stamina rules
Stamina only recovers (a) on flat ground (14/s) or (b) while **hooked in** (after placing an ice screw, or a hook/screw catching a fall) and hanging with **no hands** gripping (6/s). Holding with two hands on the wall stops the drain but does not recover. Climbing off your gear (moving with one hand) unclips you. Test: test/stam_test.c.

## Climbing gear (visible)
Placed ice screws / catching hooks are drawn on the wall with a gold D-ring; while clipped in, a red/cream rope runs from the anchor to the gold D-ring on the character's harness (slack while you climb, taut while you hang). See `build_gear` in render.c.

## Build on GitHub (no local setup)
Push this folder's contents (so `Makefile` is at the repo root) to a GitHub repo. The workflow in `.github/workflows/build.yml` builds it with devkitPro; download `summit_scouts-3dsx` from the run's Artifacts, unzip, copy `summit_scouts.3dsx` to `sdmc:/3ds/`.
