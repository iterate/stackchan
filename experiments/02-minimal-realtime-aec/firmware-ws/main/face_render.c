#include "face_render.h"
#include "face_abstract_redux.h"
#include "face_closeup_toon_actors.h"
#include "face_cyber_wildcards.h"
#include "face_eye_actors.h"
#include "face_eye_study_redux.h"
#include "face_mouth_actors.h"
#include "face_mouth_study_redux.h"
#include "face_pixel_pack.h"
#include "face_pixel_redux_actors.h"
#include "face_robot_eyes.h"
#include "face_robot_redux_actors.h"
#include "face_salvage_redux_actors.h"
#include "face_sprite_actors.h"
#include "face_sprite_mossling.h"
#include "face_sprite_redux_actors.h"
#include "face_sprite_showcase.h"
#include "face_sprite_sheet.h"
#include "face_stage.h"
#include "fta.h"
#include "../../tools/face-grid/contrib/fable_expression_actors_v3/src/fea.h"

#include <limits.h>
#include <string.h>

#define RGB565(r, g, b)                                                   \
    ((uint16_t)((((uint16_t)(r) & 0xf8U) << 8U) |                         \
                (((uint16_t)(g) & 0xfcU) << 3U) |                         \
                ((uint16_t)(b) >> 3U)))

typedef struct {
    const char *slug;
    const char *name;
    uint8_t family;
    uint8_t mouth_kind;
    uint8_t flags;
    uint8_t work_width;
    uint8_t work_height;
    uint16_t estimated_ops_per_pixel;
} profile_description_t;

typedef struct {
    uint16_t *pixels;
    int16_t width;
    int16_t height;
} canvas_t;

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

typedef struct {
    int16_t gaze_x;
    int16_t gaze_y;
    int16_t brow;
    int16_t brow_inner;
    int16_t brow_outer_left;
    int16_t brow_outer_right;
    int16_t breathe;
    int16_t bob;
    int16_t tilt;
    uint8_t cheek;
    uint8_t blink;
    uint8_t pulse;
} motion_t;

static const profile_description_t PROFILES[FACE_RENDER_PROFILE_COUNT] = {
    [FACE_RENDER_EGA_QUEST] = {
        "ega-quest", "EGA quest portrait",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 7,
    },
    [FACE_RENDER_VGA_ELDER] = {
        "vga-elder", "VGA adventure elder",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 9,
    },
    [FACE_RENDER_TALKIE_CLOSEUP] = {
        "talkie-closeup", "Talkie close-up",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 8,
    },
    [FACE_RENDER_PIXEL_AUTOMATON] = {
        "pixel-automaton", "Pixel automaton",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SEGMENTS,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 7,
    },
    [FACE_RENDER_AMBER_TERMINAL] = {
        "amber-terminal", "Amber terminal portrait",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 5,
    },
    [FACE_RENDER_POCKET_RPG] = {
        "pocket-rpg", "Pocket RPG companion",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 7,
    },
    [FACE_RENDER_DITHERED_ROGUE] = {
        "dithered-rogue", "Dithered rogue",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 9,
    },
    [FACE_RENDER_VECTOR_ROUNDED] = {
        "vector-rounded", "Vector-like rounded eyes",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 5,
    },
    [FACE_RENDER_COZMO_CUBIC] = {
        "cozmo-cubic", "Cozmo-like cubic eyes",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_SEGMENTS,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 7,
    },
    [FACE_RENDER_ROBOEYES_ALERT] = {
        "roboeyes-alert", "RoboEyes-style alert",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 5,
    },
    [FACE_RENDER_ROBOEYES_SOFT] = {
        "roboeyes-soft", "RoboEyes-style friendly",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 5,
    },
    [FACE_RENDER_M5_AVATAR_CLASSIC] = {
        "m5-avatar-classic", "M5 avatar-style classic",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_ELLIPSE,
        FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 6,
    },
    [FACE_RENDER_M5_AVATAR_MANGA] = {
        "m5-avatar-manga", "M5 avatar-style manga",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_POLYGON_MOUTH | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 7,
    },
    [FACE_RENDER_EVE_MINIMAL] = {
        "eve-minimal", "Minimal luminous eyes",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 5,
    },
    [FACE_RENDER_JIBO_ORB] = {
        "jibo-orb", "Social robot orb",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_SEGMENTS,
        FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 8,
    },
    [FACE_RENDER_SACCADE_LAB] = {
        "saccade-lab", "Saccade laboratory",
        FACE_RENDER_FAMILY_EYES, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 6,
    },
    [FACE_RENDER_BROW_DIALOGUE] = {
        "brow-dialogue", "Brow dialogue study",
        FACE_RENDER_FAMILY_EYES, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 6,
    },
    [FACE_RENDER_LID_ANTICIPATION] = {
        "lid-anticipation", "Lid anticipation study",
        FACE_RENDER_FAMILY_EYES, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 7,
    },
    [FACE_RENDER_IRIS_PARALLAX] = {
        "iris-parallax", "Iris parallax study",
        FACE_RENDER_FAMILY_EYES, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 9,
    },
    [FACE_RENDER_SLEEP_WAKE] = {
        "sleep-wake", "Sleep and wake study",
        FACE_RENDER_FAMILY_EYES, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 5,
    },
    [FACE_RENDER_CURIOUS_TILT] = {
        "curious-tilt", "Curious asymmetric gaze",
        FACE_RENDER_FAMILY_EYES, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 6,
    },
    [FACE_RENDER_DOT_MATRIX_EYES] = {
        "dot-matrix-eyes", "Dot-matrix expressions",
        FACE_RENDER_FAMILY_EYES, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_EYE_FOCUS |
            FACE_RENDER_FLAG_IDLE_MOTION | FACE_RENDER_FLAG_NO_MOUTH,
        80, 60, 5,
    },
    [FACE_RENDER_CAT_OPTICS] = {
        "cat-optics", "Cat optic study",
        FACE_RENDER_FAMILY_EYES, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 7,
    },
    [FACE_RENDER_PRESTON_SPRITES] = {
        "preston-sprites", "Nine-shape sprite mouth",
        FACE_RENDER_FAMILY_MOUTH, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 6,
    },
    [FACE_RENDER_POLYGON_JALI] = {
        "polygon-jali", "Jaw/lip polygon",
        FACE_RENDER_FAMILY_MOUTH, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_POLYGON_MOUTH | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 7,
    },
    [FACE_RENDER_BEZIER_RIBBON] = {
        "bezier-ribbon", "Bezier ribbon mouth",
        FACE_RENDER_FAMILY_MOUTH, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 8,
    },
    [FACE_RENDER_TEETH_TONGUE] = {
        "teeth-tongue", "Teeth and tongue mask",
        FACE_RENDER_FAMILY_MOUTH, FACE_RENDER_MOUTH_ELLIPSE,
        FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 8,
    },
    [FACE_RENDER_LED_VU_MOUTH] = {
        "led-vu-mouth", "Segmented VU mouth",
        FACE_RENDER_FAMILY_MOUTH, FACE_RENDER_MOUTH_SEGMENTS,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 4,
    },
    [FACE_RENDER_ORIGAMI_MASK] = {
        "origami-mask", "Origami polygon mask",
        FACE_RENDER_FAMILY_MOUTH, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_POLYGON_MOUTH | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 8,
    },
    [FACE_RENDER_NEON_SDF_CYAN] = {
        "neon-sdf-cyan", "Cyan SDF glow",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_SDF,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 24,
    },
    [FACE_RENDER_NEON_SDF_MAGENTA] = {
        "neon-sdf-magenta", "Magenta SDF glow",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_SDF,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 24,
    },
    [FACE_RENDER_LIQUID_SMIN] = {
        "liquid-smin", "Liquid blended presence",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_SDF,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 29,
    },
    [FACE_RENDER_CRT_CHROMATIC] = {
        "crt-chromatic", "Chromatic CRT ghost",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_POLYGON_MOUTH | FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 27,
    },
    [FACE_RENDER_HOLO_WIREFRAME] = {
        "holo-wireframe", "Holographic wireframe",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 11,
    },
    [FACE_RENDER_VOICE_ORB] = {
        "voice-orb", "Reactive voice orb",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_IDLE_MOTION | FACE_RENDER_FLAG_NO_MOUTH,
        80, 60, 25,
    },
    [FACE_RENDER_RED_OPTIC] = {
        "red-optic", "Single red optic",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        80, 60, 22,
    },
    [FACE_RENDER_HUB75_NEON] = {
        "hub75-neon", "HUB75 neon matrix",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_SEGMENTS,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_HALF_RES | FACE_RENDER_FLAG_IDLE_MOTION,
        64, 48, 18,
    },
    [FACE_RENDER_EDGE_GLOW] = {
        "edge-glow", "Assistant edge glow",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_IDLE_MOTION | FACE_RENDER_FLAG_NO_MOUTH,
        80, 60, 18,
    },
    [FACE_RENDER_GLITCH_MASK] = {
        "glitch-mask", "Glitch polygon mask",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_POLYGON_MOUTH | FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 26,
    },
    [FACE_RENDER_PALETTE_PLASMA] = {
        "palette-plasma", "Palette plasma face",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_SDF,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 28,
    },
    [FACE_RENDER_ROBOT_RIG_VECTOR_ROUNDED] = {
        "robot-rig-vector-rounded", "Robot rig · Vector rounded",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 11,
    },
    [FACE_RENDER_ROBOT_RIG_COZMO_CUBIC] = {
        "robot-rig-cozmo-cubic", "Robot rig · Cozmo cubic",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 10,
    },
    [FACE_RENDER_ROBOT_RIG_BROW_DIALOGUE] = {
        "robot-rig-brow-dialogue", "Robot rig · Brow dialogue",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 10,
    },
    [FACE_RENDER_ROBOT_RIG_SLEEP_WAKE] = {
        "robot-rig-sleep-wake", "Robot rig · Sleep / wake",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 10,
    },
    [FACE_RENDER_ROBOT_RIG_IRIS_PARALLAX] = {
        "robot-rig-iris-parallax", "Robot rig · Iris parallax",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 13,
    },
    [FACE_RENDER_ROBOT_RIG_CAT_OPTICS] = {
        "robot-rig-cat-optics", "Robot rig · Cat optics",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_NONE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH,
        160, 120, 13,
    },
    [FACE_RENDER_ROBOT_RIG_M5_MANGA] = {
        "robot-rig-m5-manga", "Robot rig · M5 manga",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_LINE,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 9,
    },
    [FACE_RENDER_SPRITE_VGA_STAR_NAVIGATOR] = {
        "sprite-vga-star-navigator", "Sprite · VGA star navigator",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 12,
    },
    [FACE_RENDER_SPRITE_POCKET_RELAY_CREATURE] = {
        "sprite-pocket-relay-creature", "Sprite · Pocket relay creature",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 11,
    },
    [FACE_RENDER_PIXEL_PACK_EGA_QUEST] = {
        "pixel-pack-ega-quest", "Pixel pack · EGA quest",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 7,
    },
    [FACE_RENDER_PIXEL_PACK_VGA_ELDER] = {
        "pixel-pack-vga-elder", "Pixel pack · VGA elder",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_POLYGON_MOUTH | FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 10,
    },
    [FACE_RENDER_PIXEL_PACK_TALKIE_CLOSEUP] = {
        "pixel-pack-talkie-closeup", "Pixel pack · Talkie close-up",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 8,
    },
    [FACE_RENDER_PIXEL_PACK_DITHERED_ROGUE] = {
        "pixel-pack-dithered-rogue", "Pixel pack · Dithered rogue",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 13,
    },
    [FACE_RENDER_TOON_BEAN] = {
        "toon-bean", "Toon Bean acting rig",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 11,
    },
    [FACE_RENDER_TOON_INK] = {
        "toon-ink", "Toon Ink acting rig",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 9,
    },
    [FACE_RENDER_TOON_EMBER] = {
        "toon-ember", "Toon Ember acting rig",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 10,
    },
    [FACE_RENDER_SPRITE_ACTOR_EGA_COURT_MAGE] = {
        "sprite-actor-ega-court-mage", "Sprite actor · EGA court mage",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 8,
    },
    [FACE_RENDER_SPRITE_ACTOR_VGA_STAR_CAPTAIN] = {
        "sprite-actor-vga-star-captain", "Sprite actor · VGA star captain",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 8,
    },
    [FACE_RENDER_SPRITE_ACTOR_TALKIE_MOON_MECHANIC] = {
        "sprite-actor-talkie-moon-mechanic",
        "Sprite actor · Talkie moon mechanic",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 8,
    },
    [FACE_RENDER_SPRITE_ACTOR_JRPG_STORM_FAMILIAR] = {
        "sprite-actor-jrpg-storm-familiar",
        "Sprite actor · JRPG storm familiar",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 8,
    },
    [FACE_RENDER_SPRITE_ACTOR_HANDHELD_FOREST_PET] = {
        "sprite-actor-handheld-forest-pet",
        "Sprite actor · Handheld forest pet",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 8,
    },
    [FACE_RENDER_SPRITE_ACTOR_ARCADE_CHROME_PILOT] = {
        "sprite-actor-arcade-chrome-pilot",
        "Sprite actor · Arcade chrome pilot",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED | FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 8,
    },
    [FACE_RENDER_ACTOR_MOCHI_CAT] = {
        "fea-mochi-cat", "Mochi Cat · plush actor",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 11,
    },
    [FACE_RENDER_ACTOR_KARAKURI_BRASS] = {
        "fea-karakuri-brass", "Karakuri Brass · plate puppet",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 12,
    },
    [FACE_RENDER_ACTOR_EMOTE_STICKER] = {
        "fea-emote-sticker", "Emote Sticker · badge actor",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 11,
    },
    [FACE_RENDER_ACTOR_WILL_O_WISP] = {
        "fea-will-o-wisp", "Will-o-Wisp · night spirit",
        FACE_RENDER_FAMILY_TOON, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 13,
    },
    [FACE_RENDER_ACTOR_MONO_SCOPE] = {
        "fea-mono-scope", "Mono Scope · cyclops robot",
        FACE_RENDER_FAMILY_ROBOT, FACE_RENDER_MOUTH_POLYGON,
        FACE_RENDER_FLAG_EYE_FOCUS | FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        160, 120, 12,
    },
    [FACE_RENDER_SHADER_AURORA_FAMILIAR] = {
        "shader-aurora-familiar", "Shader actor · Aurora SDF familiar",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_SDF,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_EYE_FOCUS |
            FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 25,
    },
    [FACE_RENDER_SHADER_LIQUID_CHROMA] = {
        "shader-liquid-chroma", "Shader actor · Liquid chroma familiar",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_SDF,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_EYE_FOCUS |
            FACE_RENDER_FLAG_POLYGON_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 29,
    },
    [FACE_RENDER_SHADER_CRT_GHOST] = {
        "shader-crt-ghost", "Shader actor · CRT ghost",
        FACE_RENDER_FAMILY_CYBER, FACE_RENDER_MOUTH_SEGMENTS,
        FACE_RENDER_FLAG_SHADER | FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_EYE_FOCUS |
            FACE_RENDER_FLAG_IDLE_MOTION,
        80, 60, 23,
    },
    [FACE_RENDER_SPRITE_SHEET_POCKET_MOSSLING] = {
        "sprite-sheet-pocket-mossling",
        "Pocket Mossling · authored DMG sprite",
        FACE_RENDER_FAMILY_PIXEL, FACE_RENDER_MOUTH_SPRITE,
        FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_HALF_RES,
        80, 60, 7,
    },
};

static const char *const FAMILY_NAMES[] = {
    "Pixel adventure",
    "Robot / avatar",
    "Eye studies",
    "Mouth geometry",
    "Cyber software shader",
    "Toon acting rigs",
};

static int16_t clamp_i16(int32_t value, int16_t low, int16_t high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return (int16_t)value;
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int32_t min_i32(int32_t left, int32_t right)
{
    return left < right ? left : right;
}

static int32_t max_i32(int32_t left, int32_t right)
{
    return left > right ? left : right;
}

static uint32_t hash_u32(uint32_t value)
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

static uint8_t triangle_u8(uint32_t tick, uint32_t period)
{
    if (period < 2U) {
        return 0U;
    }
    const uint32_t phase = tick % period;
    const uint32_t half = period / 2U;
    if (phase <= half) {
        return (uint8_t)((phase * 255U) / max_i32((int32_t)half, 1));
    }
    return (uint8_t)(
        ((period - phase) * 255U) /
        max_i32((int32_t)(period - half), 1));
}

static int16_t lerp_i16(
    int16_t from, int16_t to, uint16_t amount)
{
    return (int16_t)(
        from + (((int32_t)to - from) * amount) / 255);
}

static uint8_t mul_u8(uint8_t left, uint8_t right)
{
    return (uint8_t)(((uint16_t)left * right + 127U) / 255U);
}

static uint16_t smoothstep_q8(uint16_t value)
{
    const uint32_t x = min_i32(value, 255);
    return (uint16_t)(
        (x * x * (765U - 2U * x) + 32512U) / 65025U);
}

static uint32_t integer_sqrt(uint32_t value)
{
    uint32_t result = 0U;
    uint32_t bit = 1UL << 30U;
    while (bit > value) {
        bit >>= 2U;
    }
    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return result;
}

static uint16_t blend565(
    uint16_t background, uint16_t foreground, uint8_t alpha)
{
    const uint32_t inverse = 255U - alpha;
    const uint32_t br = (background >> 11U) & 31U;
    const uint32_t bg = (background >> 5U) & 63U;
    const uint32_t bb = background & 31U;
    const uint32_t fr = (foreground >> 11U) & 31U;
    const uint32_t fg = (foreground >> 5U) & 63U;
    const uint32_t fb = foreground & 31U;
    return (uint16_t)(
        ((((br * inverse + fr * alpha) / 255U) & 31U) << 11U) |
        ((((bg * inverse + fg * alpha) / 255U) & 63U) << 5U) |
        (((bb * inverse + fb * alpha) / 255U) & 31U));
}

static void canvas_clear(canvas_t *canvas, uint16_t color)
{
    for (int32_t index = 0;
         index < (int32_t)canvas->width * canvas->height;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void put_pixel(
    canvas_t *canvas, int32_t x, int32_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) {
        return;
    }
    canvas->pixels[y * canvas->width + x] = color;
}

static void blend_pixel(
    canvas_t *canvas,
    int32_t x,
    int32_t y,
    uint16_t color,
    uint8_t alpha)
{
    if (x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) {
        return;
    }
    uint16_t *pixel = &canvas->pixels[y * canvas->width + x];
    *pixel = blend565(*pixel, color, alpha);
}

static void fill_rect(
    canvas_t *canvas,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color)
{
    const int32_t left = max_i32(x, 0);
    const int32_t top = max_i32(y, 0);
    const int32_t right = min_i32(x + width, canvas->width);
    const int32_t bottom = min_i32(y + height, canvas->height);
    for (int32_t py = top; py < bottom; ++py) {
        uint16_t *row = &canvas->pixels[py * canvas->width + left];
        for (int32_t px = left; px < right; ++px) {
            *row++ = color;
        }
    }
}

static void draw_line(
    canvas_t *canvas,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t thickness,
    uint16_t color)
{
    const int32_t dx = abs_i32(x1 - x0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t dy = -abs_i32(y1 - y0);
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    const int32_t radius = max_i32(0, thickness - 1) / 2;
    for (;;) {
        fill_rect(
            canvas, x0 - radius, y0 - radius,
            radius * 2 + 1, radius * 2 + 1, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int32_t twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void fill_ellipse(
    canvas_t *canvas,
    int32_t center_x,
    int32_t center_y,
    int32_t radius_x,
    int32_t radius_y,
    uint16_t color)
{
    if (radius_x <= 0 || radius_y <= 0) {
        return;
    }
    const uint32_t ry2 = (uint32_t)(radius_y * radius_y);
    const uint32_t rx2 = (uint32_t)(radius_x * radius_x);
    for (int32_t dy = -radius_y; dy <= radius_y; ++dy) {
        const uint32_t y2 = (uint32_t)(dy * dy);
        const uint32_t term =
            (rx2 * (ry2 - min_i32((int32_t)y2, (int32_t)ry2))) /
            ry2;
        const int32_t half = (int32_t)integer_sqrt(term);
        fill_rect(
            canvas, center_x - half, center_y + dy,
            half * 2 + 1, 1, color);
    }
}

static void fill_ellipse_blend(
    canvas_t *canvas,
    int32_t center_x,
    int32_t center_y,
    int32_t radius_x,
    int32_t radius_y,
    uint16_t color,
    uint8_t alpha)
{
    if (radius_x <= 0 || radius_y <= 0) {
        return;
    }
    const int64_t rx2 = (int64_t)radius_x * radius_x;
    const int64_t ry2 = (int64_t)radius_y * radius_y;
    const int64_t bound = rx2 * ry2;
    for (int32_t y = -radius_y; y <= radius_y; ++y) {
        for (int32_t x = -radius_x; x <= radius_x; ++x) {
            if ((int64_t)x * x * ry2 +
                    (int64_t)y * y * rx2 <=
                bound) {
                blend_pixel(
                    canvas, center_x + x, center_y + y,
                    color, alpha);
            }
        }
    }
}

static void fill_round_rect(
    canvas_t *canvas,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    int32_t radius,
    uint16_t color)
{
    radius = min_i32(radius, min_i32(width / 2, height / 2));
    if (radius <= 0) {
        fill_rect(canvas, x, y, width, height, color);
        return;
    }
    fill_rect(canvas, x + radius, y, width - radius * 2, height, color);
    fill_rect(canvas, x, y + radius, radius, height - radius * 2, color);
    fill_rect(
        canvas, x + width - radius, y + radius,
        radius, height - radius * 2, color);
    fill_ellipse(canvas, x + radius, y + radius, radius, radius, color);
    fill_ellipse(
        canvas, x + width - radius - 1, y + radius,
        radius, radius, color);
    fill_ellipse(
        canvas, x + radius, y + height - radius - 1,
        radius, radius, color);
    fill_ellipse(
        canvas, x + width - radius - 1,
        y + height - radius - 1, radius, radius, color);
}

static void fill_polygon(
    canvas_t *canvas,
    const point_t *points,
    size_t count,
    uint16_t color)
{
    if (count < 3U || count > 16U) {
        return;
    }
    int16_t min_y = INT16_MAX;
    int16_t max_y = INT16_MIN;
    for (size_t index = 0; index < count; ++index) {
        min_y = (int16_t)min_i32(min_y, points[index].y);
        max_y = (int16_t)max_i32(max_y, points[index].y);
    }
    for (int32_t y = max_i32(min_y, 0);
         y <= min_i32(max_y, canvas->height - 1);
         ++y) {
        int16_t crossings[16];
        size_t crossing_count = 0U;
        for (size_t index = 0; index < count; ++index) {
            const point_t first = points[index];
            const point_t second = points[(index + 1U) % count];
            if ((first.y <= y && second.y > y) ||
                (second.y <= y && first.y > y)) {
                crossings[crossing_count++] = (int16_t)(
                    first.x +
                    ((int32_t)(y - first.y) *
                     (second.x - first.x)) /
                        (second.y - first.y));
            }
        }
        for (size_t left = 1U; left < crossing_count; ++left) {
            const int16_t value = crossings[left];
            size_t insert = left;
            while (insert > 0U && crossings[insert - 1U] > value) {
                crossings[insert] = crossings[insert - 1U];
                --insert;
            }
            crossings[insert] = value;
        }
        for (size_t index = 0U; index + 1U < crossing_count; index += 2U) {
            fill_rect(
                canvas, crossings[index], y,
                crossings[index + 1U] - crossings[index] + 1,
                1, color);
        }
    }
}

static void draw_quadratic(
    canvas_t *canvas,
    point_t start,
    point_t control,
    point_t end,
    int32_t thickness,
    uint16_t color)
{
    point_t previous = start;
    for (int32_t step = 1; step <= 20; ++step) {
        const int32_t t = (step * 256) / 20;
        const int32_t inverse = 256 - t;
        const point_t current = {
            .x = (int16_t)(
                (inverse * inverse * start.x +
                 2 * inverse * t * control.x +
                 t * t * end.x) >>
                16),
            .y = (int16_t)(
                (inverse * inverse * start.y +
                 2 * inverse * t * control.y +
                 t * t * end.y) >>
                16),
        };
        draw_line(
            canvas, previous.x, previous.y,
            current.x, current.y, thickness, color);
        previous = current;
    }
}

static void draw_glow_ellipse(
    canvas_t *canvas,
    int32_t x,
    int32_t y,
    int32_t radius_x,
    int32_t radius_y,
    uint16_t color,
    uint8_t strength)
{
    for (int32_t spread = 7; spread >= 1; spread -= 2) {
        const uint8_t alpha =
            (uint8_t)((uint16_t)strength / (uint16_t)(spread + 2));
        fill_ellipse_blend(
            canvas, x, y, radius_x + spread, radius_y + spread,
            color, alpha);
    }
    fill_ellipse_blend(
        canvas, x, y, radius_x, radius_y, color,
        (uint8_t)min_i32(255, strength + 80));
}

static void draw_dither(
    canvas_t *canvas,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height,
    uint16_t color,
    uint8_t coverage)
{
    static const uint8_t bayer[16] = {
        0, 8, 2, 10,
        12, 4, 14, 6,
        3, 11, 1, 9,
        15, 7, 13, 5,
    };
    for (int32_t py = y; py < y + height; ++py) {
        for (int32_t px = x; px < x + width; ++px) {
            const uint8_t threshold =
                (uint8_t)(bayer[((py & 3) << 2) | (px & 3)] * 16);
            if (coverage > threshold) {
                put_pixel(canvas, px, py, color);
            }
        }
    }
}

static motion_t motion_for(
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const uint32_t frame =
        (uint32_t)(((uint64_t)sample_clock * 30U) / 16000U);
    const uint32_t gaze_period =
        74U + ((uint32_t)profile % 6U) * 11U;
    const uint32_t gaze_epoch = frame / gaze_period;
    const uint32_t gaze_phase = frame % gaze_period;
    const uint32_t current_hash =
        hash_u32(gaze_epoch + (uint32_t)profile * 0x9e3779b9U);
    const uint32_t previous_hash =
        hash_u32(
            (gaze_epoch == 0U ? 0U : gaze_epoch - 1U) +
            (uint32_t)profile * 0x9e3779b9U);
    const int16_t current_x =
        (int16_t)((int32_t)((current_hash >> 3U) & 15U) - 7);
    const int16_t current_y =
        (int16_t)((int32_t)((current_hash >> 11U) & 11U) - 5);
    const int16_t previous_x =
        (int16_t)((int32_t)((previous_hash >> 3U) & 15U) - 7);
    const int16_t previous_y =
        (int16_t)((int32_t)((previous_hash >> 11U) & 11U) - 5);
    uint16_t saccade = 255U;
    if (gaze_phase < 13U) {
        saccade = smoothstep_q8(
            (uint16_t)(gaze_phase * 255U / 12U));
    } else if (gaze_phase == 13U) {
        saccade = 264U;
    } else if (gaze_phase == 14U) {
        saccade = 258U;
    }

    const uint32_t blink_period =
        94U + ((uint32_t)profile % 7U) * 13U;
    const uint32_t blink_phase =
        (frame + (uint32_t)profile * 17U) % blink_period;
    uint8_t blink = 255U;
    if (blink_phase >= 1U && blink_phase <= 16U) {
        static const uint8_t blink_curve[] = {
            248U, 230U, 202U, 164U, 120U, 74U, 34U, 14U,
            28U, 54U, 88U, 128U, 168U, 204U, 232U, 248U,
        };
        blink = blink_curve[blink_phase - 1U];
    }
    if (((profile + 1) % 6) == 0 &&
        blink_phase >= 19U && blink_phase <= 29U) {
        static const uint8_t double_blink[] = {
            238U, 204U, 158U, 104U, 52U, 18U,
            42U, 88U, 142U, 194U, 232U,
        };
        blink = double_blink[blink_phase - 19U];
    }

    const uint8_t breathe_wave =
        triangle_u8(frame + (uint32_t)profile * 9U, 112U);
    const int16_t audio_brow =
        (int16_t)(((int32_t)keyframe->mouth_open * 13) / 255);
    const int16_t idle_brow =
        (int16_t)((int32_t)((current_hash >> 19U) & 7U) - 3);
    return (motion_t){
        .gaze_x = (int16_t)(
            lerp_i16(previous_x, current_x, saccade) +
            keyframe->look_x / 3 + render_key->head_yaw / 12),
        .gaze_y = (int16_t)(
            lerp_i16(previous_y, current_y, saccade) +
            keyframe->look_y / 3 + render_key->head_pitch / 16),
        .brow = (int16_t)(
            clamp_i16(
                keyframe->brow / 4 + audio_brow + idle_brow,
                      -18, 24)),
        .brow_inner = render_key->brow_inner,
        .brow_outer_left = render_key->brow_outer_left,
        .brow_outer_right = render_key->brow_outer_right,
        .breathe = (int16_t)((int32_t)breathe_wave - 128),
        .bob = (int16_t)(
            ((int32_t)keyframe->mouth_open * 4) / 255 +
            ((int32_t)breathe_wave - 128) / 64 +
            render_key->body_lean_y / 12 +
            render_key->head_pitch / 18),
        .tilt = (int16_t)(
            ((int32_t)((current_hash >> 24U) & 7U) - 3) +
            keyframe->look_x / 8 + render_key->head_roll / 8 +
            render_key->body_lean_x / 12),
        .cheek = render_key->cheek,
        .blink = blink,
        .pulse = (uint8_t)(
            ((uint16_t)triangle_u8(frame, 28U) +
             render_key->affect_arousal) /
            2U),
    };
}

static uint8_t eye_open(
    uint8_t keyframe_open, const motion_t *motion)
{
    return mul_u8(keyframe_open, motion->blink);
}

static uint8_t mouth_sprite_index(const face_render_key_t *render_key)
{
    static const uint8_t ovr15_to_sprite[FACE_VISEME_COUNT] = {
        [FACE_VISEME_AA] = 3U,
        [FACE_VISEME_E] = 6U,
        [FACE_VISEME_I] = 8U,
        [FACE_VISEME_O] = 4U,
        [FACE_VISEME_U] = 5U,
        [FACE_VISEME_PP] = 1U,
        [FACE_VISEME_SS] = 8U,
        [FACE_VISEME_TH] = 7U,
        [FACE_VISEME_DD] = 2U,
        [FACE_VISEME_FF] = 7U,
        [FACE_VISEME_KK] = 2U,
        [FACE_VISEME_NN] = 2U,
        [FACE_VISEME_RR] = 5U,
        [FACE_VISEME_CH] = 8U,
        [FACE_VISEME_SIL] = 0U,
    };
    static const uint8_t vrm5_to_sprite[5] = {
        3U, /* A */
        8U, /* I */
        5U, /* U */
        6U, /* E */
        4U, /* O */
    };
    static const uint8_t microsoft22_to_sprite[22] = {
        0U, 3U, 3U, 4U, 6U, 5U, 8U, 5U, 4U, 3U, 4U,
        8U, 2U, 5U, 2U, 8U, 8U, 7U, 7U, 2U, 2U, 1U,
    };
    uint8_t viseme = render_key->viseme;
    if (render_key->viseme_secondary != FACE_VISEME_NONE &&
        render_key->viseme_blend >= 128U) {
        viseme = render_key->viseme_secondary;
    }
    if (render_key->viseme_weight >= 24U) {
        switch (render_key->viseme_set) {
        case FACE_VISEME_SET_OVR15:
            if (viseme < FACE_VISEME_COUNT) {
                return ovr15_to_sprite[viseme];
            }
            break;
        case FACE_VISEME_SET_VRM5:
            if (viseme < sizeof(vrm5_to_sprite)) {
                return vrm5_to_sprite[viseme];
            }
            break;
        case FACE_VISEME_SET_PRESTON9:
            if (viseme < 9U) {
                return viseme;
            }
            break;
        case FACE_VISEME_SET_MICROSOFT22:
            if (viseme < sizeof(microsoft22_to_sprite)) {
                return microsoft22_to_sprite[viseme];
            }
            break;
        default:
            break;
        }
    }
    const face_keyframe_t *keyframe = &render_key->controls;
    if (keyframe->mouth_open < 18U) {
        return keyframe->mouth_press > 110U ? 1U : 0U;
    }
    if (keyframe->mouth_teeth > 165U) {
        return keyframe->mouth_round > 120U ? 7U : 6U;
    }
    if (keyframe->mouth_round > 180U) {
        return keyframe->mouth_open > 135U ? 4U : 5U;
    }
    if (keyframe->mouth_width > 175U) {
        return keyframe->mouth_open > 125U ? 3U : 8U;
    }
    return 2U;
}

static face_render_key_t evaluate_expression_actions(
    const face_render_key_t *source)
{
    face_render_key_t result = *source;
    face_keyframe_t *controls = &result.controls;
    const uint8_t weight = source->expression_weight;
    const int32_t corner_average =
        ((int32_t)source->mouth_corner_left +
         (int32_t)source->mouth_corner_right) /
        2;
    const int32_t affect_smile =
        corner_average != 0 ? corner_average
                            : (int32_t)source->affect_valence / 2;
    controls->mouth_width = (uint8_t)clamp_i16(
        (int32_t)controls->mouth_width + affect_smile / 2,
        0, 255);
    controls->mouth_round = (uint8_t)clamp_i16(
        (int32_t)controls->mouth_round - affect_smile / 3,
        0, 255);
    controls->mouth_teeth = (uint8_t)max_i32(
        controls->mouth_teeth,
        (int32_t)source->tongue * 3 / 5);
    controls->eye_left_open = mul_u8(
        controls->eye_left_open,
        (uint8_t)(255U - source->eye_left_squint * 3U / 4U));
    controls->eye_right_open = mul_u8(
        controls->eye_right_open,
        (uint8_t)(255U - source->eye_right_squint * 3U / 4U));
    controls->brow = (int8_t)clamp_i16(
        (int32_t)controls->brow + source->brow_inner / 2 +
            ((int32_t)source->brow_outer_left +
             source->brow_outer_right) /
                4,
        -127, 127);

#define BLEND_U8(field, target)                                           \
    do {                                                                  \
        const int32_t current_ = (field);                                 \
        (field) = (uint8_t)clamp_i16(                                    \
            current_ +                                                   \
                ((int32_t)(target) - current_) * weight / 255,            \
            0, 255);                                                      \
    } while (0)
#define FLOOR_U8(field, target)                                           \
    do {                                                                  \
        const int32_t target_ = (target);                                 \
        if ((int32_t)(field) < target_) {                                 \
            BLEND_U8((field), target_);                                   \
        }                                                                 \
    } while (0)
#define CEILING_U8(field, target)                                         \
    do {                                                                  \
        const int32_t target_ = (target);                                 \
        if ((int32_t)(field) > target_) {                                 \
            BLEND_U8((field), target_);                                   \
        }                                                                 \
    } while (0)

    /*
     * Stage direction is a performance layer, not a replacement for PCM
     * articulation. Floors retain a more-open audio mouth while giving
     * silent poses a readable silhouette; ceilings make lid acting visible.
     */
    if (weight >= 8U) {
        switch (source->stage_expression) {
        case FACE_EXPRESSION_WARM:
            FLOOR_U8(controls->mouth_width, 188);
            BLEND_U8(controls->mouth_round, 64);
            CEILING_U8(controls->eye_left_open, 224);
            CEILING_U8(controls->eye_right_open, 224);
            break;
        case FACE_EXPRESSION_JOY:
            FLOOR_U8(controls->mouth_width, 224);
            BLEND_U8(controls->mouth_round, 38);
            FLOOR_U8(controls->mouth_open, 62);
            FLOOR_U8(controls->mouth_teeth, 138);
            CEILING_U8(controls->eye_left_open, 190);
            CEILING_U8(controls->eye_right_open, 190);
            break;
        case FACE_EXPRESSION_CONCERN:
            BLEND_U8(controls->mouth_width, 126);
            FLOOR_U8(controls->mouth_round, 92);
            FLOOR_U8(controls->mouth_press, 82);
            FLOOR_U8(controls->eye_left_open, 226);
            FLOOR_U8(controls->eye_right_open, 218);
            break;
        case FACE_EXPRESSION_SURPRISE:
            FLOOR_U8(controls->mouth_open, 196);
            BLEND_U8(controls->mouth_width, 108);
            FLOOR_U8(controls->mouth_round, 216);
            BLEND_U8(controls->mouth_press, 0);
            FLOOR_U8(controls->eye_left_open, 250);
            FLOOR_U8(controls->eye_right_open, 250);
            break;
        case FACE_EXPRESSION_THOUGHTFUL:
            BLEND_U8(controls->mouth_width, 142);
            FLOOR_U8(controls->mouth_round, 102);
            FLOOR_U8(controls->mouth_press, 98);
            CEILING_U8(controls->eye_right_open, 176);
            break;
        case FACE_EXPRESSION_SKEPTICAL:
            BLEND_U8(controls->mouth_width, 138);
            BLEND_U8(controls->mouth_round, 54);
            FLOOR_U8(controls->mouth_press, 126);
            CEILING_U8(controls->eye_left_open, 206);
            CEILING_U8(controls->eye_right_open, 116);
            break;
        case FACE_EXPRESSION_DETERMINED:
            BLEND_U8(controls->mouth_width, 154);
            BLEND_U8(controls->mouth_round, 34);
            FLOOR_U8(controls->mouth_press, 182);
            CEILING_U8(controls->eye_left_open, 148);
            CEILING_U8(controls->eye_right_open, 148);
            break;
        case FACE_EXPRESSION_SLEEPY:
            BLEND_U8(controls->mouth_width, 138);
            FLOOR_U8(controls->mouth_round, 76);
            CEILING_U8(controls->eye_left_open, 62);
            CEILING_U8(controls->eye_right_open, 54);
            break;
        case FACE_EXPRESSION_EXCITED:
            FLOOR_U8(controls->mouth_open, 176);
            FLOOR_U8(controls->mouth_width, 232);
            BLEND_U8(controls->mouth_round, 52);
            FLOOR_U8(controls->mouth_teeth, 184);
            FLOOR_U8(controls->eye_left_open, 232);
            FLOOR_U8(controls->eye_right_open, 232);
            break;
        case FACE_EXPRESSION_EMBARRASSED:
            FLOOR_U8(controls->mouth_width, 178);
            BLEND_U8(controls->mouth_round, 78);
            FLOOR_U8(controls->mouth_open, 28);
            CEILING_U8(controls->eye_left_open, 132);
            CEILING_U8(controls->eye_right_open, 172);
            break;
        default:
            break;
        }
    }
#undef CEILING_U8
#undef FLOOR_U8
#undef BLEND_U8
    return result;
}

static void draw_mouth_sprite(
    canvas_t *canvas,
    int32_t center_x,
    int32_t center_y,
    int32_t scale,
    uint8_t shape,
    uint16_t lip,
    uint16_t interior,
    uint16_t teeth)
{
    static const uint16_t silhouettes[9][7] = {
        {0x000, 0x000, 0x000, 0x7fe, 0x000, 0x000, 0x000},
        {0x000, 0x000, 0x3fc, 0x7fe, 0x3fc, 0x000, 0x000},
        {0x000, 0x1f8, 0x3fc, 0x606, 0x3fc, 0x1f8, 0x000},
        {0x0f0, 0x3fc, 0x7fe, 0x606, 0x606, 0x7fe, 0x3fc},
        {0x0f0, 0x198, 0x30c, 0x30c, 0x30c, 0x198, 0x0f0},
        {0x000, 0x0f0, 0x198, 0x30c, 0x30c, 0x198, 0x0f0},
        {0x000, 0x3fc, 0x7fe, 0x7fe, 0x606, 0x3fc, 0x000},
        {0x000, 0x3fc, 0x7fe, 0x7fe, 0x30c, 0x198, 0x000},
        {0x000, 0x000, 0x7fe, 0x3fc, 0x7fe, 0x000, 0x000},
    };
    shape %= 9U;
    const int32_t left = center_x - 6 * scale;
    const int32_t top = center_y - 3 * scale;
    for (int32_t row = 0; row < 7; ++row) {
        for (int32_t column = 0; column < 12; ++column) {
            if ((silhouettes[shape][row] &
                 (1U << (11 - column))) == 0U) {
                continue;
            }
            uint16_t color = lip;
            if (shape >= 2U && row >= 2 && row <= 4 &&
                column >= 3 && column <= 8) {
                color = interior;
            }
            if ((shape == 6U || shape == 7U) &&
                row == 2 && column >= 2 && column <= 9) {
                color = teeth;
            }
            fill_rect(
                canvas, left + column * scale, top + row * scale,
                scale, scale, color);
        }
    }
}

static void draw_basic_eyes(
    canvas_t *canvas,
    int32_t left_x,
    int32_t right_x,
    int32_t center_y,
    int32_t radius_x,
    int32_t radius_y,
    const motion_t *motion,
    uint8_t left_open,
    uint8_t right_open,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil)
{
    const uint8_t openings[] = {
        eye_open(left_open, motion),
        eye_open(right_open, motion),
    };
    const int32_t centers[] = {left_x, right_x};
    for (size_t eye = 0; eye < 2U; ++eye) {
        const int32_t height =
            max_i32(1, radius_y * openings[eye] / 255);
        fill_ellipse(
            canvas, centers[eye], center_y,
            radius_x, height, sclera);
        if (openings[eye] > 35U) {
            const int32_t gaze_x =
                clamp_i16(motion->gaze_x, -radius_x / 2, radius_x / 2);
            const int32_t gaze_y =
                clamp_i16(motion->gaze_y, -height / 2, height / 2);
            fill_ellipse(
                canvas, centers[eye] + gaze_x, center_y + gaze_y,
                max_i32(2, radius_x / 3),
                max_i32(2, height * 2 / 3), iris);
            fill_ellipse(
                canvas, centers[eye] + gaze_x, center_y + gaze_y,
                max_i32(1, radius_x / 7),
                max_i32(1, height / 3), pupil);
            put_pixel(
                canvas, centers[eye] + gaze_x - 2,
                center_y + gaze_y - 2, RGB565(255, 255, 255));
        }
    }
}

static void draw_brows(
    canvas_t *canvas,
    int32_t left_x,
    int32_t right_x,
    int32_t y,
    int32_t width,
    const motion_t *motion,
    uint16_t color,
    bool angled)
{
    const int32_t base = motion->brow;
    const int32_t inner = motion->brow_inner / 3;
    const int32_t outer_left = motion->brow_outer_left / 3;
    const int32_t outer_right = motion->brow_outer_right / 3;
    const int32_t angle = angled ? 3 + motion->tilt / 2 : 0;
    draw_line(
        canvas, left_x - width,
        y - base - outer_left - angle,
        left_x + width,
        y - base - inner + angle,
        3, color);
    draw_line(
        canvas, right_x - width,
        y - base - inner + angle,
        right_x + width,
        y - base - outer_right - angle,
        3, color);
}

typedef struct {
    uint8_t left_open_percent;
    uint8_t right_open_percent;
    int8_t width_delta;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t brow_lift;
    int8_t brow_outer_left;
    int8_t brow_inner;
    int8_t brow_outer_right;
    uint8_t left_inner_lid;
    uint8_t right_inner_lid;
    uint8_t pupil_percent;
} pixel_eye_pose_t;

/*
 * A tiny authored acting table keeps the five legacy pixel portraits
 * readable at 160x120.  The stage layer chooses the pose while the normal
 * blink still multiplies its opening, so stage directions never freeze the
 * eyes.  Values are percentages/pixels rather than bitmaps and therefore
 * remain cheap enough for the embedded renderer.
 */
static const pixel_eye_pose_t
    PIXEL_EYE_POSES[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] =
        {84U, 84U, 0, 0, 0, 0, 0, 0, 0, 0U, 0U, 100U},
    [FACE_EXPRESSION_WARM] =
        {62U, 62U, 2, 0, 1, 1, 2, 3, 2, 0U, 0U, 100U},
    [FACE_EXPRESSION_JOY] =
        {38U, 38U, 4, 0, 1, 2, 3, 3, 3, 0U, 0U, 112U},
    [FACE_EXPRESSION_CONCERN] =
        {100U, 96U, 4, 0, 3, 1, -2, 7, -2, 0U, 0U, 110U},
    [FACE_EXPRESSION_SURPRISE] =
        {100U, 100U, -2, 0, -1, 5, 6, 6, 6, 0U, 0U, 72U},
    [FACE_EXPRESSION_THOUGHTFUL] =
        {96U, 48U, -2, -5, -3, 2, 5, 3, -2, 0U, 1U, 88U},
    [FACE_EXPRESSION_SKEPTICAL] =
        {90U, 20U, 4, 5, 0, -1, -3, -2, 7, 0U, 3U, 110U},
    [FACE_EXPRESSION_DETERMINED] =
        {42U, 42U, 4, 0, 1, -2, 3, -7, 3, 4U, 4U, 104U},
    [FACE_EXPRESSION_SLEEPY] =
        {22U, 18U, 4, 0, 2, -2, -1, -2, -1, 1U, 1U, 100U},
    [FACE_EXPRESSION_EXCITED] =
        {100U, 100U, 2, 0, -1, 6, 7, 7, 7, 0U, 0U, 82U},
    [FACE_EXPRESSION_EMBARRASSED] =
        {34U, 68U, 2, 6, 3, 1, 5, 3, -1, 2U, 0U, 88U},
};

/*
 * Pixel portraits use the same parented hierarchy as the smooth robot rigs.
 * The sclera, iris and pupil all share a bounded socket; lids crop that
 * socket, and brows remain anchored above it.  This prevents the old
 * detached pupil pixels and makes eye squash, asymmetric lids and gaze carry
 * most of the emotion instead of leaving all acting to the mouth.
 */
static void draw_pixel_eye_pair(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    int32_t left_x,
    int32_t right_x,
    int32_t center_y,
    int32_t eye_width,
    int32_t full_height,
    int32_t pixel,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t outline,
    uint16_t brow_color)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const uint8_t expression =
        render_key->stage_expression < FACE_EXPRESSION_COUNT
            ? render_key->stage_expression
            : FACE_EXPRESSION_NEUTRAL;
    const pixel_eye_pose_t *pose = &PIXEL_EYE_POSES[expression];
    const uint16_t stage_weight = render_key->expression_weight;
    const int32_t centers[] = {left_x, right_x};
    const uint8_t requested[] = {
        keyframe->eye_left_open,
        keyframe->eye_right_open,
    };
    const uint8_t authored_open[] = {
        pose->left_open_percent,
        pose->right_open_percent,
    };
    const uint8_t inner_lid[] = {
        pose->left_inner_lid,
        pose->right_inner_lid,
    };
    const int32_t grid = max_i32(1, pixel);
    for (size_t eye = 0U; eye < 2U; ++eye) {
        const int32_t base_height =
            full_height * requested[eye] / 255;
        const int32_t target_height =
            full_height * authored_open[eye] / 100;
        int32_t visible_height = lerp_i16(
            (int16_t)base_height,
            (int16_t)target_height,
            stage_weight);
        visible_height =
            visible_height * motion->blink / 255;
        visible_height =
            max_i32(
                grid,
                ((visible_height + grid / 2) / grid) * grid);
        visible_height = min_i32(full_height, visible_height);
        int32_t socket_width = eye_width +
            (int32_t)pose->width_delta * stage_weight / 255;
        socket_width = clamp_i16(
            socket_width, eye_width - 4, eye_width + 4);
        socket_width =
            max_i32(grid * 3, (socket_width / grid) * grid);
        const int32_t left = centers[eye] - socket_width / 2;
        const int32_t top = center_y - visible_height / 2;
        fill_rect(
            canvas, left - grid, top - grid,
            socket_width + grid * 2, visible_height + grid * 2,
            outline);
        fill_rect(
            canvas, left, top, socket_width, visible_height, sclera);

        const int32_t available_width =
            socket_width - grid * 2;
        const int32_t available_height =
            visible_height - grid * 2;
        if (available_width >= grid &&
            available_height >= grid) {
            int32_t pupil_width =
                max_i32(grid, socket_width / 4);
            pupil_width =
                pupil_width * pose->pupil_percent / 100;
            pupil_width = clamp_i16(
                pupil_width, grid, available_width);
            const int32_t pupil_height = clamp_i16(
                available_height, grid, available_height);
            const int32_t base_gaze_x = motion->gaze_x / 2;
            const int32_t base_gaze_y = motion->gaze_y / 3;
            const int32_t requested_gaze_x = lerp_i16(
                (int16_t)base_gaze_x,
                pose->gaze_x,
                stage_weight);
            const int32_t requested_gaze_y = lerp_i16(
                (int16_t)base_gaze_y,
                pose->gaze_y,
                stage_weight);
            const int32_t horizontal_limit =
                max_i32(
                    0,
                    socket_width / 2 -
                        pupil_width / 2 - grid);
            const int32_t vertical_limit =
                max_i32(
                    0,
                    visible_height / 2 -
                        pupil_height / 2 - grid);
            const int32_t gaze_x = clamp_i16(
                requested_gaze_x,
                -horizontal_limit,
                horizontal_limit);
            const int32_t gaze_y = clamp_i16(
                requested_gaze_y,
                -vertical_limit,
                vertical_limit);
            fill_rect(
                canvas,
                centers[eye] + gaze_x - pupil_width / 2,
                center_y + gaze_y - pupil_height / 2,
                pupil_width, pupil_height, iris);
            fill_rect(
                canvas,
                centers[eye] + gaze_x - max_i32(1, grid / 2),
                center_y + gaze_y - pupil_height / 2,
                max_i32(grid, pupil_width / 2),
                pupil_height,
                pupil);
            if (visible_height >= grid * 4 &&
                pupil_width >= grid * 2) {
                fill_rect(
                    canvas,
                    centers[eye] + gaze_x - pupil_width / 2,
                    center_y + gaze_y - pupil_height / 2,
                    grid, grid, sclera);
            }
        }

        const int32_t lid_cut = min_i32(
            visible_height - grid,
            (int32_t)inner_lid[eye] *
                stage_weight / 255);
        if (lid_cut > 0) {
            if (eye == 0U) {
                fill_polygon(
                    canvas,
                    (point_t[]){
                        {(int16_t)left, (int16_t)top},
                        {(int16_t)(left + socket_width),
                         (int16_t)top},
                        {(int16_t)(left + socket_width),
                         (int16_t)(top + lid_cut)},
                    },
                    3U,
                    outline);
            } else {
                fill_polygon(
                    canvas,
                    (point_t[]){
                        {(int16_t)left, (int16_t)top},
                        {(int16_t)(left + socket_width),
                         (int16_t)top},
                        {(int16_t)left,
                         (int16_t)(top + lid_cut)},
                    },
                    3U,
                    outline);
            }
        }

        const int32_t base_brow_lift =
            clamp_i16(motion->brow / 3, -5, 7);
        const int32_t brow_lift = lerp_i16(
            (int16_t)base_brow_lift,
            pose->brow_lift,
            stage_weight);
        const int32_t base_inner_action =
            clamp_i16(motion->brow_inner / 10, -7, 7);
        const int32_t inner_action = lerp_i16(
            (int16_t)base_inner_action,
            pose->brow_inner,
            stage_weight);
        const int32_t base_outer_action = clamp_i16(
            (eye == 0U ? motion->brow_outer_left
                       : motion->brow_outer_right) /
                10,
            -7, 7);
        const int32_t target_outer_action =
            eye == 0U ? pose->brow_outer_left
                      : pose->brow_outer_right;
        const int32_t outer_action = lerp_i16(
            (int16_t)base_outer_action,
            (int16_t)target_outer_action,
            stage_weight);
        const int32_t brow_base =
            center_y - full_height / 2 - grid * 3 - brow_lift;
        const int32_t outer_y = clamp_i16(
            brow_base - outer_action,
            center_y - full_height / 2 - 13,
            center_y - full_height / 2 - 3);
        const int32_t inner_y = clamp_i16(
            brow_base - inner_action,
            center_y - full_height / 2 - 13,
            center_y - full_height / 2 - 3);
        if (eye == 0U) {
            draw_line(
                canvas, left, outer_y,
                left + socket_width, inner_y,
                grid, brow_color);
        } else {
            draw_line(
                canvas, left, inner_y,
                left + socket_width, outer_y,
                grid, brow_color);
        }
    }
}

/*
 * The labelled stage direction authors a recognisable mouth silhouette,
 * while a small residual from PCM opening/width keeps live speech moving.
 * This avoids eleven copies of the same open oval without disconnecting
 * expression from the audio stream.
 */
static void draw_pixel_mouth_rig(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    int32_t center_x,
    int32_t center_y,
    int32_t max_half_width,
    int32_t max_half_height,
    uint16_t lip,
    uint16_t interior,
    uint16_t teeth,
    uint16_t tongue)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    static const uint8_t width_percent[FACE_EXPRESSION_COUNT] = {
        [FACE_EXPRESSION_NEUTRAL] = 60U,
        [FACE_EXPRESSION_WARM] = 82U,
        [FACE_EXPRESSION_JOY] = 96U,
        [FACE_EXPRESSION_CONCERN] = 58U,
        [FACE_EXPRESSION_SURPRISE] = 36U,
        [FACE_EXPRESSION_THOUGHTFUL] = 48U,
        [FACE_EXPRESSION_SKEPTICAL] = 82U,
        [FACE_EXPRESSION_DETERMINED] = 88U,
        [FACE_EXPRESSION_SLEEPY] = 62U,
        [FACE_EXPRESSION_EXCITED] = 100U,
        [FACE_EXPRESSION_EMBARRASSED] = 56U,
    };
    static const uint8_t height_percent[FACE_EXPRESSION_COUNT] = {
        [FACE_EXPRESSION_NEUTRAL] = 34U,
        [FACE_EXPRESSION_WARM] = 18U,
        [FACE_EXPRESSION_JOY] = 56U,
        [FACE_EXPRESSION_CONCERN] = 26U,
        [FACE_EXPRESSION_SURPRISE] = 92U,
        [FACE_EXPRESSION_THOUGHTFUL] = 38U,
        [FACE_EXPRESSION_SKEPTICAL] = 18U,
        [FACE_EXPRESSION_DETERMINED] = 28U,
        [FACE_EXPRESSION_SLEEPY] = 16U,
        [FACE_EXPRESSION_EXCITED] = 78U,
        [FACE_EXPRESSION_EMBARRASSED] = 32U,
    };
    static const int8_t left_corner[FACE_EXPRESSION_COUNT] = {
        [FACE_EXPRESSION_NEUTRAL] = 0,
        [FACE_EXPRESSION_WARM] = 3,
        [FACE_EXPRESSION_JOY] = 5,
        [FACE_EXPRESSION_CONCERN] = -5,
        [FACE_EXPRESSION_SURPRISE] = 0,
        [FACE_EXPRESSION_THOUGHTFUL] = -2,
        [FACE_EXPRESSION_SKEPTICAL] = -3,
        [FACE_EXPRESSION_DETERMINED] = -1,
        [FACE_EXPRESSION_SLEEPY] = 0,
        [FACE_EXPRESSION_EXCITED] = 3,
        [FACE_EXPRESSION_EMBARRASSED] = 4,
    };
    static const int8_t right_corner[FACE_EXPRESSION_COUNT] = {
        [FACE_EXPRESSION_NEUTRAL] = 0,
        [FACE_EXPRESSION_WARM] = 3,
        [FACE_EXPRESSION_JOY] = 5,
        [FACE_EXPRESSION_CONCERN] = -5,
        [FACE_EXPRESSION_SURPRISE] = 0,
        [FACE_EXPRESSION_THOUGHTFUL] = 1,
        [FACE_EXPRESSION_SKEPTICAL] = 2,
        [FACE_EXPRESSION_DETERMINED] = -1,
        [FACE_EXPRESSION_SLEEPY] = 0,
        [FACE_EXPRESSION_EXCITED] = 3,
        [FACE_EXPRESSION_EMBARRASSED] = 0,
    };
    const uint8_t expression =
        render_key->stage_expression < FACE_EXPRESSION_COUNT
            ? render_key->stage_expression
            : FACE_EXPRESSION_NEUTRAL;
    const uint16_t stage_weight = render_key->expression_weight;
    const int32_t base_half_width = clamp_i16(
        7 + keyframe->mouth_width / 10 -
            keyframe->mouth_round / 24,
        7, max_half_width);
    const int32_t base_half_height = clamp_i16(
        2 + keyframe->mouth_open / 18 +
            keyframe->mouth_round / 64,
        2, max_half_height);
    const int32_t audio_width_delta =
        ((int32_t)keyframe->mouth_width - 160) / 64;
    const int32_t audio_height_delta =
        ((int32_t)keyframe->mouth_open - 128) / 64;
    const int32_t target_half_width = clamp_i16(
        max_half_width * width_percent[expression] / 100 +
            audio_width_delta,
        7, max_half_width);
    int32_t target_half_height = clamp_i16(
        max_half_height * height_percent[expression] / 100 +
            audio_height_delta,
        2, max_half_height);
    if (expression == FACE_EXPRESSION_DETERMINED) {
        target_half_height = max_i32(3, target_half_height);
    }
    const int32_t half_width = lerp_i16(
        (int16_t)base_half_width,
        (int16_t)target_half_width,
        stage_weight);
    const int32_t half_height = lerp_i16(
        (int16_t)base_half_height,
        (int16_t)target_half_height,
        stage_weight);
    const int32_t base_left_lift = clamp_i16(
        render_key->mouth_corner_left / 14, -7, 7);
    const int32_t base_right_lift = clamp_i16(
        render_key->mouth_corner_right / 14, -7, 7);
    const int32_t left_lift = lerp_i16(
        (int16_t)base_left_lift,
        left_corner[expression],
        stage_weight);
    const int32_t right_lift = lerp_i16(
        (int16_t)base_right_lift,
        right_corner[expression],
        stage_weight);
    const bool closed =
        (keyframe->mouth_open < 28U &&
         expression != FACE_EXPRESSION_SURPRISE &&
         expression != FACE_EXPRESSION_EXCITED) ||
        (keyframe->mouth_press > 154U &&
         keyframe->mouth_open < 138U &&
         expression != FACE_EXPRESSION_DETERMINED);
    if (closed) {
        const int32_t center_drop =
            clamp_i16(
                -((int32_t)render_key->affect_valence) / 34,
                -3, 3);
        draw_line(
            canvas,
            center_x - half_width,
            center_y - left_lift,
            center_x,
            center_y + center_drop,
            keyframe->mouth_press > 120U ? 3 : 2,
            lip);
        draw_line(
            canvas,
            center_x,
            center_y + center_drop,
            center_x + half_width,
            center_y - right_lift,
            keyframe->mouth_press > 120U ? 3 : 2,
            lip);
        return;
    }

    const point_t outer[] = {
        {(int16_t)(center_x - half_width),
         (int16_t)(center_y - left_lift)},
        {(int16_t)(center_x - half_width / 2),
         (int16_t)(center_y - half_height)},
        {(int16_t)(center_x + half_width / 2),
         (int16_t)(center_y - half_height)},
        {(int16_t)(center_x + half_width),
         (int16_t)(center_y - right_lift)},
        {(int16_t)(center_x + half_width / 2),
         (int16_t)(center_y + half_height)},
        {(int16_t)(center_x - half_width / 2),
         (int16_t)(center_y + half_height)},
    };
    fill_polygon(
        canvas, outer, sizeof(outer) / sizeof(outer[0]), lip);

    const int32_t inner_width = max_i32(4, half_width - 3);
    const int32_t inner_height = max_i32(1, half_height - 3);
    const point_t inner[] = {
        {(int16_t)(center_x - inner_width),
         (int16_t)(center_y - left_lift / 2)},
        {(int16_t)(center_x - inner_width / 2),
         (int16_t)(center_y - inner_height)},
        {(int16_t)(center_x + inner_width / 2),
         (int16_t)(center_y - inner_height)},
        {(int16_t)(center_x + inner_width),
         (int16_t)(center_y - right_lift / 2)},
        {(int16_t)(center_x + inner_width / 2),
         (int16_t)(center_y + inner_height)},
        {(int16_t)(center_x - inner_width / 2),
         (int16_t)(center_y + inner_height)},
    };
    fill_polygon(
        canvas, inner, sizeof(inner) / sizeof(inner[0]), interior);
    if (keyframe->mouth_teeth > 100U && inner_height >= 2) {
        fill_rect(
            canvas,
            center_x - inner_width + 2,
            center_y - inner_height,
            inner_width * 2 - 4,
            min_i32(3, inner_height),
            teeth);
    }
    if (render_key->tongue > 70U && inner_height >= 3) {
        fill_rect(
            canvas,
            center_x - max_i32(3, inner_width - 4),
            center_y + inner_height - 2,
            max_i32(6, (inner_width - 4) * 2),
            2,
            tongue);
    }
}

static void render_pixel_face(
    canvas_t *canvas,
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    const motion_t *motion)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    /*
     * Keep the five large character portraits rooted while the mouth tracks
     * PCM.  Whole-head translation from mouth_open made them appear to jump
     * on every syllable; breathing and authored body motion still provide a
     * subtle, deterministic one-pixel drift.  The two non-character legacy
     * profiles retain their original motion.
     */
    const bool stable_character =
        profile == FACE_RENDER_EGA_QUEST ||
        profile == FACE_RENDER_VGA_ELDER ||
        profile == FACE_RENDER_TALKIE_CLOSEUP ||
        profile == FACE_RENDER_POCKET_RPG ||
        profile == FACE_RENDER_DITHERED_ROGUE;
    const int32_t bob = stable_character
        ? clamp_i16(
              motion->breathe / 96 +
                  render_key->body_lean_y / 28 +
                  render_key->head_pitch / 32,
              -2,
              2)
        : motion->bob;
    switch (profile) {
    case FACE_RENDER_EGA_QUEST: {
        const uint16_t navy = RGB565(0, 0, 42);
        const uint16_t blue = RGB565(0, 84, 168);
        const uint16_t skin = RGB565(252, 168, 104);
        const uint16_t shade = RGB565(168, 84, 84);
        const uint16_t hair = RGB565(84, 42, 0);
        canvas_clear(canvas, navy);
        draw_dither(canvas, 0, 0, 160, 120, blue, 42U);
        fill_polygon(
            canvas,
            (point_t[]){{24, 120}, {34, 90 + bob}, {58, 80 + bob},
                        {102, 80 + bob}, {126, 92 + bob}, {138, 120}},
            6U, RGB565(0, 168, 168));
        fill_rect(canvas, 48, 24 + bob, 64, 62, skin);
        fill_rect(canvas, 42, 34 + bob, 76, 38, skin);
        fill_rect(canvas, 48, 74 + bob, 64, 14, shade);
        fill_rect(canvas, 44, 18 + bob, 72, 18, hair);
        fill_rect(canvas, 38, 24 + bob, 14, 40, hair);
        fill_rect(canvas, 108, 24 + bob, 12, 32, hair);
        fill_rect(canvas, 50, 20 + bob, 14, 8, RGB565(252, 252, 84));
        draw_pixel_eye_pair(
            canvas, render_key, motion,
            64, 96, 53 + bob, 16, 10, 2,
            RGB565(252, 252, 252), RGB565(0, 84, 168),
            RGB565(0, 0, 0), shade, hair);
        fill_rect(canvas, 76, 52 + bob, 8, 18, shade);
        draw_pixel_mouth_rig(
            canvas, render_key, 80, 80 + bob, 21, 10,
            RGB565(252, 84, 84), RGB565(84, 0, 0),
            RGB565(252, 252, 252), RGB565(252, 84, 104));
        const int32_t expression_color = max_i32(
            render_key->cheek,
            max_i32(0, render_key->affect_valence) * 2);
        if (expression_color > 12) {
            const uint16_t cheek = blend565(
                skin, RGB565(252, 84, 104),
                (uint8_t)min_i32(212, 36 + expression_color));
            fill_rect(canvas, 47, 65 + bob, 14, 6, cheek);
            fill_rect(canvas, 99, 65 + bob, 14, 6, cheek);
        }
        break;
    }
    case FACE_RENDER_VGA_ELDER: {
        const uint16_t night = RGB565(16, 12, 28);
        const uint16_t skin = RGB565(220, 164, 116);
        const uint16_t skin_light = RGB565(244, 196, 148);
        const uint16_t skin_dark = RGB565(140, 84, 68);
        const uint16_t silver = RGB565(184, 188, 180);
        canvas_clear(canvas, night);
        draw_dither(canvas, 8, 8, 144, 108, RGB565(76, 40, 100), 52U);
        fill_ellipse(canvas, 80, 58 + bob, 47, 50, skin_dark);
        fill_ellipse(canvas, 78, 54 + bob, 43, 46, skin);
        fill_rect(canvas, 52, 30 + bob, 50, 8, skin_light);
        fill_polygon(
            canvas,
            (point_t[]){{32, 46 + bob}, {40, 18 + bob}, {64, 8 + bob},
                        {96, 10 + bob}, {122, 32 + bob}, {112, 50 + bob},
                        {102, 30 + bob}, {52, 28 + bob}},
            8U, silver);
        fill_rect(canvas, 44, 70 + bob, 12, 30, silver);
        fill_rect(canvas, 104, 68 + bob, 12, 32, silver);
        draw_dither(
            canvas, 52, 76 + bob, 56, 30,
            RGB565(116, 120, 116), 110U);
        draw_pixel_eye_pair(
            canvas, render_key, motion,
            61, 97, 53 + bob, 19, 12, 2,
            RGB565(244, 236, 208), RGB565(48, 112, 104),
            RGB565(8, 12, 12), skin_dark, RGB565(92, 76, 68));
        fill_polygon(
            canvas,
            (point_t[]){{74, 52 + bob}, {82, 52 + bob},
                        {88, 70 + bob}, {78, 74 + bob}, {70, 68 + bob}},
            5U, skin_light);
        /*
         * The beard surrounds a small skin muzzle instead of becoming one
         * enormous dark mouth.  Its dark one-pixel border acts as an
         * occlusion mask, keeping even the widest grin attached to the jaw.
         */
        fill_ellipse(canvas, 80, 81 + bob, 22, 12, skin_dark);
        fill_ellipse(canvas, 80, 79 + bob, 20, 9, skin);
        draw_pixel_mouth_rig(
            canvas, render_key, 80, 80 + bob, 18, 9,
            RGB565(174, 68, 76), RGB565(44, 16, 24),
            RGB565(236, 224, 188), RGB565(184, 64, 76));
        if (render_key->stage_expression ==
                FACE_EXPRESSION_EMBARRASSED &&
            render_key->expression_weight > 64U) {
            const uint8_t cheek_alpha = (uint8_t)min_i32(
                196,
                render_key->expression_weight / 2 + 52);
            fill_rect(
                canvas, 47, 68 + bob, 10, 4,
                blend565(skin, RGB565(194, 76, 92), cheek_alpha));
            fill_rect(
                canvas, 103, 68 + bob, 10, 4,
                blend565(skin, RGB565(194, 76, 92), cheek_alpha));
        }
        break;
    }
    case FACE_RENDER_TALKIE_CLOSEUP: {
        const uint16_t ink = RGB565(8, 16, 24);
        const uint16_t teal = RGB565(20, 72, 80);
        const uint16_t skin = RGB565(232, 156, 108);
        const uint16_t light = RGB565(255, 204, 140);
        const uint16_t shadow = RGB565(116, 64, 76);
        canvas_clear(canvas, ink);
        fill_rect(canvas, 0, 90, 160, 30, teal);
        fill_polygon(
            canvas,
            (point_t[]){{30, 104}, {40, 38 + bob}, {60, 12 + bob},
                        {106, 18 + bob}, {132, 48 + bob}, {124, 104}},
            6U, shadow);
        fill_polygon(
            canvas,
            (point_t[]){{42, 88 + bob}, {42, 38 + bob}, {64, 18 + bob},
                        {99, 22 + bob}, {118, 44 + bob}, {108, 88 + bob}},
            6U, skin);
        fill_polygon(
            canvas,
            (point_t[]){{42, 38 + bob}, {64, 18 + bob}, {82, 22 + bob},
                        {72, 88 + bob}, {42, 88 + bob}},
            5U, light);
        fill_polygon(
            canvas,
            (point_t[]){{36, 40 + bob}, {50, 12 + bob}, {108, 14 + bob},
                        {122, 30 + bob}, {94, 26 + bob}, {68, 34 + bob}},
            6U, RGB565(36, 28, 36));
        draw_pixel_eye_pair(
            canvas, render_key, motion,
            59, 95, 52 + bob, 22, 13, 2,
            RGB565(244, 240, 220), RGB565(36, 112, 112),
            ink, shadow, RGB565(44, 24, 28));
        fill_polygon(
            canvas,
            (point_t[]){{76, 50 + bob}, {84, 50 + bob},
                        {90, 69 + bob}, {78, 72 + bob}},
            4U, shadow);
        fill_rect(canvas, 79, 70 + bob, 4, 5, shadow);
        draw_pixel_mouth_rig(
            canvas, render_key, 82, 83 + bob, 22, 10,
            RGB565(220, 68, 76), RGB565(48, 12, 20),
            RGB565(255, 244, 212), RGB565(238, 86, 104));
        if (render_key->cheek > 24U) {
            const uint16_t cheek = blend565(
                skin, RGB565(246, 78, 98),
                (uint8_t)min_i32(184, 36 + render_key->cheek));
            fill_rect(canvas, 42, 67 + bob, 13, 5, cheek);
            fill_rect(canvas, 102, 67 + bob, 13, 5, cheek);
        }
        fill_rect(canvas, 4, 98, 152, 18, RGB565(12, 30, 42));
        draw_dither(canvas, 8, 102, 144, 10, RGB565(64, 196, 180), 82U);
        break;
    }
    case FACE_RENDER_PIXEL_AUTOMATON: {
        const uint16_t black = RGB565(2, 6, 8);
        const uint16_t metal = RGB565(88, 112, 120);
        const uint16_t highlight = RGB565(160, 196, 196);
        const uint16_t cyan = RGB565(0, 244, 220);
        canvas_clear(canvas, black);
        draw_dither(canvas, 0, 0, 160, 120, RGB565(20, 52, 58), 34U);
        fill_polygon(
            canvas,
            (point_t[]){{36, 24 + bob}, {52, 10 + bob},
                        {108, 10 + bob}, {126, 28 + bob},
                        {118, 96 + bob}, {102, 110 + bob},
                        {54, 110 + bob}, {34, 92 + bob}},
            8U, RGB565(32, 44, 52));
        fill_rect(canvas, 42, 30 + bob, 76, 62, metal);
        fill_rect(canvas, 48, 34 + bob, 64, 52, RGB565(28, 40, 44));
        const int32_t left_eye_h = max_i32(
            2, 12 * eye_open(keyframe->eye_left_open, motion) / 255);
        const int32_t right_eye_h = max_i32(
            2, 12 * eye_open(keyframe->eye_right_open, motion) / 255);
        fill_rect(
            canvas, 54, 49 + bob - left_eye_h / 2,
            20, left_eye_h, cyan);
        fill_rect(
            canvas, 86, 49 + bob - right_eye_h / 2,
            20, right_eye_h, cyan);
        const int32_t pupil_dx =
            clamp_i16(motion->gaze_x / 2, -5, 5);
        const int32_t left_pupil_h =
            max_i32(2, left_eye_h - 3);
        const int32_t right_pupil_h =
            max_i32(2, right_eye_h - 3);
        const int32_t left_pupil_dy = clamp_i16(
            motion->gaze_y / 3,
            -left_eye_h / 2 + left_pupil_h / 2,
            left_eye_h / 2 - left_pupil_h / 2);
        const int32_t right_pupil_dy = clamp_i16(
            motion->gaze_y / 3,
            -right_eye_h / 2 + right_pupil_h / 2,
            right_eye_h / 2 - right_pupil_h / 2);
        fill_rect(
            canvas, 62 + pupil_dx,
            49 + bob + left_pupil_dy - left_pupil_h / 2,
            6, left_pupil_h, black);
        fill_rect(
            canvas, 94 + pupil_dx,
            49 + bob + right_pupil_dy - right_pupil_h / 2,
            6, right_pupil_h, black);
        const int32_t shutter_y =
            40 + bob - clamp_i16(motion->brow / 6, -2, 3);
        const int32_t inner_shutter =
            clamp_i16(motion->brow_inner / 24, -4, 4);
        const int32_t left_outer = clamp_i16(
            motion->brow_outer_left / 24, -4, 4);
        const int32_t right_outer = clamp_i16(
            motion->brow_outer_right / 24, -4, 4);
        draw_line(
            canvas, 54, shutter_y - left_outer,
            74, shutter_y - inner_shutter, 3, highlight);
        draw_line(
            canvas, 86, shutter_y - inner_shutter,
            106, shutter_y - right_outer, 3, highlight);
        fill_rect(canvas, 74, 22 + bob, 12, 5, highlight);
        const int32_t visible_segments = clamp_i16(
            2 + keyframe->mouth_width / 48 -
                keyframe->mouth_round / 96,
            2, 6);
        const int32_t first_segment =
            (6 - visible_segments) / 2;
        const int32_t mouth_height =
            clamp_i16(3 + keyframe->mouth_open / 28, 3, 11);
        const int32_t left_lift =
            clamp_i16(render_key->mouth_corner_left / 24, -4, 4);
        const int32_t right_lift =
            clamp_i16(render_key->mouth_corner_right / 24, -4, 4);
        for (int32_t index = 0; index < 6; ++index) {
            const bool active =
                index >= first_segment &&
                index < first_segment + visible_segments;
            const int32_t lift =
                left_lift +
                (right_lift - left_lift) * index / 5;
            fill_rect(
                canvas, 54 + index * 9,
                75 + bob - lift -
                    (active ? mouth_height / 2 : 1),
                6, active ? mouth_height : 3,
                active ? RGB565(255, 96, 44)
                       : RGB565(72, 38, 30));
        }
        draw_line(canvas, 36, 64 + bob, 24, 64 + bob, 4, highlight);
        draw_line(canvas, 124, 64 + bob, 136, 64 + bob, 4, highlight);
        break;
    }
    case FACE_RENDER_AMBER_TERMINAL: {
        const uint16_t black = RGB565(2, 4, 1);
        const uint16_t amber = RGB565(255, 168, 12);
        const uint16_t dim = RGB565(88, 48, 0);
        canvas_clear(canvas, black);
        for (int32_t y = 1; y < 120; y += 4) {
            fill_rect(canvas, 0, y, 160, 1, RGB565(14, 8, 0));
        }
        draw_line(canvas, 45, 94 + bob, 34, 48 + bob, 2, amber);
        draw_line(canvas, 34, 48 + bob, 54, 20 + bob, 2, amber);
        draw_line(canvas, 54, 20 + bob, 106, 20 + bob, 2, amber);
        draw_line(canvas, 106, 20 + bob, 126, 48 + bob, 2, amber);
        draw_line(canvas, 126, 48 + bob, 115, 94 + bob, 2, amber);
        draw_line(canvas, 115, 94 + bob, 45, 94 + bob, 2, amber);
        draw_basic_eyes(
            canvas, 62, 98, 53 + bob, 13, 7, motion,
            keyframe->eye_left_open, keyframe->eye_right_open,
            dim, amber, black);
        draw_brows(canvas, 62, 98, 40 + bob, 13, motion, amber, true);
        const int32_t open = keyframe->mouth_open / 32;
        draw_quadratic(
            canvas, (point_t){58, 77 + bob},
            (point_t){80, 82 + bob + open},
            (point_t){102, 77 + bob}, 2, amber);
        fill_rect(
            canvas, 8, 108, 20 + keyframe->mouth_open / 2, 3, amber);
        break;
    }
    case FACE_RENDER_POCKET_RPG: {
        const uint16_t sky = RGB565(100, 184, 228);
        const uint16_t skin = RGB565(255, 202, 148);
        const uint16_t outline = RGB565(50, 34, 58);
        const uint16_t hair = RGB565(102, 54, 130);
        canvas_clear(canvas, sky);
        fill_rect(canvas, 0, 88, 160, 32, RGB565(54, 126, 92));
        fill_ellipse(canvas, 80, 59 + bob, 45, 47, outline);
        fill_ellipse(canvas, 80, 57 + bob, 41, 43, skin);
        fill_polygon(
            canvas,
            (point_t[]){{37, 45 + bob}, {45, 18 + bob},
                        {64, 9 + bob}, {74, 22 + bob},
                        {88, 8 + bob}, {116, 22 + bob},
                        {124, 51 + bob}, {108, 34 + bob},
                        {52, 34 + bob}},
            9U, hair);
        draw_pixel_eye_pair(
            canvas, render_key, motion,
            63, 97, 54 + bob, 16, 11, 2,
            RGB565(252, 252, 244), RGB565(104, 76, 148),
            outline, skin, outline);
        draw_pixel_mouth_rig(
            canvas, render_key, 80, 78 + bob, 19, 9,
            RGB565(220, 72, 104), RGB565(72, 24, 52),
            RGB565(255, 244, 212), RGB565(246, 104, 126));
        if (render_key->cheek > 30U) {
            const uint16_t cheek = blend565(
                skin,
                RGB565(240, 92, 118),
                (uint8_t)min_i32(
                    184,
                    36 + render_key->cheek));
            fill_rect(canvas, 44, 68 + bob, 10, 4, cheek);
            fill_rect(canvas, 106, 68 + bob, 10, 4, cheek);
        }
        fill_polygon(
            canvas,
            (point_t[]){{40, 108}, {56, 88}, {104, 88}, {122, 108}},
            4U, RGB565(246, 196, 58));
        break;
    }
    case FACE_RENDER_DITHERED_ROGUE: {
        const uint16_t paper = RGB565(224, 216, 184);
        const uint16_t ink = RGB565(28, 30, 34);
        const uint16_t red = RGB565(164, 38, 42);
        canvas_clear(canvas, paper);
        draw_dither(canvas, 0, 0, 160, 120, ink, 30U);
        fill_polygon(
            canvas,
            (point_t[]){{32, 100 + bob}, {38, 40 + bob},
                        {60, 12 + bob}, {100, 10 + bob},
                        {124, 42 + bob}, {130, 100 + bob}},
            6U, ink);
        fill_polygon(
            canvas,
            (point_t[]){{46, 92 + bob}, {47, 43 + bob},
                        {63, 25 + bob}, {98, 25 + bob},
                        {113, 44 + bob}, {112, 92 + bob}},
            6U, paper);
        fill_polygon(
            canvas,
            (point_t[]){{46, 42 + bob}, {64, 27 + bob},
                        {98, 28 + bob}, {114, 44 + bob},
                        {109, 65 + bob}, {51, 64 + bob}},
            6U, red);
        fill_polygon(
            canvas,
            (point_t[]){{51, 65 + bob}, {109, 66 + bob},
                        {103, 72 + bob}, {57, 71 + bob}},
            4U, RGB565(188, 174, 144));
        draw_pixel_eye_pair(
            canvas, render_key, motion,
            63, 97, 54 + bob, 17, 10, 1,
            paper, RGB565(92, 100, 92), ink, red, ink);
        draw_pixel_mouth_rig(
            canvas, render_key, 80, 81 + bob, 20, 9,
            red, ink, paper, RGB565(204, 72, 72));
        draw_line(canvas, 53, 72 + bob, 61, 75 + bob, 1, ink);
        draw_line(canvas, 107, 72 + bob, 99, 75 + bob, 1, ink);
        if (render_key->stage_expression ==
                FACE_EXPRESSION_EMBARRASSED &&
            render_key->expression_weight > 48U) {
            draw_dither(
                canvas, 48, 66 + bob, 12, 5,
                red, 188U);
            draw_dither(
                canvas, 100, 66 + bob, 12, 5,
                red, 188U);
            draw_line(
                canvas, 50, 68 + bob,
                57, 70 + bob, 1, red);
            draw_line(
                canvas, 103, 70 + bob,
                110, 68 + bob, 1, red);
        }
        break;
    }
    default:
        break;
    }
}

static void draw_robot_mouth_line(
    canvas_t *canvas,
    const face_keyframe_t *keyframe,
    int32_t center_x,
    int32_t center_y,
    uint16_t color)
{
    const int32_t half =
        12 + keyframe->mouth_width / 7 - keyframe->mouth_round / 14;
    const int32_t depth = keyframe->mouth_open / 14;
    draw_quadratic(
        canvas,
        (point_t){(int16_t)(center_x - half), (int16_t)center_y},
        (point_t){(int16_t)center_x, (int16_t)(center_y + depth)},
        (point_t){(int16_t)(center_x + half), (int16_t)center_y},
        max_i32(2, 2 + keyframe->mouth_open / 90), color);
}

static void draw_robot_mouth_rig(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    int32_t center_x,
    int32_t center_y,
    uint16_t lip,
    uint16_t interior,
    uint16_t teeth,
    uint16_t tongue)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const int32_t left_corner = render_key->mouth_corner_left;
    const int32_t right_corner = render_key->mouth_corner_right;
    const int32_t smile = (left_corner + right_corner) / 2;
    const int32_t half_width = clamp_i16(
        12 + keyframe->mouth_width / 7 -
            keyframe->mouth_round / 16,
        10, 34);
    const int32_t half_height = clamp_i16(
        2 + keyframe->mouth_open / 13 +
            keyframe->mouth_round / 48,
        2, 16);
    const bool closed =
        keyframe->mouth_open < 30U ||
        (keyframe->mouth_press > 156U &&
         keyframe->mouth_open < 88U);

    if (closed) {
        const int32_t edge_y = center_y - smile / 12;
        const int32_t middle_y = center_y + smile / 14;
        draw_quadratic(
            canvas,
            (point_t){
                (int16_t)(center_x - half_width),
                (int16_t)edge_y,
            },
            (point_t){
                (int16_t)center_x,
                (int16_t)middle_y,
            },
            (point_t){
                (int16_t)(center_x + half_width),
                (int16_t)edge_y,
            },
            keyframe->mouth_press > 120U ? 3 : 2,
            lip);
        return;
    }

    /*
     * Keep the mouth inside a conservative safe area and let each corner
     * carry authored expression independently.  The previous pair of
     * ellipses turned energetic speech into a large, emotionally inert ring.
     */
    const int32_t left_lift = clamp_i16(left_corner / 9, -9, 9);
    const int32_t right_lift = clamp_i16(right_corner / 9, -9, 9);
    const int32_t inner_width = max_i32(5, half_width - 3);
    const int32_t inner_height = max_i32(2, half_height - 3);
    const point_t outer[] = {
        {(int16_t)(center_x - half_width),
         (int16_t)(center_y - left_lift)},
        {(int16_t)(center_x - half_width * 2 / 3),
         (int16_t)(center_y - half_height * 2 / 3 -
                   left_lift / 2)},
        {(int16_t)(center_x - half_width / 3),
         (int16_t)(center_y - half_height)},
        {(int16_t)(center_x + half_width / 3),
         (int16_t)(center_y - half_height)},
        {(int16_t)(center_x + half_width * 2 / 3),
         (int16_t)(center_y - half_height * 2 / 3 -
                   right_lift / 2)},
        {(int16_t)(center_x + half_width),
         (int16_t)(center_y - right_lift)},
        {(int16_t)(center_x + half_width * 2 / 3),
         (int16_t)(center_y + half_height * 2 / 3 -
                   right_lift / 3)},
        {(int16_t)(center_x + half_width / 3),
         (int16_t)(center_y + half_height)},
        {(int16_t)(center_x - half_width / 3),
         (int16_t)(center_y + half_height)},
        {(int16_t)(center_x - half_width * 2 / 3),
         (int16_t)(center_y + half_height * 2 / 3 -
                   left_lift / 3)},
    };
    const point_t inner[] = {
        {(int16_t)(center_x - inner_width),
         (int16_t)(center_y - left_lift)},
        {(int16_t)(center_x - inner_width * 2 / 3),
         (int16_t)(center_y - inner_height * 2 / 3 -
                   left_lift / 2)},
        {(int16_t)(center_x - inner_width / 3),
         (int16_t)(center_y - inner_height)},
        {(int16_t)(center_x + inner_width / 3),
         (int16_t)(center_y - inner_height)},
        {(int16_t)(center_x + inner_width * 2 / 3),
         (int16_t)(center_y - inner_height * 2 / 3 -
                   right_lift / 2)},
        {(int16_t)(center_x + inner_width),
         (int16_t)(center_y - right_lift)},
        {(int16_t)(center_x + inner_width * 2 / 3),
         (int16_t)(center_y + inner_height * 2 / 3 -
                   right_lift / 3)},
        {(int16_t)(center_x + inner_width / 3),
         (int16_t)(center_y + inner_height)},
        {(int16_t)(center_x - inner_width / 3),
         (int16_t)(center_y + inner_height)},
        {(int16_t)(center_x - inner_width * 2 / 3),
         (int16_t)(center_y + inner_height * 2 / 3 -
                   left_lift / 3)},
    };
    fill_polygon(canvas, outer, sizeof(outer) / sizeof(outer[0]), lip);
    fill_polygon(
        canvas, inner, sizeof(inner) / sizeof(inner[0]), interior);
    if (keyframe->mouth_teeth > 86U && half_height >= 6) {
        const int32_t tooth_height = clamp_i16(
            1 + keyframe->mouth_teeth / 54, 2, half_height - 2);
        fill_round_rect(
            canvas,
            center_x - inner_width + 2,
            center_y - inner_height + 2,
            inner_width * 2 - 4,
            tooth_height,
            2,
            teeth);
    }
    if (render_key->tongue > 72U && half_height >= 9) {
        const int32_t tongue_height = clamp_i16(
            2 + render_key->tongue / 72, 3, half_height - 3);
        fill_ellipse(
            canvas,
            center_x + smile / 40,
            center_y + inner_height - tongue_height,
            max_i32(4, inner_width - 4),
            tongue_height,
            tongue);
    }
    if (smile > 18) {
        draw_line(
            canvas,
            center_x - half_width - 1,
            center_y - smile / 12,
            center_x - half_width + 3,
            center_y - half_height / 3,
            2,
            lip);
        draw_line(
            canvas,
            center_x + half_width + 1,
            center_y - smile / 12,
            center_x + half_width - 3,
            center_y - half_height / 3,
            2,
            lip);
    }
}

static void draw_emissive_robot_eye(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    bool left,
    int32_t center_x,
    int32_t center_y,
    int32_t width,
    int32_t full_height,
    int32_t radius,
    uint16_t color,
    uint16_t background,
    bool highlight)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const uint8_t requested_open =
        left ? keyframe->eye_left_open : keyframe->eye_right_open;
    const uint8_t open = eye_open(requested_open, motion);
    const int32_t height = full_height;
    const int32_t visible_height =
        max_i32(3, full_height * open / 255);
    const int32_t closure = max_i32(0, height - visible_height);
    const int32_t gx = clamp_i16(motion->gaze_x * 2, -11, 11);
    const int32_t gy = clamp_i16(motion->gaze_y, -6, 6);
    const int32_t cx = clamp_i16(
        center_x + gx, width / 2 + 6,
        FACE_RENDER_WIDTH - width / 2 - 7);
    const int32_t cy = clamp_i16(
        center_y + gy + motion->bob,
        height / 2 + 8,
        FACE_RENDER_HEIGHT - height / 2 - 20);
    const int32_t x = cx - width / 2;
    const int32_t y = cy - height / 2;
    const int32_t inner_action = render_key->brow_inner;
    const int32_t outer_action =
        left ? render_key->brow_outer_left
             : render_key->brow_outer_right;
    const int32_t base_top_close = closure * 2 / 3;
    const int32_t base_bottom_close = closure - base_top_close;
    const int32_t inner_drop = clamp_i16(
        base_top_close + (-inner_action + 8) / 8,
        0, height * 2 / 3);
    const int32_t outer_drop = clamp_i16(
        base_top_close + (-outer_action + 8) / 8,
        0, height * 2 / 3);
    const int32_t lower_raise = clamp_i16(
        base_bottom_close + motion->cheek / 24 +
            (left ? render_key->eye_left_squint
                  : render_key->eye_right_squint) /
                42,
        0, max_i32(0, height / 2));

    fill_round_rect(canvas, x, y, width, height, radius, color);

    if (inner_drop > 0 || outer_drop > 0) {
        const int32_t left_drop = left ? outer_drop : inner_drop;
        const int32_t right_drop = left ? inner_drop : outer_drop;
        const int32_t center_drop =
            max_i32(left_drop, right_drop) +
            (render_key->affect_valence < -24 ? 2 : 0);
        fill_polygon(
            canvas,
            (point_t[]){
                {(int16_t)x, (int16_t)y},
                {(int16_t)(x + width), (int16_t)y},
                {(int16_t)(x + width), (int16_t)(y + right_drop)},
                {(int16_t)cx, (int16_t)(y + center_drop)},
                {(int16_t)x, (int16_t)(y + left_drop)},
            },
            5U,
            background);
    }
    if (lower_raise > 0) {
        const int32_t outer_raise =
            min_i32(height / 2, lower_raise + motion->cheek / 40);
        const int32_t left_raise =
            left ? outer_raise : lower_raise;
        const int32_t right_raise =
            left ? lower_raise : outer_raise;
        const int32_t center_raise = clamp_i16(
            lower_raise + motion->cheek / 28 +
                max_i32(0, render_key->affect_valence) / 32,
            0, height * 2 / 3);
        fill_polygon(
            canvas,
            (point_t[]){
                {(int16_t)x,
                 (int16_t)(y + height - left_raise)},
                {(int16_t)cx,
                 (int16_t)(y + height - center_raise)},
                {(int16_t)(x + width),
                 (int16_t)(y + height - right_raise)},
                {(int16_t)(x + width), (int16_t)(y + height)},
                {(int16_t)x, (int16_t)(y + height)},
            },
            5U,
            background);
    }
    /*
     * A blink may close from both lids at once.  Preserve a short emissive
     * seam instead of allowing the two background masks to erase the eye
     * completely; at 160x120 a fully black socket reads as missing geometry
     * rather than a deliberate closed eye.
     */
    if (open <= 58U) {
        const int32_t seam_y = clamp_i16(
            cy + (inner_drop - outer_drop) / 6,
            y + 2, y + height - 3);
        const uint16_t seam =
            blend565(background, color, 184U);
        draw_line(
            canvas,
            x + max_i32(2, radius / 2),
            seam_y,
            x + width - max_i32(3, radius / 2) - 1,
            seam_y,
            2,
            seam);
    }
    if (highlight && open > 86U && height >= 8) {
        fill_round_rect(
            canvas,
            x + (left ? width / 5 : width * 3 / 5),
            y + max_i32(2, height / 5),
            max_i32(2, width / 8),
            max_i32(2, height / 5),
            2,
            blend565(color, RGB565(255, 255, 255), 126U));
    }
}

static void render_robot_face(
    canvas_t *canvas,
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    const motion_t *motion)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const int32_t bob = motion->bob;
    switch (profile) {
    case FACE_RENDER_VECTOR_ROUNDED: {
        const uint16_t black = RGB565(1, 5, 8);
        const uint16_t mint = RGB565(72, 246, 222);
        const uint16_t mouth_dark = RGB565(5, 22, 24);
        canvas_clear(canvas, black);
        const int32_t eye_width = clamp_i16(
            30 + keyframe->mouth_open / 28 +
                render_key->affect_arousal / 64,
            30, 41);
        const int32_t speech_stretch =
            keyframe->mouth_open / 96;
        draw_emissive_robot_eye(
            canvas, render_key, motion, true,
            48, 52, eye_width, 31 + speech_stretch,
            8, mint, black, true);
        draw_emissive_robot_eye(
            canvas, render_key, motion, false,
            112, 52, eye_width, 31 + speech_stretch,
            8, mint, black, true);
        /*
         * Vector/Cozmo-style faces communicate through the eyes.  PCM still
         * gives the pair a restrained width pulse above, but no orphan line
         * or giant ring competes with the eye performance.
         */
        (void)mouth_dark;
        break;
    }
    case FACE_RENDER_COZMO_CUBIC: {
        const uint16_t black = RGB565(2, 6, 12);
        const uint16_t blue = RGB565(58, 186, 255);
        const uint16_t deep = RGB565(4, 20, 38);
        canvas_clear(canvas, black);
        /*
         * The original pair floated in empty black and read as two cyan
         * bars during a blink.  A restrained display bezel keeps the eye
         * acting recognisably Cozmo-like while making it a constructed
         * character rather than two independent glyphs.
         */
        fill_round_rect(canvas, 7, 13, 146, 91, 20, deep);
        fill_round_rect(canvas, 12, 18, 136, 81, 16, black);
        fill_rect(canvas, 66, 96, 28, 2, RGB565(14, 60, 88));
        const int32_t squash =
            render_key->affect_valence > 24
                ? render_key->affect_valence / 24
                : 0;
        const int32_t speech_width =
            keyframe->mouth_open / 30;
        const int32_t speech_height =
            keyframe->mouth_open / 112;
        draw_emissive_robot_eye(
            canvas, render_key, motion, true,
            48, 53, 35 + squash + speech_width,
            33 - squash / 2 + speech_height,
            5, blue, black, false);
        draw_emissive_robot_eye(
            canvas, render_key, motion, false,
            112, 53, 35 + squash + speech_width,
            33 - squash / 2 + speech_height,
            5, blue, black, false);
        break;
    }
    case FACE_RENDER_ROBOEYES_ALERT:
    case FACE_RENDER_ROBOEYES_SOFT: {
        const bool alert = profile == FACE_RENDER_ROBOEYES_ALERT;
        const uint16_t background =
            alert ? RGB565(18, 4, 6) : RGB565(2, 12, 13);
        const uint16_t foreground =
            alert ? RGB565(255, 64, 48) : RGB565(96, 255, 180);
        canvas_clear(canvas, background);
        const int32_t shake =
            alert && keyframe->mouth_open > 150U
                ? (motion->pulse > 127U ? 2 : -2)
                : 0;
        draw_emissive_robot_eye(
            canvas, render_key, motion, true,
            47 + shake, 53, alert ? 48 : 45, 34,
            alert ? 5 : 13, foreground, background, !alert);
        draw_emissive_robot_eye(
            canvas, render_key, motion, false,
            113 + shake, 53, alert ? 48 : 45, 34,
            alert ? 5 : 13, foreground, background, !alert);
        draw_robot_mouth_rig(
            canvas, render_key, 80 + shake, 94 + bob,
            foreground, RGB565(3, 14, 17),
            RGB565(240, 255, 244), RGB565(255, 98, 118));
        break;
    }
    case FACE_RENDER_M5_AVATAR_CLASSIC: {
        const uint16_t cream = RGB565(250, 238, 212);
        const uint16_t ink = RGB565(26, 36, 42);
        canvas_clear(canvas, cream);
        draw_basic_eyes(
            canvas, 50, 110, 48 + bob, 13, 19, motion,
            keyframe->eye_left_open, keyframe->eye_right_open,
            ink, ink, ink);
        draw_brows(canvas, 50, 110, 28 + bob, 18, motion, ink, false);
        draw_robot_mouth_rig(
            canvas, render_key, 80, 87 + bob,
            ink, RGB565(82, 30, 40), RGB565(255, 250, 232),
            RGB565(224, 86, 102));
        break;
    }
    case FACE_RENDER_M5_AVATAR_MANGA: {
        const uint16_t pink = RGB565(255, 228, 238);
        const uint16_t ink = RGB565(48, 30, 58);
        const uint16_t accent = RGB565(238, 80, 136);
        canvas_clear(canvas, pink);
        const int32_t blink =
            eye_open(keyframe->eye_left_open, motion) / 20;
        draw_quadratic(
            canvas, (point_t){24, 49 + bob},
            (point_t){48, 38 + bob - blink},
            (point_t){68, 50 + bob}, 4, ink);
        draw_quadratic(
            canvas, (point_t){92, 50 + bob},
            (point_t){112, 38 + bob - blink},
            (point_t){136, 49 + bob}, 4, ink);
        draw_brows(canvas, 48, 112, 30 + bob, 17, motion, accent, true);
        fill_ellipse_blend(canvas, 37, 69 + bob, 15, 7, accent, 75U);
        fill_ellipse_blend(canvas, 123, 69 + bob, 15, 7, accent, 75U);
        draw_robot_mouth_rig(
            canvas, render_key, 80, 83 + bob,
            accent, RGB565(82, 22, 58), RGB565(255, 248, 252),
            RGB565(255, 132, 174));
        break;
    }
    case FACE_RENDER_EVE_MINIMAL: {
        const uint16_t black = RGB565(0, 2, 5);
        const uint16_t blue = RGB565(42, 210, 255);
        const uint16_t core = RGB565(190, 250, 255);
        canvas_clear(canvas, black);
        const int32_t centers[] = {52, 108};
        const uint8_t openings[] = {
            eye_open(keyframe->eye_left_open, motion),
            eye_open(keyframe->eye_right_open, motion),
        };
        for (size_t eye = 0U; eye < 2U; ++eye) {
            const bool left = eye == 0U;
            const int32_t outer =
                left ? render_key->brow_outer_left
                     : render_key->brow_outer_right;
            const int32_t inner = render_key->brow_inner;
            const int32_t left_lift =
                clamp_i16((left ? outer : inner) / 22, -5, 5);
            const int32_t right_lift =
                clamp_i16((left ? inner : outer) / 22, -5, 5);
            const int32_t half_height =
                max_i32(2, 13 * openings[eye] / 255);
            const int32_t half_width =
                22 + render_key->affect_arousal / 96 +
                keyframe->mouth_open / 128;
            const int32_t center_x =
                centers[eye] + motion->gaze_x;
            const int32_t center_y =
                57 + motion->gaze_y + bob;
            draw_glow_ellipse(
                canvas, center_x, center_y,
                half_width + 2, half_height + 2, blue,
                (uint8_t)(112 + keyframe->mouth_open / 4));
            const point_t eye_shape[] = {
                {(int16_t)(center_x - half_width),
                 (int16_t)(center_y + left_lift / 2)},
                {(int16_t)(center_x - half_width / 2),
                 (int16_t)(center_y - half_height - left_lift)},
                {(int16_t)(center_x + half_width / 2),
                 (int16_t)(center_y - half_height - right_lift)},
                {(int16_t)(center_x + half_width),
                 (int16_t)(center_y + right_lift / 2)},
                {(int16_t)(center_x + half_width / 2),
                 (int16_t)(center_y + half_height)},
                {(int16_t)(center_x - half_width / 2),
                 (int16_t)(center_y + half_height)},
            };
            fill_polygon(canvas, eye_shape, 6U, blue);
            if (openings[eye] > 72U) {
                fill_ellipse(
                    canvas,
                    center_x + (left ? -3 : 3),
                    center_y - half_height / 4,
                    3,
                    max_i32(1, half_height / 3),
                    core);
            }
        }
        break;
    }
    case FACE_RENDER_JIBO_ORB: {
        const uint16_t black = RGB565(2, 6, 10);
        const uint16_t cyan = RGB565(44, 212, 234);
        const uint16_t violet = RGB565(154, 90, 255);
        canvas_clear(canvas, black);
        draw_glow_ellipse(
            canvas, 80 + motion->gaze_x, 58 + motion->gaze_y,
            39 + keyframe->mouth_open / 28,
            39 + keyframe->mouth_open / 28,
            cyan, (uint8_t)(90 + motion->pulse / 2));
        fill_ellipse(
            canvas, 80 + motion->gaze_x, 58 + motion->gaze_y,
            27, 27, black);
        const int32_t level = 2 + keyframe->mouth_open / 36;
        for (int32_t index = 0; index < 7; ++index) {
            const int32_t height =
                3 + ((index * 23 + keyframe->mouth_width) % level);
            fill_rect(
                canvas, 58 + index * 7,
                59 - height / 2 + motion->gaze_y,
                3, height, index & 1 ? violet : cyan);
        }
        draw_line(
            canvas, 48, 95, 112, 95, 2,
            blend565(cyan, violet, motion->pulse));
        break;
    }
    default:
        break;
    }
}

static void draw_eye_outline(
    canvas_t *canvas,
    int32_t center_x,
    int32_t center_y,
    int32_t radius_x,
    int32_t radius_y,
    uint8_t openness,
    const motion_t *motion,
    uint16_t outline,
    uint16_t sclera,
    uint16_t iris)
{
    const int32_t height = max_i32(1, radius_y * openness / 255);
    fill_ellipse(
        canvas, center_x, center_y, radius_x + 2, height + 2, outline);
    fill_ellipse(
        canvas, center_x, center_y, radius_x, height, sclera);
    if (openness > 28U) {
        const int32_t gx =
            clamp_i16(motion->gaze_x, -radius_x / 2, radius_x / 2);
        const int32_t gy =
            clamp_i16(motion->gaze_y, -height / 2, height / 2);
        fill_ellipse(
            canvas, center_x + gx, center_y + gy,
            max_i32(3, radius_y * 3 / 5),
            max_i32(2, height * 4 / 5), iris);
        fill_ellipse(
            canvas, center_x + gx, center_y + gy,
            max_i32(1, radius_y / 4),
            max_i32(1, height / 2), outline);
        fill_ellipse(
            canvas, center_x + gx - 3, center_y + gy - 3,
            2, 2, RGB565(255, 255, 255));
    }
}

static void render_eye_study(
    canvas_t *canvas,
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    const motion_t *motion)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    switch (profile) {
    case FACE_RENDER_SACCADE_LAB: {
        const uint16_t background = RGB565(236, 239, 232);
        const uint16_t ink = RGB565(28, 38, 42);
        const uint16_t orange = RGB565(242, 112, 44);
        canvas_clear(canvas, background);
        draw_line(canvas, 80, 10, 80, 110, 1, RGB565(198, 204, 198));
        draw_line(canvas, 10, 60, 150, 60, 1, RGB565(198, 204, 198));
        draw_eye_outline(
            canvas, 48, 58, 29, 19,
            eye_open(keyframe->eye_left_open, motion), motion,
            ink, RGB565(255, 255, 252), orange);
        draw_eye_outline(
            canvas, 112, 58, 29, 19,
            eye_open(keyframe->eye_right_open, motion), motion,
            ink, RGB565(255, 255, 252), orange);
        draw_brows(canvas, 48, 112, 34, 18, motion, ink, true);
        draw_robot_mouth_rig(
            canvas, render_key, 80, 93,
            orange, RGB565(84, 32, 24), RGB565(255, 248, 220),
            RGB565(238, 86, 92));
        const int32_t target_x = 80 + motion->gaze_x * 5;
        const int32_t target_y = 60 + motion->gaze_y * 4;
        draw_line(canvas, target_x - 4, target_y, target_x + 4, target_y, 1, orange);
        draw_line(canvas, target_x, target_y - 4, target_x, target_y + 4, 1, orange);
        for (int32_t index = 0; index < 5; ++index) {
            fill_rect(
                canvas, 10 + index * 10, 106,
                6, index <= motion->pulse / 52 ? 4 : 1, orange);
        }
        break;
    }
    case FACE_RENDER_BROW_DIALOGUE: {
        const uint16_t skin = RGB565(224, 194, 158);
        const uint16_t ink = RGB565(52, 38, 34);
        canvas_clear(canvas, skin);
        draw_basic_eyes(
            canvas, 50, 110, 58, 22, 13, motion,
            keyframe->eye_left_open, keyframe->eye_right_open,
            RGB565(255, 250, 234), RGB565(76, 112, 94), ink);
        const int32_t audio_lift = keyframe->mouth_open / 10;
        draw_quadratic(
            canvas, (point_t){26, (int16_t)(38 - audio_lift)},
            (point_t){48, (int16_t)(25 - motion->brow)},
            (point_t){70, (int16_t)(40 - audio_lift / 2)},
            5, ink);
        draw_quadratic(
            canvas, (point_t){90, (int16_t)(40 - audio_lift / 2)},
            (point_t){112, (int16_t)(25 - motion->brow)},
            (point_t){136, (int16_t)(38 - audio_lift)},
            5, ink);
        draw_robot_mouth_line(
            canvas, keyframe, 80, 94 + keyframe->mouth_open / 40, ink);
        break;
    }
    case FACE_RENDER_LID_ANTICIPATION: {
        const uint16_t background = RGB565(18, 26, 40);
        const uint16_t sclera = RGB565(224, 232, 236);
        const uint16_t iris = RGB565(88, 196, 244);
        motion_t lid_motion = *motion;
        /*
         * This profile already animates large authored lids.  A full global
         * blink on top collapsed thousands of pixels in one frame, so retain
         * a readable aperture and let the lid rig carry the close.
         */
        lid_motion.blink = (uint8_t)max_i32(220, lid_motion.blink);
        canvas_clear(canvas, background);
        const uint8_t anticipation = (uint8_t)min_i32(
            255, keyframe->eye_left_open +
                     keyframe->mouth_open / 4);
        draw_eye_outline(
            canvas, 49, 61, 32, 23,
            eye_open(anticipation, &lid_motion), &lid_motion,
            RGB565(2, 6, 12), sclera, iris);
        draw_eye_outline(
            canvas, 111, 61, 32, 23,
            eye_open(
                (uint8_t)min_i32(
                    255, anticipation + keyframe->mouth_round / 8),
                &lid_motion),
            &lid_motion, RGB565(2, 6, 12), sclera, iris);
        const int32_t lid_drop =
            (255 - eye_open(anticipation, &lid_motion)) / 9;
        fill_polygon(
            canvas,
            (point_t[]){{15, 32}, {82, 32}, {72, 45 + lid_drop},
                        {48, 39 + lid_drop}, {22, 47 + lid_drop}},
            5U, background);
        fill_polygon(
            canvas,
            (point_t[]){{78, 32}, {145, 32}, {138, 47 + lid_drop},
                        {112, 39 + lid_drop}, {88, 45 + lid_drop}},
            5U, background);
        draw_line(canvas, 16, 33, 73, 40 + lid_drop, 2, iris);
        draw_line(canvas, 87, 40 + lid_drop, 144, 33, 2, iris);
        draw_robot_mouth_rig(
            canvas, render_key, 80, 98,
            iris, RGB565(3, 10, 18), RGB565(244, 250, 252),
            RGB565(242, 96, 142));
        break;
    }
    case FACE_RENDER_IRIS_PARALLAX: {
        const uint16_t background = RGB565(14, 12, 16);
        const uint16_t sclera = RGB565(230, 222, 196);
        const uint16_t outline = RGB565(44, 28, 24);
        canvas_clear(canvas, background);
        for (const int32_t eye_x[] = {48, 112}, *it = eye_x;
             it < eye_x + 2; ++it) {
            const uint8_t open = eye_open(
                it == eye_x ? keyframe->eye_left_open
                            : keyframe->eye_right_open,
                motion);
            const int32_t eye_h = max_i32(1, 25 * open / 255);
            fill_ellipse(canvas, *it, 60, 35, eye_h + 2, outline);
            fill_ellipse(canvas, *it, 60, 33, eye_h, sclera);
            if (open > 30U) {
                const int32_t gx = motion->gaze_x * 2;
                const int32_t gy = motion->gaze_y * 2;
                fill_ellipse(
                    canvas, *it + gx, 60 + gy, 15, 18,
                    RGB565(42, 126, 130));
                fill_ellipse(
                    canvas, *it + gx, 60 + gy, 11, 15,
                    RGB565(78, 180, 150));
                fill_ellipse(
                    canvas, *it + gx, 60 + gy, 5, 13, outline);
                for (int32_t ray = -8; ray <= 8; ray += 4) {
                    draw_line(
                        canvas, *it + gx + ray / 2, 60 + gy - 13,
                        *it + gx + ray, 60 + gy + 13, 1,
                        RGB565(180, 214, 156));
                }
                fill_ellipse(
                    canvas, *it + gx - 5, 54 + gy, 3, 4,
                    RGB565(255, 255, 245));
            }
        }
        draw_brows(
            canvas, 48, 112, 29, 18, motion,
            RGB565(206, 194, 164), true);
        draw_robot_mouth_rig(
            canvas, render_key, 80, 100,
            RGB565(188, 152, 126), RGB565(34, 16, 20),
            RGB565(250, 242, 212), RGB565(212, 76, 96));
        break;
    }
    case FACE_RENDER_SLEEP_WAKE: {
        const uint16_t lavender = RGB565(42, 34, 68);
        const uint16_t glow = RGB565(206, 178, 255);
        canvas_clear(canvas, lavender);
        const bool speaking =
            (keyframe->flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U;
        for (const int32_t eye_x[] = {50, 110}, *it = eye_x;
             it < eye_x + 2; ++it) {
            const uint8_t requested_open =
                it == eye_x ? keyframe->eye_left_open
                            : keyframe->eye_right_open;
            const uint8_t wake = speaking
                ? (uint8_t)max_i32(
                      requested_open,
                      min_i32(255, 92 + keyframe->mouth_open / 2))
                : requested_open;
            const int32_t height =
                max_i32(2, 22 * mul_u8(wake, motion->blink) / 255);
            draw_glow_ellipse(
                canvas, *it + motion->gaze_x, 57 + motion->gaze_y,
                23, height, glow,
                (uint8_t)(80 + keyframe->mouth_open / 3));
        }
        draw_brows(
            canvas, 50, 110, 30, 18, motion,
            blend565(glow, RGB565(255, 255, 255), 54U), true);
        draw_robot_mouth_rig(
            canvas, render_key, 80, 91,
            glow, RGB565(18, 12, 34), RGB565(248, 242, 255),
            RGB565(226, 112, 190));
        break;
    }
    case FACE_RENDER_CURIOUS_TILT: {
        const uint16_t background = RGB565(244, 218, 120);
        const uint16_t ink = RGB565(42, 52, 62);
        const uint16_t blue = RGB565(72, 142, 186);
        canvas_clear(canvas, background);
        const int32_t tilt = clamp_i16(motion->tilt, -8, 8);
        draw_eye_outline(
            canvas, 48 - tilt, 57 + tilt, 27 + max_i32(0, -tilt),
            19 + max_i32(0, -tilt / 2),
            eye_open(keyframe->eye_left_open, motion), motion,
            ink, RGB565(255, 252, 224), blue);
        draw_eye_outline(
            canvas, 110 - tilt, 55 - tilt, 27 + max_i32(0, tilt),
            19 + max_i32(0, tilt / 2),
            eye_open(keyframe->eye_right_open, motion), motion,
            ink, RGB565(255, 252, 224), blue);
        draw_line(
            canvas, 22 - tilt, 31 + tilt - motion->brow,
            67 - tilt, 37 + tilt - motion->brow / 2, 4, ink);
        draw_line(
            canvas, 88 - tilt, 35 - tilt - motion->brow / 2,
            137 - tilt, 27 - tilt - motion->brow, 4, ink);
        draw_robot_mouth_rig(
            canvas, render_key, 79 - tilt, 94,
            ink, RGB565(80, 32, 38), RGB565(255, 250, 228),
            RGB565(220, 80, 104));
        break;
    }
    case FACE_RENDER_DOT_MATRIX_EYES: {
        const uint16_t black = RGB565(1, 3, 2);
        const int32_t positive =
            max_i32(0, render_key->affect_valence);
        const int32_t negative =
            max_i32(0, -render_key->affect_valence);
        const uint16_t green = RGB565(
            54 + positive * 3 / 4 +
                render_key->affect_arousal / 10,
            224 + positive / 4,
            104 + negative / 2 +
                render_key->affect_arousal / 5);
        const uint16_t dim = RGB565(8, 50, 24);
        canvas_clear(canvas, black);
        for (int32_t eye = 0; eye < 2; ++eye) {
            const int32_t base_x = eye == 0 ? 20 : 90;
            const uint8_t requested_open =
                eye == 0 ? keyframe->eye_left_open
                         : keyframe->eye_right_open;
            const int32_t open = clamp_i16(
                (eye_open(requested_open, motion) + 24) / 36,
                1, 7);
            const int32_t outer =
                eye == 0 ? render_key->brow_outer_left
                         : render_key->brow_outer_right;
            const int32_t slope = clamp_i16(
                (render_key->brow_inner - outer) / 22,
                -3, 3);
            for (int32_t row = 0; row < 7; ++row) {
                for (int32_t column = 0; column < 8; ++column) {
                    const int32_t dx = column - 3 - motion->gaze_x / 4;
                    const int32_t lid_y =
                        slope * (column - 3) / 4;
                    const int32_t dy =
                        row - 3 - motion->gaze_y / 3 - lid_y;
                    const bool lit =
                        abs_i32(dy) <= open / 2 &&
                        (dx * dx + dy * dy) <= 13;
                    fill_rect(
                        canvas, base_x + column * 6,
                        38 + row * 6, 5, 5, lit ? green : dim);
                }
            }
        }
        for (int32_t eye = 0; eye < 2; ++eye) {
            const int32_t base_x = eye == 0 ? 20 : 90;
            const int32_t outer =
                eye == 0 ? render_key->brow_outer_left
                         : render_key->brow_outer_right;
            const int32_t slope = clamp_i16(
                (render_key->brow_inner - outer) / 20,
                -3, 3);
            for (int32_t column = 0; column < 6; ++column) {
                const int32_t y =
                    28 + slope * (column - 2) / 3 -
                    motion->brow / 5;
                fill_rect(
                    canvas, base_x + 6 + column * 6,
                    y, 5, 4, green);
            }
        }
        if (render_key->cheek > 20U) {
            const int32_t cheek_width =
                4 + render_key->cheek / 28;
            fill_rect(canvas, 24, 82, cheek_width, 3, green);
            fill_rect(
                canvas, 136 - cheek_width, 82,
                cheek_width, 3, green);
        }
        const int32_t mouth_slots = clamp_i16(
            4 + keyframe->mouth_width / 24 -
                keyframe->mouth_round / 38,
            4, 12);
        const int32_t first_slot = (12 - mouth_slots) / 2;
        const int32_t jaw = clamp_i16(
            keyframe->mouth_open / 34 +
                keyframe->mouth_round / 80,
            0, 8);
        const int32_t smile =
            ((int32_t)render_key->mouth_corner_left +
             render_key->mouth_corner_right) /
            2;
        const int32_t lift = clamp_i16(smile / 18, -6, 6);
        const int32_t half_span = max_i32(1, mouth_slots - 1);
        for (int32_t index = 0; index < 12; ++index) {
            const bool active =
                index >= first_slot &&
                index < first_slot + mouth_slots;
            if (!active) {
                fill_rect(canvas, 44 + index * 6, 96, 4, 3, dim);
                continue;
            }
            const int32_t local = index - first_slot;
            const int32_t distance =
                abs_i32(local * 2 - half_span);
            const int32_t curve =
                lift * (half_span - distance) /
                max_i32(1, half_span);
            const int32_t y = 95 + curve;
            fill_rect(
                canvas, 44 + index * 6, y - jaw / 2,
                4, 3, green);
            if (jaw >= 3) {
                fill_rect(
                    canvas, 44 + index * 6, y + jaw / 2,
                    4, 3, green);
            }
        }
        break;
    }
    case FACE_RENDER_CAT_OPTICS: {
        const uint16_t black = RGB565(3, 4, 8);
        const uint16_t gold = RGB565(248, 198, 42);
        const uint16_t green = RGB565(130, 232, 92);
        canvas_clear(canvas, black);
        const int32_t eye_centers[] = {48, 112};
        for (size_t eye = 0U; eye < 2U; ++eye) {
            const int32_t eye_x = eye_centers[eye];
            const uint8_t open = eye_open(
                eye == 0U ? keyframe->eye_left_open
                          : keyframe->eye_right_open,
                motion);
            const int32_t height = max_i32(2, 17 * open / 255);
            const int32_t center_x = eye_x + motion->gaze_x;
            const int32_t center_y = 57 + motion->gaze_y;
            const int32_t outer =
                eye == 0U ? render_key->brow_outer_left
                          : render_key->brow_outer_right;
            const int32_t inner = render_key->brow_inner;
            const int32_t left_lift =
                clamp_i16(
                    ((eye == 0U ? outer : inner) - inner) / 20,
                    -5, 5);
            const int32_t right_lift =
                clamp_i16(
                    ((eye == 0U ? inner : outer) - inner) / 20,
                    -5, 5);
            const point_t outer_eye[] = {
                {(int16_t)(center_x - 31),
                 (int16_t)(center_y + left_lift)},
                {(int16_t)(center_x - 10),
                 (int16_t)(center_y - height)},
                {(int16_t)(center_x + 30),
                 (int16_t)(center_y + right_lift)},
                {(int16_t)(center_x + 10),
                 (int16_t)(center_y + height)},
            };
            fill_polygon(
                canvas, outer_eye, 4U, gold);
            if (open > 42U) {
                fill_ellipse(
                    canvas, center_x, center_y,
                    13, max_i32(2, height - 2), green);
                const int32_t pupil_height =
                    max_i32(2, height * 3 / 4);
                fill_round_rect(
                    canvas, center_x - 2,
                    center_y - pupil_height,
                    4, pupil_height * 2, 2, black);
                put_pixel(
                    canvas, center_x - 3,
                    center_y - max_i32(1, height / 3),
                    RGB565(244, 255, 190));
            }
        }
        draw_brows(
            canvas, 48, 112, 35, 21, motion, gold, true);
        const int32_t whisker_y = 91 + keyframe->mouth_open / 72;
        draw_line(canvas, 20, whisker_y - 5, 62, whisker_y, 1, gold);
        draw_line(canvas, 98, whisker_y, 140, whisker_y - 5, 1, gold);
        fill_polygon(
            canvas,
            (point_t[]){{75, 82}, {85, 82}, {80, 88}},
            3U, gold);
        draw_robot_mouth_rig(
            canvas, render_key, 80, 97 + motion->bob,
            gold, RGB565(30, 10, 8),
            RGB565(255, 242, 176), RGB565(238, 88, 94));
        break;
    }
    default:
        break;
    }
}

static void draw_polygon_mouth(
    canvas_t *canvas,
    const face_keyframe_t *keyframe,
    int32_t center_x,
    int32_t center_y,
    int32_t scale,
    uint16_t lip,
    uint16_t interior)
{
    const int32_t half_width =
        scale * (12 + keyframe->mouth_width / 12) -
        scale * keyframe->mouth_round / 32;
    const int32_t jaw =
        scale * (2 + keyframe->mouth_open / 20);
    const int32_t corner =
        scale * ((int32_t)keyframe->mouth_press - 96) / 64;
    const point_t outer[] = {
        {(int16_t)(center_x - half_width), (int16_t)(center_y + corner)},
        {(int16_t)(center_x - half_width / 2), (int16_t)(center_y - jaw / 4)},
        {(int16_t)center_x, (int16_t)(center_y - jaw / 3)},
        {(int16_t)(center_x + half_width / 2), (int16_t)(center_y - jaw / 4)},
        {(int16_t)(center_x + half_width), (int16_t)(center_y + corner)},
        {(int16_t)(center_x + half_width / 2), (int16_t)(center_y + jaw)},
        {(int16_t)center_x, (int16_t)(center_y + jaw + scale * 2)},
        {(int16_t)(center_x - half_width / 2), (int16_t)(center_y + jaw)},
    };
    fill_polygon(canvas, outer, 8U, lip);
    if (jaw > scale * 3) {
        const point_t inner[] = {
            {(int16_t)(center_x - half_width + scale * 3),
             (int16_t)(center_y + scale)},
            {(int16_t)center_x, (int16_t)(center_y)},
            {(int16_t)(center_x + half_width - scale * 3),
             (int16_t)(center_y + scale)},
            {(int16_t)center_x, (int16_t)(center_y + jaw)},
        };
        fill_polygon(canvas, inner, 4U, interior);
    }
}

static void render_mouth_study(
    canvas_t *canvas,
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    const motion_t *motion)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const int32_t bob = motion->bob;
    switch (profile) {
    case FACE_RENDER_PRESTON_SPRITES: {
        const uint16_t background = RGB565(34, 54, 72);
        const uint16_t skin = RGB565(238, 184, 134);
        const uint16_t ink = RGB565(38, 22, 28);
        const uint16_t lip = RGB565(188, 52, 70);
        canvas_clear(canvas, background);
        fill_rect(canvas, 14, 12 + bob, 132, 96, RGB565(14, 24, 34));
        fill_rect(canvas, 18, 16 + bob, 124, 88, skin);
        draw_basic_eyes(
            canvas, 50, 110, 42 + bob, 12, 7, motion,
            keyframe->eye_left_open, keyframe->eye_right_open,
            RGB565(252, 246, 224), RGB565(42, 110, 124), ink);
        draw_brows(
            canvas, 50, 110, 31 + bob, 11, motion, ink, true);
        const uint8_t shape = mouth_sprite_index(render_key);
        draw_mouth_sprite(
            canvas, 80, 76 + bob, 4, shape, lip, ink,
            RGB565(255, 246, 218));
        for (int32_t index = 0; index < 9; ++index) {
            fill_rect(
                canvas, 31 + index * 11, 111, 7, 3,
                index == shape ? RGB565(255, 204, 58)
                               : RGB565(74, 92, 102));
        }
        break;
    }
    case FACE_RENDER_POLYGON_JALI: {
        const uint16_t background = RGB565(236, 225, 204);
        const uint16_t ink = RGB565(42, 36, 40);
        const uint16_t coral = RGB565(222, 72, 82);
        canvas_clear(canvas, background);
        draw_line(canvas, 80, 10, 80, 110, 1, RGB565(194, 180, 164));
        draw_line(canvas, 12, 66, 148, 66, 1, RGB565(194, 180, 164));
        draw_basic_eyes(
            canvas, 54, 106, 39, 16, 9, motion,
            keyframe->eye_left_open, keyframe->eye_right_open,
            RGB565(255, 252, 240), RGB565(80, 108, 104), ink);
        draw_brows(canvas, 54, 106, 25, 13, motion, ink, true);
        draw_polygon_mouth(
            canvas, keyframe, 80, 76 + bob, 1, coral, ink);
        const int32_t jaw_axis = keyframe->mouth_open * 54 / 255;
        const int32_t lip_axis =
            ((int32_t)keyframe->mouth_width -
             (int32_t)keyframe->mouth_round) *
            32 / 255;
        fill_rect(canvas, 11, 103 - jaw_axis, 5, jaw_axis, coral);
        fill_rect(
            canvas, 80 + lip_axis, 108, 4, 7,
            RGB565(48, 130, 172));
        break;
    }
    case FACE_RENDER_BEZIER_RIBBON: {
        const uint16_t background = RGB565(246, 242, 226);
        const uint16_t ink = RGB565(32, 42, 50);
        const uint16_t blue = RGB565(44, 126, 186);
        canvas_clear(canvas, background);
        draw_basic_eyes(
            canvas, 48, 112, 43, 18, 10, motion,
            keyframe->eye_left_open, keyframe->eye_right_open,
            RGB565(255, 255, 248), blue, ink);
        draw_brows(canvas, 48, 112, 25, 16, motion, ink, true);
        const int32_t half =
            18 + keyframe->mouth_width / 5 -
            keyframe->mouth_round / 10;
        const int32_t open = keyframe->mouth_open / 5;
        const point_t left = {(int16_t)(80 - half), (int16_t)(78 + bob)};
        const point_t right = {(int16_t)(80 + half), (int16_t)(78 + bob)};
        draw_quadratic(
            canvas, left, (point_t){80, (int16_t)(70 + bob - open / 5)},
            right, 4, blue);
        draw_quadratic(
            canvas, left, (point_t){80, (int16_t)(78 + bob + open)},
            right, 4, RGB565(224, 70, 108));
        if (open > 8) {
            draw_quadratic(
                canvas,
                (point_t){(int16_t)(80 - half + 3), (int16_t)(80 + bob)},
                (point_t){80, (int16_t)(83 + bob + open / 2)},
                (point_t){(int16_t)(80 + half - 3), (int16_t)(80 + bob)},
                2, ink);
        }
        break;
    }
    case FACE_RENDER_TEETH_TONGUE: {
        const uint16_t background = RGB565(208, 170, 132);
        const uint16_t ink = RGB565(52, 18, 26);
        const uint16_t lip = RGB565(166, 36, 68);
        canvas_clear(canvas, background);
        draw_basic_eyes(
            canvas, 50, 110, 38, 15, 9, motion,
            keyframe->eye_left_open, keyframe->eye_right_open,
            RGB565(250, 244, 218), RGB565(68, 102, 102), ink);
        const int32_t width =
            22 + keyframe->mouth_width / 4 -
            keyframe->mouth_round / 12;
        const int32_t height = 3 + keyframe->mouth_open / 5;
        fill_ellipse(canvas, 80, 79 + bob, width + 4, height + 4, lip);
        fill_ellipse(canvas, 80, 79 + bob, width, height, ink);
        if (height > 7) {
            const int32_t teeth_height =
                min_i32(height, 3 + keyframe->mouth_teeth / 14);
            fill_rect(
                canvas, 80 - width + 3, 79 + bob - height,
                width * 2 - 6, teeth_height, RGB565(255, 248, 220));
        }
        if (height > 14) {
            fill_ellipse(
                canvas, 80, 83 + bob + height / 3,
                width * 2 / 3, max_i32(2, height / 4),
                RGB565(224, 88, 112));
        }
        break;
    }
    case FACE_RENDER_LED_VU_MOUTH: {
        const uint16_t black = RGB565(1, 3, 4);
        const uint16_t dim = RGB565(8, 38, 34);
        canvas_clear(canvas, black);
        for (int32_t eye = 0; eye < 2; ++eye) {
            const int32_t base = eye == 0 ? 34 : 96;
            const int32_t height =
                max_i32(2, 12 * eye_open(
                    eye == 0 ? keyframe->eye_left_open
                             : keyframe->eye_right_open,
                    motion) / 255);
            fill_rect(
                canvas, base + motion->gaze_x, 35 + motion->gaze_y,
                30, height, RGB565(44, 206, 180));
        }
        const int32_t active = keyframe->mouth_open / 18;
        for (int32_t column = 0; column < 14; ++column) {
            const int32_t distance = abs_i32(column * 2 - 13);
            const int32_t target =
                max_i32(1, active - distance / 2 +
                               ((column * 7 + keyframe->mouth_width) & 3));
            for (int32_t row = 0; row < 8; ++row) {
                uint16_t color = dim;
                if (row < target) {
                    color = row > 5 ? RGB565(255, 68, 42)
                                    : row > 3 ? RGB565(255, 194, 42)
                                              : RGB565(56, 238, 126);
                }
                fill_rect(
                    canvas, 25 + column * 8,
                    100 - row * 7, 5, 4, color);
            }
        }
        break;
    }
    case FACE_RENDER_ORIGAMI_MASK: {
        const uint16_t paper = RGB565(232, 226, 218);
        const uint16_t shadow = RGB565(126, 132, 146);
        const uint16_t ink = RGB565(36, 42, 54);
        const uint16_t red = RGB565(226, 64, 72);
        canvas_clear(canvas, RGB565(18, 22, 30));
        fill_polygon(
            canvas,
            (point_t[]){{80, 8 + bob}, {140, 42 + bob},
                        {126, 100 + bob}, {80, 114 + bob},
                        {34, 100 + bob}, {20, 42 + bob}},
            6U, paper);
        fill_polygon(
            canvas,
            (point_t[]){{80, 8 + bob}, {140, 42 + bob},
                        {80, 61 + bob}},
            3U, RGB565(198, 204, 212));
        fill_polygon(
            canvas,
            (point_t[]){{20, 42 + bob}, {80, 61 + bob},
                        {34, 100 + bob}},
            3U, shadow);
        const int32_t left_open =
            max_i32(2, eye_open(keyframe->eye_left_open, motion) / 18);
        const int32_t right_open =
            max_i32(2, eye_open(keyframe->eye_right_open, motion) / 18);
        fill_polygon(
            canvas,
            (point_t[]){{34, 48 + bob}, {66, 41 + bob},
                        {63, 48 + bob + left_open},
                        {39, 53 + bob + left_open}},
            4U, ink);
        fill_polygon(
            canvas,
            (point_t[]){{94, 41 + bob}, {126, 48 + bob},
                        {121, 53 + bob + right_open},
                        {97, 48 + bob + right_open}},
            4U, ink);
        if (left_open > 3) {
            fill_ellipse(
                canvas, 52 + motion->gaze_x,
                50 + bob + motion->gaze_y,
                3, max_i32(2, left_open / 3),
                RGB565(96, 224, 220));
        }
        if (right_open > 3) {
            fill_ellipse(
                canvas, 108 + motion->gaze_x,
                50 + bob + motion->gaze_y,
                3, max_i32(2, right_open / 3),
                RGB565(96, 224, 220));
        }
        draw_brows(
            canvas, 50, 110, 35 + bob, 18,
            motion, shadow, true);
        draw_robot_mouth_rig(
            canvas, render_key, 80, 84 + bob,
            red, ink, RGB565(255, 248, 230),
            RGB565(232, 90, 102));
        draw_line(canvas, 80, 8 + bob, 80, 61 + bob, 1, shadow);
        draw_line(canvas, 20, 42 + bob, 80, 61 + bob, 1, shadow);
        draw_line(canvas, 140, 42 + bob, 80, 61 + bob, 1, shadow);
        break;
    }
    default:
        break;
    }
}

static int32_t ellipse_distance_q8(
    int32_t x,
    int32_t y,
    int32_t center_x,
    int32_t center_y,
    int32_t radius_x,
    int32_t radius_y)
{
    const int32_t nx =
        abs_i32(x - center_x) * 256 / max_i32(1, radius_x);
    const int32_t ny =
        abs_i32(y - center_y) * 256 / max_i32(1, radius_y);
    return max_i32(nx, ny) + min_i32(nx, ny) / 2 - 256;
}

static int32_t circle_distance_q8(
    int32_t x,
    int32_t y,
    int32_t center_x,
    int32_t center_y,
    int32_t radius)
{
    const int32_t dx = abs_i32(x - center_x);
    const int32_t dy = abs_i32(y - center_y);
    const int32_t distance =
        max_i32(dx, dy) + min_i32(dx, dy) / 2;
    return (distance - radius) * 256 / max_i32(1, radius);
}

static int32_t mouth_distance_q8(
    int32_t x,
    int32_t y,
    const face_keyframe_t *keyframe,
    const motion_t *motion)
{
    const int32_t center_x = 40;
    const int32_t half_width =
        9 + keyframe->mouth_width / 15 -
        keyframe->mouth_round / 28;
    const int32_t dx = x - center_x;
    if (abs_i32(dx) > half_width + 5) {
        return 512;
    }
    const int32_t curve =
        ((int32_t)keyframe->mouth_open - 72) / 18;
    const int32_t target_y =
        44 + motion->bob / 2 +
        (dx * dx * curve) /
            max_i32(1, half_width * half_width);
    const int32_t thickness =
        1 + keyframe->mouth_open / 48 +
        keyframe->mouth_round / 96;
    return (abs_i32(y - target_y) - thickness) * 128;
}

static uint8_t glow_from_distance(
    int32_t distance_q8, uint8_t energy)
{
    const int32_t absolute = abs_i32(distance_q8);
    if (absolute >= 640) {
        return 0U;
    }
    const int32_t core = max_i32(0, 255 - absolute * 255 / 640);
    const int32_t boosted =
        core * (128 + energy / 2) / 192;
    return (uint8_t)min_i32(255, boosted);
}

static uint16_t cyber_palette(
    uint8_t phase,
    uint8_t intensity,
    uint8_t variant)
{
    const uint8_t wave_a = triangle_u8(phase, 256U);
    const uint8_t wave_b = triangle_u8((uint32_t)phase + 85U, 256U);
    const uint8_t wave_c = triangle_u8((uint32_t)phase + 171U, 256U);
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    if (variant == 0U) {
        red = (uint8_t)(18U + wave_b / 3U);
        green = (uint8_t)(100U + wave_a / 2U);
        blue = (uint8_t)(150U + wave_c / 3U);
    } else if (variant == 1U) {
        red = (uint8_t)(130U + wave_a / 2U);
        green = (uint8_t)(22U + wave_c / 4U);
        blue = (uint8_t)(105U + wave_b / 2U);
    } else if (variant == 2U) {
        red = (uint8_t)(80U + wave_c / 2U);
        green = (uint8_t)(45U + wave_a / 2U);
        blue = (uint8_t)(125U + wave_b / 2U);
    } else {
        red = (uint8_t)(120U + wave_a / 2U);
        green = (uint8_t)(80U + wave_b / 3U);
        blue = (uint8_t)(20U + wave_c / 5U);
    }
    red = (uint8_t)((uint16_t)red * intensity / 255U);
    green = (uint8_t)((uint16_t)green * intensity / 255U);
    blue = (uint8_t)((uint16_t)blue * intensity / 255U);
    return RGB565(red, green, blue);
}

static void put_half_pixel(
    canvas_t *canvas,
    int32_t x,
    int32_t y,
    uint16_t color)
{
    const int32_t target_x = x * 2;
    const int32_t target_y = y * 2;
    put_pixel(canvas, target_x, target_y, color);
    put_pixel(canvas, target_x + 1, target_y, color);
    put_pixel(canvas, target_x, target_y + 1, color);
    put_pixel(canvas, target_x + 1, target_y + 1, color);
}

static void render_neon_field(
    canvas_t *canvas,
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const uint32_t frame =
        (uint32_t)(((uint64_t)sample_clock * 30U) / 16000U);
    const uint8_t energy =
        (uint8_t)max_i32(render_key->audio_level, keyframe->mouth_open);
    const int32_t eye_height =
        2 + eye_open(keyframe->eye_left_open, motion) / 31;
    const int32_t eye_width = 9 + energy / 42;
    uint8_t palette = 0U;
    if (profile == FACE_RENDER_NEON_SDF_MAGENTA ||
        profile == FACE_RENDER_CRT_CHROMATIC ||
        profile == FACE_RENDER_GLITCH_MASK) {
        palette = 1U;
    } else if (profile == FACE_RENDER_LIQUID_SMIN ||
               profile == FACE_RENDER_PALETTE_PLASMA) {
        palette = 2U;
    }
    for (int32_t y = 0; y < 60; ++y) {
        const int32_t jitter =
            profile == FACE_RENDER_GLITCH_MASK &&
                    ((hash_u32((uint32_t)y + frame * 17U) & 31U) < 3U)
                ? (int32_t)(hash_u32((uint32_t)y + frame) % 7U) - 3
                : 0;
        for (int32_t x = 0; x < 80; ++x) {
            const int32_t px = x + jitter;
            int32_t left_distance = ellipse_distance_q8(
                px, y, 25 + motion->gaze_x / 2,
                27 + motion->gaze_y / 2,
                eye_width, eye_height);
            int32_t right_distance = ellipse_distance_q8(
                px, y, 55 + motion->gaze_x / 2,
                27 + motion->gaze_y / 2,
                eye_width, eye_height);
            const int32_t mouth_distance =
                mouth_distance_q8(px, y, keyframe, motion);
            int32_t distance =
                min_i32(min_i32(left_distance, right_distance),
                        mouth_distance);

            if (profile == FACE_RENDER_LIQUID_SMIN) {
                const int32_t eye_distance =
                    min_i32(left_distance, right_distance);
                const int32_t blend =
                    max_i32(0, 230 - abs_i32(eye_distance - mouth_distance));
                distance =
                    min_i32(eye_distance, mouth_distance) - blend / 3;
            }
            if (profile == FACE_RENDER_CRT_CHROMATIC) {
                left_distance = ellipse_distance_q8(
                    px - 1, y, 25 + motion->gaze_x / 2,
                    27 + motion->gaze_y / 2,
                    eye_width, eye_height);
                right_distance = ellipse_distance_q8(
                    px + 1, y, 55 + motion->gaze_x / 2,
                    27 + motion->gaze_y / 2,
                    eye_width, eye_height);
                distance = min_i32(
                    min_i32(left_distance, right_distance),
                    mouth_distance);
            }
            uint8_t glow = glow_from_distance(distance, energy);
            if (profile == FACE_RENDER_CRT_CHROMATIC && (y & 1) != 0) {
                glow = (uint8_t)((uint16_t)glow * 3U / 5U);
            }
            if (profile == FACE_RENDER_PALETTE_PLASMA) {
                const uint8_t plasma = triangle_u8(
                    (uint32_t)(x * 7 + y * 11) + frame * 5U, 256U);
                glow = (uint8_t)min_i32(
                    255, ((uint16_t)glow * 3U + plasma) / 4U);
            }
            const uint8_t phase = (uint8_t)(
                x * 3 + y * 5 + frame * 4U +
                keyframe->mouth_round / 2U);
            uint16_t color = cyber_palette(phase, glow, palette);
            if (profile == FACE_RENDER_GLITCH_MASK &&
                ((x + y + (int32_t)frame) % 17) == 0) {
                color = RGB565(255, 236, 250);
            }
            put_half_pixel(canvas, x, y, color);
        }
    }
}

typedef struct {
    int8_t eye_width_delta;
    int8_t eye_height_delta;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t left_outer_brow_y;
    int8_t left_inner_brow_y;
    int8_t right_inner_brow_y;
    int8_t right_outer_brow_y;
} cyber_stage_pose_t;

/*
 * The shader field is atmosphere, not facial geometry.  Give every cyber
 * character an authored, Cozmo-like acting pose so its eyes and brows still
 * communicate at the 160x120 contact scale.  Coordinates are small deltas
 * from a fixed head rig and therefore cannot escape the silhouette.
 */
static const cyber_stage_pose_t
    CYBER_STAGE_POSES[FACE_EXPRESSION_COUNT] = {
    [FACE_EXPRESSION_NEUTRAL] =
        {0, 0, 0, 0, 0, 0, 0, 0},
    [FACE_EXPRESSION_WARM] =
        {2, -1, 0, 1, 1, -2, -2, 1},
    [FACE_EXPRESSION_JOY] =
        {4, -4, 0, 1, 1, -3, -3, 1},
    [FACE_EXPRESSION_CONCERN] =
        {1, 2, 0, 2, 1, -6, -6, 1},
    [FACE_EXPRESSION_SURPRISE] =
        {-3, 5, 0, -1, -6, -6, -6, -6},
    [FACE_EXPRESSION_THOUGHTFUL] =
        {-1, 0, -3, -2, -5, -2, 1, 3},
    [FACE_EXPRESSION_SKEPTICAL] =
        {3, -2, 4, 0, 2, 1, -5, -2},
    [FACE_EXPRESSION_DETERMINED] =
        {4, -4, 0, 1, -1, 6, 6, -1},
    [FACE_EXPRESSION_SLEEPY] =
        {3, -5, 0, 2, 3, 3, 3, 3},
    [FACE_EXPRESSION_EXCITED] =
        {2, 4, 0, -1, -7, -7, -7, -7},
    [FACE_EXPRESSION_EMBARRASSED] =
        {1, -2, 4, 2, -3, -1, 1, 3},
};

static const cyber_stage_pose_t *cyber_stage_pose(
    const face_render_key_t *render_key)
{
    const uint8_t expression =
        render_key->stage_expression < FACE_EXPRESSION_COUNT
            ? render_key->stage_expression
            : FACE_EXPRESSION_NEUTRAL;
    return &CYBER_STAGE_POSES[expression];
}

static void draw_contained_cyber_brows(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    int32_t left_x,
    int32_t right_x,
    int32_t base_y,
    int32_t half_width,
    uint16_t color)
{
    const cyber_stage_pose_t *pose =
        cyber_stage_pose(render_key);
    const int32_t authored_weight =
        render_key->expression_weight;
    const int32_t lift = clamp_i16(motion->brow / 6, -3, 4);
    const int32_t inner_action =
        clamp_i16(motion->brow_inner / 32, -3, 3);
    const int32_t left_outer_action =
        clamp_i16(motion->brow_outer_left / 40, -2, 2);
    const int32_t right_outer_action =
        clamp_i16(motion->brow_outer_right / 40, -2, 2);
#define AUTHORED_Y(field)                                                 \
    ((int32_t)pose->field * authored_weight / 255)
    const int32_t left_outer_y = clamp_i16(
        base_y - lift + AUTHORED_Y(left_outer_brow_y) -
            left_outer_action,
        20, 42);
    const int32_t left_inner_y = clamp_i16(
        base_y - lift + AUTHORED_Y(left_inner_brow_y) -
            inner_action,
        20, 42);
    const int32_t right_inner_y = clamp_i16(
        base_y - lift + AUTHORED_Y(right_inner_brow_y) -
            inner_action,
        20, 42);
    const int32_t right_outer_y = clamp_i16(
        base_y - lift + AUTHORED_Y(right_outer_brow_y) -
            right_outer_action,
        20, 42);
#undef AUTHORED_Y
    draw_line(
        canvas, left_x - half_width, left_outer_y,
        left_x + half_width, left_inner_y, 3, color);
    draw_line(
        canvas, right_x - half_width, right_inner_y,
        right_x + half_width, right_outer_y, 3, color);
}

static void draw_cyber_character_rig(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    int32_t head_y,
    int32_t head_radius_x,
    int32_t head_radius_y,
    int32_t left_eye_x,
    int32_t right_eye_x,
    int32_t eye_y,
    int32_t eye_width,
    int32_t eye_height,
    int32_t mouth_y,
    uint16_t dark,
    uint16_t eye_color,
    uint16_t accent,
    uint16_t head_tint)
{
    const cyber_stage_pose_t *pose =
        cyber_stage_pose(render_key);
    const int32_t authored_weight =
        render_key->expression_weight;
    const int32_t pose_gaze_x =
        (int32_t)pose->gaze_x * authored_weight / 255;
    const int32_t pose_gaze_y =
        (int32_t)pose->gaze_y * authored_weight / 255;
    motion_t contained_motion = *motion;
    contained_motion.gaze_x = clamp_i16(
        contained_motion.gaze_x / 2 + pose_gaze_x, -3, 3);
    contained_motion.gaze_y = clamp_i16(
        contained_motion.gaze_y / 2 + pose_gaze_y, -3, 3);
    contained_motion.bob = clamp_i16(
        contained_motion.bob, -1, 1);

    const int32_t width = clamp_i16(
        eye_width +
            (int32_t)pose->eye_width_delta * authored_weight / 255,
        20, 36);
    const int32_t height = clamp_i16(
        eye_height +
            (int32_t)pose->eye_height_delta * authored_weight / 255,
        13, 31);
    const int32_t socket_width = width + 8;
    const int32_t socket_height = height + 7;

    fill_ellipse_blend(
        canvas, 80, head_y,
        head_radius_x, head_radius_y, dark, 236U);
    fill_ellipse_blend(
        canvas, 80, head_y - 2,
        max_i32(8, head_radius_x - 5),
        max_i32(8, head_radius_y - 5),
        head_tint, 42U);
    fill_round_rect(
        canvas,
        left_eye_x - socket_width / 2,
        eye_y - socket_height / 2,
        socket_width, socket_height, 9, dark);
    fill_round_rect(
        canvas,
        right_eye_x - socket_width / 2,
        eye_y - socket_height / 2,
        socket_width, socket_height, 9, dark);
    draw_emissive_robot_eye(
        canvas, render_key, &contained_motion, true,
        left_eye_x, eye_y, width, height, 8,
        eye_color, dark, true);
    draw_emissive_robot_eye(
        canvas, render_key, &contained_motion, false,
        right_eye_x, eye_y, width, height, 8,
        eye_color, dark, true);
    draw_contained_cyber_brows(
        canvas, render_key, &contained_motion,
        left_eye_x, right_eye_x,
        eye_y - height / 2 - 8, width / 2,
        accent);
    draw_robot_mouth_rig(
        canvas, render_key, 80, mouth_y,
        accent, dark, RGB565(248, 252, 255),
        blend565(accent, RGB565(255, 72, 160), 126U));

    if (render_key->cheek > 30U) {
        const uint8_t alpha = (uint8_t)min_i32(
            136, 24 + render_key->cheek);
        fill_ellipse_blend(
            canvas, left_eye_x - 8, mouth_y - 10,
            9, 3, accent, alpha);
        fill_ellipse_blend(
            canvas, right_eye_x + 8, mouth_y - 10,
            9, 3, accent, alpha);
    }
}

static void render_anchored_neon_character(
    canvas_t *canvas,
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    const motion_t *motion)
{
    uint16_t dark = RGB565(2, 7, 13);
    uint16_t eye = RGB565(92, 250, 235);
    uint16_t accent = RGB565(106, 192, 255);
    uint16_t tint = RGB565(10, 78, 92);
    if (profile == FACE_RENDER_NEON_SDF_MAGENTA) {
        dark = RGB565(12, 3, 18);
        eye = RGB565(255, 92, 224);
        accent = RGB565(255, 140, 196);
        tint = RGB565(96, 10, 82);
    } else if (profile == FACE_RENDER_LIQUID_SMIN) {
        dark = RGB565(4, 6, 18);
        eye = RGB565(110, 240, 255);
        accent = RGB565(240, 84, 218);
        tint = RGB565(74, 22, 110);
    } else if (profile == FACE_RENDER_CRT_CHROMATIC) {
        dark = RGB565(8, 3, 15);
        eye = RGB565(248, 94, 226);
        accent = RGB565(104, 232, 255);
        tint = RGB565(94, 12, 74);
    }
    draw_cyber_character_rig(
        canvas, render_key, motion,
        59, 49, 50, 56, 104, 50, 29, 24, 86,
        dark, eye, accent, tint);
}

typedef enum {
    CURATED_SHADER_AURORA = 0,
    CURATED_SHADER_LIQUID,
    CURATED_SHADER_CRT,
} curated_shader_style_t;

static uint16_t curated_shader_color(
    curated_shader_style_t style,
    uint8_t glow,
    uint8_t phase)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    if (style == CURATED_SHADER_AURORA) {
        red = (uint8_t)(
            ((uint16_t)glow * (18U + phase / 5U)) / 255U);
        green = (uint8_t)(
            ((uint16_t)glow * (142U + phase / 3U)) / 255U);
        blue = (uint8_t)(
            ((uint16_t)glow * (190U + phase / 4U)) / 255U);
    } else if (style == CURATED_SHADER_LIQUID) {
        red = (uint8_t)(
            ((uint16_t)glow * (92U + phase / 2U)) / 255U);
        green = (uint8_t)(
            ((uint16_t)glow * (42U + phase / 5U)) / 255U);
        blue = (uint8_t)(
            ((uint16_t)glow * (178U + phase / 4U)) / 255U);
    } else {
        red = (uint8_t)(
            ((uint16_t)glow * (116U + phase / 4U)) / 255U);
        green = (uint8_t)(
            ((uint16_t)glow * (182U + phase / 5U)) / 255U);
        blue = (uint8_t)(
            ((uint16_t)glow * (64U + phase / 8U)) / 255U);
    }
    return RGB565(red, green, blue);
}

/*
 * A deliberately small 80x60 software shader.  It creates a real, moving
 * distance-field atmosphere, while the stable authored silhouette drawn
 * afterwards keeps the result readable as a character rather than a VJ
 * effect.  The 2x write is the same cost profile as the original cyber set.
 */
static void render_curated_shader_field(
    canvas_t *canvas,
    curated_shader_style_t style,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    const uint32_t frame =
        (uint32_t)(((uint64_t)sample_clock * 30U) / 16000U);
    const uint8_t energy = (uint8_t)max_i32(
        render_key->audio_level,
        render_key->controls.mouth_open);
    for (int32_t y = 0; y < 60; ++y) {
        for (int32_t x = 0; x < 80; ++x) {
            const int32_t center_x =
                40 + clamp_i16(motion->gaze_x / 3, -2, 2);
            const int32_t center_y =
                30 + clamp_i16(motion->gaze_y / 4, -2, 2);
            int32_t distance = circle_distance_q8(
                x, y, center_x, center_y,
                22 + energy / 72);
            if (style == CURATED_SHADER_AURORA) {
                const int32_t second = ellipse_distance_q8(
                    x, y, center_x, center_y + 2,
                    30, 20 + energy / 96);
                distance = min_i32(
                    abs_i32(distance + 88),
                    abs_i32(second - 128));
            } else if (style == CURATED_SHADER_LIQUID) {
                const int32_t lobe = circle_distance_q8(
                    x, y,
                    31 + triangle_u8(frame * 5U, 64U) / 16,
                    36, 17 + energy / 96);
                const int32_t blend =
                    max_i32(0, 256 - abs_i32(distance - lobe));
                distance =
                    min_i32(distance, lobe) - blend / 3;
            } else {
                const int32_t rounded_screen = max_i32(
                    abs_i32(x - center_x) * 7 - 232,
                    abs_i32(y - center_y) * 9 - 216);
                distance = abs_i32(rounded_screen);
            }
            const uint8_t ripple = triangle_u8(
                (uint32_t)(
                    x * (style == CURATED_SHADER_CRT ? 3 : 5) +
                    y * (style == CURATED_SHADER_LIQUID ? 8 : 4)) +
                    frame * (style == CURATED_SHADER_AURORA ? 5U : 3U),
                256U);
            const int32_t near_field = max_i32(
                0, 232 - abs_i32(distance));
            uint8_t glow = (uint8_t)min_i32(
                255,
                7 + near_field * (110 + energy / 3) / 232 +
                    ripple / 9);
            if (style == CURATED_SHADER_CRT && (y & 1) != 0) {
                glow = (uint8_t)((uint16_t)glow * 3U / 5U);
            }
            const uint8_t phase = (uint8_t)(
                frame * 3U + x * 2 + y * 3 +
                render_key->controls.mouth_round / 3U);
            put_half_pixel(
                canvas, x, y,
                curated_shader_color(style, glow, phase));
        }
    }
}

static void draw_curated_shader_features(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    int32_t left_eye_x,
    int32_t right_eye_x,
    int32_t eye_y,
    int32_t base_eye_width,
    int32_t base_eye_height,
    int32_t corner_radius,
    int32_t mouth_y,
    uint16_t dark,
    uint16_t eye_color,
    uint16_t accent,
    uint16_t teeth,
    uint16_t tongue)
{
    const cyber_stage_pose_t *pose =
        cyber_stage_pose(render_key);
    const int32_t authored_weight =
        render_key->expression_weight;
    motion_t contained = *motion;
    contained.gaze_x = clamp_i16(
        contained.gaze_x / 2 +
            (int32_t)pose->gaze_x * authored_weight / 255,
        -3, 3);
    contained.gaze_y = clamp_i16(
        contained.gaze_y / 2 +
            (int32_t)pose->gaze_y * authored_weight / 255,
        -3, 3);
    contained.bob = clamp_i16(contained.bob, -1, 1);
    const int32_t eye_width = clamp_i16(
        base_eye_width +
            (int32_t)pose->eye_width_delta *
                authored_weight / 255,
        19, 36);
    const int32_t eye_height = clamp_i16(
        base_eye_height +
            (int32_t)pose->eye_height_delta *
                authored_weight / 255,
        12, 30);
    const int32_t socket_width = eye_width + 7;
    const int32_t socket_height = eye_height + 7;
    fill_round_rect(
        canvas,
        left_eye_x - socket_width / 2,
        eye_y - socket_height / 2,
        socket_width, socket_height,
        corner_radius + 2, dark);
    fill_round_rect(
        canvas,
        right_eye_x - socket_width / 2,
        eye_y - socket_height / 2,
        socket_width, socket_height,
        corner_radius + 2, dark);
    draw_emissive_robot_eye(
        canvas, render_key, &contained, true,
        left_eye_x, eye_y,
        eye_width, eye_height, corner_radius,
        eye_color, dark, true);
    draw_emissive_robot_eye(
        canvas, render_key, &contained, false,
        right_eye_x, eye_y,
        eye_width, eye_height, corner_radius,
        eye_color, dark, true);
    draw_contained_cyber_brows(
        canvas, render_key, &contained,
        left_eye_x, right_eye_x,
        eye_y - eye_height / 2 - 8,
        eye_width / 2, accent);
    draw_robot_mouth_rig(
        canvas, render_key, 80, mouth_y,
        accent, dark, teeth, tongue);
}

static void render_curated_shader_face(
    canvas_t *canvas,
    curated_shader_style_t style,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    render_curated_shader_field(
        canvas, style, render_key, motion, sample_clock);
    const uint8_t expression =
        render_key->stage_expression < FACE_EXPRESSION_COUNT
            ? render_key->stage_expression
            : FACE_EXPRESSION_NEUTRAL;
    const int32_t bob = clamp_i16(motion->bob, -2, 2);
    const int32_t lean = clamp_i16(
        motion->gaze_x / 3 +
            (expression == FACE_EXPRESSION_THOUGHTFUL ? 2 : 0) -
            (expression == FACE_EXPRESSION_CONCERN ? 1 : 0),
        -3, 3);

    if (style == CURATED_SHADER_AURORA) {
        const uint16_t dark = RGB565(2, 12, 20);
        const uint16_t cyan = RGB565(112, 255, 238);
        const uint16_t blue = RGB565(82, 156, 255);
        const uint16_t violet = RGB565(202, 96, 255);
        point_t outer[] = {
            {80 + lean, 8 + bob},
            {113 + lean, 21 + bob},
            {135 + lean, 52 + bob},
            {126 + lean, 91 + bob},
            {105 + lean, 108 + bob},
            {80 + lean, 112 + bob},
            {55 + lean, 108 + bob},
            {34 + lean, 91 + bob},
            {25 + lean, 52 + bob},
            {47 + lean, 21 + bob},
        };
        fill_polygon(
            canvas, outer,
            sizeof(outer) / sizeof(outer[0]), dark);
        fill_ellipse_blend(
            canvas, 80 + lean, 59 + bob,
            46, 48, RGB565(18, 94, 106), 112U);
        fill_ellipse_blend(
            canvas, 80 + lean, 52 + bob,
            35, 34, RGB565(44, 46, 112), 76U);
        for (size_t index = 0U;
             index < sizeof(outer) / sizeof(outer[0]);
             ++index) {
            const point_t first = outer[index];
            const point_t second =
                outer[(index + 1U) %
                      (sizeof(outer) / sizeof(outer[0]))];
            draw_line(
                canvas, first.x, first.y,
                second.x, second.y, 2,
                blend565(cyan, violet, (uint8_t)(index * 22U)));
        }
        const int32_t fin_spread =
            expression == FACE_EXPRESSION_SURPRISE ||
                    expression == FACE_EXPRESSION_EXCITED
                ? 9
                : expression == FACE_EXPRESSION_SLEEPY ? -3 : 3;
        draw_line(
            canvas, 41 + lean, 34 + bob,
            24 - fin_spread, 20 + bob, 2, blue);
        draw_line(
            canvas, 119 + lean, 34 + bob,
            136 + fin_spread, 20 + bob, 2, violet);
        fill_ellipse(
            canvas, 24 - fin_spread, 20 + bob, 3, 3, cyan);
        fill_ellipse(
            canvas, 136 + fin_spread, 20 + bob, 3, 3, violet);
        draw_curated_shader_features(
            canvas, render_key, motion,
            56 + lean, 104 + lean, 51 + bob,
            28, 23, 9, 85 + bob,
            dark, cyan, blend565(blue, violet, 122U),
            RGB565(238, 255, 252), RGB565(255, 92, 188));
    } else if (style == CURATED_SHADER_LIQUID) {
        const uint16_t dark = RGB565(5, 7, 24);
        const uint16_t aqua = RGB565(90, 248, 224);
        const uint16_t pink = RGB565(255, 84, 198);
        const uint16_t purple = RGB565(112, 54, 194);
        const uint16_t lilac = RGB565(198, 142, 255);
        const int32_t crown_x = 83 + lean * 2;
        const int32_t crown_y =
            (expression == FACE_EXPRESSION_SURPRISE ||
             expression == FACE_EXPRESSION_EXCITED)
                ? 9 + bob
                : 14 + bob;
        /*
         * Liquid has its own asymmetrical, teardrop-shaped acting rig.  The
         * first draft reused Aurora's paired rounded sockets and polygon lip,
         * which made the two shader profiles feel like palette swaps.  This
         * silhouette, soft iris pools and JALI-like mouth keep it recognisable
         * even in a monochrome thumbnail.
         */
        const point_t body[] = {
            {crown_x, crown_y},
            {96 + lean, 24 + bob},
            {119 + lean, 29 + bob},
            {132 + lean, 52 + bob},
            {128 + lean, 80 + bob},
            {114 + lean, 105 + bob},
            {94 + lean, 104 + bob},
            {81 + lean, 114 + bob},
            {68 + lean, 104 + bob},
            {45 + lean, 108 + bob},
            {29 + lean, 86 + bob},
            {33 + lean, 56 + bob},
            {49 + lean, 31 + bob},
            {70 + lean, 24 + bob},
        };
        fill_polygon(
            canvas, body, sizeof(body) / sizeof(body[0]), dark);
        fill_ellipse(
            canvas, crown_x, crown_y, 11, 18, dark);
        fill_ellipse(
            canvas, 35 + lean, 87 + bob, 10, 18, dark);
        fill_ellipse(
            canvas, 123 + lean, 86 + bob, 9, 20, dark);
        fill_ellipse_blend(
            canvas, 81 + lean, 61 + bob,
            45, 42, purple, 132U);
        fill_ellipse_blend(
            canvas, 66 + lean, 42 + bob,
            27, 24, aqua, 54U);
        const uint32_t frame =
            (uint32_t)(((uint64_t)sample_clock * 30U) / 16000U);
        const int32_t flow =
            (int32_t)triangle_u8(frame * 3U, 64U) / 12 - 2;
        draw_quadratic(
            canvas,
            (point_t){(int16_t)(43 + lean), (int16_t)(69 + bob)},
            (point_t){(int16_t)(79 + lean + flow),
                      (int16_t)(61 + bob)},
            (point_t){(int16_t)(120 + lean), (int16_t)(67 + bob)},
            2, blend565(aqua, pink, 92U));
        draw_quadratic(
            canvas,
            (point_t){(int16_t)(48 + lean), (int16_t)(94 + bob)},
            (point_t){(int16_t)(82 + lean - flow),
                      (int16_t)(102 + bob)},
            (point_t){(int16_t)(112 + lean), (int16_t)(92 + bob)},
            1, lilac);
        draw_line(
            canvas, 42 + lean, 83 + bob,
            34 + lean, 104 + bob, 3, pink);
        draw_line(
            canvas, 117 + lean, 84 + bob,
            125 + lean, 108 + bob, 3, aqua);

        motion_t liquid_motion = *motion;
        liquid_motion.gaze_x =
            clamp_i16(liquid_motion.gaze_x / 2, -4, 4);
        liquid_motion.gaze_y =
            clamp_i16(liquid_motion.gaze_y / 2, -3, 3);
        liquid_motion.bob = clamp_i16(liquid_motion.bob, -1, 1);
        draw_basic_eyes(
            canvas, 57 + lean, 105 + lean, 51 + bob,
            17, 13, &liquid_motion,
            render_key->controls.eye_left_open,
            render_key->controls.eye_right_open,
            aqua, pink, dark);
        draw_brows(
            canvas, 57 + lean, 105 + lean, 35 + bob,
            14, &liquid_motion, pink, true);
        if (render_key->cheek > 38U) {
            fill_ellipse_blend(
                canvas, 48 + lean, 73 + bob,
                6, 3, pink, 138U);
            fill_ellipse_blend(
                canvas, 114 + lean, 73 + bob,
                6, 3, aqua, 112U);
        }
        draw_polygon_mouth(
            canvas, &render_key->controls,
            81 + lean, 84 + bob, 1, pink, dark);
        if (render_key->controls.mouth_open > 82U) {
            draw_quadratic(
                canvas,
                (point_t){(int16_t)(70 + lean), (int16_t)(88 + bob)},
                (point_t){(int16_t)(81 + lean), (int16_t)(93 + bob)},
                (point_t){(int16_t)(92 + lean), (int16_t)(88 + bob)},
                1, lilac);
        }
    } else {
        const uint16_t casing = RGB565(8, 17, 18);
        const uint16_t screen = RGB565(3, 13, 9);
        const uint16_t phosphor = RGB565(176, 255, 106);
        const uint16_t amber = RGB565(255, 194, 72);
        const uint16_t pale = RGB565(235, 255, 194);
        const int32_t head_y = 61 + bob;
        fill_round_rect(
            canvas, 26 + lean, 12 + bob,
            108, 102, 17, casing);
        fill_round_rect(
            canvas, 32 + lean, 18 + bob,
            96, 86, 13, screen);
        draw_line(
            canvas, 43 + lean, 12 + bob,
            34 + lean, 2 + bob, 3, amber);
        draw_line(
            canvas, 117 + lean, 12 + bob,
            126 + lean, 2 + bob, 3, phosphor);
        fill_ellipse(
            canvas, 34 + lean, 2 + bob, 3, 3, amber);
        fill_ellipse(
            canvas, 126 + lean, 2 + bob, 3, 3, phosphor);
        for (int32_t y = 25; y < 98; y += 5) {
            draw_line(
                canvas, 35 + lean, y + bob,
                125 + lean, y + bob, 1,
                RGB565(5, 29, 14));
        }
        draw_line(
            canvas, 39 + lean, head_y + 43,
            121 + lean, head_y + 43, 2, amber);
        fill_ellipse(
            canvas, 45 + lean, head_y + 44,
            3, 3, phosphor);
        fill_ellipse(
            canvas, 56 + lean, head_y + 44,
            3, 3, amber);
        draw_curated_shader_features(
            canvas, render_key, motion,
            56 + lean, 104 + lean, 50 + bob,
            27, 22, 5, 82 + bob,
            screen, phosphor, amber, pale,
            RGB565(255, 116, 54));
    }
}

static void render_neon_character_overlay(
    canvas_t *canvas,
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    const motion_t *motion)
{
    const bool glitch = profile == FACE_RENDER_GLITCH_MASK;
    /*
     * These two profiles draw a bounded head over an unbounded shader field.
     * `draw_emissive_robot_eye()` normally moves the complete Cozmo-style eye
     * glyph during a glance.  That is appropriate on a full-width black
     * display, but it let the glyph escape this smaller head silhouette and
     * produced the long black "clipping" bars visible in contact sheets.
     * Keep the authored glance, but contain it to the fixed eye sockets.
     */
    motion_t contained_motion = *motion;
    contained_motion.gaze_x =
        clamp_i16(contained_motion.gaze_x, -2, 2);
    contained_motion.gaze_y =
        clamp_i16(contained_motion.gaze_y, -2, 2);
    contained_motion.bob =
        clamp_i16(contained_motion.bob, -1, 1);
    const uint16_t dark =
        glitch ? RGB565(5, 5, 13) : RGB565(8, 6, 20);
    const uint16_t eye_color =
        glitch ? RGB565(88, 252, 236) : RGB565(184, 148, 255);
    const uint16_t accent =
        glitch ? RGB565(255, 56, 178) : RGB565(84, 236, 255);
    if (glitch) {
        static const point_t mask[] = {
            {80, 10}, {119, 22}, {133, 53}, {123, 96},
            {80, 111}, {37, 96}, {27, 53}, {41, 22},
        };
        fill_polygon(
            canvas, mask, sizeof(mask) / sizeof(mask[0]), dark);
        for (size_t index = 0U;
             index < sizeof(mask) / sizeof(mask[0]);
             ++index) {
            const point_t first = mask[index];
            const point_t second =
                mask[(index + 1U) %
                     (sizeof(mask) / sizeof(mask[0]))];
            draw_line(
                canvas, first.x, first.y,
                second.x, second.y, 2, accent);
        }
        draw_line(canvas, 34, 66, 48, 66, 1, eye_color);
        draw_line(canvas, 112, 66, 126, 66, 1, eye_color);
    } else {
        /*
         * Plasma remains visible as a halo, but not through the facial
         * features.  This gives the shader a stable silhouette to animate.
         */
        fill_ellipse(canvas, 80, 60, 48, 50, dark);
        fill_ellipse_blend(
            canvas, 80, 60, 44, 46,
            RGB565(66, 30, 104), 72U);
        fill_ellipse_blend(
            canvas, 80, 57, 35, 37,
            RGB565(16, 34, 62), 112U);
    }

    const int32_t eye_y = glitch ? 48 : 49;
    const int32_t left_eye_x = glitch ? 57 : 56;
    const int32_t right_eye_x = glitch ? 103 : 104;
    int32_t eye_width = glitch ? 29 : 30;
    int32_t eye_height = glitch ? 23 : 25;
    switch ((face_expression_t)render_key->stage_expression) {
    case FACE_EXPRESSION_WARM:
        eye_height -= 1;
        break;
    case FACE_EXPRESSION_JOY:
        eye_width += 2;
        eye_height -= 3;
        break;
    case FACE_EXPRESSION_SURPRISE:
        eye_width -= 2;
        eye_height += 4;
        break;
    case FACE_EXPRESSION_DETERMINED:
        eye_width += 2;
        eye_height -= 2;
        break;
    case FACE_EXPRESSION_EXCITED:
        eye_width += 1;
        eye_height += 2;
        break;
    default:
        break;
    }
    const int32_t socket_width = glitch ? 35 : 37;
    const int32_t socket_height = glitch ? 29 : 31;
    fill_round_rect(
        canvas,
        left_eye_x - socket_width / 2,
        eye_y - socket_height / 2,
        socket_width, socket_height,
        glitch ? 5 : 11, dark);
    fill_round_rect(
        canvas,
        right_eye_x - socket_width / 2,
        eye_y - socket_height / 2,
        socket_width, socket_height,
        glitch ? 5 : 11, dark);
    draw_emissive_robot_eye(
        canvas, render_key, &contained_motion, true,
        left_eye_x, eye_y,
        eye_width, eye_height,
        glitch ? 4 : 10, eye_color, dark, true);
    draw_emissive_robot_eye(
        canvas, render_key, &contained_motion, false,
        right_eye_x, eye_y,
        eye_width, eye_height,
        glitch ? 4 : 10, eye_color, dark, true);
    const int32_t brow_lift =
        clamp_i16(motion->brow / 4, -4, 6);
    const int32_t inner_brow =
        clamp_i16(motion->brow_inner / 10, -7, 7);
    const int32_t left_outer_brow =
        clamp_i16(motion->brow_outer_left / 10, -7, 7);
    const int32_t right_outer_brow =
        clamp_i16(motion->brow_outer_right / 10, -7, 7);
    const int32_t brow_y = 32 - brow_lift;
    draw_line(
        canvas, 43,
        clamp_i16(brow_y - left_outer_brow, 22, 40),
        71,
        clamp_i16(brow_y - inner_brow, 22, 40),
        2, accent);
    draw_line(
        canvas, 89,
        clamp_i16(brow_y - inner_brow, 22, 40),
        117,
        clamp_i16(brow_y - right_outer_brow, 22, 40),
        2, accent);
    draw_robot_mouth_rig(
        canvas, render_key, 80, glitch ? 83 : 84,
        accent, dark, RGB565(248, 250, 255),
        glitch ? RGB565(255, 92, 166)
               : RGB565(226, 88, 212));

    if (render_key->cheek > 30U) {
        const uint8_t alpha = (uint8_t)min_i32(
            148, 30 + render_key->cheek);
        fill_ellipse_blend(
            canvas, 43, 70, 10, 4, accent, alpha);
        fill_ellipse_blend(
            canvas, 117, 70, 10, 4, accent, alpha);
    }
}

static void render_voice_orb(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const uint32_t frame =
        (uint32_t)(((uint64_t)sample_clock * 30U) / 16000U);
    const uint8_t energy =
        (uint8_t)max_i32(render_key->audio_level, keyframe->mouth_open);
    const int32_t radius = 14 + energy / 24;
    for (int32_t y = 0; y < 60; ++y) {
        for (int32_t x = 0; x < 80; ++x) {
            const int32_t distance = circle_distance_q8(
                x, y, 40 + motion->gaze_x / 3,
                30 + motion->gaze_y / 3, radius);
            const int32_t ring_distance =
                abs_i32(abs_i32(distance) - 120);
            uint8_t glow = glow_from_distance(
                min_i32(abs_i32(distance), ring_distance), energy);
            const uint8_t ripple = triangle_u8(
                (uint32_t)(abs_i32(distance) / 3) + frame * 8U, 256U);
            glow = (uint8_t)min_i32(
                255, ((uint16_t)glow * (150U + ripple / 3U)) / 200U);
            put_half_pixel(
                canvas, x, y,
                cyber_palette(
                    (uint8_t)(frame * 4U + x * 2 + y),
                    glow, 0U));
        }
    }
    /*
     * Keep the reactive field, but give it a stable character silhouette.
     * The old version was a VU ornament whose 11 emotional poses collapsed
     * to three frames; these small features consume the same resolved rig as
     * every figurative renderer.
     */
    const uint16_t dark = RGB565(3, 9, 18);
    const uint16_t cyan = RGB565(116, 250, 236);
    const uint16_t violet = RGB565(198, 116, 255);
    draw_cyber_character_rig(
        canvas, render_key, motion,
        59, 39, 42, 62, 98, 50, 25, 21, 82,
        dark, cyan, blend565(cyan, violet, 148U),
        RGB565(22, 60, 94));
}

static void render_red_optic(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const uint32_t frame =
        (uint32_t)(((uint64_t)sample_clock * 30U) / 16000U);
    const uint8_t energy =
        (uint8_t)max_i32(render_key->audio_level, keyframe->mouth_open);
    for (int32_t y = 0; y < 60; ++y) {
        for (int32_t x = 0; x < 80; ++x) {
            const int32_t distance = circle_distance_q8(
                x, y, 40 + motion->gaze_x / 2,
                29 + motion->gaze_y / 2, 10 + energy / 45);
            const uint8_t glow = glow_from_distance(distance, energy);
            const uint8_t red = (uint8_t)min_i32(
                255, glow + (glow * motion->pulse) / 512);
            const uint8_t blue =
                (uint8_t)((glow * ((x + (int32_t)frame) & 3)) / 24);
            put_half_pixel(
                canvas, x, y,
                RGB565(red, glow / 12, blue));
        }
    }
    /*
     * Retain the single-optic science-fiction identity, but give it the same
     * lid / pupil / brow / mouth hierarchy as the figurative rigs.  The old
     * full-screen crosshair read as instrumentation rather than a character.
     */
    const uint16_t helmet = RGB565(8, 5, 10);
    const uint16_t red = RGB565(255, 38, 44);
    const uint16_t amber = RGB565(255, 154, 42);
    fill_ellipse_blend(canvas, 80, 61, 39, 45, helmet, 216U);
    const uint8_t eye_opening = (uint8_t)(
        ((uint16_t)eye_open(keyframe->eye_left_open, motion) +
         eye_open(keyframe->eye_right_open, motion)) /
        2U);
    const cyber_stage_pose_t *pose =
        cyber_stage_pose(render_key);
    const int32_t authored_weight =
        render_key->expression_weight;
    const int32_t eye_height = clamp_i16(
        3 + 13 * eye_opening / 255 +
            (int32_t)pose->eye_height_delta *
                authored_weight / 255,
        3, 20);
    const int32_t gaze_x = clamp_i16(
        motion->gaze_x +
            (int32_t)pose->gaze_x * authored_weight / 255,
        -8, 8);
    const int32_t gaze_y = clamp_i16(
        motion->gaze_y +
            (int32_t)pose->gaze_y * authored_weight / 255,
        -eye_height / 2, eye_height / 2);
    fill_ellipse(canvas, 80, 54, 25, eye_height + 3, red);
    fill_ellipse(canvas, 80, 54, 21, eye_height, RGB565(38, 4, 8));
    if (eye_opening > 38U) {
        fill_ellipse(
            canvas, 80 + gaze_x, 54 + gaze_y,
            8, max_i32(2, eye_height * 3 / 4), amber);
        fill_ellipse(
            canvas, 80 + gaze_x, 54 + gaze_y,
            3, max_i32(1, eye_height / 2), helmet);
        put_pixel(
            canvas, 78 + gaze_x,
            51 + gaze_y, RGB565(255, 244, 196));
    }
    const int32_t brow_y = clamp_i16(
        34 - motion->brow / 6, 25, 39);
    const int32_t left_brow_y = clamp_i16(
        brow_y +
            ((int32_t)pose->left_outer_brow_y +
             pose->left_inner_brow_y) *
                authored_weight / 510,
        22, 42);
    const int32_t right_brow_y = clamp_i16(
        brow_y +
            ((int32_t)pose->right_inner_brow_y +
             pose->right_outer_brow_y) *
                authored_weight / 510,
        22, 42);
    draw_line(
        canvas, 58, left_brow_y,
        78, clamp_i16(
            brow_y +
                (int32_t)pose->left_inner_brow_y *
                    authored_weight / 255,
            22, 42),
        4, red);
    draw_line(
        canvas, 82, clamp_i16(
            brow_y +
                (int32_t)pose->right_inner_brow_y *
                    authored_weight / 255,
            22, 42),
        102, right_brow_y, 4, red);
    draw_robot_mouth_rig(
        canvas, render_key, 80, 88 + motion->bob,
        red, RGB565(30, 2, 6),
        RGB565(255, 224, 190), RGB565(255, 72, 80));
    draw_line(canvas, 80, 12, 80, 27, 1, RGB565(104, 10, 16));
    draw_line(canvas, 52, 54, 41, 54, 1, RGB565(104, 10, 16));
    draw_line(canvas, 108, 54, 119, 54, 1, RGB565(104, 10, 16));
}

static void render_hub75(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    (void)sample_clock;
    canvas_clear(canvas, RGB565(0, 0, 0));
    const cyber_stage_pose_t *pose =
        cyber_stage_pose(render_key);
    const int32_t authored_weight =
        render_key->expression_weight;
    const uint16_t dim_magenta = RGB565(64, 10, 54);
    const uint16_t dim_cyan = RGB565(8, 48, 46);
    const uint16_t led = RGB565(72, 255, 166);
    const uint16_t pink = RGB565(250, 62, 190);
    const uint16_t white = RGB565(224, 255, 244);

    /*
     * A real HUB75 panel is a stable matrix, not a sparse SDF bloom.  The
     * dim fixed pixels establish the head and sockets; only the bright LED
     * cells animate.  All geometry remains at least six pixels from an edge.
     */
    for (int32_t y = 10; y <= 108; y += 6) {
        for (int32_t x = 31; x <= 127; x += 6) {
            const int32_t nx = abs_i32(x - 80) * 100 / 50;
            const int32_t ny = abs_i32(y - 59) * 100 / 52;
            if (nx * nx / 100 + ny * ny / 100 > 100) {
                continue;
            }
            const uint16_t pixel =
                ((x + y) & 12) == 0 ? dim_cyan : dim_magenta;
            fill_rect(canvas, x, y, 3, 3, pixel);
        }
    }

    const int32_t pose_gaze_x =
        (int32_t)pose->gaze_x * authored_weight / 255;
    const int32_t pose_gaze_y =
        (int32_t)pose->gaze_y * authored_weight / 255;
    const int32_t gaze_x =
        clamp_i16(motion->gaze_x / 2 + pose_gaze_x, -3, 3) * 3;
    const int32_t gaze_y =
        clamp_i16(motion->gaze_y / 2 + pose_gaze_y, -2, 2) * 2;
    const uint8_t openings[] = {
        eye_open(keyframe->eye_left_open, motion),
        eye_open(keyframe->eye_right_open, motion),
    };
    const int32_t eye_centers[] = {53, 107};
    fill_round_rect(
        canvas, 35, 34, 36, 31, 6, RGB565(3, 0, 8));
    fill_round_rect(
        canvas, 89, 34, 36, 31, 6, RGB565(3, 0, 8));
    for (size_t eye_index = 0U; eye_index < 2U; ++eye_index) {
        const int32_t rows = clamp_i16(
            1 + (int32_t)openings[eye_index] * 3 / 255 +
                (int32_t)pose->eye_height_delta *
                    authored_weight / 768,
            1, 4);
        const int32_t top = 49 - (rows - 1) * 3 + gaze_y;
        for (int32_t row = 0; row < rows; ++row) {
            for (int32_t column = -2; column <= 2; ++column) {
                const int32_t taper =
                    abs_i32(column) + abs_i32(row * 2 - rows + 1);
                if (taper > 5) {
                    continue;
                }
                fill_rect(
                    canvas,
                    eye_centers[eye_index] + gaze_x +
                        column * 6 - 2,
                    top + row * 6,
                    5, 5,
                    taper >= 4 ? pink : led);
            }
        }
        if (rows == 1) {
            draw_line(
                canvas,
                eye_centers[eye_index] - 13,
                top + 1,
                eye_centers[eye_index] + 13,
                top + 1,
                3, led);
        } else {
            fill_rect(
                canvas,
                eye_centers[eye_index] + gaze_x - 1,
                top + (rows - 1) * 3 - 1,
                3, 3, white);
        }
    }
    draw_contained_cyber_brows(
        canvas, render_key, motion,
        53, 107, 29, 15, led);

    face_render_key_t mouth_key = *render_key;
    mouth_key.controls.mouth_open = (uint8_t)clamp_i16(
        12 + (int32_t)keyframe->mouth_open * 3 / 5,
        12, 180);
    mouth_key.controls.mouth_width = (uint8_t)clamp_i16(
        60 + (int32_t)keyframe->mouth_width / 4,
        60, 124);
    draw_robot_mouth_rig(
        canvas, &mouth_key, 80, 84,
        pink, RGB565(20, 0, 18), white,
        RGB565(255, 80, 164));
    if (render_key->cheek > 24U) {
        const int32_t cheek_segments =
            1 + render_key->cheek / 72;
        for (int32_t segment = 0; segment < cheek_segments; ++segment) {
            fill_rect(canvas, 36 + segment * 6, 72, 4, 3, pink);
            fill_rect(canvas, 124 - segment * 6, 72, 4, 3, pink);
        }
    }
}

static void render_edge_glow(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    const face_keyframe_t *keyframe = &render_key->controls;
    const uint32_t frame =
        (uint32_t)(((uint64_t)sample_clock * 30U) / 16000U);
    const uint8_t energy =
        (uint8_t)max_i32(render_key->audio_level, keyframe->mouth_open);
    for (int32_t y = 0; y < 60; ++y) {
        for (int32_t x = 0; x < 80; ++x) {
            const int32_t edge = min_i32(
                min_i32(x, 79 - x), min_i32(y, 59 - y));
            const int32_t wave =
                abs_i32(((x * 3 + y * 5 + (int32_t)frame * 4) & 63) - 31);
            uint8_t glow = 0U;
            if (edge < 6) {
                glow = (uint8_t)max_i32(
                    0, 220 - edge * 31 - wave * 2 +
                           energy / 3);
            }
            const int32_t orb = circle_distance_q8(
                x, y, 40 + motion->gaze_x / 4,
                30 + motion->gaze_y / 4, 5 + energy / 48);
            glow = (uint8_t)max_i32(
                glow, glow_from_distance(orb, energy));
            put_half_pixel(
                canvas, x, y,
                cyber_palette(
                    (uint8_t)(x * 4 + y * 2 + frame * 3U),
                    glow, 0U));
        }
    }
    const uint16_t dark = RGB565(2, 6, 12);
    const uint16_t cyan = RGB565(72, 244, 226);
    const uint16_t rose = RGB565(248, 78, 172);
    draw_cyber_character_rig(
        canvas, render_key, motion,
        59, 40, 43, 62, 98, 49, 26, 22, 83,
        dark, cyan, rose, RGB565(14, 52, 72));
}

static void render_holo_wireframe(
    canvas_t *canvas,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    const uint32_t frame =
        (uint32_t)(((uint64_t)sample_clock * 30U) / 16000U);
    const uint16_t cyan = RGB565(46, 228, 220);
    const uint16_t dim = RGB565(12, 62, 70);
    canvas_clear(canvas, RGB565(1, 8, 13));
    for (int32_t y = 82; y < 120; y += 8) {
        draw_line(canvas, 0, y, 160, y, 1, dim);
    }
    for (int32_t x = -40; x <= 200; x += 20) {
        draw_line(canvas, 80, 70, x, 120, 1, dim);
    }
    const int32_t wobble = clamp_i16(motion->tilt, -3, 3);
    const point_t mask[] = {
        {80, 10 + motion->bob},
        {132 + wobble, 38},
        {122 + wobble, 91},
        {80, 108},
        {38 + wobble, 91},
        {28 + wobble, 38},
    };
    for (size_t index = 0; index < 6U; ++index) {
        const point_t first = mask[index];
        const point_t second = mask[(index + 1U) % 6U];
        draw_line(
            canvas, first.x, first.y,
            second.x, second.y, 2, cyan);
    }
    draw_line(canvas, 80, 10, 80, 108, 1, dim);
    draw_line(canvas, 28 + wobble, 38, 122 + wobble, 91, 1, dim);
    draw_line(canvas, 132 + wobble, 38, 38 + wobble, 91, 1, dim);
    const cyber_stage_pose_t *pose =
        cyber_stage_pose(render_key);
    const int32_t authored_weight =
        render_key->expression_weight;
    motion_t contained_motion = *motion;
    contained_motion.gaze_x = clamp_i16(
        contained_motion.gaze_x / 2 +
            (int32_t)pose->gaze_x * authored_weight / 255,
        -3, 3);
    contained_motion.gaze_y = clamp_i16(
        contained_motion.gaze_y / 2 +
            (int32_t)pose->gaze_y * authored_weight / 255,
        -3, 3);
    contained_motion.bob = clamp_i16(
        contained_motion.bob, -1, 1);
    const int32_t eye_width = clamp_i16(
        27 + (int32_t)pose->eye_width_delta *
                 authored_weight / 255,
        22, 34);
    const int32_t eye_height = clamp_i16(
        21 + (int32_t)pose->eye_height_delta *
                 authored_weight / 255,
        14, 28);
    const uint16_t dark = RGB565(1, 8, 13);
    fill_round_rect(
        canvas, 56 - (eye_width + 7) / 2,
        51 - (eye_height + 6) / 2,
        eye_width + 7, eye_height + 6, 4, dark);
    fill_round_rect(
        canvas, 104 - (eye_width + 7) / 2,
        51 - (eye_height + 6) / 2,
        eye_width + 7, eye_height + 6, 4, dark);
    draw_emissive_robot_eye(
        canvas, render_key, &contained_motion, true,
        56, 51, eye_width, eye_height, 4,
        cyan, dark, false);
    draw_emissive_robot_eye(
        canvas, render_key, &contained_motion, false,
        104, 51, eye_width, eye_height, 4,
        cyan, dark, false);
    const uint16_t accent =
        blend565(cyan, RGB565(234, 60, 210), 82U);
    draw_contained_cyber_brows(
        canvas, render_key, &contained_motion,
        56, 104, 31, 14, accent);
    draw_robot_mouth_rig(
        canvas, render_key, 80, 81,
        blend565(
            cyan, RGB565(234, 60, 210),
            (uint8_t)(96U + (frame * 7U & 63U))),
        dark, RGB565(230, 255, 255),
        RGB565(244, 80, 194));
}

static void render_cyber_face(
    canvas_t *canvas,
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    const motion_t *motion,
    uint32_t sample_clock)
{
    switch (profile) {
    case FACE_RENDER_NEON_SDF_CYAN:
    case FACE_RENDER_NEON_SDF_MAGENTA:
    case FACE_RENDER_LIQUID_SMIN:
    case FACE_RENDER_CRT_CHROMATIC:
    case FACE_RENDER_GLITCH_MASK:
    case FACE_RENDER_PALETTE_PLASMA:
        render_neon_field(
            canvas, profile, render_key, motion, sample_clock);
        if (profile >= FACE_RENDER_NEON_SDF_CYAN &&
            profile <= FACE_RENDER_CRT_CHROMATIC) {
            render_anchored_neon_character(
                canvas, profile, render_key, motion);
        } else if (profile == FACE_RENDER_GLITCH_MASK ||
                   profile == FACE_RENDER_PALETTE_PLASMA) {
            render_neon_character_overlay(
                canvas, profile, render_key, motion);
        }
        break;
    case FACE_RENDER_HOLO_WIREFRAME:
        render_holo_wireframe(
            canvas, render_key, motion, sample_clock);
        break;
    case FACE_RENDER_VOICE_ORB:
        render_voice_orb(
            canvas, render_key, motion, sample_clock);
        break;
    case FACE_RENDER_RED_OPTIC:
        render_red_optic(
            canvas, render_key, motion, sample_clock);
        break;
    case FACE_RENDER_HUB75_NEON:
        render_hub75(
            canvas, render_key, motion, sample_clock);
        break;
    case FACE_RENDER_EDGE_GLOW:
        render_edge_glow(
            canvas, render_key, motion, sample_clock);
        break;
    case FACE_RENDER_SHADER_AURORA_FAMILIAR:
        render_curated_shader_face(
            canvas, CURATED_SHADER_AURORA,
            render_key, motion, sample_clock);
        break;
    case FACE_RENDER_SHADER_LIQUID_CHROMA:
        render_curated_shader_face(
            canvas, CURATED_SHADER_LIQUID,
            render_key, motion, sample_clock);
        break;
    case FACE_RENDER_SHADER_CRT_GHOST:
        render_curated_shader_face(
            canvas, CURATED_SHADER_CRT,
            render_key, motion, sample_clock);
        break;
    default:
        break;
    }
}

static bool profile_is_valid(face_render_profile_t profile)
{
    return (unsigned int)profile < (unsigned int)FACE_RENDER_PROFILE_COUNT;
}

size_t face_render_profile_count(void)
{
    return FACE_RENDER_PROFILE_COUNT;
}

const char *face_render_profile_slug(face_render_profile_t profile)
{
    if (!profile_is_valid(profile)) {
        return NULL;
    }
    face_pixel_redux_actor_t pixel_actor;
    if (face_pixel_redux_actor_from_legacy_id(
            (uint8_t)profile, &pixel_actor)) {
        return face_pixel_redux_actor_slug(pixel_actor);
    }
    face_robot_redux_style_t robot_redux_style;
    if (face_robot_redux_from_legacy_id(
            (uint8_t)profile, &robot_redux_style)) {
        return face_robot_redux_slug(robot_redux_style);
    }
    face_abstract_redux_style_t abstract_style;
    if (face_abstract_redux_from_legacy_id(
            (uint8_t)profile, &abstract_style)) {
        return face_abstract_redux_slug(abstract_style);
    }
    face_mouth_study_redux_profile_t mouth_study_profile;
    if (face_mouth_study_redux_profile_from_legacy_id(
            (uint8_t)profile, &mouth_study_profile)) {
        return face_mouth_study_redux_profile_slug(
            mouth_study_profile);
    }
    face_eye_study_redux_profile_t eye_study_profile;
    if (face_eye_study_redux_profile_from_legacy_id(
            (uint8_t)profile, &eye_study_profile)) {
        return face_eye_study_redux_profile_slug(eye_study_profile);
    }
    face_salvage_redux_style_t salvage_style;
    if (face_salvage_redux_from_legacy_id(
            (uint8_t)profile, &salvage_style)) {
        return face_salvage_redux_slug(salvage_style);
    }
    face_sprite_redux_actor_t sprite_redux_actor;
    if (face_sprite_redux_actor_from_legacy_id(
            (uint8_t)profile, &sprite_redux_actor)) {
        return face_sprite_redux_actor_slug(sprite_redux_actor);
    }
    face_closeup_toon_style_t closeup_style;
    if (face_closeup_toon_from_legacy_id(
            (uint8_t)profile, &closeup_style)) {
        return face_closeup_toon_slug(closeup_style);
    }
    face_eye_actor_style_t eye_style;
    if (face_eye_actor_from_legacy_id((uint8_t)profile, &eye_style)) {
        return face_eye_actor_slug(eye_style);
    }
    face_cyber_wildcard_profile_t cyber_profile;
    if (face_cyber_wildcard_from_legacy_id(
            (uint8_t)profile, &cyber_profile)) {
        return face_cyber_wildcard_slug(cyber_profile);
    }
    return PROFILES[profile].slug;
}

const char *face_render_profile_name(face_render_profile_t profile)
{
    if (!profile_is_valid(profile)) {
        return NULL;
    }
    face_pixel_redux_actor_t pixel_actor;
    if (face_pixel_redux_actor_from_legacy_id(
            (uint8_t)profile, &pixel_actor)) {
        return face_pixel_redux_actor_name(pixel_actor);
    }
    face_robot_redux_style_t robot_redux_style;
    if (face_robot_redux_from_legacy_id(
            (uint8_t)profile, &robot_redux_style)) {
        return face_robot_redux_name(robot_redux_style);
    }
    face_abstract_redux_style_t abstract_style;
    if (face_abstract_redux_from_legacy_id(
            (uint8_t)profile, &abstract_style)) {
        return face_abstract_redux_name(abstract_style);
    }
    face_mouth_study_redux_profile_t mouth_study_profile;
    if (face_mouth_study_redux_profile_from_legacy_id(
            (uint8_t)profile, &mouth_study_profile)) {
        return face_mouth_study_redux_profile_name(
            mouth_study_profile);
    }
    face_eye_study_redux_profile_t eye_study_profile;
    if (face_eye_study_redux_profile_from_legacy_id(
            (uint8_t)profile, &eye_study_profile)) {
        return face_eye_study_redux_profile_name(eye_study_profile);
    }
    face_salvage_redux_style_t salvage_style;
    if (face_salvage_redux_from_legacy_id(
            (uint8_t)profile, &salvage_style)) {
        return face_salvage_redux_name(salvage_style);
    }
    face_sprite_redux_actor_t sprite_redux_actor;
    if (face_sprite_redux_actor_from_legacy_id(
            (uint8_t)profile, &sprite_redux_actor)) {
        return face_sprite_redux_actor_name(sprite_redux_actor);
    }
    face_closeup_toon_style_t closeup_style;
    if (face_closeup_toon_from_legacy_id(
            (uint8_t)profile, &closeup_style)) {
        return face_closeup_toon_name(closeup_style);
    }
    face_eye_actor_style_t eye_style;
    if (face_eye_actor_from_legacy_id((uint8_t)profile, &eye_style)) {
        return face_eye_actor_name(eye_style);
    }
    face_cyber_wildcard_profile_t cyber_profile;
    if (face_cyber_wildcard_from_legacy_id(
            (uint8_t)profile, &cyber_profile)) {
        return face_cyber_wildcard_name(cyber_profile);
    }
    return PROFILES[profile].name;
}

const char *face_render_profile_family_name(
    face_render_profile_t profile)
{
    if (!profile_is_valid(profile)) {
        return NULL;
    }
    face_salvage_redux_style_t salvage_style;
    if (face_salvage_redux_from_legacy_id(
            (uint8_t)profile, &salvage_style)) {
        static const uint8_t salvage_families[] = {
            [FACE_SALVAGE_REDUX_STORY_SCOUT] =
                FACE_RENDER_FAMILY_TOON,
            [FACE_SALVAGE_REDUX_POCKET_COURIER] =
                FACE_RENDER_FAMILY_PIXEL,
            [FACE_SALVAGE_REDUX_VELA_EYES] =
                FACE_RENDER_FAMILY_EYES,
            [FACE_SALVAGE_REDUX_KITE_ORACLE] =
                FACE_RENDER_FAMILY_TOON,
            [FACE_SALVAGE_REDUX_ORBIT_GARDENER] =
                FACE_RENDER_FAMILY_ROBOT,
            [FACE_SALVAGE_REDUX_FELT_FAMILIAR] =
                FACE_RENDER_FAMILY_TOON,
        };
        return FAMILY_NAMES[salvage_families[salvage_style]];
    }
    face_closeup_toon_style_t closeup_style;
    if (face_closeup_toon_from_legacy_id(
            (uint8_t)profile, &closeup_style)) {
        (void)closeup_style;
        return FAMILY_NAMES[FACE_RENDER_FAMILY_TOON];
    }
    const uint8_t family = PROFILES[profile].family;
    if (family >= sizeof(FAMILY_NAMES) / sizeof(FAMILY_NAMES[0])) {
        return NULL;
    }
    return FAMILY_NAMES[family];
}

bool face_render_profile_info(
    face_render_profile_t profile, face_render_info_t *info)
{
    if (!profile_is_valid(profile) || info == NULL) {
        return false;
    }
    const profile_description_t *description = &PROFILES[profile];
    *info = (face_render_info_t){
        .width = FACE_RENDER_WIDTH,
        .height = FACE_RENDER_HEIGHT,
        .work_width = description->work_width,
        .work_height = description->work_height,
        .framebuffer_bytes = FACE_RENDER_FRAME_BYTES,
        .family = description->family,
        .mouth_kind = description->mouth_kind,
        .flags = description->flags,
        .reserved = 0U,
        .estimated_ops_per_pixel =
            description->estimated_ops_per_pixel,
    };
    face_pixel_redux_actor_t pixel_actor;
    face_pixel_redux_actor_info_t pixel_info;
    if (face_pixel_redux_actor_from_legacy_id(
            (uint8_t)profile, &pixel_actor) &&
        face_pixel_redux_actor_info(pixel_actor, &pixel_info)) {
        static const uint8_t mouth_kinds[] = {
            [FACE_PIXEL_REDUX_MOUTH_CELS] =
                FACE_RENDER_MOUTH_SPRITE,
            [FACE_PIXEL_REDUX_MOUTH_SHADED] =
                FACE_RENDER_MOUTH_ELLIPSE,
            [FACE_PIXEL_REDUX_MOUTH_CINEMATIC] =
                FACE_RENDER_MOUTH_POLYGON,
            [FACE_PIXEL_REDUX_MOUTH_LED] =
                FACE_RENDER_MOUTH_SEGMENTS,
            [FACE_PIXEL_REDUX_MOUTH_CHIBI] =
                FACE_RENDER_MOUTH_SPRITE,
        };
        info->work_width = pixel_info.logical_width;
        info->work_height = pixel_info.logical_height;
        info->mouth_kind = mouth_kinds[pixel_info.mouth_kind];
        info->estimated_ops_per_pixel =
            pixel_info.estimated_ops_per_pixel;
        info->flags |=
            FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_HALF_RES;
        info->flags &= (uint8_t)~(
            FACE_RENDER_FLAG_NO_MOUTH |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_POLYGON_MOUTH);
        if (pixel_info.mouth_kind ==
                FACE_PIXEL_REDUX_MOUTH_CELS ||
            pixel_info.mouth_kind ==
                FACE_PIXEL_REDUX_MOUTH_CHIBI) {
            info->flags |= FACE_RENDER_FLAG_SPRITE_MOUTH;
        } else if (
            pixel_info.mouth_kind ==
            FACE_PIXEL_REDUX_MOUTH_CINEMATIC) {
            info->flags |= FACE_RENDER_FLAG_POLYGON_MOUTH;
        }
    }
    face_robot_redux_style_t robot_redux_style;
    face_robot_redux_info_t robot_redux_info;
    if (face_robot_redux_from_legacy_id(
            (uint8_t)profile, &robot_redux_style) &&
        face_robot_redux_info(
            robot_redux_style, &robot_redux_info)) {
        static const uint8_t mouth_kinds[] = {
            [FACE_ROBOT_REDUX_MOUTH_NONE] =
                FACE_RENDER_MOUTH_NONE,
            [FACE_ROBOT_REDUX_MOUTH_CAVITY] =
                FACE_RENDER_MOUTH_ELLIPSE,
            [FACE_ROBOT_REDUX_MOUTH_MANGA] =
                FACE_RENDER_MOUTH_POLYGON,
        };
        info->work_width = FACE_RENDER_WIDTH;
        info->work_height = FACE_RENDER_HEIGHT;
        info->mouth_kind =
            mouth_kinds[robot_redux_info.mouth_kind];
        info->estimated_ops_per_pixel =
            robot_redux_info.estimated_ops_per_pixel;
        info->flags &= (uint8_t)~(
            FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_NO_MOUTH |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_POLYGON_MOUTH);
        if (robot_redux_info.deliberate_mouthless) {
            info->flags |= FACE_RENDER_FLAG_NO_MOUTH;
        } else if (
            robot_redux_info.mouth_kind ==
            FACE_ROBOT_REDUX_MOUTH_MANGA) {
            info->flags |= FACE_RENDER_FLAG_POLYGON_MOUTH;
        }
    }
    face_abstract_redux_style_t abstract_style;
    face_abstract_redux_info_t abstract_info;
    if (face_abstract_redux_from_legacy_id(
            (uint8_t)profile, &abstract_style) &&
        face_abstract_redux_info(abstract_style, &abstract_info)) {
        static const uint8_t mouth_kinds[] = {
            [FACE_ABSTRACT_REDUX_MOUTH_NONE] =
                FACE_RENDER_MOUTH_NONE,
            [FACE_ABSTRACT_REDUX_MOUTH_RIBBON] =
                FACE_RENDER_MOUTH_LINE,
            [FACE_ABSTRACT_REDUX_MOUTH_LIQUID] =
                FACE_RENDER_MOUTH_ELLIPSE,
            [FACE_ABSTRACT_REDUX_MOUTH_MATRIX] =
                FACE_RENDER_MOUTH_SEGMENTS,
            [FACE_ABSTRACT_REDUX_MOUTH_LINE] =
                FACE_RENDER_MOUTH_LINE,
        };
        info->work_width = FACE_RENDER_WIDTH;
        info->work_height = FACE_RENDER_HEIGHT;
        info->mouth_kind = mouth_kinds[abstract_info.mouth_kind];
        info->estimated_ops_per_pixel =
            abstract_info.estimated_ops_per_pixel;
        info->flags &= (uint8_t)~(
            FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_NO_MOUTH |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_POLYGON_MOUTH);
        if (abstract_info.mouth_kind ==
            FACE_ABSTRACT_REDUX_MOUTH_NONE) {
            info->flags |= FACE_RENDER_FLAG_NO_MOUTH;
        }
    }
    face_eye_study_redux_profile_t eye_study_profile;
    if (face_eye_study_redux_profile_from_legacy_id(
            (uint8_t)profile, &eye_study_profile)) {
        info->work_width = FACE_RENDER_WIDTH;
        info->work_height = FACE_RENDER_HEIGHT;
        info->mouth_kind = FACE_RENDER_MOUTH_NONE;
        info->estimated_ops_per_pixel = 8U;
        info->flags &= (uint8_t)~(
            FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_POLYGON_MOUTH);
        info->flags |=
            FACE_RENDER_FLAG_NO_MOUTH |
            FACE_RENDER_FLAG_EYE_FOCUS |
            FACE_RENDER_FLAG_IDLE_MOTION;
    }
    face_salvage_redux_style_t salvage_style;
    face_salvage_redux_info_t salvage_info;
    if (face_salvage_redux_from_legacy_id(
            (uint8_t)profile, &salvage_style) &&
        face_salvage_redux_info(salvage_style, &salvage_info)) {
        static const uint8_t families[] = {
            [FACE_SALVAGE_REDUX_STORY_SCOUT] =
                FACE_RENDER_FAMILY_TOON,
            [FACE_SALVAGE_REDUX_POCKET_COURIER] =
                FACE_RENDER_FAMILY_PIXEL,
            [FACE_SALVAGE_REDUX_VELA_EYES] =
                FACE_RENDER_FAMILY_EYES,
            [FACE_SALVAGE_REDUX_KITE_ORACLE] =
                FACE_RENDER_FAMILY_TOON,
            [FACE_SALVAGE_REDUX_ORBIT_GARDENER] =
                FACE_RENDER_FAMILY_ROBOT,
            [FACE_SALVAGE_REDUX_FELT_FAMILIAR] =
                FACE_RENDER_FAMILY_TOON,
        };
        static const uint8_t mouth_kinds[] = {
            [FACE_SALVAGE_REDUX_STORY_SCOUT] =
                FACE_RENDER_MOUTH_ELLIPSE,
            [FACE_SALVAGE_REDUX_POCKET_COURIER] =
                FACE_RENDER_MOUTH_SPRITE,
            [FACE_SALVAGE_REDUX_VELA_EYES] =
                FACE_RENDER_MOUTH_NONE,
            [FACE_SALVAGE_REDUX_KITE_ORACLE] =
                FACE_RENDER_MOUTH_POLYGON,
            [FACE_SALVAGE_REDUX_ORBIT_GARDENER] =
                FACE_RENDER_MOUTH_SEGMENTS,
            [FACE_SALVAGE_REDUX_FELT_FAMILIAR] =
                FACE_RENDER_MOUTH_ELLIPSE,
        };
        static const uint8_t style_flags[] = {
            [FACE_SALVAGE_REDUX_STORY_SCOUT] =
                FACE_RENDER_FLAG_EYE_FOCUS |
                FACE_RENDER_FLAG_IDLE_MOTION,
            [FACE_SALVAGE_REDUX_POCKET_COURIER] =
                FACE_RENDER_FLAG_PIXELATED |
                FACE_RENDER_FLAG_SPRITE_MOUTH |
                FACE_RENDER_FLAG_IDLE_MOTION,
            [FACE_SALVAGE_REDUX_VELA_EYES] =
                FACE_RENDER_FLAG_EYE_FOCUS |
                FACE_RENDER_FLAG_IDLE_MOTION |
                FACE_RENDER_FLAG_NO_MOUTH,
            [FACE_SALVAGE_REDUX_KITE_ORACLE] =
                FACE_RENDER_FLAG_POLYGON_MOUTH |
                FACE_RENDER_FLAG_IDLE_MOTION,
            [FACE_SALVAGE_REDUX_ORBIT_GARDENER] =
                FACE_RENDER_FLAG_EYE_FOCUS |
                FACE_RENDER_FLAG_IDLE_MOTION,
            [FACE_SALVAGE_REDUX_FELT_FAMILIAR] =
                FACE_RENDER_FLAG_EYE_FOCUS |
                FACE_RENDER_FLAG_IDLE_MOTION,
        };
        info->work_width = FACE_RENDER_WIDTH;
        info->work_height = FACE_RENDER_HEIGHT;
        info->family = families[salvage_style];
        info->mouth_kind = mouth_kinds[salvage_style];
        info->estimated_ops_per_pixel =
            salvage_info.estimated_ops_per_pixel;
        info->flags &= (uint8_t)~(
            FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_EYE_FOCUS |
            FACE_RENDER_FLAG_IDLE_MOTION |
            FACE_RENDER_FLAG_NO_MOUTH |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_POLYGON_MOUTH);
        info->flags |= style_flags[salvage_style];
    }
    face_sprite_redux_actor_t sprite_redux_actor;
    face_sprite_redux_actor_info_t sprite_redux_info;
    if (face_sprite_redux_actor_from_legacy_id(
            (uint8_t)profile, &sprite_redux_actor) &&
        face_sprite_redux_actor_info(
            sprite_redux_actor, &sprite_redux_info)) {
        static const uint8_t mouth_kinds[] = {
            [FACE_SPRITE_REDUX_MOUTH_EGA_CELS] =
                FACE_RENDER_MOUTH_SPRITE,
            [FACE_SPRITE_REDUX_MOUTH_VGA_SHADED] =
                FACE_RENDER_MOUTH_ELLIPSE,
            [FACE_SPRITE_REDUX_MOUTH_ARCADE_CELS] =
                FACE_RENDER_MOUTH_SPRITE,
        };
        info->work_width = sprite_redux_info.logical_width;
        info->work_height = sprite_redux_info.logical_height;
        info->mouth_kind =
            mouth_kinds[sprite_redux_info.mouth_kind];
        info->estimated_ops_per_pixel =
            sprite_redux_info.estimated_ops_per_pixel;
        info->flags &= (uint8_t)~(
            FACE_RENDER_FLAG_NO_MOUTH |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_POLYGON_MOUTH);
        info->flags |=
            FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_IDLE_MOTION;
        if (sprite_redux_info.mouth_kind !=
            FACE_SPRITE_REDUX_MOUTH_VGA_SHADED) {
            info->flags |= FACE_RENDER_FLAG_SPRITE_MOUTH;
        }
    }
    face_closeup_toon_style_t closeup_style;
    face_closeup_toon_info_t closeup_info;
    if (face_closeup_toon_from_legacy_id(
            (uint8_t)profile, &closeup_style) &&
        face_closeup_toon_info(closeup_style, &closeup_info)) {
        static const uint8_t mouth_kinds[] = {
            [FACE_CLOSEUP_TOON_MOUTH_CURVE] =
                FACE_RENDER_MOUTH_LINE,
            [FACE_CLOSEUP_TOON_MOUTH_CAVITY] =
                FACE_RENDER_MOUTH_ELLIPSE,
            [FACE_CLOSEUP_TOON_MOUTH_MUZZLE] =
                FACE_RENDER_MOUTH_ELLIPSE,
            [FACE_CLOSEUP_TOON_MOUTH_MANGA] =
                FACE_RENDER_MOUTH_POLYGON,
            [FACE_CLOSEUP_TOON_MOUTH_BEARD] =
                FACE_RENDER_MOUTH_POLYGON,
        };
        info->family = FACE_RENDER_FAMILY_TOON;
        info->work_width = FACE_RENDER_WIDTH;
        info->work_height = FACE_RENDER_HEIGHT;
        info->mouth_kind = mouth_kinds[closeup_info.mouth_kind];
        info->estimated_ops_per_pixel =
            closeup_info.estimated_ops_per_pixel;
        info->flags &= (uint8_t)~(
            FACE_RENDER_FLAG_HALF_RES |
            FACE_RENDER_FLAG_PIXELATED |
            FACE_RENDER_FLAG_NO_MOUTH |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_POLYGON_MOUTH);
        info->flags |=
            FACE_RENDER_FLAG_EYE_FOCUS |
            FACE_RENDER_FLAG_IDLE_MOTION;
        if (closeup_info.mouth_kind ==
                FACE_CLOSEUP_TOON_MOUTH_MANGA ||
            closeup_info.mouth_kind ==
                FACE_CLOSEUP_TOON_MOUTH_BEARD) {
            info->flags |= FACE_RENDER_FLAG_POLYGON_MOUTH;
        }
    }
    face_eye_actor_style_t eye_style;
    face_eye_actor_info_t eye_info;
    if (face_eye_actor_from_legacy_id((uint8_t)profile, &eye_style) &&
        face_eye_actor_info(eye_style, &eye_info)) {
        static const uint8_t mouth_kinds[] = {
            [FACE_EYE_ACTOR_MOUTH_NONE] = FACE_RENDER_MOUTH_NONE,
            [FACE_EYE_ACTOR_MOUTH_LINE] = FACE_RENDER_MOUTH_LINE,
            [FACE_EYE_ACTOR_MOUTH_CAVITY] = FACE_RENDER_MOUTH_ELLIPSE,
            [FACE_EYE_ACTOR_MOUTH_PIXEL] = FACE_RENDER_MOUTH_SEGMENTS,
        };
        info->mouth_kind = mouth_kinds[eye_info.mouth_kind];
        info->estimated_ops_per_pixel = eye_info.estimated_ops_per_pixel;
        info->flags &= (uint8_t)~(
            FACE_RENDER_FLAG_NO_MOUTH |
            FACE_RENDER_FLAG_SPRITE_MOUTH |
            FACE_RENDER_FLAG_POLYGON_MOUTH);
        if (eye_info.mouth_kind == FACE_EYE_ACTOR_MOUTH_NONE) {
            info->flags |= FACE_RENDER_FLAG_NO_MOUTH;
        }
    }
    face_cyber_wildcard_profile_t cyber_profile;
    face_cyber_wildcard_info_t cyber_info;
    if (face_cyber_wildcard_from_legacy_id(
            (uint8_t)profile, &cyber_profile) &&
        face_cyber_wildcard_info(cyber_profile, &cyber_info)) {
        info->work_width = FACE_RENDER_WIDTH;
        info->work_height = FACE_RENDER_HEIGHT;
        info->estimated_ops_per_pixel =
            cyber_info.estimated_ops_per_pixel;
        info->flags &= (uint8_t)~FACE_RENDER_FLAG_HALF_RES;
    }
    return true;
}

static bool render_sprite_showcase(
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    enum { SHOWCASE_PLAYER_COUNT = 2 };
    static face_sprite_player_t players[SHOWCASE_PLAYER_COUNT];
    static uint8_t ready[SHOWCASE_PLAYER_COUNT];
    const size_t showcase_index = (size_t)(
        profile - FACE_RENDER_SPRITE_VGA_STAR_NAVIGATOR);
    if (showcase_index >= SHOWCASE_PLAYER_COUNT) {
        return false;
    }
    if (ready[showcase_index] == 0U) {
        const face_sprite_showcase_info_t *showcase =
            face_sprite_showcase_info(showcase_index);
        if (showcase == NULL ||
            !face_sprite_player_init(
                &players[showcase_index], showcase->atlas)) {
            return false;
        }
        /*
         * Validation is cached, but rendering remains a pure snapshot of the
         * 40-byte key and 16 kHz clock. Interleaved matrix cells never share
         * mouth debounce or blink state.
         */
        ready[showcase_index] = 1U;
    }
    return face_sprite_render_snapshot(
        &players[showcase_index],
        render_key,
        sample_clock,
        rgb565,
        pixel_capacity);
}

bool face_render_frame(
    face_render_profile_t profile,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (!profile_is_valid(profile) || render_key == NULL ||
        rgb565 == NULL || pixel_capacity < FACE_RENDER_PIXEL_COUNT) {
        return false;
    }

    face_pixel_redux_actor_t pixel_actor;
    if (face_pixel_redux_actor_from_legacy_id(
            (uint8_t)profile, &pixel_actor)) {
        return face_pixel_redux_actor_render(
            pixel_actor,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_robot_redux_style_t robot_redux_style;
    if (face_robot_redux_from_legacy_id(
            (uint8_t)profile, &robot_redux_style)) {
        return face_robot_redux_render(
            robot_redux_style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_abstract_redux_style_t abstract_style;
    if (face_abstract_redux_from_legacy_id(
            (uint8_t)profile, &abstract_style)) {
        return face_abstract_redux_render(
            abstract_style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_eye_study_redux_profile_t eye_study_profile;
    if (face_eye_study_redux_profile_from_legacy_id(
            (uint8_t)profile, &eye_study_profile)) {
        return face_eye_study_redux_render(
            eye_study_profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_salvage_redux_style_t salvage_style;
    if (face_salvage_redux_from_legacy_id(
            (uint8_t)profile, &salvage_style)) {
        return face_salvage_redux_render(
            salvage_style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_sprite_redux_actor_t sprite_redux_actor;
    if (face_sprite_redux_actor_from_legacy_id(
            (uint8_t)profile, &sprite_redux_actor)) {
        return face_sprite_redux_actor_render(
            sprite_redux_actor,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_closeup_toon_style_t closeup_style;
    if (face_closeup_toon_from_legacy_id(
            (uint8_t)profile, &closeup_style)) {
        return face_closeup_toon_render(
            closeup_style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_eye_actor_style_t eye_style;
    if (face_eye_actor_from_legacy_id((uint8_t)profile, &eye_style)) {
        return face_eye_actor_render(
            eye_style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_cyber_wildcard_profile_t cyber_profile;
    if (face_cyber_wildcard_from_legacy_id(
            (uint8_t)profile, &cyber_profile)) {
        return face_cyber_wildcard_render(
            cyber_profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    face_mouth_study_redux_profile_t mouth_study_profile;
    if (face_mouth_study_redux_profile_from_legacy_id(
            (uint8_t)profile, &mouth_study_profile)) {
        return face_mouth_study_redux_render(
            mouth_study_profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    if (profile >= FACE_RENDER_ROBOT_RIG_VECTOR_ROUNDED &&
        profile <= FACE_RENDER_ROBOT_RIG_M5_MANGA) {
        const face_robot_eyes_profile_t robot_profile =
            (face_robot_eyes_profile_t)(
                profile - FACE_RENDER_ROBOT_RIG_VECTOR_ROUNDED);
        return face_robot_eyes_render(
            robot_profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    if (profile >= FACE_RENDER_SPRITE_VGA_STAR_NAVIGATOR &&
        profile <= FACE_RENDER_SPRITE_POCKET_RELAY_CREATURE) {
        return render_sprite_showcase(
            profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    if (profile == FACE_RENDER_SPRITE_SHEET_POCKET_MOSSLING) {
        return face_sprite_mossling_render(
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    if (profile >= FACE_RENDER_PIXEL_PACK_EGA_QUEST &&
        profile <= FACE_RENDER_PIXEL_PACK_DITHERED_ROGUE) {
        const face_pixel_pack_profile_t pixel_profile =
            (face_pixel_pack_profile_t)(
                profile - FACE_RENDER_PIXEL_PACK_EGA_QUEST);
        return face_pixel_pack_render(
            pixel_profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    if (profile >= FACE_RENDER_PRESTON_SPRITES &&
        profile <= FACE_RENDER_ORIGAMI_MASK) {
        const face_mouth_actor_profile_t mouth_profile =
            (face_mouth_actor_profile_t)(
                profile - FACE_RENDER_PRESTON_SPRITES);
        return face_mouth_actors_render(
            mouth_profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    if (profile >= FACE_RENDER_TOON_BEAN &&
        profile <= FACE_RENDER_TOON_EMBER) {
        const fta_profile_t toon_profile = (fta_profile_t)(
            profile - FACE_RENDER_TOON_BEAN);
        return fta_render_frame(
            toon_profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    if (profile >= FACE_RENDER_SPRITE_ACTOR_EGA_COURT_MAGE &&
        profile <= FACE_RENDER_SPRITE_ACTOR_ARCADE_CHROME_PILOT) {
        const fsa_profile_t sprite_profile = (fsa_profile_t)(
            profile - FACE_RENDER_SPRITE_ACTOR_EGA_COURT_MAGE);
        return fsa_render_frame(
            sprite_profile,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    if (profile >= FACE_RENDER_ACTOR_MOCHI_CAT &&
        profile <= FACE_RENDER_ACTOR_MONO_SCOPE) {
        return fea_render_frame(
            (fea_profile_t)(profile - FACE_RENDER_ACTOR_MOCHI_CAT),
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
    }

    canvas_t canvas = {
        .pixels = rgb565,
        .width = FACE_RENDER_WIDTH,
        .height = FACE_RENDER_HEIGHT,
    };
    const face_render_key_t evaluated =
        evaluate_expression_actions(render_key);
    const motion_t motion =
        motion_for(profile, &evaluated, sample_clock);
    switch (PROFILES[profile].family) {
    case FACE_RENDER_FAMILY_PIXEL:
        render_pixel_face(&canvas, profile, &evaluated, &motion);
        break;
    case FACE_RENDER_FAMILY_ROBOT:
        render_robot_face(&canvas, profile, &evaluated, &motion);
        break;
    case FACE_RENDER_FAMILY_EYES:
        render_eye_study(&canvas, profile, &evaluated, &motion);
        break;
    case FACE_RENDER_FAMILY_MOUTH:
        render_mouth_study(&canvas, profile, &evaluated, &motion);
        break;
    case FACE_RENDER_FAMILY_CYBER:
        render_cyber_face(
            &canvas, profile, &evaluated, &motion, sample_clock);
        break;
    default:
        return false;
    }
    return true;
}
