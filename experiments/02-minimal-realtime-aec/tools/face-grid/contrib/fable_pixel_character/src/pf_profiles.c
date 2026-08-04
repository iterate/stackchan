#include "pf_internal.h"

typedef struct {
    const char *slug;
    const char *name;
    uint32_t salt;
    pixel_face_style_t style;
    pf_render_fn render;
} pf_profile_entry_t;

static const pf_profile_entry_t pf_profiles[PIXEL_FACE_PROFILE_COUNT] = {
    [PIXEL_FACE_EGA_QUEST] = {
        "ega_quest", "EGA Quest Portrait", 0xE6A00001U,
        { 80, 60, 2, 2, PIXEL_FACE_MOUTH_SPRITE_BANK, PF_SHAPE_COUNT, 16,
          "fixed 16-colour EGA hardware palette",
          "2x2 checkerboard pairs stand in for missing midtones" },
        pf_render_ega_quest,
    },
    [PIXEL_FACE_VGA_ELDER] = {
        "vga_elder", "VGA Elder Closeup", 0x06A00002U,
        { 160, 120, 1, 1, PIXEL_FACE_MOUTH_POLYGON, 0, 26,
          "hand-picked 26-entry ramps (skin/beard/robe) in VGA style",
          "none: nested offset ellipses give deliberate ramp banding" },
        pf_render_vga_elder,
    },
    [PIXEL_FACE_TALKIE_CLOSEUP] = {
        "talkie_closeup", "Talkie Closeup", 0x7A1C0003U,
        { 80, 60, 2, 2, PIXEL_FACE_MOUTH_SPRITE_BANK, PF_SHAPE_COUNT, 23,
          "flat talkie fills with bold black outlines",
          "checker stubble patch only" },
        pf_render_talkie_closeup,
    },
    [PIXEL_FACE_PIXEL_AUTOMATON] = {
        "pixel_automaton", "Pixel Automaton", 0xA0704004U,
        { 80, 60, 2, 2, PIXEL_FACE_MOUTH_SEGMENTS, 0, 18,
          "brushed-metal ramp plus LED on/off colours",
          "vertical brushed-metal banding" },
        pf_render_pixel_automaton,
    },
    [PIXEL_FACE_AMBER_TERMINAL] = {
        "amber_terminal", "Amber Terminal", 0x3E4A0005U,
        { 160, 120, 1, 1, PIXEL_FACE_MOUTH_GLYPH, 9, 7,
          "single amber phosphor ramp on near-black",
          "scanline dimming of odd rows" },
        pf_render_amber_terminal,
    },
    [PIXEL_FACE_POCKET_RPG] = {
        "pocket_rpg", "Pocket RPG Dialogue", 0x90C4E006U,
        { 80, 60, 2, 2, PIXEL_FACE_MOUTH_FLAP, 3, 4,
          "four-shade green handheld palette",
          "checker mixes the two middle shades" },
        pf_render_pocket_rpg,
    },
    [PIXEL_FACE_DITHERED_ROGUE] = {
        "dithered_rogue", "Dithered Rogue", 0xD0060007U,
        { 160, 120, 1, 1, PIXEL_FACE_MOUTH_POLYGON, 0, 2,
          "1-bit ink on black",
          "ordered Bayer 8x8 over procedural grayscale shading" },
        pf_render_dithered_rogue,
    },
    [PIXEL_FACE_ATKINSON_PORTRAIT] = {
        "atkinson_portrait", "Atkinson Portrait", 0xA7C10008U,
        { 160, 120, 1, 1, PIXEL_FACE_MOUTH_POLYGON, 0, 2,
          "1-bit ink on paper white",
          "Atkinson error diffusion (6/8 of error, serial scan)" },
        pf_render_atkinson_portrait,
    },
    [PIXEL_FACE_ZX_ATTRIBUTE] = {
        "zx_attribute", "ZX Attribute Bard", 0x2A0A7009U,
        { 160, 120, 1, 1, PIXEL_FACE_MOUTH_SPRITE_BANK, PF_SHAPE_COUNT, 15,
          "15-colour ink/paper set, two colours per 8x8 cell",
          "post-pass per-cell colour resolve produces authentic clash" },
        pf_render_zx_attribute,
    },
    [PIXEL_FACE_CGA_ARCADE] = {
        "cga_arcade", "CGA Arcade Cadet", 0xC6AA000AU,
        { 80, 60, 2, 2, PIXEL_FACE_MOUTH_SPRITE_BANK, PF_SHAPE_COUNT, 4,
          "CGA palette 1 high intensity (black/cyan/magenta/white)",
          "diagonal cross-hatch fills at 25/50/75 percent" },
        pf_render_cga_arcade,
    },
    [PIXEL_FACE_NES_TILE] = {
        "nes_tile", "NES Tile Minstrel", 0x4E5B000BU,
        { 80, 60, 2, 2, PIXEL_FACE_MOUTH_SPRITE_BANK, PF_SHAPE_COUNT, 13,
          "13 console-approximate colours, 3-colour subpalettes",
          "post-pass 16x16 attribute-block remap (visible at edges)" },
        pf_render_nes_tile,
    },
    [PIXEL_FACE_C64_MULTICOLOR] = {
        "c64_multicolor", "C64 Multicolor Punk", 0xC64F000CU,
        { 80, 120, 2, 1, PIXEL_FACE_MOUTH_SPRITE_BANK, PF_SHAPE_COUNT, 16,
          "16-colour breadbin palette with raster-bar backdrop",
          "double-wide multicolor pixels (2x1)" },
        pf_render_c64_multicolor,
    },
};

size_t pixel_face_profile_count(void) { return PIXEL_FACE_PROFILE_COUNT; }

static const pf_profile_entry_t *pf_profile_get(pixel_face_profile_t p) {
    if ((int)p < 0 || p >= PIXEL_FACE_PROFILE_COUNT) {
        return 0;
    }
    return &pf_profiles[p];
}

const char *pixel_face_profile_slug(pixel_face_profile_t profile) {
    const pf_profile_entry_t *e = pf_profile_get(profile);
    return e ? e->slug : 0;
}

const char *pixel_face_profile_name(pixel_face_profile_t profile) {
    const pf_profile_entry_t *e = pf_profile_get(profile);
    return e ? e->name : 0;
}

bool pixel_face_profile_style(
    pixel_face_profile_t profile, pixel_face_style_t *style) {
    const pf_profile_entry_t *e = pf_profile_get(profile);
    if (!e || !style) {
        return false;
    }
    *style = e->style;
    return true;
}

bool pixel_face_render(
    pixel_face_profile_t profile,
    const face_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity) {
    const pf_profile_entry_t *e = pf_profile_get(profile);
    if (!e || !keyframe || !rgb565 ||
        pixel_capacity < (size_t)PIXEL_FACE_PIXEL_COUNT) {
        return false;
    }
    pf_rig_t rig;
    pf_rig_compute(&rig, keyframe, sample_clock, e->salt);
    e->render(rgb565, keyframe, &rig, sample_clock);
    return true;
}
