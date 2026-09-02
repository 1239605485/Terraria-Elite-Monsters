#include "EliteNPC.h"
#include "../Core/HookManager.h"
#include "../Core/ModuleRegistry.h"
#include "mod_logger.h"

#include <cstdlib>

static const em_game_api_t *g_api = nullptr;
static bool g_enabled = false;
static uint32_t g_night_hunt_log_count = 0;
static const int k_spawn_chance_percent_test = 100;

static int32_t scaled_i32(int32_t value, float multiplier) {
    double result = (double)value * (double)multiplier;
    if (result < 1.0) return 1;
    if (result > 2147483647.0) return 2147483647;
    return (int32_t)(result + 0.5);
}

static bool excluded_npc(patch_handle_t instance) {
    bool value = false;
    if (em_field_read_bool(g_api->npc_friendly, instance, &value) && value) {
        return true;
    }
    if (em_field_read_bool(g_api->npc_town, instance, &value) && value) {
        return true;
    }
    if (em_field_read_bool(g_api->npc_boss, instance, &value) && value) {
        return true;
    }
    return false;
}

void em_elite_npc_initialize(const em_game_api_t *api) {
    g_api = api;
    g_night_hunt_log_count = 0;
    g_enabled = api &&
                em_field_valid(api->npc_type_field, PATCH_INT32) &&
                em_field_valid(api->npc_life, PATCH_INT32) &&
                em_field_valid(api->npc_life_max, PATCH_INT32) &&
                em_field_valid(api->npc_damage, PATCH_INT32) &&
                em_field_valid(api->npc_defense, PATCH_INT32);
}

void em_elite_npc_shutdown(void) {
    g_enabled = false;
    g_api = nullptr;
}

bool em_elite_npc_enabled(void) { return g_enabled; }

void em_elite_npc_postfix(patch_handle_t instance, void **args, void *result,
                          const patch_method_signature_t *sig_info) {
    (void)args;
    (void)result;
    (void)sig_info;
    if (!g_enabled || !g_api || !instance || !patchlib_is_valid(instance)) {
        return;
    }
    if (excluded_npc(instance)) return;

    int32_t npc_type = 0;
    if (!em_field_read_i32(g_api->npc_type_field, instance, &npc_type) ||
        npc_type <= 0) {
        return;
    }
    if ((std::rand() % 100) >= k_spawn_chance_percent_test) return;

    int32_t life = 0;
    int32_t life_max = 0;
    int32_t damage = 0;
    int32_t defense = 0;
    if (!em_field_read_i32(g_api->npc_life, instance, &life) ||
        !em_field_read_i32(g_api->npc_life_max, instance, &life_max) ||
        !em_field_read_i32(g_api->npc_damage, instance, &damage) ||
        !em_field_read_i32(g_api->npc_defense, instance, &defense)) {
        return;
    }

    /* Base elite profile. */
    em_field_write_i32(g_api->npc_life_max, instance, scaled_i32(life_max, 1.40f));
    em_field_write_i32(g_api->npc_life, instance, scaled_i32(life, 1.40f));
    float damage_multiplier = 1.15f;
    bool day_time = true;
    /* First gameplay rule batch: Night Hunt only changes elite damage at
     * night. If the world rule or dayTime field is unavailable, do nothing. */
    if (em_world_rule_active(1) &&
        em_static_field_read_bool(g_api->main_day_time, &day_time) &&
        !day_time) {
        damage_multiplier *= 1.25f;
        if (g_night_hunt_log_count < 3 && mod_logger_write) {
            mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters.NPC",
                             "Night Hunt applied: damage_multiplier=%.2f test_mode=%d",
                             (double)damage_multiplier,
                             em_world_rule_night_hunt_test_mode() ? 1 : 0);
            ++g_night_hunt_log_count;
        }
    }
    em_field_write_i32(g_api->npc_damage, instance,
                       scaled_i32(damage, damage_multiplier));
    em_field_write_i32(g_api->npc_defense, instance, defense + 4);
}
