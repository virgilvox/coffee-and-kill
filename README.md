# COFFEE & KILL

Brew first. Break things. Swing wrenches.

COFFEE & KILL is a hackerspace brawler for two. It started as MOHEEB and LUIS, a pipe wrench, a big spanner, a sledge and a crowbar, in a workshop full of robot arms, cogs, an Arduino Mega the size of a wall and a robot eye that watches the fight. It now also has KEYHOLDER's people, weapons, specials and places:

- **Fighters:** MOHEEB and LUIS, plus Vee, Dex and Ruth, and the day's visitors: Vinyl Vic, Rennick, Dobbs, High Score, Bex, Copper Cal, Root and Brant Holloway.
- **Weapons:** the page's four, plus KEYHOLDER's five: the Arc Blade, the Ball Peen Maul, the Deburr Twins, the Spool Naginata and the Beam Staff.
- **EXTREME:** the page's four weapons keep the ground pound. KEYHOLDER's five bring their cinematic specials: Final Weld, Cold Forge Hammerfall, Thousand Burr Cut, First Layer Adhesion, and Full Power, No Air Assist.
- **Stages:** the workshop, then KEYHOLDER's places: the lab (HSL-108), Main Street, the back lot, and the five Battle Zone arenas. Those arenas are the Long Bench, Laser Corner, the Fridge, the Machine Shop and the Whole Lab, Unfolded.
- **Modes:** one player against the CPU, two players, and a TOURNEY of 4 or 8 entrants, each played by a person or the CPU. Online versus over the CLASP relay, and phones as controllers.

Every match is best of three rounds, with a dance break between rounds that charges the EXTREME meter.

This repository is the [wasmcart](https://github.com/wasmcart/wasmcart) port of the web version at [weird.link/coffeekill](https://weird.link/coffeekill), built for the HeatSync Labs game station (Batocera with the wasmcart core). The build makes two things:

- the cart, `web/coffee-and-kill.wasc`
- a one-file web version of the cart, `web/coffee-and-kill.html`

## The port

The web version is one 8.8 MB page: three.js r186, the Bellows audio engine, a CLASP client and the game. The cart redoes it in C on the same stack as the shop's other carts, and keeps the page's data:

- **Models and animation:** the page's own assets, pulled out by `tools/extract.mjs`. That covers the two character rigs, 33 Kenney props and the 63 baked animation clips per rig, with their exact quaternions. `src/anim.c` ports three.js's `AnimationMixer` and `AnimationAction` line for line: crossfades, LoopOnce, clampWhenFinished and the PropertyMixer blend. The game's timing depends on all four.
- **Rendering:** three.c draws the r186 programs.
  - The arena (`src/arena.c`) builds the page's room piece for piece.
  - Every canvas texture is repainted with `src/cv.cpp`, an HTML canvas over canvas_ity, patched to blend in sRGB space as browsers do.
  - The page's ShaderMaterial and additive effects go through `src/fx.c` after the scene, depth tested and blended on encoded colour as WebGL does.
  - Static meshes are merged per material (`src/batch.c`), so the 660 draws of the page become about 330.
- **The game:** `src/fighter.c` (the fighter, its moves and the CPU levels) and `src/game.c` (rounds, camera, combat, effects, online host and guest), ported from the page's code. Names in comments point at the page's minified identifiers.
- **The UI:** `src/ui.c` and `src/ui_screens.c` draw the HUD and the page's screens from the page's CSS, resolved at 1280 x 720. The page's film grain is left out.
- **The frame on the station:** the game renders at 1280 x 720 with 4x MSAA. On a host that asks for a bigger 16:9 frame, `src/present.c` scales the finished frame up to the host's size with a Catmull-Rom filter. The station asks for 1920x1080 and scales frames by whole multiples only. The scale is a shader draw rather than a `glBlitFramebuffer`, because the wasmcart libretro core takes a blit's source rect as the frame size.
- **The dance break:** `src/dance.c`, with the songs and charts of `src/songs.c`.
- **Sound:** the music is rendered from the page's own Bellows worklet and score code by `tools/audio_bake.mjs`. The SFX are rendered through Chrome's WebAudio from the page's own SA code, and `src/sfx.c` and `src/music.cpp` mix them live through the page's bus chain, duck and compressor.
- **Online:** `src/net.c` is a CLASP client over wasmcart's `wc_peer`, and `src/online.c` ports the lobby, challenges, matches and phone pads. The wire format is the page's, so a cart and a browser meet in the same rooms (see Online below).

## What came from KEYHOLDER

The sister game KEYHOLDER (`../keyholder`) grew from this port's code, so its modules come back in with few changes. They live in `src/kh/`, under their KEYHOLDER names, with `src/kh/ds.h` mapping its shared header onto `ck.h`:

| From KEYHOLDER | Here |
|---|---|
| `actor.c` (VRoid bodies: cel lit, outline hull, spring bones, blinking, palette swaps) | `src/kh/actor.c`, plus freeing, moving between scenes and an external mixer |
| `world.c`, `city.c`, `arena.c`, the facades, `src/gen/*` data | `src/kh/world.c`, `city.c`, `kharena.c` (renamed beside this game's `arena.c`) |
| `fx.c` (particles, streaks, beams, shock rings) | `src/kh/kfx.c` (renamed `kfx_` beside this game's `fx.c`) |
| `weapon.c` (the five weapon builders) | `src/kh/kweapon.c` |
| the specials and their camera (`fight.c` `cine_update`) | `src/special.c` |
| `tools/cel-patch.mjs`, `tools/regen.sh`, `tools/inline-html.mjs`, `web/vendor/host-bundle.js` | the same names here |

KEYHOLDER's PS2 look (an 800 x 450 render with bloom) is not used: everything renders clean at the game's own size.

**Animation.** KEYHOLDER's bodies play `assets/anim/rig_vroid.bin`, which `tools/rig-vroid.mjs` builds from two sources:

- KEYHOLDER's own clips, from its `assets/anim/rig.bin`.
- Every page clip that KEYHOLDER lacks: the light and heavy chains, the kick, the ground pound, the dodges, the strafes and the celebration. These are retargeted from `rig_a.bin`.

Both rigs carry the same Quaternius motion, so the clips they share fit the retarget: one constant offset per bone, checked at 0.26 degrees mean error against KEYHOLDER's own bake. So every fighter has the page's move set, with the page's timings and hit windows. Version 2 of the rig format (`anim.h`) stores the hips as offsets in hip heights.

## The code

| Module | What it is |
|---|---|
| `roster.c` | the fighters, one table entry each |
| `body.c` | a fighter's body behind one interface: the page's rigs or a KEYHOLDER actor; loaded when picked, freed when not |
| `weapons.c` | the rack, one table entry each, built in a grip frame every body provides |
| `special.c` | EXTREME: KEYHOLDER's cinematics (timeline, blows, effects, camera), and the guest's copy online |
| `stage.c` | the stages, one table entry each: the workshop backend and KEYHOLDER's (its world under one group, turned and shifted so the ring's centre is the origin), each with its own scene, look, ring and camera rules |
| `tourney.c` | the bracket as data |
| `ui_select.c` | the fighter select: the portrait grid, a cursor, a panel and steps for each side, colours, RANDOM; the grid also opens as a modal for the tourney and online screens |
| `ui_menus.c` | the stage select, tourney setup, bracket and champion screens (`ui_kit.h` is what they share with `ui_screens.c`) |
| `ui_hint.c` | prompts that name the buttons of the device in use: pad glyphs, key boxes, the mouse and the phone pad |
| `fighter.c`, `game.c` | the page's fight and flow, now driven by the tables |

### Adding things

- **A fighter:** an entry in `ROSTER` (`src/roster.c`). A page body names its glb, rig, scale and grip axis. A KEYHOLDER body names its body letter (`src/kh/actor.h`). Each fighter has three alternate colours (`alts`): `KH_ALTS` turns coloured clothes round the wheel, `KH_DYES` dyes black, grey and white ones; a page body takes tone tints. A `CK_SHOWROOM` build shows a fighter in all four. If the body is new to this game, run `node tools/import-kh.mjs`, which copies every body the roster names. Then bake its portrait (Building and testing).
- **A weapon:** an entry in `WEAPONS` (`src/weapons.c`), with a builder. It gets a grip in every hand, a trail, a striking segment and an EXTREME.
- **A stage:** an entry in `STAGES` (`src/stage.c`) and its short name in the stage select's strip (`SHORT`, `src/ui_menus.c`). Any KEYHOLDER arena (`world_stage`) or named town place (`world_place`) works as it is. The ring, the yaw of the fighters' line and the camera rules (`cam_lift`, `cam_max`, `cam_clear`, `cam_side`) fit it to a duel. A new kind of place needs a backend in `stage.c`. Then bake its still.

Keys in these tables go over the wire and into saves: do not rename them.

## Modes

- **1 PLAYER, 2 PLAYERS:** the fighter select is a grid of the 13 fighters' portraits and RANDOM, with the two picks standing large at the sides. Each side has its own cursor, tab and panel, and picks in steps: the fighter, the weapon (the fighter's own to start), then READY; in one player mode P1 then picks the CPU's fighter, weapon and level. B steps back, holding B (or Escape) leaves, Y swaps the sides, LB and RB change the colour, Start takes what each side shows. A mirror match puts the later picker in the next colour. A body loads only once the cursor rests on a fighter for a quarter second, or on a confirm. Then the stage select: a strip of every stage's still and RANDOM. A KEYHOLDER stage that is not loaded yet shows its still; its world loads the first time one of its stages is picked, behind a card with both fighters, and stays loaded after that.
- **TOURNEY:** 4 or 8 entrants. Each one has a fighter, a weapon, and someone to play it: PLAYER, or the CPU at one of three levels.
  - Pick one stage for every match, or a new one each match.
  - It is single elimination, best of three, with sudden death on a tied match.
  - Between matches, the bracket shows results and the next match. CPU-only matches can be watched or skipped. After the final, the champion celebrates.
  - On the setup, A on an entrant opens the fighter grid, left and right change its weapon, Y who plays.
  - People take turns on the controls: two people split them (pad 1 or WASD on the left, pad 2 or the arrows on the right), and one person against the CPU plays on any pad. The bracket and the round intro say who takes which.
  - QUIT on the bracket asks again; the pause card in a tourney match goes back to the bracket.
- **Online:** the lobby is the page's. Carts add an `x` map (wire version 2: fighter, weapon, stage) to presence, challenges and answers, and the web page ignores it.
  - Two carts fight with their own picks on the challenger's stage. Cinematic specials ride in the snapshot (`sp`).
  - Against the web page (no `x`), it is MOHEEB against LUIS in the workshop with the page's four weapons. The page's snapshots and events never see anything it does not know.

## Controls

The page's keyboard controls are unchanged:

| | One player | Two players |
|---|---|---|
| Move | WASD or arrows | P1 WASD, P2 arrows |
| Light | Q / J / Z | P1 Q, P2 , |
| Heavy | E / K / X | P1 E, P2 . |
| Kick | R / L / C | P1 R, P2 / |
| Block (hold) | Space / U / V | P1 Space, P2 right Shift |
| Dodge | Shift / I | P1 left Shift, P2 Enter |
| EXTREME | F / O / B | P1 F, P2 ' |

Gamepads follow the page's standard mapping: X light, Y heavy, B kick, A dodge, either bumper or LT to block, RT for EXTREME. In one player both pads drive P1; in two players pad 1 is P1 and pad 2 is P2. Escape or P pauses, M mutes, N toggles the music, Escape skips the dance break.

Menus work with pads, the keyboard, the mouse and phone pads, and their prompts name the buttons of the device last used. On a pad: the stick or d-pad moves, A picks, B goes back, Y swaps sides (or changes who plays on the tourney setup), LB and RB change a fighter's colour, Start begins (and pauses a fight). On the keyboard: the arrows, Enter, Escape or Backspace, Tab, E. In two players the fighter select splits the keyboard: P1 WASD with Q to pick, R back and E colour; P2 the arrows with comma, slash and period. The mouse hovers and clicks, and a right click goes back. Every pad is read whether or not it reports connected, since the station's core flags only port 0.

## The web version

`web/index.src.html` is the shell. `build.sh` inlines it with the wasmcart web host and the cart into `web/coffee-and-kill.html`, one file of about 50 MB:

- a start card in the game's style, over a frame of the game (`web/backdrop.jpg`)
- a controls table, fullscreen and sound
- the mouse as the cart's pointer: a press captures it to the canvas, and a click shorter than a frame is held until the cart has seen it
- saves in IndexedDB

Serve the `web/` folder over HTTP to test it; `?autostart` skips the start card.

## Building and testing

```
./build.sh                          # build/coffee-and-kill.wasc, copied to web/, and web/coffee-and-kill.html
tools/regen.sh <run log> [OUT]      # add the shader states a run printed to the table (with the cel patch), rebuild
node tools/rig-vroid.mjs            # rebake assets/anim/rig_vroid.bin from ../keyholder and rig_a.bin
node tools/import-kh.mjs            # copy KEYHOLDER's bodies (those the roster names), lab, arenas, signs and tables
node tools/shot.mjs build/coffee-and-kill.wasc out/shot 120,600 [script.json]
PREF=1920x1080 TIMING=1 node tools/shot.mjs ...     # the station's frame size, with timings and wasm memory
OUT=build-portraits EXTRA_CFLAGS="-DCK_PORTRAITS -DCK_NOUI" ./build.sh && node tools/bake-portraits.mjs build-portraits/coffee-and-kill.wasc
OUT=build-stills EXTRA_CFLAGS="-DCK_STILLS -DCK_NOUI" ./build.sh && node tools/bake-stills.mjs build-stills/coffee-and-kill.wasc   # needs ffmpeg
```

The portraits (`assets/portraits/<key>.png`, 140 px with alpha, matted from a black and a white render) and the stills (`assets/stills/<key>.jpg`, 640 x 360) are committed; bake them again after changing a body or a stage. A shot script can also move the mouse: `{ "from": 20, "to": 24, "ptr": [640, 360], "mb": 1 }`.

The build needs emsdk and node 22. `third_party/three.c` is a copy of the three.c the shop's other carts use, with two changes: up to 16 lights per type (the arena has 11 point lights), and state names that write light counts as hex digits. `third_party/wasmcart` and `third_party/bellows` are shared with red-tag-rumble.

Test builds take `EXTRA_CFLAGS` and `OUT=build-x`:

| Flag | What it does |
|---|---|
| `-DCK_AUTOSTART=1` (or `=2`) | straight into a one (two) player fight |
| `-DCK_PICK0="vee" -DCK_WEAPON0="arc" -DCK_PICK1=... -DCK_WEAPON1=... -DCK_STAGE=4` | the picks |
| `-DCK_TEST_METER` | full EXTREME meters |
| `-DCK_TEST_ROUND=6` | six second rounds and no dance |
| `-DCK_NOUI` | clean frames |
| `-DCK_TEST_TOP` | looking straight down on the ring |
| `-DCK_ONLINE_TEST="ROOM"` | join a room and accept challenges |
| `-DCK_ONLINE_CHALLENGE` | challenge the first player in the room |
| `-DCK_PORTRAITS -DCK_NOUI` | each fighter alone, framed for its portrait (tools/bake-portraits.mjs) |
| `-DCK_STILLS -DCK_NOUI` | each stage under the stage select's camera (tools/bake-stills.mjs) |
| `-DCK_SHOWROOM -DCK_NOUI` | each fighter in its four colours side by side, eight frames apiece |

A run that prints state names such as `lambert+map+skin+fog+atest+ds+l1001` has met programs missing from the table: give its log to `tools/regen.sh`.

## Measured (headless, webgl-node on the dev Mac, 1920x1080 frames)

| Scene | Frame, average | Wasm memory |
|---|---|---|
| The workshop, MOHEEB against LUIS | 8.7 ms | 273 MB |
| The workshop, two KEYHOLDER bodies | 6.4 ms | 273 MB |
| The workshop, a KEYHOLDER fighter, a full EXTREME meter, specials | 7.2 ms | 273 MB |
| The fighter select, portraits and two bodies | 8.7 ms | 273 MB |
| The lab, the town and the arenas (KEYHOLDER's world loaded) | 1.4 to 10 ms | 382 MB |
| Five matches on Main Street and the arenas, with new picks and colours between them | 1.4 to 8.5 ms | 395 MB, flat |

The web version in Chrome on the same Mac holds 60 fps on the fighter select and on Main Street at a 2560x1440 canvas.

Loading KEYHOLDER's world takes one long frame, behind the loading card: a few seconds headless. A body loads in one frame of 130 to 225 ms on the Mac, so the select loads one only when the cursor rests or a pick is confirmed; passing over fighters costs nothing. Picking fighters over and over does not grow memory.

## Credits and licences

See `CREDITS`.
