/*
 * character.h — Warriors: side, name and the look used to draw a portrait.
 * Pure data, no rendering, so presets can be chosen by the game or tests.
 */
#ifndef CHARACTER_H
#define CHARACTER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { SIDE_PANDAVA, SIDE_KAURAVA } Side;

/* Each field is an index into that feature's option list. */
typedef enum {
    LOOK_SKIN,
    LOOK_HAIR,
    LOOK_HAIR_COLOR,
    LOOK_HEADGEAR,
    LOOK_FACIAL_HAIR,
    LOOK_TILAK,
    LOOK_EARRINGS,
    LOOK_ARMOUR,
    LOOK_EXPRESSION,
    LOOK_FIELD_COUNT
} LookField;

typedef struct {
    uint8_t v[LOOK_FIELD_COUNT];
} Look;

/* Option values for the fields the portrait code branches on. */
enum { HAIR_LONG, HAIR_SHORT, HAIR_TOPKNOT, HAIR_SHAVED };
enum { HEAD_CROWN, HEAD_TURBAN, HEAD_HELMET, HEAD_CIRCLET, HEAD_NONE };
enum { BEARD_NONE, BEARD_MOUSTACHE, BEARD_SHORT, BEARD_FULL };
enum { TILAK_NONE, TILAK_VAISHNAVA, TILAK_TRIPUNDRA, TILAK_BINDI, TILAK_GEM };
enum { EXPR_RESOLUTE, EXPR_FIERCE, EXPR_SERENE };

#define NAME_MAX_LEN 18

typedef struct {
    char name[NAME_MAX_LEN + 1];
    char epithet[40];   /* e.g. "son of Arjuna"; empty for custom warriors */
    Side side;
    Look look;
    int  preset;        /* index into the side's presets, -1 = custom */
} Character;

/* Option counts and display names for the character creator. */
int         look_option_count(LookField f);
const char *look_field_name(LookField f);
const char *look_option_name(LookField f, int option);

const char *side_name(Side s);        /* "Pandava" / "Kaurava" */
const char *side_plural(Side s);      /* "Pandavas" / "Kauravas" */

/* Legendary warriors of each side. */
int  preset_count(Side s);
void character_from_preset(Character *c, Side s, int preset);

/* Random look; `rng` is advanced. Keeps name/side. */
void character_randomize_look(Character *c, uint32_t *rng);

/* A random legendary warrior of side `s`. */
void character_random_preset(Character *c, Side s, uint32_t *rng);

#endif /* CHARACTER_H */
