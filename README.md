# COFFEE & KILL

Brew first. Break things. Swing wrenches.

COFFEE & KILL is a two player hackerspace brawler: MOHEEB and LUIS, a pipe wrench, a big spanner, a sledge and a crowbar, in a workshop full of robot arms, cogs, an Arduino Mega the size of a wall and a robot eye that watches the fight. Best of three rounds, a dance break between rounds that charges the EXTREME meter, online versus over the CLASP relay and phones as controllers.

This repository is the [wasmcart](https://github.com/wasmcart/wasmcart) port of the web version at [weird.link/coffeekill](https://weird.link/coffeekill), built for the HeatSync Labs game station (Batocera with the wasmcart core). The cart is `web/coffee-and-kill.wasc`.

## The port

The web version is one 8.8 MB page: three.js r186, the Bellows audio engine, a CLASP client and the game. The cart redoes it in C on the same stack as the shop's other carts and keeps the page's data:

- **Models and animation:** the page's own assets, pulled out by `tools/extract.mjs`. That covers the two character rigs, 33 Kenney props and the 63 baked animation clips per rig, with their exact quaternions. `src/anim.c` ports three.js's `AnimationMixer` and `AnimationAction` line for line: crossfades, LoopOnce, clampWhenFinished and the PropertyMixer blend. The game's timing depends on all four.
- **Rendering:** three.c draws the r186 programs.
  - The arena (`src/arena.c`) builds the page's room piece for piece.
  - Every canvas texture is repainted with `src/cv.cpp`, an HTML canvas over canvas_ity, patched to blend in sRGB space as browsers do.
  - The page's ShaderMaterial and additive effects go through `src/fx.c` after the scene, depth tested and blended on encoded colour as WebGL does.
  - Static meshes are merged per material (`src/batch.c`), so the 660 draws of the page become about 330.
- **The game:** `src/fighter.c` (the fighter, moves, weapons and CPU levels) and `src/game.c` (rounds, camera, combat, effects, online host and guest), ported from the page's code. Names in comments point at the page's minified identifiers.
- **The UI:** `src/ui.c` and `src/ui_screens.c` draw the HUD and every screen from the page's CSS, resolved at 1280 x 720.
- **The frame on the station:** On a host that asks for a bigger 16:9 frame the game still renders at 1280 x 720, and `src/present.c` scales the finished frame up to the host's size with a Catmull-Rom filter. The station asks for 1920x1080 and scales frames by whole multiples only, so before this a 1280x720 frame sat in a box at 1x. The scale is a shader draw rather than a `glBlitFramebuffer`, because the wasmcart libretro core takes a blit's source rect as the frame size. `PREF=1920x1080 node tools/shot.mjs ...` shoots the cart at the station's size.
- **The dance break:** `src/dance.c`, with the songs and charts of `src/songs.c`.
- **Sound:**
  - The music is rendered from the page's own Bellows worklet and score code by `tools/audio_bake.mjs`, checked note for note against the live page.
  - The SFX are rendered through Chrome's WebAudio from the page's own SA code. `src/sfx.c` and `src/music.cpp` mix them live through the page's bus chain, duck and compressor.
- **Online:** `src/net.c` is a CLASP client over wasmcart's `wc_peer`, and `src/online.c` ports the lobby, challenges, matches and phone pads. The wire format is the page's, so a cart and a browser meet in the same rooms and fight each other.

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

The page leaves its menus to the keyboard and mouse. A cabinet has neither, so the cart maps the pads onto the same keys in the menus: the stick or d-pad is the arrows, A is Enter, B is Escape, Y is Tab (swap sides on the select screen), and Start is Enter on a menu and pause in a fight.

## Building

```
./build.sh                      # build/coffee-and-kill.wasc, copied to web/
REGEN_STATES=1 ./build.sh       # regenerate the program table (src/gen) from tools/states.txt
node tools/shot.mjs build/coffee-and-kill.wasc out/shot 120,600 [script.json]
```

Needs emsdk and node 22. `third_party/three.c` is a copy of the three.c the shop's other carts use, with two changes: up to 16 lights per type, as the arena has 11 point lights, and state names that write light counts as hex digits. `third_party/wasmcart` and `third_party/bellows` are shared with red-tag-rumble.

## Credits and licences

- Animations: Quaternius Universal Animation Library 1 and 2 (CC0). Props: Kenney Survival and Factory kits (CC0). As credited by the web version.
- Engine: three.js (MIT) by way of three.c; canvas_ity (ISC); dr_flac (MIT-0).
- Fonts: Archivo, IBM Plex Mono and JetBrains Mono, under the SIL Open Font License.
