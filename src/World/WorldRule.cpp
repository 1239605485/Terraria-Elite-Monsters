#include "../Core/ModuleRegistry.h"

#include "mod_logger.h"

#include <cstdint>
#include <cstring>

#define EM_WORLD_LOG(level, ...) \
    do { \
        if (mod_logger_write) { \
            mod_logger_write((level), "EliteMonsters.WorldRule", __VA_ARGS__); \
        } \
    } while (0)

enum {
    WORLD_RULE_COUNT = 10,
    WORLD_RULE_MIN_ACTIVE = 3,
    WORLD_RULE_MAX_ACTIVE = 5
};

enum { WORLD_RULE_NIGHT_HUNT = 1 };

static const char *const g_world_rule_names[WORLD_RULE_COUNT] = {
    "裂变回响", "夜行猎杀", "精英弹幕", "百杀敌潮", "Boss狂暴",
    "暴击震荡", "地下增殖", "宝箱增益", "濒死反击", "危险轮换"
};

static const em_game_api_t *g_api = nullptr;
static bool g_enabled = false;
static bool g_hook_installed = false;
static bool g_world_active = false;
static bool g_rules_initialized = false;
static bool g_field_warning_logged = false;
static uint32_t g_world_identity = 0;
static uint32_t g_rng_state = 0;
static int g_active_rule_count = 0;
static int g_active_rules[WORLD_RULE_MAX_ACTIVE] = {};

static uint32_t next_random(void) {
    uint32_t value = g_rng_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    g_rng_state = value;
    return value;
}

static void reset_world_session(void) {
    g_world_active = false;
}

static void initialize_rules(uint32_t world_identity) {
    bool used[WORLD_RULE_COUNT] = {};
    g_world_identity = world_identity;
    g_rng_state = world_identity ^ 0xA5C31F27u;
    if (g_rng_state == 0) g_rng_state = 0x6D2B79F5u;
    g_active_rule_count = WORLD_RULE_MIN_ACTIVE +
                          (int)(next_random() %
                                (WORLD_RULE_MAX_ACTIVE -
                                 WORLD_RULE_MIN_ACTIVE + 1));

    for (int i = 0; i < g_active_rule_count; ++i) {
        int candidate = (int)(next_random() % WORLD_RULE_COUNT);
        while (used[candidate]) {
            candidate = (candidate + 1) % WORLD_RULE_COUNT;
        }
        used[candidate] = true;
        g_active_rules[i] = candidate;
    }

    g_rules_initialized = true;
    EM_WORLD_LOG(MOD_LOG_LEVEL_INFO,
                 "World rules state initialized: world_id=%u count=%d",
                 (unsigned)world_identity, g_active_rule_count);
    for (int i = 0; i < g_active_rule_count; ++i) {
        EM_WORLD_LOG(MOD_LOG_LEVEL_INFO, "World rule %d: %s", i + 1,
                     g_world_rule_names[g_active_rules[i]]);
    }
}

void em_world_rule_initialize(const em_game_api_t *api) {
    g_api = api;
    /* Hook discovery is intentionally independent from field discovery.
     * Main.Update is the thing this milestone is validating; unavailable
     * fields must only disable the passive state update, not hide whether the
     * lifecycle hook itself can be installed. */
    g_enabled = api && api->main_type_class &&
                patchlib_is_valid(api->main_type_class);
    g_hook_installed = false;
    g_world_active = false;
    g_rules_initialized = false;
    g_field_warning_logged = false;
    g_world_identity = 0;
    g_rng_state = 0;
    g_active_rule_count = 0;
    std::memset(g_active_rules, 0, sizeof(g_active_rules));

    if (g_enabled) {
        EM_WORLD_LOG(MOD_LOG_LEVEL_INFO,
                     "WorldRule state module ready; waiting for Main.Update hook; "
                     "fields are validated at callback time");
    } else {
        EM_WORLD_LOG(MOD_LOG_LEVEL_WARNING,
                     "WorldRule state module disabled: Terraria.Main unavailable");
    }
}

void em_world_rule_set_hook_installed(bool installed) {
    g_hook_installed = installed;
    if (!installed && g_enabled) {
        g_enabled = false;
        EM_WORLD_LOG(MOD_LOG_LEVEL_WARNING,
                     "WorldRule state module disabled: Main.Update hook unavailable");
    }
}

/* Core uses this during startup to decide whether it should discover and
 * install the Main.Update hook. The callback separately requires the hook
 * flag, so a failed installation cannot leave the module active. */
bool em_world_rule_enabled(void) { return g_enabled; }

bool em_world_rule_active(int rule_id) {
    if (!g_world_active || !g_rules_initialized ||
        rule_id < 0 || rule_id >= WORLD_RULE_COUNT) {
        return false;
    }
    for (int i = 0; i < g_active_rule_count; ++i) {
        if (g_active_rules[i] == rule_id) return true;
    }
    return false;
}

void em_world_rule_update(patch_handle_t instance, void **args, void *result,
                          const patch_method_signature_t *sig_info) {
    (void)instance;
    (void)args;
    (void)result;
    (void)sig_info;
    if (!g_enabled || !g_hook_installed || !g_api) return;

    bool game_menu = true;
    int32_t world_id = 0;
    if (!em_static_field_read_bool(g_api->main_game_menu, &game_menu) ||
        !em_static_field_read_i32(g_api->main_world_id, &world_id)) {
        if (!g_field_warning_logged) {
            EM_WORLD_LOG(MOD_LOG_LEVEL_WARNING,
                         "WorldRule state update paused: Main.gameMenu/worldID "
                         "fields unavailable or have unexpected types");
            g_field_warning_logged = true;
        }
        return;
    }

    bool in_world = !game_menu;
    if (!in_world) {
        if (g_world_active) reset_world_session();
        return;
    }

    uint32_t identity = (uint32_t)world_id;
    if (!g_world_active || !g_rules_initialized ||
        identity != g_world_identity) {
        if (!g_rules_initialized || identity != g_world_identity) {
            initialize_rules(identity);
        }
        g_world_active = true;
        EM_WORLD_LOG(MOD_LOG_LEVEL_INFO,
                     "World session active; passive rule effects remain disabled");
    }
}

void em_world_rule_shutdown(void) {
    g_enabled = false;
    g_hook_installed = false;
    g_api = nullptr;
    reset_world_session();
    g_rules_initialized = false;
    g_world_identity = 0;
    g_rng_state = 0;
    g_active_rule_count = 0;
    std::memset(g_active_rules, 0, sizeof(g_active_rules));
}
