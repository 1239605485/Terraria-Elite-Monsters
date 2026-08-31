#include "mod_core.h"
#include "mod_logger.h"
#include "tefkernel/patchlib/type.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/array.h"
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

/* The vanilla AI style gives us a stable, game-side way to choose a
 * movement pattern without replacing the NPC's original attack logic. */
typedef enum elite_behavior_t {
    ELITE_BEHAVIOR_MELEE,
    ELITE_BEHAVIOR_CHARGER,
    ELITE_BEHAVIOR_RANGED,
    ELITE_BEHAVIOR_FLYING,
    ELITE_BEHAVIOR_WORM,
    ELITE_BEHAVIOR_SPECIAL
} elite_behavior_t;

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
        {ELITE_LEGENDARY, 3.00f, 1.80f, 12, 1.20f, 1.28f, 10.0f, 0}
    },
    {
        {ELITE_NORMAL, 1.70f, 1.35f, 8, 1.08f, 1.12f, 3.0f, 0},
        {ELITE_RARE, 2.60f, 1.80f, 15, 1.18f, 1.22f, 6.0f, 0},
        {ELITE_LEGENDARY, 4.20f, 2.40f, 24, 1.30f, 1.35f, 15.0f, 0}
    },
    {
        {ELITE_NORMAL, 2.00f, 1.55f, 12, 1.10f, 1.14f, 4.0f, 0},
        {ELITE_RARE, 3.40f, 2.15f, 22, 1.22f, 1.28f, 8.0f, 0},
        {ELITE_LEGENDARY, 5.50f, 3.00f, 36, 1.38f, 1.42f, 20.0f, 0}
    },
    {
        {ELITE_NORMAL, 2.40f, 1.80f, 18, 1.12f, 1.16f, 5.0f, 0},
        {ELITE_RARE, 4.20f, 2.60f, 32, 1.28f, 1.32f, 10.0f, 0},
        {ELITE_LEGENDARY, 7.00f, 3.80f, 52, 1.50f, 1.48f, 25.0f, 0}
    },
    {
        {ELITE_NORMAL, 3.00f, 2.10f, 26, 1.15f, 1.18f, 6.0f, 0},
        {ELITE_RARE, 5.50f, 3.20f, 45, 1.32f, 1.38f, 12.0f, 0},
        {ELITE_LEGENDARY, 9.00f, 4.80f, 75, 1.60f, 1.58f, 30.0f, 0}
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
#define LOOT_HOOK_LIMIT 1
static patch_hook_id_t g_loot_hooks[LOOT_HOOK_LIMIT];
static size_t g_loot_hook_count = 0;
static unsigned long g_setdefaults_calls = 0;
static unsigned long g_elite_count = 0;
static unsigned long g_legendary_reward_count = 0;

/* SetDefaults can be called more than once for the same object.  Keep a
 * bounded pointer set so a transformed NPC is not multiplied repeatedly. */
#define PROCESSED_INSTANCE_LIMIT 1024
static void *g_processed_instances[PROCESSED_INSTANCE_LIMIT];
static size_t g_processed_instance_count = 0;
static void *g_elite_instances[PROCESSED_INSTANCE_LIMIT];
static elite_rank_t g_elite_ranks[PROCESSED_INSTANCE_LIMIT];
static elite_behavior_t g_elite_behaviors[PROCESSED_INSTANCE_LIMIT];
static uint32_t g_elite_ai_ticks[PROCESSED_INSTANCE_LIMIT];
static int32_t g_elite_base_damage[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_enraged[PROCESSED_INSTANCE_LIMIT];
static size_t g_elite_instance_count = 0;
static void *g_rewarded_instances[PROCESSED_INSTANCE_LIMIT];
static size_t g_rewarded_instance_count = 0;

/* Terraria 1.4 item IDs from the target game's ItemID table. These are both
 * original Terraria items; no custom item or custom material is introduced. */
#define ITEM_GOLDEN_CRATE 2336
#define ITEM_GOLDEN_CRATE_HARD 3981

static patch_handle_t g_main_game_mode_field = NULL;
static patch_handle_t g_main_hard_mode_field = NULL;
static patch_handle_t g_main_net_mode_field = NULL;
static patch_handle_t g_main_player_field = NULL;
static patch_handle_t g_item_new_item_method = NULL;
static patch_handle_t g_npc_downed_mech_field = NULL;
static patch_handle_t g_npc_downed_plant_field = NULL;
static patch_handle_t g_npc_downed_golem_field = NULL;
static patch_handle_t g_npc_downed_moonlord_field = NULL;
static patch_handle_t g_main_my_player_field = NULL;
static patch_handle_t g_field_type = NULL;
static patch_handle_t g_field_position = NULL;
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
static patch_handle_t g_field_ai_style = NULL;
static patch_handle_t g_field_direction = NULL;
static patch_handle_t g_field_net_update = NULL;
static patch_handle_t g_field_no_gravity = NULL;
static patch_handle_t g_field_velocity = NULL;
static patch_handle_t g_player_position_field = NULL;
static patch_handle_t g_player_width_field = NULL;
static patch_handle_t g_player_height_field = NULL;
static patch_handle_t g_player_active_field = NULL;
static patch_handle_t g_player_dead_field = NULL;
static bool g_progress_fields_logged = false;

#define LEGENDARY_ENRAGE_LIFE_PERCENT 35
#define LEGENDARY_ENRAGE_DAMAGE_MULTIPLIER 1.25f
#define LEGENDARY_TELEPORT_DISTANCE 480.0f
#define LEGENDARY_TELEPORT_OFFSET 96.0f

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

static bool get_field_value(patch_handle_t field, patch_handle_t instance,
                            void *out) {
    if (!field || !out || !patchlib_is_valid(field)) return false;

#if defined(__ANDROID__)
    /* TEFKernel's Android field helper historically handled const/thread
     * static fields specially, but not every ordinary static field. Use its
     * real static-data pointer here so Main.gameMode, Main.player, and the
     * other static Terraria fields are read reliably. */
    if (!instance && (patchlib_field_is_const(field) ||
                      patchlib_field_is_thread_static(field))) {
        patchlib_field_get_value(field, instance, out);
        return true;
    }
    if (!instance && patchlib_field_is_static(field)) {
        void *raw = patchlib_field_get_pointer(field, NULL);
        if (!raw) return false;
        memcpy(out, raw, patchlib_field_get_size(field));
        return true;
    }
#endif

    patchlib_field_get_value(field, instance, out);
    return true;
}

static bool set_field_value(patch_handle_t field, patch_handle_t instance,
                            void *value) {
    if (!field || !value || !patchlib_is_valid(field)) return false;

#if defined(__ANDROID__)
    if (!instance && (patchlib_field_is_const(field) ||
                      patchlib_field_is_thread_static(field))) {
        return false;
    }
    if (!instance && patchlib_field_is_static(field)) {
        void *raw = patchlib_field_get_pointer(field, NULL);
        if (!raw) return false;
        memcpy(raw, value, patchlib_field_get_size(field));
        return true;
    }
#endif

    patchlib_field_set_value(field, instance, value);
    return true;
}

static bool read_i32(patch_handle_t field, patch_handle_t instance, int32_t *out) {
    if (!out || !valid_field(field, PATCH_INT32)) return false;
    return get_field_value(field, instance, out);
}

static bool read_bool(patch_handle_t field, patch_handle_t instance, bool *out) {
    if (!out || !valid_field(field, PATCH_BOOL)) return false;
    return get_field_value(field, instance, out);
}

static bool write_i32(patch_handle_t field, patch_handle_t instance, int32_t value) {
    if (!valid_field(field, PATCH_INT32)) return false;
    return set_field_value(field, instance, &value);
}

static bool write_float(patch_handle_t field, patch_handle_t instance, float value) {
    if (!valid_field(field, PATCH_FLOAT)) return false;
    return set_field_value(field, instance, &value);
}

static bool write_bool(patch_handle_t field, patch_handle_t instance, bool value) {
    if (!valid_field(field, PATCH_BOOL)) return false;
    return set_field_value(field, instance, &value);
}

typedef struct elite_vector2_t {
    float x;
    float y;
} elite_vector2_t;

static bool valid_vector2_field(patch_handle_t field) {
    if (!field || !patchlib_is_valid(field) ||
        patchlib_field_get_size(field) != sizeof(elite_vector2_t)) {
        return false;
    }

    patch_type_t type = patchlib_field_get_type(field);
    return type == PATCH_POINTER || type == PATCH_OBJECT;
}

static bool read_vector2_field(patch_handle_t field, patch_handle_t instance,
                               elite_vector2_t *out) {
    if (!out || !valid_vector2_field(field)) return false;
    return get_field_value(field, instance, out);
}

static bool write_vector2_field(patch_handle_t field, patch_handle_t instance,
                                const elite_vector2_t *value) {
    if (!value || !valid_vector2_field(field)) return false;

#if defined(__ANDROID__)
    /* Vector2 is a value type in the target game. When the runtime exposes
     * the backing address, writing the two floats directly avoids boxing and
     * is the same safe path already used by the existing velocity hook. */
    void *raw = patchlib_field_get_pointer(field, instance);
    if (raw) {
        memcpy(raw, value, sizeof(*value));
        return true;
    }
#endif

    return set_field_value(field, instance, (void *)value);
}

static float float_abs(float value) {
    return value < 0.0f ? -value : value;
}

static float float_sign(float value) {
    return value < 0.0f ? -1.0f : 1.0f;
}

static float vector_distance_sq(elite_vector2_t a, elite_vector2_t b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
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

static void set_elite_state(size_t slot, elite_rank_t rank,
                            elite_behavior_t behavior, int32_t base_damage) {
    g_elite_ranks[slot] = rank;
    g_elite_behaviors[slot] = behavior;
    g_elite_ai_ticks[slot] = 0;
    g_elite_base_damage[slot] = base_damage;
    g_elite_enraged[slot] = false;
}

static void remember_elite_instance(void *instance, elite_rank_t rank,
                                    elite_behavior_t behavior,
                                    int32_t base_damage) {
    if (!instance || is_elite_instance(instance)) return;
    if (g_elite_instance_count < PROCESSED_INSTANCE_LIMIT) {
        size_t slot = g_elite_instance_count;
        g_elite_instances[slot] = instance;
        set_elite_state(slot, rank, behavior, base_damage);
        ++g_elite_instance_count;
    } else {
        size_t slot = g_elite_count % PROCESSED_INSTANCE_LIMIT;
        g_elite_instances[slot] = instance;
        set_elite_state(slot, rank, behavior, base_damage);
    }
}

static elite_rank_t elite_rank_for_instance(void *instance) {
    for (size_t i = 0; i < g_elite_instance_count; ++i) {
        if (g_elite_instances[i] == instance) return g_elite_ranks[i];
    }
    return ELITE_NORMAL;
}

static bool already_rewarded(void *instance) {
    for (size_t i = 0; i < g_rewarded_instance_count; ++i) {
        if (g_rewarded_instances[i] == instance) return true;
    }
    return false;
}

static void remember_rewarded(void *instance) {
    if (!instance || already_rewarded(instance)) return;
    if (g_rewarded_instance_count < PROCESSED_INSTANCE_LIMIT) {
        g_rewarded_instances[g_rewarded_instance_count++] = instance;
    } else {
        size_t slot = g_legendary_reward_count % PROCESSED_INSTANCE_LIMIT;
        g_rewarded_instances[slot] = instance;
    }
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

static bool read_npc_position(patch_handle_t instance, int32_t *x, int32_t *y) {
    if (!instance || !x || !y) {
        return false;
    }

    elite_vector2_t position = {0.0f, 0.0f};
    if (!read_vector2_field(g_field_position, instance, &position)) {
        return false;
    }
    *x = (int32_t)position.x;
    *y = (int32_t)position.y;
    return true;
}

static bool read_player_state(int32_t player_index, elite_vector2_t *position,
                              int32_t *width, int32_t *height) {
    if (player_index < 0 || player_index > 255 || !position ||
        !g_main_player_field || !patchlib_is_valid(g_main_player_field) ||
        !g_player_position_field) {
        return false;
    }

    patch_handle_t players = NULL;
    if (!get_field_value(g_main_player_field, NULL, &players)) return false;
    if (!players || !patchlib_is_valid(players) ||
        (size_t)player_index >= patchlib_array_length(players)) {
        return false;
    }

    patch_handle_t player = NULL;
    if (!patchlib_array_at(players, (size_t)player_index, &player) ||
        !player || !patchlib_is_valid(player)) {
        return false;
    }

    bool active = true;
    bool dead = false;
    if (valid_field(g_player_active_field, PATCH_BOOL) &&
        read_bool(g_player_active_field, player, &active) && !active) {
        return false;
    }
    if (valid_field(g_player_dead_field, PATCH_BOOL) &&
        read_bool(g_player_dead_field, player, &dead) && dead) {
        return false;
    }
    if (!read_vector2_field(g_player_position_field, player, position)) {
        return false;
    }

    if (width) {
        *width = 20;
        (void)read_i32(g_player_width_field, player, width);
        if (*width <= 0) *width = 20;
    }
    if (height) {
        *height = 40;
        (void)read_i32(g_player_height_field, player, height);
        if (*height <= 0) *height = 40;
    }
    return true;
}

static int32_t target_player_index(patch_handle_t instance) {
    int32_t target = -1;
    int32_t local_player = -1;
    int32_t net_mode = 0;
    (void)read_i32(g_field_target, instance, &target);
    (void)read_i32(g_main_my_player_field, NULL, &local_player);
    (void)read_i32(g_main_net_mode_field, NULL, &net_mode);

    /* In single-player, Main.myPlayer is authoritative. On a server, keep
     * the target selected by vanilla AI instead of forcing player zero. */
    if (net_mode == 0 && local_player >= 0 && local_player <= 255) {
        return local_player;
    }
    if (target >= 0 && target <= 255) return target;
    if (local_player >= 0 && local_player <= 255) return local_player;
    return -1;
}

static int legendary_reward_item(elite_progress_t progress) {
    /* Before hardmode, give the original Golden Crate. From hardmode onward,
     * give the original hardmode Golden Crate (Titanium Crate). */
    return progress == PROGRESS_PRE_HARDMODE
               ? ITEM_GOLDEN_CRATE
               : ITEM_GOLDEN_CRATE_HARD;
}

static elite_behavior_t detect_behavior(patch_handle_t instance) {
    int32_t ai_style = -1;
    bool no_gravity = false;
    (void)read_i32(g_field_ai_style, instance, &ai_style);
    (void)read_bool(g_field_no_gravity, instance, &no_gravity);

    switch (ai_style) {
        case 1:  /* Slime */
        case 3:  /* Fighter */
        case 16: /* Piranha */
        case 22: /* Hovering fighter */
        case 23: /* Enchanted sword */
        case 40: /* Spider */
        case 41: /* Herpling */
            return ELITE_BEHAVIOR_MELEE;
        case 26: /* Unicorn / wolf / other charge AI */
        case 39: /* Giant tortoise / shellies */
            return ELITE_BEHAVIOR_CHARGER;
        case 8:  /* Caster */
        case 9:  /* Spell */
        case 17: /* Vulture */
        case 19: /* Antlion */
        case 38: /* Snowman ranged variants */
        case 49: /* Angry Nimbus */
            return ELITE_BEHAVIOR_RANGED;
        case 6:  /* Worm */
            return ELITE_BEHAVIOR_WORM;
        case 13: /* Man Eater / Clinger style */
        case 20: /* Spike ball */
        case 21: /* Blazing wheel */
            return ELITE_BEHAVIOR_SPECIAL;
        case 2:  /* Demon Eye */
        case 5:  /* Flying */
        case 14: /* Bat */
        case 18: /* Jellyfish */
        case 24: /* Bird */
        case 44: /* Flying fish */
            return ELITE_BEHAVIOR_FLYING;
        default:
            /* Unknown no-gravity enemies are safer to treat as flyers. For a
             * normal grounded enemy, the vanilla contact AI is the closest
             * reliable indication that it is melee-oriented. */
            return no_gravity ? ELITE_BEHAVIOR_FLYING
                              : ELITE_BEHAVIOR_MELEE;
    }
}

static bool reward_drop_allowed(void) {
    int32_t net_mode = 0; /* 0=single player, 1=multiplayer client, 2=server */
    if (!read_i32(g_main_net_mode_field, NULL, &net_mode)) return true;
    return net_mode != 1;
}

static bool spawn_vanilla_reward(patch_handle_t instance, int item_type,
                                 int item_stack) {
    if (!instance || item_type <= 0 || item_stack <= 0 ||
        !g_item_new_item_method || !patchlib_is_valid(g_item_new_item_method)) {
        return false;
    }

    int32_t x = 0;
    int32_t y = 0;
    if (!read_npc_position(instance, &x, &y)) return false;

    int32_t width = 0;
    int32_t height = 0;
    (void)read_i32(g_field_width, instance, &width);
    (void)read_i32(g_field_height, instance, &height);
    if (width < 0) width = 0;
    if (height < 0) height = 0;

    int32_t type = (int32_t)item_type;
    int32_t stack = (int32_t)item_stack;
    bool no_broadcast = false;
    int32_t prefix = 0;
    bool no_grab_delay = false;
    int32_t spawned_item = -1;
    void *args[9] = {
        &x, &y, &width, &height, &type,
        &stack, &no_broadcast, &prefix, &no_grab_delay
    };

    if (!patchlib_method_invoke_args(g_item_new_item_method, PATCH_NULL,
                                     &spawned_item, args)) {
        return false;
    }
    return spawned_item >= 0;
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
    int32_t elite_damage = 0;
    if (read_i32(g_field_damage, instance, &damage)) {
        elite_damage = scaled_i32(damage, profile.damage_multiplier);
        changed |= write_i32(g_field_damage, instance, elite_damage);
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
        /* Terraria uses 0.0 as complete knockback immunity. Only legendary
         * elites get this rule; normal and rare elites retain graded
         * resistance so ordinary crowd control remains useful. */
        float adjusted_knockback = profile.rank == ELITE_LEGENDARY
                                       ? 0.0f
                                       : knockback * profile.knockback_multiplier;
        changed |= write_float(g_field_knockback_resist, instance,
                               adjusted_knockback);
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
    remember_elite_instance(instance, profile.rank, detect_behavior(instance),
                            elite_damage);
    if (changed) {
        ++g_elite_count;
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Elite NPC transformed: type=%d progress=%s rank=%d gold=%.1fx affixes=0x%X total=%lu",
                  (int)npc_type, progress_name(progress), (int)profile.rank,
                  (double)profile.gold_multiplier,
                  (unsigned)profile.affix_mask, g_elite_count);
    }
}

/* NPCLoot runs after Terraria has processed the original loot. Legendary
 * elites add one progression-appropriate vanilla crate on top of that loot.
 * The instance guard prevents a repeated NPCLoot call from duplicating it. */
static void npc_loot_postfix(patch_handle_t instance, void **args, void *result,
                             const patch_method_signature_t *sig_info) {
    (void)args;
    (void)result;
    (void)sig_info;
    if (!instance || !is_elite_instance(instance) ||
        elite_rank_for_instance(instance) != ELITE_LEGENDARY ||
        already_rewarded(instance)) {
        return;
    }

    remember_rewarded(instance);
    if (!reward_drop_allowed()) return;

    elite_progress_t progress = current_progress();
    int item_type = legendary_reward_item(progress);
    if (spawn_vanilla_reward(instance, item_type, 1)) {
        ++g_legendary_reward_count;
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Legendary vanilla reward dropped: item=%d progress=%s total=%lu",
                  item_type, progress_name(progress), g_legendary_reward_count);
    } else {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Legendary reward could not be spawned: item=%d progress=%s",
                  item_type, progress_name(progress));
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
    g_field_position = patchlib_type_get_field(npc, "position");
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
    g_field_ai_style = patchlib_type_get_field(npc, "aiStyle");
    g_field_direction = patchlib_type_get_field(npc, "direction");
    g_field_net_update = patchlib_type_get_field(npc, "netUpdate");
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
        g_main_net_mode_field = patchlib_type_get_field(main_type, "netMode");
        g_main_my_player_field = patchlib_type_get_field(main_type, "myPlayer");
        g_main_player_field = patchlib_type_get_field(main_type, "player");
    }

    patch_handle_t player_type = patchlib_type_get_type("Terraria", "Player");
    if (player_type && patchlib_is_valid(player_type)) {
        g_player_position_field = patchlib_type_get_field(player_type, "position");
        g_player_width_field = patchlib_type_get_field(player_type, "width");
        g_player_height_field = patchlib_type_get_field(player_type, "height");
        g_player_active_field = patchlib_type_get_field(player_type, "active");
        g_player_dead_field = patchlib_type_get_field(player_type, "dead");
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

static void discover_reward_api(patch_handle_t npc, patch_handle_t item_type) {
    patch_handle_t loot = patchlib_type_get_method_by_param_count(
        npc, "NPCLoot", 0);
    if (loot && patchlib_is_valid(loot)) {
        patch_method_signature_t loot_sig = {0};
        if (patchlib_method_get_signature(loot, &loot_sig)) {
            bool loot_supported = loot_sig.is_instance &&
                                  loot_sig.return_type == PATCH_VOID &&
                                  tefstd_vector_size(&loot_sig.arg_types) == 0;
            if (loot_supported) {
                patch_hook_id_t hook_id = patchlib_install_prepost_hook(
                    loot, NULL, npc_loot_postfix);
                if (hook_id != PATCH_HOOK_INVALID_ID &&
                    g_loot_hook_count < LOOT_HOOK_LIMIT) {
                    g_loot_hooks[g_loot_hook_count++] = hook_id;
                    ELITE_LOG(MOD_LOG_LEVEL_INFO,
                              "NPC.NPCLoot reward hook installed: id=%d",
                              (int)hook_id);
                } else {
                    ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                              "NPC.NPCLoot reward hook failed");
                }
            } else {
                ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                          "NPC.NPCLoot signature is not supported");
            }
            patchlib_method_signature_free(&loot_sig);
        }
    } else {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC.NPCLoot method not found; legendary crate reward disabled");
    }

    g_item_new_item_method = patchlib_type_get_method_by_param_count(
        item_type, "NewItem", 9);
    if (!g_item_new_item_method || !patchlib_is_valid(g_item_new_item_method)) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Item.NewItem(X,Y,Width,Height,Type,Stack,...) not found; legendary crate reward disabled");
        g_item_new_item_method = NULL;
        return;
    }

    patch_method_signature_t item_sig = {0};
    if (!patchlib_method_get_signature(g_item_new_item_method, &item_sig)) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Item.NewItem signature unavailable; legendary crate reward disabled");
        g_item_new_item_method = NULL;
        return;
    }

    bool item_supported = !item_sig.is_instance &&
                          item_sig.return_type == PATCH_INT32 &&
                          tefstd_vector_size(&item_sig.arg_types) == 9;
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Item.NewItem reward API: found=%d static=%d return=%d params=%d positionField=%d netModeField=%d",
              item_supported ? 1 : 0, item_sig.is_instance ? 0 : 1,
              (int)item_sig.return_type,
              (int)tefstd_vector_size(&item_sig.arg_types),
              valid_field(g_field_position, PATCH_POINTER) ? 1 : 0,
              valid_field(g_main_net_mode_field, PATCH_INT32) ? 1 : 0);
    patchlib_method_signature_free(&item_sig);
    if (!item_supported) g_item_new_item_method = NULL;
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

static void request_npc_net_update(patch_handle_t instance) {
    if (!instance) return;
    (void)write_bool(g_field_net_update, instance, true);
}

static bool multiplayer_client(void) {
    int32_t net_mode = 0;
    (void)read_i32(g_main_net_mode_field, NULL, &net_mode);
    return net_mode == 1;
}

static uint32_t legendary_action_interval(elite_behavior_t behavior,
                                          bool enraged) {
    uint32_t interval = 150u;
    switch (behavior) {
        case ELITE_BEHAVIOR_MELEE:
            interval = 180u;
            break;
        case ELITE_BEHAVIOR_CHARGER:
            interval = 120u;
            break;
        case ELITE_BEHAVIOR_RANGED:
            interval = 150u;
            break;
        case ELITE_BEHAVIOR_FLYING:
            interval = 105u;
            break;
        case ELITE_BEHAVIOR_WORM:
            interval = 135u;
            break;
        case ELITE_BEHAVIOR_SPECIAL:
            interval = 165u;
            break;
    }
    if (enraged && interval > 60u) interval = (interval * 3u) / 4u;
    return interval < 45u ? 45u : interval;
}

static void update_legendary_enrage(patch_handle_t instance, size_t index) {
    if (g_elite_enraged[index]) return;

    int32_t life = 0;
    int32_t life_max = 0;
    if (!read_i32(g_field_life, instance, &life) ||
        !read_i32(g_field_life_max, instance, &life_max) || life <= 0 ||
        life_max <= 0) {
        return;
    }

    if ((int64_t)life * 100 >
        (int64_t)life_max * LEGENDARY_ENRAGE_LIFE_PERCENT) {
        return;
    }

    if (g_elite_base_damage[index] > 0) {
        (void)write_i32(
            g_field_damage, instance,
            scaled_i32(g_elite_base_damage[index],
                       LEGENDARY_ENRAGE_DAMAGE_MULTIPLIER));
    }
    g_elite_enraged[index] = true;
    request_npc_net_update(instance);
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Legendary elite entered enrage: damage=%.2fx life=%d/%d",
              (double)LEGENDARY_ENRAGE_DAMAGE_MULTIPLIER, (int)life,
              (int)life_max);
}

static bool legendary_melee_teleport(patch_handle_t instance,
                                     elite_vector2_t player_position,
                                     int32_t player_width,
                                     int32_t player_height) {
    elite_vector2_t npc_position = {0.0f, 0.0f};
    if (!read_vector2_field(g_field_position, instance, &npc_position)) {
        return false;
    }

    int32_t npc_width = 32;
    int32_t npc_height = 40;
    (void)read_i32(g_field_width, instance, &npc_width);
    (void)read_i32(g_field_height, instance, &npc_height);
    if (npc_width <= 0) npc_width = 32;
    if (npc_height <= 0) npc_height = 40;
    if (player_width <= 0) player_width = 20;
    if (player_height <= 0) player_height = 40;

    elite_vector2_t npc_center = {
        npc_position.x + (float)npc_width * 0.5f,
        npc_position.y + (float)npc_height * 0.5f
    };
    elite_vector2_t player_center = {
        player_position.x + (float)player_width * 0.5f,
        player_position.y + (float)player_height * 0.5f
    };
    if (vector_distance_sq(npc_center, player_center) <
        LEGENDARY_TELEPORT_DISTANCE * LEGENDARY_TELEPORT_DISTANCE) {
        return false;
    }

    /* Appear on the side beyond the player, rather than directly inside the
     * player hitbox. This creates pressure while leaving a short reaction
     * window and avoids an unavoidable contact hit on the teleport frame. */
    float side = npc_center.x <= player_center.x ? 1.0f : -1.0f;
    elite_vector2_t destination = {
        side > 0.0f
            ? player_position.x + (float)player_width + LEGENDARY_TELEPORT_OFFSET
            : player_position.x - (float)npc_width - LEGENDARY_TELEPORT_OFFSET,
        player_center.y - (float)npc_height * 0.5f
    };
    if (!write_vector2_field(g_field_position, instance, &destination)) {
        return false;
    }

    int32_t direction = destination.x + (float)npc_width * 0.5f <
                                player_center.x
                            ? 1
                            : -1;
    (void)write_i32(g_field_direction, instance, direction);
    elite_vector2_t stopped = {0.0f, 0.0f};
    (void)write_vector2_field(g_field_velocity, instance, &stopped);
    request_npc_net_update(instance);
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Legendary melee teleport executed: offset=%.0f",
              (double)LEGENDARY_TELEPORT_OFFSET);
    return true;
}

static void apply_rank_dash(patch_handle_t instance, elite_rank_t rank) {
    if (!valid_vector2_field(g_field_velocity)) return;

    elite_vector2_t velocity = {0.0f, 0.0f};
    if (!read_vector2_field(g_field_velocity, instance, &velocity)) return;

    int32_t direction = 1;
    (void)read_i32(g_field_direction, instance, &direction);
    if (direction == 0) direction = 1;
    velocity.x = direction > 0 ? (rank == ELITE_LEGENDARY ? 8.0f : 6.0f)
                               : (rank == ELITE_LEGENDARY ? -8.0f : -6.0f);

    bool no_gravity = true;
    (void)read_bool(g_field_no_gravity, instance, &no_gravity);
    if (!no_gravity && velocity.y > -6.0f) velocity.y = -6.0f;
    (void)write_vector2_field(g_field_velocity, instance, &velocity);
}

static void apply_legendary_movement(patch_handle_t instance, size_t index,
                                     elite_vector2_t player_position,
                                     int32_t player_width,
                                     int32_t player_height) {
    elite_behavior_t behavior = g_elite_behaviors[index];
    elite_vector2_t npc_position = {0.0f, 0.0f};
    elite_vector2_t velocity = {0.0f, 0.0f};
    if (!read_vector2_field(g_field_position, instance, &npc_position) ||
        !read_vector2_field(g_field_velocity, instance, &velocity)) {
        return;
    }

    int32_t npc_width = 32;
    int32_t npc_height = 40;
    (void)read_i32(g_field_width, instance, &npc_width);
    (void)read_i32(g_field_height, instance, &npc_height);
    if (npc_width <= 0) npc_width = 32;
    if (npc_height <= 0) npc_height = 40;
    if (player_width <= 0) player_width = 20;
    if (player_height <= 0) player_height = 40;

    elite_vector2_t npc_center = {
        npc_position.x + (float)npc_width * 0.5f,
        npc_position.y + (float)npc_height * 0.5f
    };
    elite_vector2_t player_center = {
        player_position.x + (float)player_width * 0.5f,
        player_position.y + (float)player_height * 0.5f
    };
    float dx = player_center.x - npc_center.x;
    float dy = player_center.y - npc_center.y;
    float abs_dx = float_abs(dx);
    float abs_dy = float_abs(dy);
    float distance_sq = vector_distance_sq(npc_center, player_center);
    float diagonal_scale = abs_dx + abs_dy;
    if (diagonal_scale < 1.0f) diagonal_scale = 1.0f;
    float toward_x = dx / diagonal_scale;
    float toward_y = dy / diagonal_scale;
    float phase = ((g_elite_ai_ticks[index] /
                    legendary_action_interval(behavior,
                                              g_elite_enraged[index])) &
                   1u) == 0u
                      ? 1.0f
                      : -1.0f;
    bool no_gravity = false;
    (void)read_bool(g_field_no_gravity, instance, &no_gravity);

    switch (behavior) {
        case ELITE_BEHAVIOR_MELEE:
            if (distance_sq >= LEGENDARY_TELEPORT_DISTANCE *
                                  LEGENDARY_TELEPORT_DISTANCE &&
                legendary_melee_teleport(instance, player_position,
                                         player_width, player_height)) {
                return;
            }
            if (abs_dx > 160.0f) {
                velocity.x = float_sign(dx) *
                             (g_elite_enraged[index] ? 9.0f : 7.0f);
            }
            if (no_gravity && abs_dy > 80.0f) velocity.y = toward_y * 6.0f;
            break;
        case ELITE_BEHAVIOR_CHARGER:
            if (distance_sq >= LEGENDARY_TELEPORT_DISTANCE *
                                  LEGENDARY_TELEPORT_DISTANCE &&
                legendary_melee_teleport(instance, player_position,
                                         player_width, player_height)) {
                return;
            }
            velocity.x = float_sign(dx) *
                         (g_elite_enraged[index] ? 14.0f : 12.0f);
            if (no_gravity) {
                velocity.y = toward_y * 8.0f;
            } else if (dy < -96.0f && velocity.y > -1.0f) {
                velocity.y = -8.0f;
            }
            break;
        case ELITE_BEHAVIOR_RANGED:
            /* Keep the vanilla projectile attack, but force a periodic
             * lateral reposition so a ranged legendary cannot be defeated by
             * standing still directly in front of it. */
            if (abs_dx < 720.0f) {
                velocity.x = -float_sign(dx) * 8.0f + phase * 5.0f;
            } else {
                velocity.x = float_sign(dx) * 7.0f + phase * 3.0f;
            }
            if (no_gravity && abs_dy > 120.0f) velocity.y = toward_y * 6.0f;
            break;
        case ELITE_BEHAVIOR_FLYING:
            /* The perpendicular component makes flyers weave instead of
             * following a predictable straight line. */
            velocity.x = toward_x * 11.0f - toward_y * phase * 8.0f;
            velocity.y = toward_y * 11.0f + toward_x * phase * 8.0f;
            break;
        case ELITE_BEHAVIOR_WORM:
            /* A periodic diagonal burst preserves the worm's original AI and
             * makes its approach dangerous without teleporting underground. */
            velocity.x = toward_x * 13.0f + phase * 3.0f;
            velocity.y = toward_y * 13.0f - phase * 3.0f;
            break;
        case ELITE_BEHAVIOR_SPECIAL:
            velocity.x = -float_sign(dx) * 6.0f + phase * 7.0f;
            if (no_gravity) velocity.y = toward_y * 7.0f;
            break;
    }

    if (write_vector2_field(g_field_velocity, instance, &velocity)) {
        request_npc_net_update(instance);
    }
}

/* AI enhancement layer. The original NPC.AI runs first. Normal and rare
 * elites retain the earlier target-lock/dash behavior. Legendary elites add
 * type-aware movement, a one-time low-health enrage, and true knockback
 * immunity without replacing vanilla attack/projectile logic. */
static void ai_postfix(patch_handle_t instance, void **args, void *result,
                       const patch_method_signature_t *sig_info) {
    (void)args;
    (void)result;
    (void)sig_info;
    if (!instance || !is_elite_instance(instance)) return;

    size_t index = elite_instance_index(instance);
    if (index >= PROCESSED_INSTANCE_LIMIT) return;
    ++g_elite_ai_ticks[index];

    int32_t player = target_player_index(instance);
    if (player >= 0) (void)write_i32(g_field_target, instance, player);

    elite_rank_t rank = g_elite_ranks[index];
    if (rank != ELITE_LEGENDARY) {
        if (rank == ELITE_RARE &&
            g_elite_ai_ticks[index] % 150u == 0u) {
            apply_rank_dash(instance, rank);
        }
        return;
    }

    /* Re-apply this each AI tick in case a vanilla AI branch writes the field
     * back while processing its own movement. */
    (void)write_float(g_field_knockback_resist, instance, 0.0f);
    update_legendary_enrage(instance, index);

    /* Movement is authoritative on the server/single-player side. The
     * client still receives the normal target and stat changes, but does not
     * create a second teleport or velocity update. */
    if (multiplayer_client() || player < 0) return;

    uint32_t interval = legendary_action_interval(
        g_elite_behaviors[index], g_elite_enraged[index]);
    if (g_elite_ai_ticks[index] % interval != 0u) return;

    elite_vector2_t player_position = {0.0f, 0.0f};
    int32_t player_width = 20;
    int32_t player_height = 40;
    if (!read_player_state(player, &player_position, &player_width,
                           &player_height)) {
        return;
    }
    apply_legendary_movement(instance, index, player_position, player_width,
                             player_height);
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

/* The Terraria 1.4.5.6.4 metadata identifies NPC.AI as the parameterless
 * dispatcher. Let the kernel validate its own MethodInfo when installing this
 * known method; older API builds can report its hidden MethodInfo argument
 * incorrectly through the public signature helper. */
static bool install_known_ai_hook(patch_handle_t method, const char *name) {
    if (!method || !patchlib_is_valid(method) ||
        g_ai_hook_count >= AI_HOOK_LIMIT) {
        return false;
    }

    patch_hook_id_t hook_id = patchlib_install_prepost_hook(
        method, NULL, ai_postfix);
    if (hook_id == PATCH_HOOK_INVALID_ID) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Known NPC AI hook failed: name=%s", name);
        return false;
    }

    g_ai_hooks[g_ai_hook_count++] = hook_id;
    g_ai_method_token = patchlib_method_get_token(method);
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Known NPC AI hook installed: name=%s id=%d",
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
    if (!direct) direct = patchlib_type_get_method(npc, "AI");
    if (direct && install_known_ai_hook(direct, "AI")) {
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
        if (npc && patchlib_is_valid(npc)) {
            patch_handle_t item_type = patchlib_type_get_type("Terraria", "Item");
            if (item_type && patchlib_is_valid(item_type)) {
                discover_reward_api(npc, item_type);
            } else {
                ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                          "Terraria.Item type not found; legendary crate reward disabled");
            }
        }
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
    for (size_t i = 0; i < g_loot_hook_count; ++i) {
        patchlib_uninstall_hook(g_loot_hooks[i]);
    }
    g_loot_hook_count = 0;
    g_item_new_item_method = NULL;
    g_processed_instance_count = 0;
    g_elite_instance_count = 0;
    g_rewarded_instance_count = 0;
    g_setdefaults_calls = 0;
    g_elite_count = 0;
    g_legendary_reward_count = 0;
    ELITE_LOG(MOD_LOG_LEVEL_INFO, "Unloaded");
}

static kernel_mod_info_t g_info = {
    .pkg_id = "eternal.future.elitemonsters",
    .version_code = 2026083120,
    .api_version = 1,
    .version = "1.1.0"
};

static kernel_mod_info_t* get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t* create_kernel_mod(void) { return &g_ops; }
