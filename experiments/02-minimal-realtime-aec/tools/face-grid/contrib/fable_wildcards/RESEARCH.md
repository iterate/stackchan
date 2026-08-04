# Wildcard visual languages: prior-art research and licensing audit

Research pass for the wildcard worker: surprising, cheap face/presence
languages beyond parametric robot eyes, pixel portraits, mouth geometry and
neon SDF shaders (those lanes belong to sibling workers, and the forty
existing `face_render.h` profiles already cover amber terminals, dot-matrix
eyes, LED VU mouths, CRT chromatic aberration, holo wireframes, voice orbs,
glitch masks and palette plasma). Every renderer here was chosen to be a
different *algorithm*, not a different palette, and each is an independent
implementation from first principles — no code was copied from any project
listed below.

## 1. Vector CRT / oscilloscope music (wc-scope-beam)

- Jerobeam Fenderson and Hansi Raber's *Oscilloscope Music* draws images by
  feeding crafted stereo audio into an analog scope in XY mode: left channel
  deflects X, right channel deflects Y, and the moving dot plus phosphor
  persistence draws the shape. Their OsciStudio tool turns 3D meshes into
  such audio ([oscilloscopemusic.com](https://oscilloscopemusic.com/info/about/),
  [CreativeApplications write-up](https://www.creativeapplications.net/project/oscilloscope-music-jerobeam-fenderson-and-hansi-raber/),
  [Wikipedia](https://en.wikipedia.org/wiki/Oscilloscope_Music)).
- The Vectrex and calligraphic arcade monitors (Asteroids, Tempest) render
  from a display list of point-to-point vectors, with intensity a function of
  beam dwell ([vectrex.com teardown](https://vectrex.com/understanding-the-vector-display-of-the-vectrex/),
  [e-basteln Asteroids monitor notes](https://www.e-basteln.de/arcade/asteroids/monitor/),
  [Trammell Hudson's vector games](https://trmm.net/Vector_games_32c3/)).
- P7 is the classic long-persistence radar/scope phosphor: a blue-white
  prompt flash over a green afterglow lasting seconds; digital radar displays
  simulate it by decaying pixel intensity per scan
  ([radartutorial.eu PPI scope](https://www.radartutorial.eu/12.scopes/sc16.en.html),
  [EDN on persistence displays](https://www.edn.com/oscilloscope-persistence-displays/),
  [oscilloclock.com trailing effect](https://oscilloclock.com/archives/335)).
- Novelty here: the persistence is *stateless* — the last 120 ms of beam
  history is recomputed from the sample clock each frame, so trails survive
  the pure-function contract; and the mouth is a literal synthesized
  waveform whose harmonic mix comes from the semantic mouth bytes.

## 2. Flip-dot displays (wc-flipdot-cascade)

- Flip-dot signs are grids of magnetized discs flipped by coil pulses;
  AlfaZeta sells modern XY5 walls and BREAKFAST (Brooklyn) drives them at
  animation rates; hobby drivers document row-strobed refresh and per-dot
  capacitor discharge ([flipdots.com](https://flipdots.com/en/products-services/custom-made-flip-dot-wall/),
  [Hackaday on BREAKFAST's display](https://hackaday.com/2012/07/19/flip-dot-display-is-an-advertising-experience-we-can-get-behind/),
  [PierreMuth's "Flipping dots fast"](https://pierremuth.wordpress.com/2021/02/17/flipping-dots-fast/)).
- Simulators exist (owenmcateer/FlipDots for Processing, dcreemer/flipdot,
  JakeWimberley/AniDot) but none were consulted beyond their READMEs and no
  code was taken; this implementation's mechanics (row latency, |cos|
  foreshortening, dual-refresh-tick comparison for stateless flip animation)
  were derived from the physical behavior documented above.

## 3. Chladni figures / cymatics (wc-chladni-sand)

- Ernst Chladni's 1787 experiments: sand on a vibrating plate collects at
  nodal lines. For a centrally driven square plate the standard classroom
  model of the nodal set is cos(nπx)cos(mπy) − cos(mπx)cos(nπy) = 0
  ([UVic PDF on Chladni standing waves](https://web.uvic.ca/~mcindoe/128.pdf),
  [VCU mathematical exploration](https://scholarscompass.vcu.edu/cgi/viewcontent.cgi?article=1098&context=jmsce_vamsc),
  [COMSOL blog](https://www.comsol.com/blogs/how-do-chladni-plates-make-it-possible-to-visualize-sound),
  [dynamicmath.xyz interactive](https://www.dynamicmath.xyz/chladni-patterns/)).
- Mapping visemes to resonance mode pairs, clamping the field into rings at
  the eyes, and using the mouth rim as a nodal attractor appears to be
  original to this prototype; I found no prior audio-driven *face* built
  from Chladni figures.

## 4. Halftone / comic print (wc-halftone-press)

- Amplitude-modulated halftoning renders tone as dot area on a rotated
  screen grid; classic press practice rotates plates ~30° apart (black at
  45°) to trade moiré for rosettes
  ([The Print Guide on screen angles](http://the-print-guide.blogspot.com/2009/05/halftone-screen-angles.html),
  [Printing Technologist](https://printingtechnologist.blogspot.com/2011/07/color-halftone-and-screen-angles.html)).
- Ben-Day dots, Roy Lichtenstein's pop-art enlargements of them, and manga
  screentone plus radial emphasis lines (shūchūsen) are the visual
  vocabulary quoted here; all are techniques, not copyrightable designs, and
  the face artwork is original.
- The 45° screen via u=x+y/v=x−y is exact in integers; the second plate's
  arbitrary angle uses an incremental DDA; the drifting misregistration is
  this prototype's own gag, modeled on worn-press registration error.

## 5. Shadow puppetry (wc-wayang-lamp)

- Wayang kulit: perforated leather puppets between an oil lamp and a cotton
  screen; the carving (tatahan) produces light-through-holes detail, and
  puppets articulate at hinged joints. Academic work on digitizing the form
  documents rigging and silhouette aesthetics
  ([Maya rigging paper](https://www.researchgate.net/publication/252004247_Wayang_kulit_Digital_puppetry_character_rigging_using_Maya_MEL_language),
  [CGI emulation framework](https://link.springer.com/chapter/10.1007/978-3-319-66984-7_19)).
- The renderer borrows only the physical staging (backlight, perforation,
  hinge). The puppet design is original, deliberately generic (crown,
  filigree, almond eyes) and not a reproduction of any traditional or
  protected character; the hinged-jaw-as-light-gap speech mapping is this
  prototype's own idea.

## 6. Ferrofluid (wc-ferro-pool)

- The Rosensweig (normal-field) instability makes a magnetized ferrofluid
  surface erupt into a spike array, hexagonally packed above threshold
  ([amplitude-equation paper, arXiv:1101.3742](https://arxiv.org/pdf/1101.3742),
  [side-wall study, arXiv:0904.3864](https://arxiv.org/pdf/0904.3864)).
- Audio-reactive ferrofluid is an established product genre (XELLO Glowbe,
  UFaudiO sound sculpture), which validates "voice makes the fluid spike" as
  a legible presence language
  ([Glowbe](https://www.helloxello.com/products/ferrofluid-speaker),
  [Yanko Design on UFaudiO](https://www.yankodesign.com/2025/06/30/ufaudio-turns-music-into-motion-with-ferrofluid-driven-sound-sculpture/)).
- The three-plane-wave interference (0°/120°/240°) is the textbook minimal
  model of a hexagonal lattice; using it as a cheap integer bump field, and
  the catchlight-as-pupil droplet eyes, are original to this prototype.

## 7. Teletext mosaics (wc-teletext-sextant)

- Teletext G1 block mosaics: 64 characters covering every 2×3 subcell
  combination; one foreground color per character cell over a background,
  which is what produces attribute clash; BBC Micro Mode 7 made the look
  famous, and Unicode now encodes the sextants in Symbols for Legacy
  Computing (U+1FB00…)
  ([Teletext art wiki](https://teletext.wiki.zxnet.co.uk/wiki/Teletext_art),
  [teletext character set overview](https://grokipedia.com/page/teletext_character_set),
  [Unicode terminals proposal L2/17-435](https://www.unicode.org/L2/L2017/17435r-terminals-prop.pdf)).
- Existing converters (e.g. @techandsoftware/image-to-sextants, edit.tf)
  demonstrate image→sextant reduction; none were consulted beyond
  documentation. "CEEFAX" and "ORACLE" are broadcasters' names, so the page
  header uses an invented service name. The header/clock, per-cell majority
  color vote and test-card bar row are original code.

## Explicitly avoided

- **AAlib / libcaca** (ASCII art): LGPL/WTFPL-adjacent lineage and an
  already-crowded terminal lane (`FACE_RENDER_AMBER_TERMINAL` exists);
  teletext covers cell-quantized art with a stronger identity.
- **FluxGarage RoboEyes** (GPL) and other eye libraries: robot-eye lane
  belongs to a sibling worker; nothing here draws parametric robot eyes.
- **Shadertoy shaders / Inigo Quilez material**: mixed licenses (some
  CC-BY-NC-SA); no shader code was ported. The only SDF-adjacent math here
  is the shared normalized-ellipse helper, which is elementary geometry.
- **String art** (greedy chord optimization): genuinely lovely prior art
  ([Birsak et al. 2018](https://www.researchgate.net/publication/322766118_String_Art_Towards_Computational_Fabrication_of_String_Images),
  [a greedy generative variant](https://hal.science/hal-03901755/document)),
  but the greedy solve is offline-shaped and the per-frame line budget at
  160×120 punished readability in early sketches, so it was dropped in
  favor of the seven shipped languages.

## Licensing statement

All C in this directory was written for this contribution from the physics
and format documentation cited above. The only externally derived constants
are: the `lowbias32` integer-mixing constants (Chris Wellons' public-domain
hash research), the CRC-32 polynomial 0xEDB88320 (public standard, tests
only), and the classroom Chladni plate equation (public science). The
quarter-wave sine table was generated numerically for this project. Nothing
here reproduces protected characters, logos, typefaces or trade dress; the
teletext page uses an invented service name, and the 3×5 glyphs were drawn
by hand for this file. The directory is safe to license under the
repository's own terms.
