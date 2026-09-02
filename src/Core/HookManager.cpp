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
static patch_hook_id_t g_main_update_hooks[1] = {};
static size_t g_main_update_hook_count = 0;

const em_game_api_t *em_game_api(void) { return &g_api; }

bool em_field_valid(patch_handle_t field, patch_type_t type) {
    return field && patchlib_is_valid(field) &&
           patchlib_field_is_instance(field) &&
           patchlib_field_get_type(field) == type;
}

bool em_static_field_valid(patch_handle_t field, patch_type_t type) {
    return field && patchlib_is_valid(field) &&
           patchlib_field_is_static(field) &&
           patchlib_field_get_type(field) == type;
}

bool em_field_read_i32(patch_handle_t field, patch_handle_t instance,
                       int32_t *value) {
    if (!value || !em_field_valid(field, PATCH_INT32)) return false;
    patchlib_field_get_value(field, instance, value);
    return true;
}

bool em_static_field_read_i32(patch_handle_t field, int32_t *value) {
    if (!value || !em_static_field_valid(field, PATCH_INT32)) return false;
    patchlib_field_get_value(field, PATCH_NULL, value);
    return true;
}

bool em_static_field_read_bool(patch_handle_t field, bool *value) {
    if (!value || !em_static_field_valid(field, PATCH_BOOL)) return false;
    patchlib_field_get_value(field, PATCH_NULL, value);
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
    g_api.npc_type_class = npc_type;
    g_api.npc_type_field = patchlib_type_get_field(npc_type, "type");
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

static void cache_main_api(void) {
    patch_handle_t main_type =
        patchlib_type_get_type("Terraria", "Main");
    if (!main_type || !patchlib_is_valid(main_type)) {
        EM_LOG(MOD_LOG_LEVEL_WARNING, "Terraria.Main unavailable");
        return;
    }

    g_api.main_type_class = main_type;
    g_api.main_game_menu = patchlib_type_get_field(main_type, "gameMenu");
    if (!g_api.main_game_menu || !patchlib_is_valid(g_api.main_game_menu)) {
        g_api.main_game_menu = patchlib_type_get_field(main_type, "GameMenu");
    }
    g_api.main_world_id = patchlib_type_get_field(main_type, "worldID");
    if (!g_api.main_world_id || !patchlib_is_valid(g_api.main_world_id)) {
        g_api.main_world_id = patchlib_type_get_field(main_type, "WorldID");
    }

}

static bool install_main_update_hook(patch_handle_t method,
                                     const char *name) {
    if (!method || !patchlib_is_valid(method) ||
        g_main_update_hook_count >= 1) {
        return false;
    }

    patch_method_signature_t signature = {};
    if (!patchlib_method_get_signature(method, &signature)) return false;
    bool supported = signature.is_instance &&
                     signature.return_type == PATCH_VOID &&
                     tefstd_vector_size(&signature.arg_types) <= 2;
    if (!supported) {
        patchlib_method_signature_free(&signature);
        return false;
    }

    patch_hook_id_t hook_id = patchlib_install_prepost_hook(
        method, nullptr, em_world_rule_update);
    if (hook_id == PATCH_HOOK_INVALID_ID) {
        patchlib_method_signature_free(&signature);
        return false;
    }

    g_main_update_hooks[g_main_update_hook_count++] = hook_id;
    EM_LOG(MOD_LOG_LEVEL_INFO,
           "WorldRule Main update hook installed: name=%s params=%d id=%d",
           name, (int)tefstd_vector_size(&signature.arg_types), (int)hook_id);
    patchlib_method_signature_free(&signature);
    return true;
}

static void discover_main_update_hook(void) {
    if (!em_world_rule_enabled()) return;

    patch_handle_t main_type = g_api.main_type_class;
    const char *names[] = {"Update", "DoUpdate"};
    for (size_t name_index = 0; name_index < 2; ++name_index) {
        for (int parameter_count = 0; parameter_count <= 2;
             ++parameter_count) {
            patch_handle_t method = patchlib_type_get_method_by_param_count(
                main_type, names[name_index], parameter_count);
            if (install_main_update_hook(method, names[name_index])) {
                em_world_rule_set_hook_installed(true);
                return;
            }
        }
    }

    patch_handle_t fallback = patchlib_type_get_method(main_type, "Update");
    if (install_main_update_hook(fallback, "Update")) {
        em_world_rule_set_hook_installed(true);
        return;
    }

    em_world_rule_set_hook_installed(false);
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
}

static void shutdown_modules(void) {
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
    cache_main_api();
    initialize_modules();
    if (em_elite_npc_enabled()) {
        discover_npc_hooks(npc_type);
    } else {
        EM_LOG(MOD_LOG_LEVEL_WARNING,
               "Modular NPC fields unavailable; NPC module disabled");
    }
    discover_main_update_hook();
    if (em_world_rule_enabled()) {
        EM_LOG(MOD_LOG_LEVEL_INFO,
               "Modular baseline loaded: Core + NPC + passive WorldRule state; "
               "Terrain/Boss/Event/UI disabled");
    } else {
        EM_LOG(MOD_LOG_LEVEL_INFO,
               "Modular baseline loaded: Core + NPC enabled; WorldRule disabled; "
               "Terrain/Boss/Event/UI disabled");
    }
}

static void cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    shutdown_modules();
    for (size_t i = 0; i < g_setdefaults_hook_count; ++i) {
        patchlib_uninstall_hook(g_setdefaults_hooks[i]);
    }
    g_setdefaults_hook_count = 0;
    for (size_t i = 0; i < g_main_update_hook_count; ++i) {
        patchlib_uninstall_hook(g_main_update_hooks[i]);
    }
    g_main_update_hook_count = 0;
    std::memset(&g_api, 0, sizeof(g_api));
    EM_LOG(MOD_LOG_LEVEL_INFO, "Modular baseline unloaded");
}

static kernel_mod_info_t g_info = {
    "eternal.future.elitemonsters", 2026090405, 1,
    "2.0.0-alpha4.3-safe-noui"
};

static kernel_mod_info_t *get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {init_mod, cleanup_mod, get_info};

extern "C" kernel_mod_ops_t *create_kernel_mod(void) { return &g_ops; }
