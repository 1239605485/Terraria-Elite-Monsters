#include "EliteNPC.h"
#include "../Core/HookManager.h"

#include <cstdlib>

static const em_game_api_t *g_api = nullptr;
static bool g_enabled = false;

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
    g_enabled = api &&
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
    if (!em_field_read_i32(g_api->npc_type, instance, &npc_type) ||
        npc_type <= 0) {
        return;
    }
    if ((std::rand() % 100) >= 20) return;

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

    /* The first modular milestone intentionally has one simple, deterministic
     * profile. Rules, terrain, AI, rewards and boss changes are not involved. */
    em_field_write_i32(g_api->npc_life_max, instance, scaled_i32(life_max, 1.40f));
    em_field_write_i32(g_api->npc_life, instance, scaled_i32(life, 1.40f));
    em_field_write_i32(g_api->npc_damage, instance, scaled_i32(damage, 1.15f));
    em_field_write_i32(g_api->npc_defense, instance, defense + 4);
}
