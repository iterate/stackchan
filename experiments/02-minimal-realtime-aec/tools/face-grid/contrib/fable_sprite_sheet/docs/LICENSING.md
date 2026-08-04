# Licensing and sourcing research note

Research date: 2026-07-28 (web-verified against first-party sources;
key URLs inline). This note governs which sprite sheets may be wired
into this engine and under what conditions.

## The demo assets in this directory

All four demo sheets (`ega_sorcerer`, `handheld_gobbo`,
`vga_navigator`, `terminal_operator`) are generated from geometric
primitives by `tools/gen_demo_sheets.py` in this repository. They are
original works with scriptable provenance and are dedicated to the
public domain under **CC0 1.0**
(https://creativecommons.org/publicdomain/zero/1.0/). The styles
imitate hardware-era constraints (EGA palette, four-shade handheld
green, VGA colour counts, amber phosphor), and style is not
copyrightable (US Copyright Office Circular 33,
https://www.copyright.gov/circs/circ33.pdf); no character, name, or
ensemble of visual elements from any actual game is reproduced.

## Never: ripped sprites

Sprites extracted from real games (The Spriters Resource and similar)
are copyrighted artwork with no licence granted; the sites' own terms
say the material "cannot be used in any commercial project without
express written consent from their copyright holders"
(https://www.spriters-resource.com/page/tou/). An open-source repo
redistributes to everyone, including commercial users, so ripped
sprites are categorically incompatible — the site's informal
"personal use" tolerance is not a licence.

Also relevant: *Tetris Holding, LLC v. Xio Interactive* (D.N.J. 2012,
https://en.wikipedia.org/wiki/Tetris_Holding,_LLC_v._Xio_Interactive,_Inc.)
— cloning one specific game's overall look and feel can infringe even
without copying files. Generic era-styling is fine; a pixel-accurate
pastiche of one identifiable game is not.

## Safe sources for real retro-style sheets

| Source | Licence | Repo-embeddable as C arrays? |
|--------|---------|------------------------------|
| Kenney.nl | CC0, all assets (https://kenney.nl/support) | yes, no obligations |
| OpenGameArt CC0 filter | CC0 (https://opengameart.org/content/faq) | yes, no obligations |
| OpenGameArt CC-BY / OGA-BY | CC-BY 3.0/4.0; OGA-BY = CC-BY minus the anti-DRM clause (https://static.opengameart.org/OGA-BY-3.0.txt) | yes, with a CREDITS/NOTICE entry (author, title, licence, link) |
| OGA "CC0 Portraits" collection | CC0 (https://opengameart.org/content/cc0-portraits) | yes — closest existing pool of talking-face material |
| Glitch (Tiny Speck) assets | CC0 (https://archive.org/details/glitch-public-domain-game-art) | yes, but vector Flash art, not pixel art |
| Liberated Pixel Cup | dual CC-BY-SA 3.0 / GPLv3 (https://lpc.opengameart.org/) | avoid: share-alike/copyleft over a firmware binary is legally murky, and per-author CREDITS.TXT attribution is heavy |
| itch.io "royalty-free" packs | per-creator EULAs; typically forbid redistribution "as standalone files" (https://itch.io/blog/929708/general-paid-asset-license) | no — a C array in a public repo is trivially extractable; only packs explicitly CC0/CC-BY qualify |

Policy for this project: **CC0 by default**; CC-BY/OGA-BY acceptable
with a credits file; CC-BY-SA/GPL art and royalty-free EULA packs are
out. No dedicated CC0 viseme/mouth collection was found on OGA, so
expect to draw mouth patches (or generate them) even when reusing a
CC0 portrait — mapping them to the Preston Blair categories is safe:
the *categories* are an uncopyrightable system; only Blair's actual
book plates are potentially protected (his 1947 "Advanced
Animation" circulates as claimed-PD on a non-renewal theory,
https://archive.org/details/advanced-animation-by-preston-blair, but
that is unproven — do not scan or trace the plates).

## Tooling licences

- **Aseprite**: source-available, not OSS since 2016; binaries may not
  be redistributed, but art made with it is fully yours and its file
  formats are publicly documented for independent implementation
  (https://www.aseprite.org/faq/,
  https://github.com/aseprite/aseprite/blob/main/docs/ase-file-specs.md).
  This converter reads Aseprite's *exports*; no Aseprite code is used.
- **LibreSprite** (GPL-2.0 fork, https://github.com/LibreSprite/LibreSprite)
  and **Piskel** (Apache-2.0, https://github.com/piskelapp/piskel) are
  fully open alternatives that can author compatible sheets.
- **Rhubarb Lip Sync** is MIT
  (https://github.com/DanielSWolf/rhubarb-lip-sync); its shape
  definitions and timing constants are referenced (reimplemented, not
  copied) in this engine.
- **TexturePacker** is commercial; its generic JSON output is
  compatible with the converter's Aseprite mode normalisation.

## Embedding licence text

When a CC-BY/OGA-BY sheet is added: put the attribution block in a
`CREDITS.md` next to the manifest, and have the converter's generated
header carry a one-line pointer. CC licences allow attribution "in
any reasonable manner based on the medium"
(https://creativecommons.org/licenses/by/4.0/legalcode), so
source-tree + product-docs credit satisfies firmware distribution.
