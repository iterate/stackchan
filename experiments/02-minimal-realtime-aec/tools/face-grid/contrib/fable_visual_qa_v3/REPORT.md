# Renderer-by-renderer visual audit — fable_visual_qa_v3

Date: 2026-07-29. Tree state: uncommitted working tree; note that parallel
workers were editing renderer sources during the audit window (the v41 gate
run at ~03:46 reports mouth-family slugs `preston-sprites`, while the
acceptance run built at ~04:0x reports `preston-sprites-redux`). All
detailed findings below were verified against the newest artifacts,
`local/fable_visual_qa_v3/acceptance-v1/`.

## Evidence base

| Artifact | Path (under `local/fable_visual_qa_v3/`) |
|---|---|
| Fresh production gate run (62 profiles, quality_pass=true, 0 abrupt jumps) | `native-quality-v41/` |
| Enhanced acceptance sheets + advisory metrics (this tool) | `acceptance-v1/` (`index.html`, `acceptance.json`, `sheets-png/`) |
| Atlas triage bands (all 62 rows reviewed) | `triage/band-0*.png` |
| Live web-matrix check + screenshots | `web/matrix-*.png` |

Manual review performed: all 8 atlas bands (every profile × 11 emotions),
16 enhanced sheets in detail, plus the live browser matrix
(`ready=true`, ABI 2, 62 profiles, 66/66 distinct scenario hashes,
30.3 fps, ~500–548 ms full sweep). **All numeric metrics here are
advisory. Nothing in this report promotes a renderer; promotion requires
human review of the actual sheets and storyboards.**

The 11 stage emotions are indexed 00 neutral, 01 warm, 02 joy, 03 concern,
04 surprise, 05 thoughtful, 06 skeptical, 07 determined, 08 sleepy,
09 excited, 10 embarrassed (order from `face_stage.h`).

## Fleet-level findings

### F1. Clone and palette-swap clusters (code- and pixel-confirmed)

1. **Anki rig quadruplet — 7 `FACE_RENDER_VECTOR_ROUNDED` (vector-felt),
   8 `FACE_RENDER_COZMO_CUBIC` (cozmo-tiles), 40
   `FACE_RENDER_ROBOT_RIG_VECTOR_ROUNDED` (vector-stage), 41
   `FACE_RENDER_ROBOT_RIG_COZMO_CUBIC` (cozmo-console).** All four are
   one-line wrappers around `draw_anki_procedural_rig` in
   `face_eye_actors.c`; they differ only in eye width/height/pupil and a
   4-value corner-radius row. (7,40) and (8,41) are the same look at two
   scales; in the triage bands the pairs are indistinguishable. Keep one
   Vector look and one Cozmo look; retire or intentionally re-skin the
   other two.
2. **Eye-study octet — 15–22 (`FACE_RENDER_SACCADE_LAB` …
   `FACE_RENDER_CAT_OPTICS`), `face_eye_study_redux.c`.** One draw path,
   all differences in the `ESR_STYLES` table. Tightest near-clones:
   16 `brow-dialogue` ↔ 19 `sleep-wake` and 17 `lid-anticipation` ↔ 20
   `curious-tilt`. Pixel evidence agrees: the fleet-closest structural
   pairs are inside this family (saccade-lab↔curious-tilt 439‰,
   iris-parallax↔cat-optics 558‰ in `acceptance.json.closest_pairs`).
   Eight matrix rows currently spend eight slots on one acting vocabulary
   with different wallpaper. Keep 1–2 (see per-renderer notes).
3. **Toon trio — 53/54/55 (`FACE_RENDER_TOON_BEAN/INK/EMBER`).**
   `fta_draw.c` tests only `look == FTA_LOOK_INK` (six alpha/outline
   flips); `FTA_LOOK_EMBER` is never tested, so toon-ember is a pure
   palette+proportion swap of toon-bean. This trio is an *honest* skin
   system — fine to keep — but it should be presented as one renderer
   with three skins, not three independent entries.
4. **RoboEyes pair — 9/10 (`FACE_RENDER_ROBOEYES_ALERT/SOFT`).** Genuinely
   different aperture rasterizers, but the acting vocabulary and layout
   are identical; at 40×30 they read as a palette swap.
5. **Cross-pack concept duplicates:** (01 `FACE_RENDER_VGA_ELDER`
   pixel-redux-vga-oracle ↔ 50 `FACE_RENDER_PIXEL_PACK_VGA_ELDER`
   vga-elder-storyteller), (02 `FACE_RENDER_TALKIE_CLOSEUP` ↔ 51
   `FACE_RENDER_PIXEL_PACK_TALKIE_CLOSEUP`), (06
   `FACE_RENDER_DITHERED_ROGUE` ↔ 52
   `FACE_RENDER_PIXEL_PACK_DITHERED_ROGUE`), and the single-eye pair
   (14 `FACE_RENDER_JIBO_ORB` ↔ 34 `FACE_RENDER_VOICE_ORB`). Each pair
   should keep the stronger member (see tiers).

### F2. Emblem- and palette-coded emotion instead of facial acting

The pixel-redux family (00–03, 05) and sprite actors (56–61) lean on
emblem icons (♥ ! ? Zz ✳) for several emotions; 33
`cyber-chladni-voiceplate` changes brow *color* per emotion; 38
`cyber-teletext-performer` and 39 `cyber-ferrofluid-familiar` change
palette/side-tab color. Cozmo/Vector acting is geometry acting — lids,
corners, timing — not iconography. Icons are acceptable garnish only when
the face alone already reads at 40×30 in the grey contact row.

### F3. Cue-boundary topology pops in the Anki rig

`acceptance-v1` sparklines for 8/41 (and 7/40) show red pop clusters at
the joy-cue attack (~frames 42–49) and release (~139–149): the eye
switches shape family (rounded-rect → happy arc) discontinuously. Real
Cozmo hides topology swaps inside blinks. Concrete fix in per-renderer
notes. (The production gate's 180-frame sweep at HEAD reports 0 abrupt
jumps because the luma step stays under its ROI threshold — the pop is
structural, which is exactly what the component-count metric caught.)

### F4. Speaking-fixture mouth dominance

With the gate fixture (`mouth_open=152`, viseme AA, weight 220), most
humanoid renderers hold a large open oval that visually swamps
mouth-corner acting; rows 43, 46, 47, 49–51 read samey mid-row mainly for
this reason. Renderers need corner/width acting that survives an open
jaw: corner offsets should displace the *outline*, not just the closed
lip line. This is also why `emotion vs. speech` must be reviewed on the
storyboard rows, not only the frozen-clock row.

### F5. Registered-but-shadowed dead code

The dispatch chain in `face_render.c` resolves all 62 profiles before the
legacy family switch; consequences worth cleaning up:
`face_eye_actors.c` exposes 23 styles but only 4 are reachable (7, 8, 40,
41); `face_sprite_actors.c` exposes 6, reachable 3 (58–60);
`face_robot_eyes*.c`, `face_pixel_pack*.c`, `face_sprite_sheet.c`,
`face_sprite_showcase.c`, `face_mouth_study_redux.c` and the entire
legacy `switch` path are dead but still compiled into firmware, WASM, and
every test binary.

### F6. Contact scale governs the web matrix

Matrix density displays every renderer at 40×30 — identical to the
contact-scale row in the acceptance sheets. Dark-on-dark profiles
(35 red-optic, 33 chladni, 32 crt to a lesser degree) effectively vanish
there; bright-mass profiles (toon trio, 13 eve, 8/41 cozmo, 47 navigator)
dominate. Also: `REVIEWED_STRONG_PROFILES = {8, 29, 40, 41, 53, 54, 58,
59}` in `app.js` is stale — 40/41 are duplicates of 8, and 58/59 rank
near the bottom of this audit; suggested replacement after fixes:
{8, 11, 13, 26, 29, 30, 47, 48, 53}.

### F7. Fleet motion is otherwise healthy

Fresh gate run: 0 abrupt jumps, 0 frozen-pair failures, geometry
61 pass / 1 warn (38 cyber-teletext). The remaining risk is structural
(F3) and stylistic, which pixel-delta gates cannot see — hence the
component/topology metrics in this tool.

## Ranked tiers

Ranking criterion: closeness to "Cozmo/Vector-quality acting on a 160×120
plate" — geometry acting strength across all 11 emotions, contact-scale
survival, temporal cleanliness, identity distinctness. Tier order within
a tier is rough preference.

### Tier A — lead performers (keep, polish listed defects)

| # | Legacy enum ID | Slug | Verdict |
|---|---|---|---|
| 53 | FACE_RENDER_TOON_BEAN | toon-bean | Best all-round face; calm, readable, real brow/lid/corner acting |
| 8 | FACE_RENDER_COZMO_CUBIC | cozmo-tiles | Best eyes-only actor (keep this *or* 41, not both) |
| 13 | FACE_RENDER_EVE_MINIMAL | eve-minimal-performer | Clean visor acting, excellent at contact scale |
| 29 | FACE_RENDER_NEON_SDF_CYAN | neon-cyan-faceplate | High-contrast plate, distinct emotion silhouettes |
| 30 | FACE_RENDER_NEON_SDF_MAGENTA | neon-ribbon-performer | Liveliest eyes in the fleet (real pupils that track) |
| 11 | FACE_RENDER_M5_AVATAR_CLASSIC | m5-avatar-classic-performer | Strong brow/blush acting, honest M5 heritage |
| 26 | FACE_RENDER_TEETH_TONGUE | teeth-tongue-redux | Strongest mouth vocabulary; memorable identity |
| 48 | FACE_RENDER_SPRITE_POCKET_RELAY_CREATURE | pocket-relay-creature-performer | Charming creature acting; ears+face carry emotion |
| 47 | FACE_RENDER_SPRITE_VGA_STAR_NAVIGATOR | vga-star-navigator-performer | Big readable masses; star twinkle is charm, not defect |
| 4 | FACE_RENDER_AMBER_TERMINAL | amber-terminal-operator | Distinct CRT identity, good sleepy/dim acting |

Tier-A defects to fix:

- **53 toon-bean** (and skins 54/55): mouth too small to read at 40×30 —
  grey contact row shows 05/06/07 collapsing to identical flat-brow
  faces. Geometry: enlarge mouth plate ~1.5× (half-width from ~14 px to
  ~21 px at 160-wide), raise corner gain so `mouth_corner_*=±78` (joy)
  displaces corners ≥5 px vertically. Timing: storyboard shows almost no
  head/body motion despite `head_roll` and NOD in the fixture — multiply
  `motion_gain_q8` so the joy+nod cue produces ≥2 px visible head arc,
  with 2-frame ease-out (Cozmo leads with the whole eye-plate, not just
  features).
- **8 cozmo-tiles / 41 cozmo-console**: (a) F3 pops — crossfade shape
  families over ≥4 frames or force the round→arc switch to complete
  inside a blink (lid fully closed for 1 frame at the swap); (b) sleepy
  (08) fragments each eye into 3–4 blobs — keep a single ≥30%-area slab
  per eye with a wavy top lid instead of disjoint tiles; (c) embarrassed
  (10) merges both eyes into one low band — enforce a ≥6 px inter-eye
  gutter at all times; (d) warm (01)/excited (09) cut the pupil as a
  notch into the lid edge — draw the pupil only when fully interior, else
  drop it for that pose (Cozmo lids occlude the pupil, they don't bite
  it).
- **13 eve**: joy/sleepy arcs get thin; keep stroke ≥3 px at all
  emotion extremes so the arc survives 4× downscale.
- **29 neon-cyan-faceplate**: TOPO ×11 around frames 15–42 — outline
  segments detach during eye-shape morphs; bridge outline joints
  (1 px fillet) during transitions.
- **30 neon-ribbon**: magenta ribbon (~40% luma) dies in the grey contact
  row — thicken mouth/brow strokes to 3 px and lift ribbon luminance;
  keep the chromatic fringe.
- **26 teeth-tongue**: three TOPO events (54/59/178) when teeth rows
  appear — fade teeth in over 2 frames instead of popping the row.
- **48 pocket-relay**: concern (03) recolors the whole face blue —
  keep palette shifts ≤ hue accent; let ears+brows carry concern.
- **47 vga-star-navigator**: TOPO ×80 is background star twinkle — keep,
  but exclude bg from the mask by dimming stars ~30% so tile hashing and
  human eyes track the face, not the sky.
- **4 amber-terminal**: rectangle-outline mouth has no corner vocabulary
  (corners can't rise) — add 2 px corner risers/droppers on the outline
  ends for joy/concern; TOPO ×21 = scanline flicker, clamp per-frame
  changed-cell fraction below ~4%.
- **11 m5-classic**: mouth oval saturates at fixture jaw; map corner
  offsets onto the oval's end tangents so joy ≠ surprise mid-speech.

### Tier B — solid supporting cast (integrate after fixes)

| # | Legacy enum ID | Slug | Main defects and concrete fixes |
|---|---|---|---|
| 12 | FACE_RENDER_M5_AVATAR_MANGA | m5-avatar-manga-performer | Busy at contact scale; reduce highlight sparkle count from ~4 to 2; TOPO ×2 benign |
| 3 | FACE_RENDER_PIXEL_AUTOMATON | pixel-redux-arcade-automaton | Good robot; TOPO ×21 from segment mouth — debounce segment on/off ≥2 frames |
| 0 | FACE_RENDER_EGA_QUEST | pixel-redux-ega-wayfarer | Emblem-driven (F2); hatch bg noisy at 40×30 — dim hatch ~25%; make brows do the ?/! work before icons |
| 1 | FACE_RENDER_VGA_ELDER | pixel-redux-vga-oracle | Teeth dither reads as noise at contact scale — simplify mouth interior to 2 tones; wins the duel vs 50 |
| 2 | FACE_RENDER_TALKIE_CLOSEUP | pixel-redux-talkie-mechanic | Mid-row emotions samey (F4); add head-tilt ±3 px for thoughtful/skeptical; wins vs 51 |
| 5 | FACE_RENDER_POCKET_RPG | pixel-redux-pocket-mossling | Same hatch-bg note as 00; otherwise charming; TOPO ×5 benign (blink) |
| 6 | FACE_RENDER_DITHERED_ROGUE | dithered-film-noir-rogue | Uniform open mouth deadens corners (F4) — add corner-lift to the open-mouth outline; wins vs 52 |
| 36 | FACE_RENDER_HUB75_NEON | hub75-block-mascot | Crude boxes but great contrast; headphone blocks touch L/R edges (v40 geometry 9/10) — inset 2 px |
| 37 | FACE_RENDER_EDGE_GLOW | edge-light-sentinel | Single pop+topo at frame 54 (brow-bar switch) — 2-frame fade; otherwise clean |
| 23 | FACE_RENDER_PRESTON_SPRITES | preston-sprites-redux | **Edge clip ×11**: scalp flat-cut at y=0 every emotion (red overlay ticks across top) — shrink head 6 px vertically, scalp ≥3 px below frame top |
| 25 | FACE_RENDER_BEZIER_RIBBON | bezier-ribbon-redux | **Edge clip ×11**: crown against y=0; drop head 4 px; curtain folds touch side borders — clamp folds to x∈[2,158); lip morphs otherwise excellent |
| 24 | FACE_RENDER_POLYGON_JALI | polygon-jali-redux | Facet pops near frames 164–168 (POPS ×1, TOPO ×6) — interpolate facet vertices, don't re-tessellate per keypose; identity is odd but distinct |
| 27 | FACE_RENDER_LED_VU_MOUTH | led-vu-mouth-redux | Mouth acts, eyes dead (pupil-less blocks; only sleepy changes them) — add 2×2 pupil dot with saccade; TOPO ×53 inherent to LED dots, debounce dot toggles ≥2 frames to avoid strobe |
| 28 | FACE_RENDER_ORIGAMI_MASK | origami-mask-redux | Fold pops at 46–53 (POPS ×2) — crossfade fold shading over 3 frames; sleepy/ear acting good |
| 9 | FACE_RENDER_ROBOEYES_ALERT | roboeyes-alert-performer | Warm/excited ≈ neutral at 40×30 — add ≥12% eye-height delta and 3 px brow-bar shift; merge with 10 as one profile with two skins |
| 14 | FACE_RENDER_JIBO_ORB | jibo-orb-performer | Single-eye acting subtle; add lid-angle asymmetry for skeptical/determined; wins the single-eye duel vs 34 |
| 57 | FACE_RENDER_SPRITE_ACTOR_VGA_STAR_CAPTAIN | sprite-redux-bridge-commander | Solid; POPS ×1 @141 (icon swap) — move icon switches to cue attack midpoint; tone console furniture |
| 61 | FACE_RENDER_SPRITE_ACTOR_ARCADE_CHROME_PILOT | sprite-redux-arcade-ace | Similar furniture note; visor reflections eat eye detail at 40×30 — reduce reflection alpha |
| 56 | FACE_RENDER_SPRITE_ACTOR_EGA_COURT_MAGE | sprite-redux-rune-magister | Corner brackets are pure furniture — remove; brows already act well |
| 42 | FACE_RENDER_ROBOT_RIG_BROW_DIALOGUE | brow-dialogue-director | Thin outlines fade at contact scale — 2 px minimum stroke; TOPO ×22 = outline joint detach, fillet joints |
| 45 | FACE_RENDER_ROBOT_RIG_CAT_OPTICS | cat-optics-familiar | Whisker components trip corner-detach (by design); slit-pupil acting good; add ear-flatten for determined |
| 44 | FACE_RENDER_ROBOT_RIG_IRIS_PARALLAX | iris-parallax-scout | TOPO ×75 = goggle glint dither — make glint static per pose; mouth = small blob, add corner rig |
| 46 | FACE_RENDER_ROBOT_RIG_M5_MANGA | m5-manga-lead | F4 mouth dominance; hair fringe near y=0 — verify ≥2 px margin; add lid asymmetry for skeptical |

### Tier C — weak, redundant, or demote to diagnostics

| # | Legacy enum ID | Slug | Assessment |
|---|---|---|---|
| 40 | FACE_RENDER_ROBOT_RIG_VECTOR_ROUNDED | vector-stage | Duplicate of 7 at larger scale (F1) — retire or re-skin with genuinely new corner/timing persona |
| 7 | FACE_RENDER_VECTOR_ROUNDED | vector-felt | Keep only if 40 retires (or vice versa); flat fills hide gaze — acceptable for Vector look, but then blink/saccade timing must carry life; POPS ×14 are blinks (fine) plus F3 arc swaps (fix as 8) |
| 41 | FACE_RENDER_ROBOT_RIG_COZMO_CUBIC | cozmo-console | Same face as 8 (F1); keep exactly one Cozmo |
| 10 | FACE_RENDER_ROBOEYES_SOFT | roboeyes-soft-performer | Palette-swap presentation of 9 (F1) — fold into 9 as a skin |
| 16 | FACE_RENDER_BROW_DIALOGUE | brow-dialogue | Eye-study octet (F1). Keep **one** brow-forward study (16 is the best of the eight) |
| 22 | FACE_RENDER_CAT_OPTICS | cat-optics | Second-best of the octet (slit pupils) but 45 covers cat acting better |
| 15 | FACE_RENDER_SACCADE_LAB | saccade-lab | Diagnostic value only; red dotted rail = furniture in the mouth band; TOPO ×21; keep as dev tool, drop from showcase |
| 17 | FACE_RENDER_LID_ANTICIPATION | lid-anticipation | Near-clone of 20 (F1); fold in |
| 18 | FACE_RENDER_IRIS_PARALLAX | iris-parallax | Fold into the surviving study |
| 19 | FACE_RENDER_SLEEP_WAKE | sleep-wake | Near-clone of 16 (F1); its doze acting can become a *state* of the surviving study |
| 20 | FACE_RENDER_CURIOUS_TILT | curious-tilt | **Brow lines run off the L/R frame edges** on strong raises (CLIP ×9) — clamp brow endpoints to x∈[6,154); sleepy collapses to a dashed line — min eye mass 3 px; then fold into the octet survivor |
| 21 | FACE_RENDER_DOT_MATRIX_EYES | dot-matrix-eyes | Weakest expression pair in v40/v41 (dot grid barely changes); POPS ×5 = LED strobing; needs an emotion→dot-shape table before it earns a slot |
| 43 | FACE_RENDER_ROBOT_RIG_SLEEP_WAKE | sleep-wake-dreamer | Dead-gaze eyes: fixed dot pupils never move in the storyboard — add saccade/parallax; 00/01/05/09 near-identical (F4) |
| 49 | FACE_RENDER_PIXEL_PACK_EGA_QUEST | ega-quest-squire-performer | Redundant vs 0; helmet hides brows so acting range is capped; TOPO ×25 plume flicker |
| 50 | FACE_RENDER_PIXEL_PACK_VGA_ELDER | vga-elder-storyteller | Loses the elder duel to 1 (busier beard noise, TOPO ×79 dither shimmer — make dither pattern pose-static) |
| 51 | FACE_RENDER_PIXEL_PACK_TALKIE_CLOSEUP | talkie-moon-mechanic | Loses to 2; monocle is nice — port it to 2 as an accessory; TOPO ×75 dither shimmer |
| 52 | FACE_RENDER_PIXEL_PACK_DITHERED_ROGUE | two-tone-zine-rogue | Distinct zine style but red + crosses are furniture; loses to 6 unless the furniture becomes acting (e.g., crosses only on excited) |
| 31 | FACE_RENDER_LIQUID_SMIN | liquid-droplet-familiar | Red-eye recolor for concern/skeptical reads as alarm (palette-coded, F2); keep blob squash, drive emotion via blob geometry |
| 32 | FACE_RENDER_CRT_CHROMATIC | crt-phosphor-puppet | Mid; TOPO ×22 scanline flicker — clamp per-frame changed fraction; dim at contact scale |
| 34 | FACE_RENDER_VOICE_ORB | voice-orbit-familiar | Loses single-eye duel to 14; mostly aperture-size acting; ray spikes are furniture |
| 33 | FACE_RENDER_HOLO_WIREFRAME | cyber-chladni-voiceplate | Palette-coded brows (F2), TOPO ×99 sand thrash, face dissolves at 40×30. Make sand settle per pose (static pattern, animate only during transitions ≤500 ms; freeze between); else demote to screensaver |
| 38 | FACE_RENDER_GLITCH_MASK | cyber-teletext-performer | Only geometry warn in v41 (6/10): emotion = palette change (F2); mosaic mush at contact scale; needs sextant-level *shape* changes (eye/mouth cell outlines) per emotion |
| 39 | FACE_RENDER_PALETTE_PLASMA | cyber-ferrofluid-familiar | Side-tab color codes emotion (F2); blob acting weak; TOPO ×22; either drive blob silhouettes from brow/corner actions or demote |
| 35 | FACE_RENDER_RED_OPTIC | red-optic-performer | **Bottom of fleet for device use**: contact row nearly black (red-on-black), POPS ×33 continuous speckle shimmer, emotion deltas tiny. Fixes: lift optic luminance ~2×, slew-limit flicker ≤8 luma levels/frame and bind amplitude to `audio_level`, add aperture-blade geometry per emotion. Otherwise keep as a villain easter egg, not a conversational face |
| 58 | FACE_RENDER_SPRITE_ACTOR_TALKIE_MOON_MECHANIC | sprite-actor-talkie-moon-mechanic | SILENT-MOUTH confirmed: mouth is a fixed black oval, corners never move; HUD brackets/gauges touch all borders (CLIP ×11) and out-act the face (F2). Shrink furniture to ≤8 px corner ticks, grow face ~20%, add real corner rig; currently tagged "Reviewed strong" in app.js — remove |
| 59 | FACE_RENDER_SPRITE_ACTOR_JRPG_STORM_FAMILIAR | sprite-actor-jrpg-storm-familiar | Same furniture pattern (CLIP ×11, bolts/road lines); face itself is decent — declutter first; remove from "Reviewed strong" |
| 60 | FACE_RENDER_SPRITE_ACTOR_HANDHELD_FOREST_PET | sprite-actor-handheld-forest-pet | SILENT-MOUTH (line mouth static); green-on-green ~zero contrast at 40×30. GB charm is real: raise face/bg contrast one shade and give the mouth 3 shapes; ears already act |

Duplicates note: tier placement of 1/2/6 vs 50/51/52 is a *pair*
decision — if art direction prefers the pack versions, swap winners; the
defect lists still apply.

## Cross-cutting recommended changes (geometry/timing)

1. **Shape-family morphs must be continuous or blink-masked** (8, 41, 7,
   40, 24, 28): interpolate control points (corner radius, arc height)
   over ≥4 frames, or complete the swap during a 1-frame full lid
   closure.
2. **Safe-area rule**: no *face* pixel in the outer 2 px except
   deliberate full-bleed backdrops; add scalp/crown margin ≥3 px
   (23, 25), clamp brows to x∈[6,154) (20). The acceptance sheets'
   red border overlay makes violations visible per emotion.
3. **Effect-noise budget**: per-frame changed-area from decorative
   animation (scanlines, sand, dither, LED dots, star twinkle) should
   stay under ~4% of the frame and be slew-limited; texture patterns
   should be a pure function of pose, not of the frame clock (33, 50,
   51, 44, 32, 27, 35).
4. **Contact-scale floor**: every emotion must remain distinguishable in
   a 40×30 grey thumbnail (this is literally the web-matrix tile). The
   acceptance sheet row 2 is the review surface; strokes < 2 px or
   luma < ~45 fail it in practice (30, 42, 13-extremes, 35, 60).
5. **Eye life floor**: any renderer with a visible iris/pupil should show
   ≥1 px saccade drift and pose-dependent pupil position in the
   storyboard row (43, 27, 60 currently fail; 30 is the reference).
6. **Corner rig everywhere there is a mouth**: `mouth_corner_left/right`
   at ±78 (joy target) should displace drawn corners ≥4 px vertically
   even with the jaw open (F4 offenders: 43, 46, 47 mouth blob, 58, 60,
   4's rectangle ends).
7. **Present skin families honestly**: one entry + skin selector for
   (53,54,55), (9,10), the surviving Anki pair, and the eye-study
   survivor — frees ~10 matrix slots for genuinely new acting.
8. **Delete or split shadowed dead code** (F5) so audits, firmware size,
   and WASM builds track reality.
9. **Update `REVIEWED_STRONG_PROFILES`** in `tools/face-grid/app.js`
   after fixes land (suggest {8, 11, 13, 26, 29, 30, 47, 48, 53}).

## Review workflow notes

- Storyboard/atlas review by an external vision model can continue via
  the existing `tools/review_face_visuals.py` image pipeline (x.ai
  `/v1/responses` with base64 PNGs), which is already proven in
  `local/visual-reviews/`. No claims are made here about other Grok API
  modalities; nothing in this audit depends on them.
- The acceptance tool in this directory (`make test && make run`) is
  designed to run beside `tools/run_face_render_quality.py` each round:
  the gate proves pixel-level separability/continuity; this tool adds
  border, topology, region (dead-eye/silent-mouth/corner), contact-scale
  and clone-suspect evidence plus one-glance sheets. Both remain
  advisory by design.
