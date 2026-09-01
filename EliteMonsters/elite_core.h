#ifndef ELITE_CORE_H
#define ELITE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mod_logger.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"

#define ELITEMONSTERS_LOG(level, ...) \
    do { \
        if (mod_logger_write) mod_logger_write((level), "EliteMonsters", __VA_ARGS__); \
    } while (0)

typedef enum { ELITE_NORMAL = 1, ELITE_RARE = 3, ELITE_LEGENDARY = 5 } elite_rank_t;
typedef enum {
    PROGRESS_PRE_HARDMODE,
    PROGRESS_HARDMODE_EARLY,
    PROGRESS_PRE_PLANTERA,
    PROGRESS_POST_PLANTERA,
    PROGRESS_ENDGAME
} elite_progress_t;

typedef struct {
    patch_handle_t main_game_mode, main_game_mode_getter, main_zenith_world;
    patch_handle_t main_hard_mode, main_net_mode, main_player, main_my_player;
    patch_handle_t main_world_id, main_day_time;
    patch_handle_t npc_type, npc_position, npc_life, npc_life_max;
    patch_handle_t npc_damage, npc_defense, npc_knockback_resist;
    patch_handle_t npc_width, npc_height, npc_scale, npc_value;
    patch_handle_t npc_friendly, npc_town, npc_boss, npc_target;
    patch_handle_t npc_ai_style, npc_direction, npc_net_update;
    patch_handle_t npc_no_gravity, npc_velocity;
    patch_handle_t player_position, player_width, player_height;
    patch_handle_t player_active, player_dead;
    patch_handle_t zone_dungeon, zone_corrupt, zone_crimson, zone_jungle;
    patch_handle_t zone_snow, zone_desert, zone_beach, zone_underworld;
    patch_handle_t zone_hallow, zone_sky;
    patch_handle_t downed_mech, downed_plant, downed_golem, downed_moonlord;
    patch_hook_id_t setdefaults_hooks[8];
    patch_hook_id_t name_hooks[3];
    patch_hook_id_t mouse_hooks[2];
    size_t setdefaults_hook_count, name_hook_count, mouse_hook_count;
} EliteContext;

#define ELITE_TRACK_LIMIT 1024
typedef struct {
    void* instance;
    bool active, rewarded, enraged;
    elite_rank_t rank;
    int32_t base_damage, base_defense;
    uint32_t ai_ticks;
    int biome;
} EliteState;

EliteContext* elite_core_context(void);
EliteState* elite_core_state(size_t slot);
size_t elite_core_slot(void* instance);
bool elite_core_is_elite(void* instance);
void elite_core_try_apply(void* instance);
void elite_core_clear(void* instance);
void elite_core_mark(void* instance, elite_rank_t rank, int32_t damage, int32_t defense);
elite_rank_t elite_core_rank(void* instance);
bool elite_core_read_i32(patch_handle_t, patch_handle_t, int32_t*);
bool elite_core_read_bool(patch_handle_t, patch_handle_t, bool*);
bool elite_core_read_float(patch_handle_t, patch_handle_t, float*);
bool elite_core_write_i32(patch_handle_t, patch_handle_t, int32_t);
bool elite_core_write_float(patch_handle_t, patch_handle_t, float);
bool elite_core_write_bool(patch_handle_t, patch_handle_t, bool);
bool elite_core_read_vector2(patch_handle_t, patch_handle_t, float*, float*);
bool elite_core_write_vector2(patch_handle_t, patch_handle_t, float, float);
bool elite_core_valid_field(patch_handle_t, patch_type_t);
int elite_core_target_player(void* npc);
bool elite_core_player_state(int, float*, float*, int32_t*, int32_t*);
bool elite_core_player_in_zone(int, patch_handle_t);
elite_progress_t elite_core_progress(void);
bool elite_core_reward_allowed(void);
const char* elite_core_progress_name(elite_progress_t);
void elite_core_init(void);
void elite_core_cleanup(void);

#endif
