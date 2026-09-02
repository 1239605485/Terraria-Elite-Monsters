#include "HookManager.h"
#include "ModuleRegistry.h"
#include "../NPC/EliteNPC.h"

#include "mod_core.h"
#include "mod_logger.h"
#include "tefkernel/patchlib/property.h"

#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <cstring>

extern "C" void (*mod_logger_write)(mod_log_level_t level, const char *tag,
                                     const char *fmt, ...) = nullptr;

#define EM_LOG(level, ...) \
    do { \
        if (mod_logger_write) mod_logger_write((level), "EliteMonsters", __VA_ARGS__); \
    } while (0)

static em_game_api_t g_api = {};
static patch_hook_id_t g_setdefaults_hooks[8] = {};
static size_t g_setdefaults_hook_count = 0;

const em_game_api_t *em_game_api(void) { return &g_api; }

bool em_field_valid(patch_handle_t field, patch_type_t type) {
    return field && patchlib_is_valid(field) &&
           patchlib_field_is_instance(field) &&
           patchlib_field_get_type(field) == type;
}

bool em_field_read_i32(patch_handle_t field, patch_handle_t instance,
                       int32_t *value) {
    if (!value || !em_field_valid(field, PATCH_INT32)) return false;
    patchlib_field_get_value(field, instance, value);
    return true;
}

bool em_field_read_bool(patch_handle_t field, patch_handle_t instance,
                        bool *value) {
    if (!value || !em_field_valid(field, PATCH_BOOL)) return false;
    patchlib_field_get_value(field, instance, value);
    return true;
}

bool em_field_read_float(patch_handle_t field, patch_handle_t instance,
                         float *value) {
    if (!value || !em_field_valid(field, PATCH_FLOAT)) return false;
    patchlib_field_get_value(field, instance, value);
    return true;
}

bool em_field_write_i32(patch_handle_t field, patch_handle_t instance,
                        int32_t value) {
    if (!em_field_valid(field, PATCH_INT32)) return false;
    patchlib_field_set_value(field, instance, &value);
    return true;
}

bool em_field_write_float(patch_handle_t field, patch_handle_t instance,
                          float value) {
    if (!em_field_valid(field, PATCH_FLOAT)) return false;
    patchlib_field_set_value(field, instance, &value);
    return true;
}

static void cache_npc_api(patch_handle_t npc_type) {
    g_api.npc_type = npc_type;
    g_api.npc_friendly = patchlib_type_get_field(npc_type, "friendly");
    g_api.npc_town = patchlib_type_get_field(npc_type, "townNPC");
    g_api.npc_boss = patchlib_type_get_field(npc_type, "boss");
    g_api.npc_life = patchlib_type_get_field(npc_type, "life");
    g_api.npc_life_max = patchlib_type_get_field(npc_type, "lifeMax");
    g_api.npc_damage = patchlib_type_get_field(npc_type, "damage");
    g_api.npc_defense = patchlib_type_get_field(npc_type, "defense");
    g_api.npc_scale = patchlib_type_get_field(npc_type, "scale");
    g_api.npc_knockback_resist =
        patchlib_type_get_field(npc_type, "knockBackResist");
}

static void discover_npc_hooks(patch_handle_t npc_type) {
    /* Hook one canonical overload only. Hooking every overload is unsafe on
     * IL2CPP builds because SetDefaults overloads can delegate to each other,
     * causing the same NPC to be promoted more than once. Prefer the common
     * one-argument overload, then fall back to the first compatible overload
     * exposed by metadata. */
    const int preferred_parameter_counts[] = {1, 2, 0, 3, 4};
    for (int preference = 0;
         preference < (int)(sizeof(preferred_parameter_counts) /
                            sizeof(preferred_parameter_counts[0]));
         ++preference) {
        int parameter_count = preferred_parameter_counts[preference];
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            npc_type, "SetDefaults", parameter_count);
        if (!method || !patchlib_is_valid(method)) continue;

        patch_method_signature_t signature = {};
        if (!patchlib_method_get_signature(method, &signature)) continue;
        bool supported = signature.is_instance &&
                         signature.return_type == PATCH_VOID;
        patchlib_method_signature_free(&signature);
        if (!supported) continue;

        patch_hook_id_t hook_id = patchlib_install_prepost_hook(
            method, nullptr, em_elite_npc_postfix);
        if (hook_id != PATCH_HOOK_INVALID_ID) {
            g_setdefaults_hooks[g_setdefaults_hook_count++] = hook_id;
            EM_LOG(MOD_LOG_LEVEL_INFO,
                   "Modular NPC SetDefaults hook installed: params=%d id=%d",
                   parameter_count, (int)hook_id);
            return;
        }
    }
    EM_LOG(MOD_LOG_LEVEL_WARNING,
           "Modular NPC SetDefaults hook unavailable; NPC module disabled");
}

static void initialize_modules(void) {
    em_elite_npc_initialize(&g_api);
    /* These modules are intentionally inert in the first stable baseline.
     * They are registered now so later feature additions cannot leak hooks
     * into unrelated modules. */
    em_world_rule_initialize(&g_api);
    em_terrain_detector_initialize(&g_api);
    em_boss_modify_initialize(&g_api);
    em_random_event_initialize(&g_api);
    em_notice_initialize(&g_api);
}

static void shutdown_modules(void) {
    em_notice_shutdown();
    em_random_event_shutdown();
    em_boss_modify_shutdown();
    em_terrain_detector_shutdown();
    em_world_rule_shutdown();
    em_elite_npc_shutdown();
}

static void init_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    std::srand((unsigned)(std::time(nullptr) ^ (uintptr_t)handle));
    std::memset(&g_api, 0, sizeof(g_api));

    patch_handle_t npc_type = patchlib_type_get_type("Terraria", "NPC");
    if (!npc_type || !patchlib_is_valid(npc_type)) {
        EM_LOG(MOD_LOG_LEVEL_WARNING, "Modular baseline: Terraria.NPC unavailable");
        return;
    }

    cache_npc_api(npc_type);
    initialize_modules();
    if (em_elite_npc_enabled()) {
        discover_npc_hooks(npc_type);
    } else {
        EM_LOG(MOD_LOG_LEVEL_WARNING,
               "Modular NPC fields unavailable; NPC module disabled");
    }
    EM_LOG(MOD_LOG_LEVEL_INFO,
           "Modular baseline loaded: Core + NPC enabled; World/Boss/Event/UI disabled");
}

static void cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    shutdown_modules();
    for (size_t i = 0; i < g_setdefaults_hook_count; ++i) {
        patchlib_uninstall_hook(g_setdefaults_hooks[i]);
    }
    g_setdefaults_hook_count = 0;
    std::memset(&g_api, 0, sizeof(g_api));
    EM_LOG(MOD_LOG_LEVEL_INFO, "Modular baseline unloaded");
}

static kernel_mod_info_t g_info = {
    "eternal.future.elitemonsters", 2026090301, 1, "2.0.0-alpha1"
};

static kernel_mod_info_t *get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {init_mod, cleanup_mod, get_info};

extern "C" kernel_mod_ops_t *create_kernel_mod(void) { return &g_ops; }
