/*
 * sfx.h — Battlefield sound effects and music.
 *
 * Everything is synthesised at startup (war drums, conch, sword clashes,
 * a tanpura drone and a shehnai melody), so the game needs no asset files.
 * If a sound pack is present it is used instead, file by file:
 *   assets/sfx/<name>.(wav|ogg|mp3|flac)     names: see SFX names in sfx.c
 *   assets/music/menu.*, assets/music/battle.*
 * The conch that starts a battle is a recording (res/conch.mp3) compiled
 * into the game.
 */
#ifndef SFX_H
#define SFX_H

#include <stdbool.h>

typedef enum {
    SFX_STEP,        /* runner's footstep in the dust */
    SFX_ENEMY_STEP,  /* heavy armoured step */
    SFX_BLOCKED,     /* no path that way */
    SFX_CLASH,       /* sword clash: breaching a gate */
    SFX_CLASH_HEAVY, /* the killing blow */
    SFX_DRUM_ROLL,   /* the formation rotates */
    SFX_CONCH,       /* battle begins */
    SFX_VICTORY,
    SFX_DEFEAT,
    SFX_CAPTURE,     /* gate captured */
    SFX_ENEMY_GATE,  /* chaser pushes through a gate: war horn */
    SFX_UI_MOVE,
    SFX_UI_SELECT,
    SFX_DICE,        /* Shakuni's dice decide the sides */
    SFX_DANGER,      /* heartbeat drum when the chaser is close */
    SFX_COUNT
} SfxId;

typedef enum { MUSIC_MENU, MUSIC_BATTLE, MUSIC_COUNT, MUSIC_NONE = -1 } MusicId;

void sfx_init(void);  /* safe to call with no audio device */
void sfx_close(void);
void sfx_update(float dt); /* stream music and run crossfades; call every frame */

void sfx_play(SfxId id);
void sfx_play_ex(SfxId id, float volume, float pitch, float pan);

void sfx_music(MusicId id);       /* crossfade to a track (MUSIC_NONE = silence) */
void sfx_intensity(float level);  /* 0..1: battle music speeds up a little */

void sfx_toggle_mute(void);
bool sfx_muted(void);
int  sfx_pack_files(void);        /* how many files came from assets/ */

#endif /* SFX_H */
