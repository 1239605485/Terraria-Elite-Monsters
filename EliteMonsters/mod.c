#include "mod_core.h"
#include "mod_logger.h"
#include "tefkernel/patchlib/type.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/string.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void (*mod_logger_write)(mod_log_level_t level, const char* tag, const char* fmt, ...) = NULL;

#define ELITE_LOG(level, ...) \
    do { \
        if (mod_logger_write) { \
            mod_logger_write((level), "EliteMonsters", __VA_ARGS__); \
        } \
    } while (0)

typedef enum elite_rank_t {
    ELITE_NORMAL = 1,
    ELITE_RARE = 3,
    ELITE_LEGENDARY = 5
} elite_rank_t;

typedef enum elite_progress_t {
    PROGRESS_PRE_HARDMODE,
    PROGRESS_HARDMODE_EARLY,
    PROGRESS_PRE_PLANTERA,
    PROGRESS_POST_PLANTERA,
    PROGRESS_ENDGAME
} elite_progress_t;

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
    int32_t defense_bonus;
    float scale_multiplier;
    float knockback_multiplier;
    float gold_multiplier;
    uint32_t affix_mask;
} elite_profile_t;

/* Final values are selected from both world progress and elite rank.  This
 * keeps early-game elites reasonable while allowing late-game elites to stay
 * threatening. Defense is additive so low-defense enemies still receive a
 * meaningful increase. */
static const elite_profile_t g_progress_profiles[5][3] = {
    {
        {ELITE_NORMAL, 1.40f, 1.15f, 4, 1.05f, 1.10f, 2.0f, 0},
        {ELITE_RARE, 2.00f, 1.40f, 8, 1.12f, 1.18f, 4.0f, 0},
        {ELITE_LEGENDARY, 3.00f, 1.80f, 12, 1.20f, 1.28f, 8.0f, 0}
    },
    {
        {ELITE_NORMAL, 1.70f, 1.35f, 8, 1.08f, 1.12f, 3.0f, 0},
        {ELITE_RARE, 2.60f, 1.80f, 15, 1.18f, 1.22f, 6.0f, 0},
        {ELITE_LEGENDARY, 4.20f, 2.40f, 24, 1.30f, 1.35f, 12.0f, 0}
    },
    {
        {ELITE_NORMAL, 2.00f, 1.55f, 12, 1.10f, 1.14f, 4.0f, 0},
        {ELITE_RARE, 3.40f, 2.15f, 22, 1.22f, 1.28f, 8.0f, 0},
        {ELITE_LEGENDARY, 5.50f, 3.00f, 36, 1.38f, 1.42f, 16.0f, 0}
    },
    {
        {ELITE_NORMAL, 2.40f, 1.80f, 18, 1.12f, 1.16f, 5.0f, 0},
        {ELITE_RARE, 4.20f, 2.60f, 32, 1.28f, 1.32f, 10.0f, 0},
        {ELITE_LEGENDARY, 7.00f, 3.80f, 52, 1.50f, 1.48f, 20.0f, 0}
    },
    {
        {ELITE_NORMAL, 3.00f, 2.10f, 26, 1.15f, 1.18f, 6.0f, 0},
        {ELITE_RARE, 5.50f, 3.20f, 45, 1.32f, 1.38f, 12.0f, 0},
        {ELITE_LEGENDARY, 9.00f, 4.80f, 75, 1.60f, 1.58f, 25.0f, 0}
    }
};

/* 测试配置：按旅途、经典、专家、大师、传奇顺序排列，全部提高到 100%。
 * 这样每个符合条件的普通敌怪都会尝试转化为精英怪，方便验证功能。
 * 测试完成后建议恢复为正式概率。 */
static const int g_spawn_chance_percent[5] = {100, 100, 100, 100, 100};
#define SETDEFAULTS_HOOK_LIMIT 8
static patch_hook_id_t g_setdefaults_hooks[SETDEFAULTS_HOOK_LIMIT];
static size_t g_setdefaults_hook_count = 0;
#define NPC_NAME_HOOK_LIMIT 3
static patch_hook_id_t g_npc_name_hooks[NPC_NAME_HOOK_LIMIT];
static size_t g_npc_name_hook_count = 0;
#define MOUSE_TEXT_HOOK_LIMIT 2
static patch_hook_id_t g_mouse_text_hooks[MOUSE_TEXT_HOOK_LIMIT];
static size_t g_mouse_text_hook_count = 0;
#define AI_HOOK_LIMIT 1
static patch_hook_id_t g_ai_hooks[AI_HOOK_LIMIT];
static size_t g_ai_hook_count = 0;
static int g_ai_method_token = -1;
static unsigned long g_setdefaults_calls = 0;
static unsigned long g_elite_count = 0;

/* SetDefaults can be called more than once for the same object.  Keep a
 * bounded pointer set so a transformed NPC is not multiplied repeatedly. */
#define PROCESSED_INSTANCE_LIMIT 1024
static void *g_processed_instances[PROCESSED_INSTANCE_LIMIT];
static size_t g_processed_instance_count = 0;
static void *g_elite_instances[PROCESSED_INSTANCE_LIMIT];
static elite_rank_t g_elite_ranks[PROCESSED_INSTANCE_LIMIT];
static uint32_t g_elite_ai_ticks[PROCESSED_INSTANCE_LIMIT];
static size_t g_elite_instance_count = 0;

static patch_handle_t g_main_game_mode_field = NULL;
static patch_handle_t g_main_hard_mode_field = NULL;
static patch_handle_t g_npc_downed_mech_field = NULL;
static patch_handle_t g_npc_downed_plant_field = NULL;
static patch_handle_t g_npc_downed_golem_field = NULL;
static patch_handle_t g_npc_downed_moonlord_field = NULL;
static patch_handle_t g_main_my_player_field = NULL;
static patch_handle_t g_field_type = NULL;
static patch_handle_t g_field_life = NULL;
static patch_handle_t g_field_life_max = NULL;
static patch_handle_t g_field_damage = NULL;
static patch_handle_t g_field_defense = NULL;
static patch_handle_t g_field_knockback_resist = NULL;
static patch_handle_t g_field_width = NULL;
static patch_handle_t g_field_height = NULL;
static patch_handle_t g_field_scale = NULL;
static patch_handle_t g_field_value = NULL;
static patch_handle_t g_field_friendly = NULL;
static patch_handle_t g_field_town_npc = NULL;
static patch_handle_t g_field_boss = NULL;
static patch_handle_t g_field_target = NULL;
static patch_handle_t g_field_direction = NULL;
static patch_handle_t g_field_no_gravity = NULL;
static patch_handle_t g_field_velocity = NULL;
static bool g_progress_fields_logged = false;

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

static bool is_elite_instance(void *instance) {
    for (size_t i = 0; i < g_elite_instance_count; ++i) {
        if (g_elite_instances[i] == instance) return true;
    }
    return false;
}

static size_t elite_instance_index(void *instance) {
    for (size_t i = 0; i < g_elite_instance_count; ++i) {
        if (g_elite_instances[i] == instance) return i;
    }
    return PROCESSED_INSTANCE_LIMIT;
}

static void remember_elite_instance(void *instance, elite_rank_t rank) {
    if (!instance || is_elite_instance(instance)) return;
    if (g_elite_instance_count < PROCESSED_INSTANCE_LIMIT) {
        g_elite_instances[g_elite_instance_count] = instance;
        g_elite_ranks[g_elite_instance_count] = rank;
        g_elite_ai_ticks[g_elite_instance_count] = 0;
        ++g_elite_instance_count;
    } else {
        size_t slot = g_elite_count % PROCESSED_INSTANCE_LIMIT;
        g_elite_instances[slot] = instance;
        g_elite_ranks[slot] = rank;
        g_elite_ai_ticks[slot] = 0;
    }
}

static elite_rank_t elite_rank_for_instance(void *instance) {
    for (size_t i = 0; i < g_elite_instance_count; ++i) {
        if (g_elite_instances[i] == instance) return g_elite_ranks[i];
    }
    return ELITE_NORMAL;
}

static elite_profile_t make_profile(elite_progress_t progress) {
    if (progress < PROGRESS_PRE_HARDMODE || progress > PROGRESS_ENDGAME) {
        progress = PROGRESS_PRE_HARDMODE;
    }

    int rank_roll = random_percent();
    size_t rank_index = 0;
    if (rank_roll < 5) {
        rank_index = 2;
    } else if (rank_roll < 30) {
        rank_index = 1;
    }

    elite_profile_t p = g_progress_profiles[progress][rank_index];
    if (p.rank == ELITE_LEGENDARY) {
        p.affix_mask = (1u << AFFIX_ABYSSAL) | (1u << AFFIX_ENRAGED) |
                       (1u << AFFIX_FLAME) | (1u << AFFIX_SPLIT);
    } else if (p.rank == ELITE_RARE) {
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

static elite_progress_t current_progress(void) {
    bool hard_mode = false;
    bool downed_mech = false;
    bool downed_plant = false;
    bool downed_golem = false;
    bool downed_moonlord = false;

    (void)read_bool(g_main_hard_mode_field, NULL, &hard_mode);
    (void)read_bool(g_npc_downed_mech_field, NULL, &downed_mech);
    (void)read_bool(g_npc_downed_plant_field, NULL, &downed_plant);
    (void)read_bool(g_npc_downed_golem_field, NULL, &downed_golem);
    (void)read_bool(g_npc_downed_moonlord_field, NULL, &downed_moonlord);

    if (downed_moonlord) return PROGRESS_ENDGAME;
    if (downed_plant || downed_golem) return PROGRESS_POST_PLANTERA;
    if (downed_mech) return PROGRESS_PRE_PLANTERA;
    if (hard_mode) return PROGRESS_HARDMODE_EARLY;
    return PROGRESS_PRE_HARDMODE;
}

static const char *progress_name(elite_progress_t progress) {
    static const char *names[5] = {
        "pre-hardmode", "hardmode-early", "pre-plantera",
        "post-plantera", "endgame"
    };
    if (progress < PROGRESS_PRE_HARDMODE || progress > PROGRESS_ENDGAME) {
        return names[0];
    }
    return names[progress];
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

    /* Do not transform friendly, town, or boss NPCs.
     * SetDefaults can run before active becomes true, so active must not be
     * used as a filter here. */
    if (read_bool(g_field_friendly, instance, &value) && value) return;
    if (read_bool(g_field_town_npc, instance, &value) && value) return;
    if (read_bool(g_field_boss, instance, &value) && value) return;

    remember_processed(instance);
    if (!elite_should_spawn(current_world_mode())) return;

    elite_progress_t progress = current_progress();
    elite_profile_t profile = make_profile(progress);
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
        int64_t adjusted = (int64_t)defense + profile.defense_bonus;
        if (adjusted < 0) adjusted = 0;
        if (adjusted > INT32_MAX) adjusted = INT32_MAX;
        changed |= write_i32(g_field_defense, instance, adjusted);
    }

    float knockback = 0.0f;
    if (valid_field(g_field_knockback_resist, PATCH_FLOAT)) {
        patchlib_field_get_value(g_field_knockback_resist, instance, &knockback);
        changed |= write_float(g_field_knockback_resist, instance,
                               knockback * profile.knockback_multiplier);
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

    /* Vanilla NPC.value controls the normal coin drop. Keep the reward
     * entirely vanilla while making higher elite ranks worth more. */
    if (valid_field(g_field_value, PATCH_FLOAT)) {
        float value = 0.0f;
        patchlib_field_get_value(g_field_value, instance, &value);
        changed |= write_float(g_field_value, instance,
                               value * profile.gold_multiplier);
    }

    /* Keep the instance marked even if a field is unavailable in this game
     * build. The name and drawing hooks still need to identify the NPC. */
    remember_elite_instance(instance, profile.rank);
    if (changed) {
        ++g_elite_count;
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Elite NPC transformed: type=%d progress=%s rank=%d gold=%.1fx affixes=0x%X total=%lu",
                  (int)npc_type, progress_name(progress), (int)profile.rank,
                  (double)profile.gold_multiplier,
                  (unsigned)profile.affix_mask, g_elite_count);
    }
}

/* Add a visible marker at the NPC name source. The MouseText hook below also
 * applies a rank-specific vanilla rarity directly, so this works even when the Android build
 * does not parse [c/...] tags in the NPC hover renderer. */
static void npc_name_postfix(patch_handle_t instance, void **args, void *result,
                             const patch_method_signature_t *sig_info) {
    (void)args;
    (void)sig_info;
    if (!instance || !result || !is_elite_instance(instance)) return;

    patch_handle_t original = *(patch_handle_t *)result;
    if (!original || !patchlib_is_valid(original)) return;
    char *name = patchlib_string_cstr(original);
    if (!name || !name[0]) {
        free(name);
        return;
    }
    if (strstr(name, "精英·") != NULL ||
        strstr(name, "稀有·") != NULL ||
        strstr(name, "传奇·") != NULL) {
        free(name);
        return;
    }

    char decorated[512];
    /* Do not put [c/...] into the NPC name: this Android build displays the
 * tag literally. Main.MouseText applies the rank color separately. */
    const char *prefix = "精英·";
    elite_rank_t rank = elite_rank_for_instance(instance);
    if (rank == ELITE_RARE) prefix = "稀有·";
    if (rank == ELITE_LEGENDARY) prefix = "传奇·";
    (void)snprintf(decorated, sizeof(decorated), "%s%s", prefix, name);
    patch_handle_t replacement = patchlib_string_create(decorated);
    if (replacement && patchlib_is_valid(replacement)) {
        *(patch_handle_t *)result = replacement;
    }
    free(name);
}

static void setdefaults_postfix(patch_handle_t instance, void **args, void *result,
                                const patch_method_signature_t *sig_info) {
    (void)instance;
    (void)args;
    (void)result;
    ++g_setdefaults_calls;
    if (g_setdefaults_calls == 1) {
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
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
    g_field_value = patchlib_type_get_field(npc, "value");
    g_field_friendly = patchlib_type_get_field(npc, "friendly");
    g_field_town_npc = patchlib_type_get_field(npc, "townNPC");
    g_field_boss = patchlib_type_get_field(npc, "boss");
    g_field_target = patchlib_type_get_field(npc, "target");
    g_field_direction = patchlib_type_get_field(npc, "direction");
    g_field_no_gravity = patchlib_type_get_field(npc, "noGravity");
    g_field_velocity = patchlib_type_get_field(npc, "velocity");

    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (main_type && patchlib_is_valid(main_type)) {
        g_main_game_mode_field = patchlib_type_get_field(main_type, "GameMode");
        if (!g_main_game_mode_field || !patchlib_is_valid(g_main_game_mode_field)) {
            g_main_game_mode_field = patchlib_type_get_field(main_type, "gameMode");
        }
        g_main_hard_mode_field = patchlib_type_get_field(main_type, "hardMode");
        if (!g_main_hard_mode_field || !patchlib_is_valid(g_main_hard_mode_field)) {
            g_main_hard_mode_field = patchlib_type_get_field(main_type, "HardMode");
        }
        g_main_my_player_field = patchlib_type_get_field(main_type, "myPlayer");
    }

    g_npc_downed_mech_field = patchlib_type_get_field(npc, "downedMechBossAny");
    g_npc_downed_plant_field = patchlib_type_get_field(npc, "downedPlantBoss");
    g_npc_downed_golem_field = patchlib_type_get_field(npc, "downedGolemBoss");
    g_npc_downed_moonlord_field = patchlib_type_get_field(npc, "downedMoonlord");

    if (!g_progress_fields_logged) {
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Progress fields: hardMode=%d mech=%d plant=%d golem=%d moonlord=%d",
                  g_main_hard_mode_field != NULL,
                  g_npc_downed_mech_field != NULL,
                  g_npc_downed_plant_field != NULL,
                  g_npc_downed_golem_field != NULL,
                  g_npc_downed_moonlord_field != NULL);
        g_progress_fields_logged = true;
    }
}

/* Resolve the game-side spawn entry point at runtime.  Terraria's IL2CPP
 * metadata uses overloads, so hook every SetDefaults overload that the API
 * exposes.  This avoids silently selecting an unrelated overload by name. */
static void discover_spawn_api(void) {
    patch_handle_t npc = patchlib_type_get_type("Terraria", "NPC");
    if (!npc || !patchlib_is_valid(npc)) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC type not found; spawn hook deferred");
        return;
    }
    cache_npc_fields(npc);
    for (int args_count = 0;
         args_count <= 4 && g_setdefaults_hook_count < SETDEFAULTS_HOOK_LIMIT;
         ++args_count) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            npc, "SetDefaults", args_count);
        if (!method || !patchlib_is_valid(method)) continue;

        patch_method_signature_t sig = {0};
        if (!patchlib_method_get_signature(method, &sig)) continue;
        if (!sig.is_instance) {
            patchlib_method_signature_free(&sig);
            continue;
        }

        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "SetDefaults candidate: params=%d", args_count);
        patch_hook_id_t hook_id = patchlib_install_prepost_hook(
            method, NULL, setdefaults_postfix);
        if (hook_id != PATCH_HOOK_INVALID_ID) {
            g_setdefaults_hooks[g_setdefaults_hook_count++] = hook_id;
            ELITE_LOG(MOD_LOG_LEVEL_INFO,
                      "SetDefaults postfix hook installed: params=%d id=%d",
                      args_count, (int)hook_id);
        } else {
            ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                      "SetDefaults postfix hook failed: params=%d", args_count);
        }
        patchlib_method_signature_free(&sig);
    }
}

static void discover_name_api(patch_handle_t npc) {
    const char *name_getters[NPC_NAME_HOOK_LIMIT] = {
        "get_FullName", "get_TypeName", "get_GivenOrTypeName"
    };

    for (size_t i = 0; i < NPC_NAME_HOOK_LIMIT; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            npc, name_getters[i], 0);
        if (!method || !patchlib_is_valid(method)) continue;

        patch_method_signature_t sig = {0};
        if (!patchlib_method_get_signature(method, &sig)) continue;
        if (!sig.is_instance || sig.return_type != PATCH_OBJECT) {
            patchlib_method_signature_free(&sig);
            continue;
        }

        patch_hook_id_t hook_id = patchlib_install_prepost_hook(
            method, NULL, npc_name_postfix);
        if (hook_id != PATCH_HOOK_INVALID_ID &&
            g_npc_name_hook_count < NPC_NAME_HOOK_LIMIT) {
            g_npc_name_hooks[g_npc_name_hook_count++] = hook_id;
            ELITE_LOG(MOD_LOG_LEVEL_INFO,
                      "NPC name getter hook installed: %s id=%d",
                      name_getters[i], (int)hook_id);
        }
        patchlib_method_signature_free(&sig);
    }
}

/* Main.MouseText receives the final hover string and an integer rarity.
 * Vanilla rarity 0 is white, 1 is blue, and 11 is purple. This direct color
 * path avoids putting [c/...] markup into the NPC name on Android. */
static bool mouse_text_prefix(patch_handle_t instance, void **args,
                              const patch_method_signature_t *sig_info,
                              void *result) {
    (void)instance;
    (void)result;
    if (!args || !sig_info ||
        tefstd_vector_size(&sig_info->arg_types) < 3 || !args[0]) {
        return false;
    }

    patch_handle_t text_handle = *(patch_handle_t *)args[0];
    if (!text_handle || !patchlib_is_valid(text_handle)) return false;
    char *text = patchlib_string_cstr(text_handle);
    if (!text) return false;

    int rarity = -1;
    if (strstr(text, "传奇·") != NULL) {
        rarity = 11;
    } else if (strstr(text, "稀有·") != NULL) {
        rarity = 1;
    } else if (strstr(text, "精英·") != NULL) {
        rarity = 0;
    }
    free(text);
    if (rarity < 0) return false;

    const size_t arg_count = tefstd_vector_size(&sig_info->arg_types);
    /* MouseText(string, int, byte, ...) and
     * MouseText(string, string, int, byte, ...). */
    const size_t rare_index = (arg_count >= 10) ? 2 : 1;
    if (args[rare_index]) *(int *)args[rare_index] = rarity;
    return false;
}

static void discover_mouse_text_api(patch_handle_t main_type) {
    const int arg_counts[MOUSE_TEXT_HOOK_LIMIT] = {8, 10};
    for (size_t i = 0; i < MOUSE_TEXT_HOOK_LIMIT; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            main_type, "MouseText", arg_counts[i]);
        if (!method || !patchlib_is_valid(method)) continue;

        patch_method_signature_t sig = {0};
        if (!patchlib_method_get_signature(method, &sig)) continue;
        if (!sig.is_instance || sig.return_type != PATCH_VOID) {
            patchlib_method_signature_free(&sig);
            continue;
        }

        patch_hook_id_t hook_id = patchlib_install_prepost_hook(
            method, mouse_text_prefix, NULL);
        if (hook_id != PATCH_HOOK_INVALID_ID &&
            g_mouse_text_hook_count < MOUSE_TEXT_HOOK_LIMIT) {
            g_mouse_text_hooks[g_mouse_text_hook_count++] = hook_id;
            ELITE_LOG(MOD_LOG_LEVEL_INFO,
                      "Main.MouseText color hook installed: params=%d id=%d",
                      arg_counts[i], (int)hook_id);
        }
        patchlib_method_signature_free(&sig);
    }
}

/* AI enhancement layer. The original NPC.AI runs first; this postfix only
 * improves target selection and gives rare/legendary elites a cooldown-based
 * dash. If velocity is not exposed as a safe 8-byte Vector2 field on a game
 * build, target locking remains active and the dash is skipped. */
static void ai_postfix(patch_handle_t instance, void **args, void *result,
                       const patch_method_signature_t *sig_info) {
    (void)args;
    (void)result;
    (void)sig_info;
    if (!instance || !is_elite_instance(instance)) return;

    size_t index = elite_instance_index(instance);
    if (index >= PROCESSED_INSTANCE_LIMIT) return;
    ++g_elite_ai_ticks[index];

    int32_t player = -1;
    if (read_i32(g_main_my_player_field, NULL, &player) && player >= 0) {
        (void)write_i32(g_field_target, instance, player);
    }

    elite_rank_t rank = g_elite_ranks[index];
    if (rank == ELITE_NORMAL) return;
    uint32_t dash_interval = rank == ELITE_LEGENDARY ? 90u : 150u;
    if (g_elite_ai_ticks[index] % dash_interval != 0u) return;

#if defined(__ANDROID__)
    if (!valid_field(g_field_velocity, PATCH_OBJECT) ||
        patchlib_field_get_size(g_field_velocity) != 8u) {
        return;
    }
    float *velocity = (float *)patchlib_field_get_pointer(g_field_velocity,
                                                           instance);
    if (!velocity) return;

    int32_t direction = 1;
    (void)read_i32(g_field_direction, instance, &direction);
    if (direction == 0) direction = 1;
    float dash_speed = rank == ELITE_LEGENDARY ? 8.0f : 6.0f;
    velocity[0] = direction > 0 ? dash_speed : -dash_speed;

    bool no_gravity = true;
    (void)read_bool(g_field_no_gravity, instance, &no_gravity);
    if (!no_gravity && velocity[1] > -6.0f) velocity[1] = -6.0f;
#endif
}

static bool install_ai_hook(patch_handle_t method, const char *name) {
    if (!method || !patchlib_is_valid(method)) return false;

    patch_method_signature_t sig = {0};
    if (!patchlib_method_get_signature(method, &sig)) return false;

    bool supported = sig.is_instance && sig.return_type == PATCH_VOID &&
                     tefstd_vector_size(&sig.arg_types) == 0;
    int token = supported ? patchlib_method_get_token(method) : 0;
    patchlib_method_signature_free(&sig);
    if (!supported) return false;

    patch_hook_id_t hook_id = patchlib_install_prepost_hook(
        method, NULL, ai_postfix);
    if (hook_id == PATCH_HOOK_INVALID_ID ||
        g_ai_hook_count >= AI_HOOK_LIMIT) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC AI enhancement hook failed: name=%s", name);
        return false;
    }

    g_ai_hooks[g_ai_hook_count++] = hook_id;
    g_ai_method_token = token;
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "NPC AI enhancement hook installed: name=%s params=0 id=%d",
              name, (int)hook_id);
    return true;
}

static void discover_ai_api(patch_handle_t npc) {
    /* Method names differ between Terraria IL2CPP exports. In particular,
     * some builds expose NPC AI as AI_007 instead of AI, and parameter-count
     * lookup can miss methods whose metadata is renamed. Enumerate the actual
     * method table first, then choose the best supported instance void method
     * whose name starts with AI. */
    /* The kernel implementation initializes the output vector as well, but
     * initialize it here too for compatibility with older TEFKernel builds. */
    tefstd_vector_t methods = {0};
    if (!tefstd_vector_init(&methods, sizeof(patch_handle_t))) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC AI method vector initialization failed");
        return;
    }

    /* Terraria 1.4.5.6.4 has a public parameterless NPC.AI dispatcher. Use
     * this exact method first so the postfix runs for every NPC AI style. */
    patch_handle_t direct = patchlib_type_get_method_by_param_count(
        npc, "AI", 0);
    if (direct && install_ai_hook(direct, "AI")) {
        tefstd_vector_destroy(&methods);
        return;
    }
    ELITE_LOG(MOD_LOG_LEVEL_WARNING,
              "NPC.AI direct lookup failed; using method enumeration");

    if (!patchlib_type_get_methods(npc, true, &methods)) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC AI method enumeration failed");
        tefstd_vector_destroy(&methods);
        return;
    }

    patch_handle_t selected_method = NULL;
    const char *selected_name = NULL;
    int selected_token = -1;
    int selected_score = INT_MAX;
    size_t method_count = tefstd_vector_size(&methods);

    for (size_t i = 0; i < method_count; ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
        patch_handle_t method = entry ? *entry : NULL;
        if (!method || !patchlib_is_valid(method)) continue;

        const char *name = patchlib_method_get_name(method);
        if (!name || strncmp(name, "AI", 2) != 0) continue;

        int params = patchlib_method_get_param_count(method);
        patch_method_signature_t sig = {0};
        if (!patchlib_method_get_signature(method, &sig)) continue;

        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "NPC AI candidate: name=%s params=%d instance=%d return=%d",
                  name, params, sig.is_instance ? 1 : 0,
                  (int)sig.return_type);

        bool supported = sig.is_instance && sig.return_type == PATCH_VOID &&
                         params == 0;
        int token = supported ? patchlib_method_get_token(method) : 0;
        patchlib_method_signature_free(&sig);
        if (!supported) continue;

        int score = 100;
        if (strcmp(name, "AI") == 0) {
            score = 0;
        } else if (strncmp(name, "AI_", 3) == 0) {
            score = 10;
        }
        if (params != 0) score += 1;

        if (score < selected_score) {
            selected_method = method;
            selected_name = name;
            selected_token = token;
            selected_score = score;
        }
    }

    if (selected_method && selected_token != g_ai_method_token &&
        install_ai_hook(selected_method, selected_name)) {
        tefstd_vector_destroy(&methods);
        return;
    }

    tefstd_vector_destroy(&methods);
    ELITE_LOG(MOD_LOG_LEVEL_WARNING,
              "NPC AI enhancement hook not found in this game build");
}

static void init_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    srand(0x454C4954u);
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Loaded Android Hook probe; resolving NPC spawn API");
    discover_spawn_api();
    patch_handle_t npc = patchlib_type_get_type("Terraria", "NPC");
    if (npc && patchlib_is_valid(npc)) {
        discover_name_api(npc);
        discover_ai_api(npc);
    }
    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (main_type && patchlib_is_valid(main_type)) {
        discover_mouse_text_api(main_type);
    }
    (void)elite_should_spawn;
    (void)make_profile;
}

static void cleanup_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    for (size_t i = 0; i < g_setdefaults_hook_count; ++i) {
        patchlib_uninstall_hook(g_setdefaults_hooks[i]);
    }
    g_setdefaults_hook_count = 0;
    for (size_t i = 0; i < g_npc_name_hook_count; ++i) {
        patchlib_uninstall_hook(g_npc_name_hooks[i]);
    }
    g_npc_name_hook_count = 0;
    for (size_t i = 0; i < g_mouse_text_hook_count; ++i) {
        patchlib_uninstall_hook(g_mouse_text_hooks[i]);
    }
    g_mouse_text_hook_count = 0;
    for (size_t i = 0; i < g_ai_hook_count; ++i) {
        patchlib_uninstall_hook(g_ai_hooks[i]);
    }
    g_ai_hook_count = 0;
    g_ai_method_token = -1;
    ELITE_LOG(MOD_LOG_LEVEL_INFO, "Unloaded");
}

static kernel_mod_info_t g_info = {
    .pkg_id = "eternal.future.elitemonsters",
    .version_code = 2026083113,
    .api_version = 1,
    .version = "1.0.3"
};

static kernel_mod_info_t* get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t* create_kernel_mod(void) { return &g_ops; }
