#ifndef ELITE_MONSTERS_GAME_API_H
#define ELITE_MONSTERS_GAME_API_H

#include <stdbool.h>
#include <stdint.h>

#include "tefkernel/patchlib/type.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct em_game_api_t {
    patch_handle_t npc_type_class;
    patch_handle_t npc_type_field;
    patch_handle_t npc_friendly;
    patch_handle_t npc_town;
    patch_handle_t npc_boss;
    patch_handle_t npc_life;
    patch_handle_t npc_life_max;
    patch_handle_t npc_damage;
    patch_handle_t npc_defense;
    patch_handle_t npc_scale;
    patch_handle_t npc_knockback_resist;
    patch_handle_t main_type_class;
    patch_handle_t main_game_menu;
    patch_handle_t main_world_id;
    patch_handle_t player_type_class;
    patch_handle_t player_zone_dungeon;
    patch_handle_t player_zone_corrupt;
    patch_handle_t player_zone_crimson;
    patch_handle_t player_zone_jungle;
    patch_handle_t player_zone_snow;
    patch_handle_t player_zone_desert;
    patch_handle_t player_zone_beach;
    patch_handle_t player_zone_underworld;
    patch_handle_t player_zone_hallow;
    patch_handle_t player_zone_sky;
    patch_handle_t player_zone_forest;
    patch_handle_t player_zone_rock_layer;
    patch_handle_t player_zone_dirt_layer;
    patch_handle_t player_zone_glowshroom;
    patch_handle_t player_zone_spider;
    patch_handle_t player_zone_meteor;
    patch_handle_t player_zone_temple;
} em_game_api_t;

const em_game_api_t *em_game_api(void);
bool em_field_valid(patch_handle_t field, patch_type_t type);
bool em_static_field_valid(patch_handle_t field, patch_type_t type);
bool em_field_read_i32(patch_handle_t field, patch_handle_t instance,
                       int32_t *value);
bool em_static_field_read_i32(patch_handle_t field, int32_t *value);
bool em_static_field_read_bool(patch_handle_t field, bool *value);
bool em_main_text_available(void);
bool em_main_text_show(const char *text, uint8_t red, uint8_t green,
                       uint8_t blue);
bool em_field_read_bool(patch_handle_t field, patch_handle_t instance,
                        bool *value);
bool em_field_read_float(patch_handle_t field, patch_handle_t instance,
                         float *value);
bool em_field_write_i32(patch_handle_t field, patch_handle_t instance,
                        int32_t value);
bool em_field_write_float(patch_handle_t field, patch_handle_t instance,
                          float value);

#ifdef __cplusplus
}
#endif

#endif
