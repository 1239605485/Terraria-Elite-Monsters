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
} em_game_api_t;

const em_game_api_t *em_game_api(void);
bool em_field_valid(patch_handle_t field, patch_type_t type);
bool em_static_field_valid(patch_handle_t field, patch_type_t type);
bool em_field_read_i32(patch_handle_t field, patch_handle_t instance,
                       int32_t *value);
bool em_static_field_read_i32(patch_handle_t field, int32_t *value);
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
