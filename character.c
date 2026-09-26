/*
 * character.c — Look options and the legendary warriors of Kurukshetra.
 */
#include "character.h"

#include <stdio.h>
#include <string.h>

static const char *FIELD_NAMES[LOOK_FIELD_COUNT] = {
    "Skin", "Hair", "Hair colour", "Headgear", "Facial hair",
    "Tilak", "Earrings", "Armour", "Expression",
};

static const char *SKIN[]   = { "Sandal", "Wheat", "Honey", "Bronze", "Teak", "Ebony" };
static const char *HAIR[]   = { "Long", "Short", "Topknot", "Shaved" };
static const char *HAIRC[]  = { "Black", "Dark brown", "Grey", "White" };
static const char *HEAD[]   = { "Mukut crown", "Turban", "War helmet", "Peacock circlet", "Bare" };
static const char *BEARD[]  = { "Clean", "Moustache", "Short beard", "Full beard" };
static const char *TILAK[]  = { "None", "Vaishnava", "Tripundra", "Bindi", "Forehead gem" };
static const char *EARS[]   = { "Kundala", "None" };
static const char *ARMOUR[] = { "Bronze", "Gold", "Silver", "Crimson", "Peacock", "Saffron", "Indigo" };
static const char *EXPR[]   = { "Resolute", "Fierce", "Serene" };

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char **OPTIONS[LOOK_FIELD_COUNT] = {
    SKIN, HAIR, HAIRC, HEAD, BEARD, TILAK, EARS, ARMOUR, EXPR,
};
static const int OPTION_COUNTS[LOOK_FIELD_COUNT] = {
    COUNT(SKIN), COUNT(HAIR), COUNT(HAIRC), COUNT(HEAD), COUNT(BEARD),
    COUNT(TILAK), COUNT(EARS), COUNT(ARMOUR), COUNT(EXPR),
};

int look_option_count(LookField f) { return OPTION_COUNTS[f]; }
const char *look_field_name(LookField f) { return FIELD_NAMES[f]; }

const char *look_option_name(LookField f, int option)
{
    if (option < 0 || option >= OPTION_COUNTS[f])
        return "?";
    return OPTIONS[f][option];
}

const char *side_name(Side s) { return s == SIDE_PANDAVA ? "Pandava" : "Kaurava"; }
const char *side_plural(Side s) { return s == SIDE_PANDAVA ? "Pandavas" : "Kauravas"; }

/* ------------------------------------------------------------------------ */
/* Presets                                                                  */
/* Look order: skin, hair, hair colour, headgear, facial hair, tilak,       */
/*             earrings, armour, expression                                 */
/* ------------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *epithet;
    Look        look;
} Preset;

static const Preset PANDAVAS[] = {
    { "Abhimanyu",   "who entered the Chakravyuha", { { 1, 0, 0, 3, 0, 1, 0, 4, 0 } } },
    { "Arjuna",      "wielder of Gandiva",          { { 2, 0, 0, 0, 1, 1, 0, 1, 0 } } },
    { "Bhima",       "slayer of a hundred",         { { 3, 1, 0, 2, 2, 3, 1, 0, 1 } } },
    { "Yudhishthira","king of dharma",              { { 1, 0, 1, 0, 3, 1, 0, 2, 2 } } },
    { "Nakula",      "master of horses",            { { 0, 0, 0, 1, 1, 3, 0, 5, 0 } } },
    { "Sahadeva",    "reader of the stars",         { { 1, 2, 1, 4, 1, 2, 0, 6, 2 } } },
};

static const Preset KAURAVAS[] = {
    { "Duryodhana",  "eldest of the Kauravas",      { { 3, 0, 0, 0, 1, 3, 0, 3, 1 } } },
    { "Karna",       "son of Surya",                { { 2, 0, 1, 2, 0, 0, 0, 1, 0 } } },
    { "Drona",       "preceptor of the princes",    { { 1, 2, 3, 4, 3, 2, 1, 2, 1 } } },
    { "Ashwatthama", "bearer of the forehead gem",  { { 3, 0, 0, 2, 1, 4, 1, 3, 1 } } },
    { "Jayadratha",  "king of Sindhu",              { { 2, 1, 0, 1, 2, 3, 0, 6, 1 } } },
    { "Dushasana",   "second of the hundred",       { { 3, 1, 0, 2, 3, 0, 1, 0, 1 } } },
    { "Shakuni",     "master of the dice",          { { 0, 1, 2, 1, 1, 0, 0, 0, 1 } } },
    { "Kripa",       "the eternal teacher",         { { 1, 2, 2, 4, 3, 2, 1, 5, 2 } } },
};

int preset_count(Side s)
{
    return s == SIDE_PANDAVA ? COUNT(PANDAVAS) : COUNT(KAURAVAS);
}

void character_from_preset(Character *c, Side s, int preset)
{
    const Preset *p = s == SIDE_PANDAVA ? &PANDAVAS[preset] : &KAURAVAS[preset];
    snprintf(c->name, sizeof c->name, "%s", p->name);
    snprintf(c->epithet, sizeof c->epithet, "%s", p->epithet);
    c->side   = s;
    c->look   = p->look;
    c->preset = preset;
}

static uint32_t next_rand(uint32_t *rng)
{
    /* xorshift32 */
    uint32_t x = *rng ? *rng : 0x12345678u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

void character_randomize_look(Character *c, uint32_t *rng)
{
    for (int f = 0; f < LOOK_FIELD_COUNT; f++)
        c->look.v[f] = (uint8_t)(next_rand(rng) % (uint32_t)OPTION_COUNTS[f]);
    c->preset = -1;
    c->epithet[0] = '\0';
}

void character_random_preset(Character *c, Side s, uint32_t *rng)
{
    character_from_preset(c, s, (int)(next_rand(rng) % (uint32_t)preset_count(s)));
}
