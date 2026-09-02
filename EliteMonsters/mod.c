#include "mod_core.h"
#include "mod_logger.h"
#include "tefkernel/patchlib/type.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

void (*mod_logger_write)(mod_log_level_t level, const char* tag, const char* fmt, ...) = NULL;

typedef enum elite_rank_t {
    ELITE_NORMAL = 1,
    ELITE_RARE = 3,
    ELITE_LEGENDARY = 5
} elite_rank_t;

typedef enum elite_affix_t {
    AFFIX_FLAME,
    AFFIX_FROST,
    AFFIX_VAMPIRIC,
    AFFIX_SPLIT,
    AFFIX_ENRAGED,
    AFFIX_ABYSSAL
} elite_affix_t;

typedef struct elite_profile_t {
    elite_rank_t rank;
    float health_multiplier;
    float damage_multiplier;
    float defense_multiplier;
    float speed_multiplier;
    float scale_multiplier;
    uint32_t affix_mask;
} elite_profile_t;

/* 概率按旅途、经典、专家、大师、传奇顺序排列。 */
static const int g_spawn_chance_percent[5] = {2, 5, 10, 15, 20};
static patch_hook_id_t g_setdefaults_hook = PATCH_HOOK_INVALID_ID;
static unsigned long g_setdefaults_calls = 0;
static unsigned long g_elite_count = 0;

/* SetDefaults can be called more than once for the same object.  Keep a
 * bounded pointer set so a transformed NPC is not multiplied repeatedly. */
#define PROCESSED_INSTANCE_LIMIT 1024
static void *g_processed_instances[PROCESSED_INSTANCE_LIMIT];
static size_t g_processed_instance_count = 0;

static patch_handle_t g_main_game_mode_field = NULL;
static patch_handle_t g_field_type = NULL;
static patch_handle_t g_field_life = NULL;
static patch_handle_t g_field_life_max = NULL;
static patch_handle_t g_field_damage = NULL;
static patch_handle_t g_field_defense = NULL;
static patch_handle_t g_field_knockback_resist = NULL;
static patch_handle_t g_field_width = NULL;
static patch_handle_t g_field_height = NULL;
static patch_handle_t g_field_scale = NULL;
static patch_handle_t g_field_friendly = NULL;
static patch_handle_t g_field_town_npc = NULL;
static patch_handle_t g_field_boss = NULL;
static patch_handle_t g_field_active = NULL;

static int random_percent(void) {
    return rand() % 100;
}

static int32_t scaled_i32(int32_t value, float multiplier) {
    double scaled = (double)value * (double)multiplier;
    if (scaled < 1.0) return 1;
    if (scaled > (double)INT32_MAX) return INT32_MAX;
    return (int32_t)(scaled + 0.5);
}

static bool valid_field(patch_handle_t field, patch_type_t type) {
    return field && patchlib_is_valid(field) && patchlib_field_get_type(field) == type;
}

static bool read_i32(patch_handle_t field, patch_handle_t instance, int32_t *out) {
    if (!out || !valid_field(field, PATCH_INT32)) return false;
    patchlib_field_get_value(field, instance, out);
    return true;
}

static bool read_bool(patch_handle_t field, patch_handle_t instance, bool *out) {
    if (!out || !valid_field(field, PATCH_BOOL)) return false;
    patchlib_field_get_value(field, instance, out);
    return true;
}

static bool write_i32(patch_handle_t field, patch_handle_t instance, int32_t value) {
    if (!valid_field(field, PATCH_INT32)) return false;
    patchlib_field_set_value(field, instance, &value);
    return true;
}

static bool write_float(patch_handle_t field, patch_handle_t instance, float value) {
    if (!valid_field(field, PATCH_FLOAT)) return false;
    patchlib_field_set_value(field, instance, &value);
    return true;
}

static bool already_processed(void *instance) {
    for (size_t i = 0; i < g_processed_instance_count; ++i) {
        if (g_processed_instances[i] == instance) return true;
    }
    return false;
}

static void remember_processed(void *instance) {
    if (!instance || already_processed(instance)) return;
    if (g_processed_instance_count < PROCESSED_INSTANCE_LIMIT) {
        g_processed_instances[g_processed_instance_count++] = instance;
    } else {
        /* Reuse the oldest slot rather than growing unboundedly. */
        size_t slot = g_elite_count % PROCESSED_INSTANCE_LIMIT;
        g_processed_instances[slot] = instance;
    }
}

static elite_profile_t make_profile(int world_mode) {
    elite_profile_t p = { ELITE_NORMAL, 1.25f, 1.20f, 0.90f, 1.05f, 1.20f, 0 };
    int roll = random_percent();

    if (world_mode >= 4 && roll < 3) {
        p.rank = ELITE_LEGENDARY;
        p.health_multiplier = 10.0f;
        p.damage_multiplier = 5.0f;
        p.defense_multiplier = 0.65f;
        p.speed_multiplier = 1.45f;
        p.scale_multiplier = 3.0f;
        p.affix_mask = (1u << AFFIX_ABYSSAL) | (1u << AFFIX_ENRAGED) |
                       (1u << AFFIX_FLAME) | (1u << AFFIX_SPLIT);
    } else if (world_mode >= 2 && roll < 20) {
        p.rank = ELITE_RARE;
        p.health_multiplier = 2.5f;
        p.damage_multiplier = 1.8f;
        p.defense_multiplier = 0.80f;
        p.speed_multiplier = 1.20f;
        p.scale_multiplier = 1.50f;
        p.affix_mask = (1u << (random_percent() % 5)) |
                       (1u << (random_percent() % 5));
    } else {
        p.affix_mask = 1u << (random_percent() % 4);
    }
    return p;
}

/* 生成 Hook 接入后调用此函数；world_mode: 0=旅途,1=经典,2=专家,3=大师,4=传奇。 */
static int elite_should_spawn(int world_mode) {
    if (world_mode < 0) world_mode = 0;
    if (world_mode > 4) world_mode = 4;
    return random_percent() < g_spawn_chance_percent[world_mode];
}

static int current_world_mode(void) {
    int32_t mode = 1; /* classic is the safe fallback when Main is unavailable */
    if (g_main_game_mode_field) {
        (void)read_i32(g_main_game_mode_field, NULL, &mode);
    }
    if (mode < 0) mode = 0;
    if (mode > 4) mode = 4;
    return (int)mode;
}

static void apply_elite_profile(patch_handle_t instance) {
    if (!instance || already_processed(instance)) return;

    int32_t npc_type = 0;
    int32_t life_max = 0;
    bool value = false;
    if (!read_i32(g_field_type, instance, &npc_type) || npc_type <= 0 ||
        !read_i32(g_field_life_max, instance, &life_max) || life_max <= 0) {
        return;
    }

    /* Do not transform friendly, town, boss, inactive, or invulnerable NPCs. */
    if (read_bool(g_field_friendly, instance, &value) && value) return;
    if (read_bool(g_field_town_npc, instance, &value) && value) return;
    if (read_bool(g_field_boss, instance, &value) && value) return;
    if (read_bool(g_field_active, instance, &value) && !value) return;

    remember_processed(instance);
    if (!elite_should_spawn(current_world_mode())) return;

    elite_profile_t profile = make_profile(current_world_mode());
    int32_t life = life_max;
    (void)read_i32(g_field_life, instance, &life);

    bool changed = false;
    changed |= write_i32(g_field_life_max, instance,
                         scaled_i32(life_max, profile.health_multiplier));
    changed |= write_i32(g_field_life, instance,
                         scaled_i32(life, profile.health_multiplier));

    int32_t damage = 0;
    if (read_i32(g_field_damage, instance, &damage)) {
        changed |= write_i32(g_field_damage, instance,
                             scaled_i32(damage, profile.damage_multiplier));
    }

    int32_t defense = 0;
    if (read_i32(g_field_defense, instance, &defense)) {
        int32_t adjusted = (int32_t)((double)defense * profile.defense_multiplier);
        if (adjusted < 0) adjusted = 0;
        changed |= write_i32(g_field_defense, instance, adjusted);
    }

    float knockback = 0.0f;
    if (valid_field(g_field_knockback_resist, PATCH_FLOAT)) {
        patchlib_field_get_value(g_field_knockback_resist, instance, &knockback);
        changed |= write_float(g_field_knockback_resist, instance,
                               knockback * profile.defense_multiplier);
    }

    int32_t width = 0;
    int32_t height = 0;
    if (read_i32(g_field_width, instance, &width)) {
        changed |= write_i32(g_field_width, instance,
                             scaled_i32(width, profile.scale_multiplier));
    }
    if (read_i32(g_field_height, instance, &height)) {
        changed |= write_i32(g_field_height, instance,
                             scaled_i32(height, profile.scale_multiplier));
    }
    if (valid_field(g_field_scale, PATCH_FLOAT)) {
        float scale = 1.0f;
        patchlib_field_get_value(g_field_scale, instance, &scale);
        changed |= write_float(g_field_scale, instance,
                               scale * profile.scale_multiplier);
    }

    if (changed) {
        ++g_elite_count;
        if (mod_logger_write) {
            mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                             "Elite NPC transformed: type=%d rank=%d affixes=0x%X total=%lu",
                             (int)npc_type, (int)profile.rank,
                             (unsigned)profile.affix_mask, g_elite_count);
        }
    }
}

static void setdefaults_postfix(patch_handle_t instance, void **args, void *result,
                                const patch_method_signature_t *sig_info) {
    (void)instance;
    (void)args;
    (void)result;
    ++g_setdefaults_calls;
    if (g_setdefaults_calls == 1 && mod_logger_write) {
        mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                         "SetDefaults hook executed (params=%d)",
                         sig_info ? (int)tefstd_vector_size(&sig_info->arg_types) : -1);
    }
    apply_elite_profile(instance);
}

static void cache_npc_fields(patch_handle_t npc) {
    g_field_type = patchlib_type_get_field(npc, "type");
    g_field_life = patchlib_type_get_field(npc, "life");
    g_field_life_max = patchlib_type_get_field(npc, "lifeMax");
    g_field_damage = patchlib_type_get_field(npc, "damage");
    g_field_defense = patchlib_type_get_field(npc, "defense");
    g_field_knockback_resist = patchlib_type_get_field(npc, "knockBackResist");
    g_field_width = patchlib_type_get_field(npc, "width");
    g_field_height = patchlib_type_get_field(npc, "height");
    g_field_scale = patchlib_type_get_field(npc, "scale");
    g_field_friendly = patchlib_type_get_field(npc, "friendly");
    g_field_town_npc = patchlib_type_get_field(npc, "townNPC");
    g_field_boss = patchlib_type_get_field(npc, "boss");
    g_field_active = patchlib_type_get_field(npc, "active");

    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (main_type && patchlib_is_valid(main_type)) {
        g_main_game_mode_field = patchlib_type_get_field(main_type, "GameMode");
        if (!g_main_game_mode_field || !patchlib_is_valid(g_main_game_mode_field)) {
            g_main_game_mode_field = patchlib_type_get_field(main_type, "gameMode");
        }
    }
}

/* Resolve the game-side spawn entry point at runtime.  Terraria's IL2CPP
 * metadata uses overloads, so keep this discovery separate from the hook
 * implementation and report every candidate we can safely inspect. */
static void discover_spawn_api(void) {
    patch_handle_t npc = patchlib_type_get_type("Terraria", "NPC");
    if (!npc || !patchlib_is_valid(npc)) {
        mod_logger_write(MOD_LOG_LEVEL_WARNING, "EliteMonsters",
                         "NPC type not found; spawn hook deferred");
        return;
    }
    cache_npc_fields(npc);
    const char *names[] = { "NewNPC", "SpawnNPC", "SetDefaults" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        patch_handle_t method = patchlib_type_get_method(npc, names[i]);
        if (method && patchlib_is_valid(method)) {
            mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                             "Spawn API candidate: Terraria.NPC.%s (params=%d)",
                             names[i], patchlib_method_get_param_count(method));
            if (names[i][0] == 'S' && names[i][1] == 'e') {
                patch_method_signature_t sig = {0};
                if (patchlib_method_get_signature(method, &sig)) {
                    for (size_t j = 0; j < tefstd_vector_size(&sig.arg_types); ++j) {
                        patch_type_t *t = (patch_type_t *)tefstd_vector_at(&sig.arg_types, j);
                        const char **n = (const char **)tefstd_vector_at(&sig.arg_names, j);
                        mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                                         "SetDefaults arg[%d]: name=%s type=%d", (int)j,
                                         (n && *n) ? *n : "?", t ? (int)*t : -1);
                    }
                    patchlib_method_signature_free(&sig);
                }
                g_setdefaults_hook = patchlib_install_prepost_hook(method, NULL,
                                                                     setdefaults_postfix);
                mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                                 "SetDefaults postfix hook id=%d", (int)g_setdefaults_hook);
            }
        }
    }
}

static void init_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    srand(0x454C4954u);
    mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                     "Loaded Android Hook probe; resolving NPC spawn API");
    discover_spawn_api();
    (void)elite_should_spawn;
    (void)make_profile;
}

static void cleanup_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    if (g_setdefaults_hook != PATCH_HOOK_INVALID_ID) {
        patchlib_uninstall_hook(g_setdefaults_hook);
        g_setdefaults_hook = PATCH_HOOK_INVALID_ID;
    }
    mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters", "Unloaded");
}

static kernel_mod_info_t g_info = {
    .pkg_id = "eternal.future.elitemonsters",
    .version_code = 2026083101,
    .api_version = 1,
    .version = "0.3.0"
};

static kernel_mod_info_t* get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t* create_kernel_mod(void) { return &g_ops; }
