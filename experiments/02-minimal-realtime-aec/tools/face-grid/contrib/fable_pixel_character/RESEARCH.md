# Source & licensing research — fable_pixel_character

Scope: pixel-art talking portraits for the StackChan face grid, informed by
1980s/90s EGA/VGA adventure games, talkie-era closeups, JRPG dialogue boxes,
handheld/console/micro hardware constraints, and 1-bit desktop dithering.

## Originality statement

- **All drawing code and all character art in this directory are original.**
  Every face (Sir Rowan, Eldrin, Captain Marlow, Unit A7, TTY0, Pip, the
  rogue, the librarian, Ziggy, the cadet, Sir Lute, Razz) was designed for
  this contribution; nothing reproduces a copyrighted character, sprite, or
  screen.
- No source code was copied from any external project. Techniques were
  re-implemented from documented behaviour, published algorithm
  descriptions, and hardware facts.
- The only files taken from elsewhere are `compat/face_keyframe.h` and
  `compat/face_pose.h`, verbatim copies of this repository's own firmware
  ABI headers (`firmware-ws/main/`), vendored so the contribution builds
  standalone.

## Techniques and their provenance

| Technique used | Provenance | IP status |
|---|---|---|
| Ten-shape mouth bank named after the six-basic/three-extended lip-sync convention | Convention popularised by Hanna-Barbera studios and documented by [Rhubarb Lip Sync](https://github.com/DanielSWolf/rhubarb-lip-sync) (MIT); the Preston Blair phoneme series ([reference charts by Gary C. Martin](https://www.garycmartin.com/mouth_shapes.html)) | Naming/technique only; **all mouth drawings here are original** |
| EGA checkerboard dithering for missing midtones | Period practice in Sierra SCI0-era art; discussed in e.g. [Hackaday's "Upscaling the Sierras"](https://hackaday.com/2022/06/13/upscaling-the-sierras/) and [VOGONS threads on Sierra's EGA drivers](https://www.vogons.org/viewtopic.php?t=72278) | Technique, not copyrightable; no Sierra art reproduced |
| EGA/CGA 16- and 4-colour palettes | IBM hardware specifications (canonical 0x55/0xAA RGB levels) | Hardware facts |
| Bayer 8×8 ordered dithering | Standard ordered-dither index matrix (Bayer 1973) | Public domain algorithm |
| Atkinson error diffusion (6/8 of error to six neighbours) | Bill Atkinson at Apple, used by MacPaint/HyperCard; described at [Wikipedia](https://en.wikipedia.org/wiki/Atkinson_dithering) and [beyondloom](https://beyondloom.com/blog/dither.html) | Public-knowledge algorithm; independently implemented |
| ZX Spectrum 8×8 ink/paper attribute cells with a shared bright bit, tape-loading border stripes | Sinclair hardware documentation / common knowledge | Hardware facts; the resolve pass emulating clash is original code |
| Game Boy DMG four-shade green palette | Commonly cited approximation of DMG LCD shades (0F380F/306230/8BAC0F/9BBC0F) | Hardware approximation |
| NES per-16×16-block subpalette constraint | 2C02 PPU attribute-table behaviour, public hardware documentation; colours here are loose approximations, not a dumped PPU palette | Hardware facts |
| C64 double-wide multicolor pixels, 16-colour palette | MOS VIC-II behaviour; palette values in the neighbourhood of the community "Pepto" measurements | Hardware facts / community-measured values |
| lowbias32 integer hash (idle-rig determinism) | [Chris Wellons, "Prospecting for Hash Functions"](https://nullprogram.com/blog/2018/07/31/) | Explicitly public domain |
| Keyframe-driven parametric face (eyes/lids/brows/mouth from a byte vector) | Pattern established by Anki Cozmo/Vector procedural faces, [m5stack-avatar](https://github.com/meganetaaan/m5stack-avatar) (MIT), and the project's own research notes | Pattern only; no code reused |

## Projects reviewed and deliberately NOT used

- **[FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes) — GPL-3.0.**
  Copyleft; no code, constants, or drawings were taken from it. (Its
  rounded-rect-eye idea is generic prior art, and this contribution's eye
  systems are keyframe/LED/glyph based, not derived from RoboEyes.)
- Sierra/LucasArts/Nintendo/Sega assets: studied only as art-history
  reference via commentary articles; no asset, palette dump, or sprite was
  copied.

## Licensing conclusion

Everything in this directory can be offered under the repository's licence
without additional attribution obligations. The MIT-licensed projects above
were referenced for conventions, not code; the GPL project was excluded;
hardware palettes and dithering algorithms are facts/public-domain
knowledge; the lowbias32 hash is public domain (attributed in a comment in
`src/pf_engine.c` as a courtesy).
