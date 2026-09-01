#include "mod_core.h"
#include "mod_logger.h"
#include "tefkernel/patchlib/type.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/property.h"
#include "tefkernel/patchlib/struct/array.h"
#include "tefkernel/patchlib/struct/string.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* Terraria's native GameModeID values are Normal=0, Expert=1,
 * Master=2, and Creative/Journey=3.  The fourth profile is the mod's
 * custom Legendary profile and is enabled by Main.zenithWorld for the
 * Zenith/fixed-boi special seed world. */
typedef enum elite_world_mode_t {
    ELITE_MODE_NORMAL,
    ELITE_MODE_EXPERT,
    ELITE_MODE_MASTER,
    ELITE_MODE_LEGENDARY,
    ELITE_MODE_COUNT
} elite_world_mode_t;

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

typedef struct elite_mode_modifier_t {
    const char *name;
    float health_multiplier;
    float damage_multiplier;
    int32_t defense_bonus;
    float scale_multiplier;
    float knockback_multiplier;
    float gold_multiplier;
} elite_mode_modifier_t;

/* Base values are selected from world progress and elite rank.  A separate
 * mode modifier below then makes the same elite meaningfully different in
 * Normal, Expert, Master, and Legendary profiles. Defense is additive so
 * low-defense enemies still receive a meaningful increase. */
static const elite_profile_t g_progress_profiles[5][3] = {
    {
        {ELITE_NORMAL, 1.40f, 1.15f, 4, 1.05f, 1.10f, 10.0f, 0},
        {ELITE_RARE, 2.00f, 1.40f, 8, 1.12f, 1.18f, 25.0f, 0},
        {ELITE_LEGENDARY, 3.00f, 1.80f, 12, 1.20f, 1.28f, 50.0f, 0}
    },
    {
        {ELITE_NORMAL, 1.70f, 1.35f, 8, 1.08f, 1.12f, 15.0f, 0},
        {ELITE_RARE, 2.60f, 1.80f, 15, 1.18f, 1.22f, 40.0f, 0},
        {ELITE_LEGENDARY, 4.20f, 2.40f, 24, 1.30f, 1.35f, 80.0f, 0}
    },
    {
        {ELITE_NORMAL, 2.00f, 1.55f, 12, 1.10f, 1.14f, 25.0f, 0},
        {ELITE_RARE, 3.40f, 2.15f, 22, 1.22f, 1.28f, 60.0f, 0},
        {ELITE_LEGENDARY, 5.50f, 3.00f, 36, 1.38f, 1.42f, 130.0f, 0}
    },
    {
        {ELITE_NORMAL, 2.40f, 1.80f, 18, 1.12f, 1.16f, 40.0f, 0},
        {ELITE_RARE, 4.20f, 2.60f, 32, 1.28f, 1.32f, 100.0f, 0},
        {ELITE_LEGENDARY, 7.00f, 3.80f, 52, 1.50f, 1.48f, 220.0f, 0}
    },
    {
        {ELITE_NORMAL, 3.00f, 2.10f, 26, 1.15f, 1.18f, 60.0f, 0},
        {ELITE_RARE, 5.50f, 3.20f, 45, 1.32f, 1.38f, 150.0f, 0},
        {ELITE_LEGENDARY, 9.00f, 4.80f, 75, 1.60f, 1.58f, 320.0f, 0}
    }
};

/* Mode-specific modifiers are intentionally applied on top of Terraria's
 * own difficulty scaling.  The fourth entry is the custom Legendary profile
 * selected only when Main.zenithWorld identifies the Zenith world. */
static const elite_mode_modifier_t g_mode_modifiers[ELITE_MODE_COUNT] = {
    {"普通", 1.00f, 1.00f, 0, 1.00f, 1.00f, 1.00f},
    {"专家", 1.15f, 1.10f, 4, 1.02f, 0.90f, 1.50f},
    {"大师", 1.35f, 1.25f, 8, 1.05f, 0.80f, 2.25f},
    {"传奇", 1.60f, 1.45f, 12, 1.08f, 0.70f, 3.25f}
};

/* 正式配置：按普通、专家、大师、传奇顺序排列，每档递增 10%。 */
static const int g_spawn_chance_percent[ELITE_MODE_COUNT] = {
    20, 30, 40, 50
};
#define SETDEFAULTS_HOOK_LIMIT 8
static patch_hook_id_t g_setdefaults_hooks[SETDEFAULTS_HOOK_LIMIT];
static size_t g_setdefaults_hook_count = 0;
#define NPC_NAME_HOOK_LIMIT 1
static patch_hook_id_t g_npc_name_hooks[NPC_NAME_HOOK_LIMIT];
static size_t g_npc_name_hook_count = 0;
static int g_npc_name_tokens[NPC_NAME_HOOK_LIMIT];
#define ENABLE_NAME_SOURCE_HOOK 1
static unsigned long g_npc_name_calls = 0;
static bool g_pending_name_valid = false;
static char g_pending_source_name[512];
static char g_pending_decorated_name[512];
static patch_handle_t g_main_new_text_method = NULL;
#define MOUSE_TEXT_HOOK_LIMIT 2
static patch_hook_id_t g_mouse_text_hooks[MOUSE_TEXT_HOOK_LIMIT];
static size_t g_mouse_text_hook_count = 0;
#define MOUSE_TEXT_HACK_HOOK_LIMIT 2
static patch_hook_id_t g_mouse_text_hack_hooks[MOUSE_TEXT_HACK_HOOK_LIMIT];
static size_t g_mouse_text_hack_hook_count = 0;
#define AI_HOOK_LIMIT 1
static patch_hook_id_t g_ai_hooks[AI_HOOK_LIMIT];
static size_t g_ai_hook_count = 0;
static int g_ai_method_token = -1;
#define LOOT_HOOK_LIMIT 1
static patch_hook_id_t g_loot_hooks[LOOT_HOOK_LIMIT];
static size_t g_loot_hook_count = 0;
static unsigned long g_setdefaults_calls = 0;
static unsigned long g_elite_count = 0;

/* Terraria keeps a fixed NPC object pool and reuses the same object pointer
 * for many different spawns.  State stored here therefore describes only the
 * NPC's current SetDefaults lifecycle; it must be reset every time
 * SetDefaults completes for that object. */
#define PROCESSED_INSTANCE_LIMIT 1024
static void *g_elite_instances[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_active[PROCESSED_INSTANCE_LIMIT];
static int32_t g_elite_types[PROCESSED_INSTANCE_LIMIT];
static char g_elite_source_names[PROCESSED_INSTANCE_LIMIT][256];
static elite_rank_t g_elite_ranks[PROCESSED_INSTANCE_LIMIT];
static elite_behavior_t g_elite_behaviors[PROCESSED_INSTANCE_LIMIT];
static uint32_t g_elite_ai_ticks[PROCESSED_INSTANCE_LIMIT];
static int32_t g_elite_base_damage[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_enraged[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_rewarded[PROCESSED_INSTANCE_LIMIT];
static uint32_t g_elite_reward_mask[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_spawn_announced[PROCESSED_INSTANCE_LIMIT];
static uint32_t g_elite_affix_masks[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_split_triggered[PROCESSED_INSTANCE_LIMIT];
static size_t g_elite_instance_count = 0;

/* Terraria 1.4.5.6.4 ItemID values from the target game's ItemID table.
 * Every reward below is an original Terraria item or environment crate. */
#define ITEM_LIFE_CRYSTAL 29
#define ITEM_GOLDEN_CRATE 2336
#define ITEM_TITANIUM_CRATE 3981
#define ITEM_MANA_CRYSTAL 109
#define ITEM_FALLEN_STAR 75
#define ITEM_GEL 23
#define ITEM_MAGIC_MIRROR 50
#define ITEM_HERMES_BOOTS 54
#define ITEM_CLOUD_IN_A_BOTTLE 53
#define ITEM_HOOK 118
#define ITEM_COBALT_BAR 381
#define ITEM_MYTHRIL_BAR 382
#define ITEM_ADAMANTITE_BAR 391
#define ITEM_DEMON_WINGS 492
#define ITEM_ANGEL_WINGS 493
#define ITEM_HEALING_POTION 188
#define ITEM_GREATER_HEALING_POTION 499
#define ITEM_GREATER_MANA_POTION 500
#define ITEM_SOUL_OF_LIGHT 520
#define ITEM_SOUL_OF_NIGHT 521
#define ITEM_SOUL_OF_FRIGHT 547
#define ITEM_SOUL_OF_MIGHT 548
#define ITEM_SOUL_OF_SIGHT 549
#define ITEM_CHLOROPHYTE_ORE 947
#define ITEM_CHLOROPHYTE_BAR 1006
#define ITEM_HALLOWED_BAR 1225
#define ITEM_LIFE_FRUIT 1291
#define ITEM_TEMPLE_KEY 1141
#define ITEM_LIHZAHRD_POWER_CELL 1293
#define ITEM_ECTOPLASM 1508
#define ITEM_SHROOMITE_BAR 1552
#define ITEM_BEETLE_HUSK 2218
#define ITEM_SPECTRE_BAR 3261
#define ITEM_LUNAR_BAR 3467
#define ITEM_CELESTIAL_SIGIL 3601

/* Normal and hardmode variants of the original biome/environment crates. */
#define ITEM_CORRUPT_CRATE 3203
#define ITEM_CRIMSON_CRATE 3204
#define ITEM_DUNGEON_CRATE 3205
#define ITEM_SKY_CRATE 3206
#define ITEM_HALLOWED_CRATE 3207
#define ITEM_JUNGLE_CRATE 3208
#define ITEM_CORRUPT_CRATE_HARD 3982
#define ITEM_CRIMSON_CRATE_HARD 3983
#define ITEM_DUNGEON_CRATE_HARD 3984
#define ITEM_SKY_CRATE_HARD 3985
#define ITEM_HALLOWED_CRATE_HARD 3986
#define ITEM_JUNGLE_CRATE_HARD 3987
#define ITEM_FROZEN_CRATE 4405
#define ITEM_FROZEN_CRATE_HARD 4406
#define ITEM_OASIS_CRATE 4407
#define ITEM_OASIS_CRATE_HARD 4408
#define ITEM_LAVA_CRATE 4877
#define ITEM_LAVA_CRATE_HARD 4878
#define ITEM_OCEAN_CRATE 5002
#define ITEM_OCEAN_CRATE_HARD 5003

typedef struct vanilla_reward_entry_t {
    int32_t item_type;
    int32_t min_stack;
    int32_t max_stack;
} vanilla_reward_entry_t;

#define PROGRESS_REWARD_POOL_SIZE 8

/* A rare elite gives one random useful original equipment item. The separate
 * material pools below provide the progression material, so this pool never
 * replaces the requested equipment with a bar or potion. */
static const vanilla_reward_entry_t
    g_progress_reward_pools[5][PROGRESS_REWARD_POOL_SIZE] = {
        {
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HERMES_BOOTS, 1, 1},
            {ITEM_CLOUD_IN_A_BOTTLE, 1, 1},
            {ITEM_HOOK, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HERMES_BOOTS, 1, 1},
            {ITEM_CLOUD_IN_A_BOTTLE, 1, 1},
            {ITEM_HOOK, 1, 1}
        },
        {
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HOOK, 1, 1},
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HOOK, 1, 1}
        },
        {
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HOOK, 1, 1},
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HOOK, 1, 1}
        },
        {
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HOOK, 1, 1},
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HOOK, 1, 1}
        },
        {
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HOOK, 1, 1},
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HOOK, 1, 1}
        }
    };

#define PROGRESS_MATERIAL_POOL_SIZE 4
#define PROGRESS_POTION_POOL_SIZE 2

/* Every elite rank also receives a progression-appropriate material. These
 * are all vanilla items and are deliberately kept separate from the rare
 * utility pool above so normal and legendary rewards cannot roll a mirror or
 * accessory when a material was requested. */
static const vanilla_reward_entry_t
    g_progress_material_pools[5][PROGRESS_MATERIAL_POOL_SIZE] = {
        {
            {ITEM_FALLEN_STAR, 3, 6}, {ITEM_LIFE_CRYSTAL, 1, 1},
            {ITEM_MANA_CRYSTAL, 1, 1}, {ITEM_GEL, 8, 16}
        },
        {
            {ITEM_COBALT_BAR, 2, 4}, {ITEM_MYTHRIL_BAR, 2, 4},
            {ITEM_ADAMANTITE_BAR, 2, 4}, {ITEM_SOUL_OF_NIGHT, 3, 6}
        },
        {
            {ITEM_HALLOWED_BAR, 2, 4}, {ITEM_CHLOROPHYTE_ORE, 8, 16},
            {ITEM_CHLOROPHYTE_BAR, 2, 4}, {ITEM_SOUL_OF_SIGHT, 3, 6}
        },
        {
            {ITEM_ECTOPLASM, 2, 4}, {ITEM_SPECTRE_BAR, 2, 4},
            {ITEM_SHROOMITE_BAR, 2, 4}, {ITEM_LIHZAHRD_POWER_CELL, 1, 2}
        },
        {
            {ITEM_LUNAR_BAR, 2, 5}, {ITEM_BEETLE_HUSK, 2, 4},
            {ITEM_ECTOPLASM, 3, 6}, {ITEM_CHLOROPHYTE_BAR, 4, 8}
        }
    };

static const vanilla_reward_entry_t
    g_progress_potion_pools[5][PROGRESS_POTION_POOL_SIZE] = {
        {{ITEM_HEALING_POTION, 2, 4}, {ITEM_GREATER_HEALING_POTION, 1, 2}},
        {{ITEM_GREATER_HEALING_POTION, 2, 4}, {ITEM_GREATER_MANA_POTION, 2, 4}},
        {{ITEM_GREATER_HEALING_POTION, 3, 6}, {ITEM_GREATER_MANA_POTION, 3, 6}},
        {{ITEM_GREATER_HEALING_POTION, 4, 8}, {ITEM_GREATER_MANA_POTION, 4, 8}},
        {{ITEM_GREATER_HEALING_POTION, 5, 10}, {ITEM_GREATER_MANA_POTION, 5, 10}}
    };

#define PRE_HARDMODE_ENVIRONMENT_CRATE_POOL_SIZE 9
#define HARDMODE_ENVIRONMENT_CRATE_POOL_SIZE 10

static const int32_t g_pre_hardmode_environment_crates[
    PRE_HARDMODE_ENVIRONMENT_CRATE_POOL_SIZE] = {
        ITEM_CORRUPT_CRATE, ITEM_CRIMSON_CRATE, ITEM_DUNGEON_CRATE,
        ITEM_SKY_CRATE, ITEM_JUNGLE_CRATE, ITEM_FROZEN_CRATE,
        ITEM_OASIS_CRATE, ITEM_LAVA_CRATE, ITEM_OCEAN_CRATE
    };

static const int32_t g_hardmode_environment_crates[
    HARDMODE_ENVIRONMENT_CRATE_POOL_SIZE] = {
        ITEM_CORRUPT_CRATE_HARD, ITEM_CRIMSON_CRATE_HARD,
        ITEM_DUNGEON_CRATE_HARD, ITEM_SKY_CRATE_HARD,
        ITEM_HALLOWED_CRATE_HARD, ITEM_JUNGLE_CRATE_HARD,
        ITEM_FROZEN_CRATE_HARD, ITEM_OASIS_CRATE_HARD,
        ITEM_LAVA_CRATE_HARD, ITEM_OCEAN_CRATE_HARD
    };

/* Legendary elites always drop exactly one crate.  Before hardmode the common
 * branch is a Golden Crate; after hardmode it is a Titanium Crate. */
#define LEGENDARY_COMMON_CRATE_CHANCE_PERCENT 70
#define LEGENDARY_ENVIRONMENT_CRATE_CHANCE_PERCENT 30

static patch_handle_t g_main_game_mode_field = NULL;
static patch_handle_t g_main_game_mode_getter = NULL;
static patch_handle_t g_main_zenith_world_field = NULL;
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
static patch_handle_t g_field_color = NULL;
static patch_handle_t g_field_friendly = NULL;
static patch_handle_t g_field_town_npc = NULL;
static patch_handle_t g_field_boss = NULL;
static patch_handle_t g_field_target = NULL;
static patch_handle_t g_field_ai_style = NULL;
static patch_handle_t g_field_direction = NULL;
static patch_handle_t g_field_net_update = NULL;
static patch_handle_t g_field_no_gravity = NULL;
static patch_handle_t g_field_velocity = NULL;
static patch_handle_t g_field_who_am_i = NULL;
static patch_handle_t g_field_immortal = NULL;
static patch_handle_t g_field_dont_take_damage = NULL;
static patch_handle_t g_field_catchable = NULL;
static patch_handle_t g_player_position_field = NULL;
static patch_handle_t g_player_width_field = NULL;
static patch_handle_t g_player_height_field = NULL;
static patch_handle_t g_player_active_field = NULL;
static patch_handle_t g_player_dead_field = NULL;
static patch_handle_t g_player_zone_dungeon_field = NULL;
static patch_handle_t g_player_zone_corrupt_field = NULL;
static patch_handle_t g_player_zone_crimson_field = NULL;
static patch_handle_t g_player_zone_jungle_field = NULL;
static patch_handle_t g_player_zone_snow_field = NULL;
static patch_handle_t g_player_zone_desert_field = NULL;
static patch_handle_t g_player_zone_beach_field = NULL;
static patch_handle_t g_player_zone_underworld_field = NULL;
static patch_handle_t g_player_zone_hallow_field = NULL;
static patch_handle_t g_player_zone_sky_field = NULL;
static bool g_progress_fields_logged = false;

#define LEGENDARY_ENRAGE_LIFE_PERCENT 35
#define LEGENDARY_ENRAGE_DAMAGE_MULTIPLIER 1.25f
#define LEGENDARY_TELEPORT_DISTANCE 480.0f
#define LEGENDARY_TELEPORT_OFFSET 96.0f

/* Reward components are tracked independently so a temporary Item.NewItem
 * failure can retry only the missing part without duplicating earlier drops. */
#define REWARD_COMPONENT_PRIMARY 0x01u
#define REWARD_COMPONENT_SECONDARY 0x02u

typedef struct elite_color_t {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} elite_color_t;

static int random_percent(void) {
    return rand() % 100;
}

static int32_t random_range_i32(int32_t minimum, int32_t maximum) {
    if (maximum <= minimum) return minimum;
    return minimum + (int32_t)(rand() % (maximum - minimum + 1));
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

static bool read_static_i32_method(patch_handle_t method, int32_t *out) {
    if (!method || !out || !patchlib_is_valid(method)) return false;

    patch_method_signature_t sig = {0};
    if (!patchlib_method_get_signature(method, &sig)) return false;
    bool supported = !sig.is_instance && sig.return_type == PATCH_INT32 &&
                     tefstd_vector_size(&sig.arg_types) == 0;
    patchlib_method_signature_free(&sig);
    if (!supported) return false;

    return patchlib_method_invoke_args(method, PATCH_NULL, out, NULL);
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

static bool write_elite_color(patch_handle_t instance, elite_rank_t rank) {
    if (!instance || !g_field_color || !patchlib_is_valid(g_field_color) ||
        patchlib_field_get_size(g_field_color) != sizeof(elite_color_t)) {
        return false;
    }

    elite_color_t color = {255, 255, 255, 255};
    if (rank == ELITE_RARE) color = (elite_color_t){110, 170, 255, 255};
    if (rank == ELITE_LEGENDARY) color = (elite_color_t){225, 120, 255, 255};

#if defined(__ANDROID__)
    void *raw = patchlib_field_get_pointer(g_field_color, instance);
    if (raw) {
        memcpy(raw, &color, sizeof(color));
        return true;
    }
#endif
    return set_field_value(g_field_color, instance, &color);
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

static size_t tracked_instance_index(void *instance) {
    for (size_t i = 0; i < g_elite_instance_count; ++i) {
        if (g_elite_instances[i] == instance) return i;
    }
    return PROCESSED_INSTANCE_LIMIT;
}

static size_t elite_instance_index(void *instance) {
    for (size_t i = 0; i < g_elite_instance_count; ++i) {
        if (g_elite_active[i] && g_elite_instances[i] == instance) return i;
    }
    return PROCESSED_INSTANCE_LIMIT;
}

static bool is_elite_instance(void *instance) {
    return elite_instance_index(instance) < PROCESSED_INSTANCE_LIMIT;
}

static void clear_elite_instance(void *instance) {
    size_t slot = tracked_instance_index(instance);
    if (slot >= PROCESSED_INSTANCE_LIMIT) return;
    g_elite_active[slot] = false;
    g_elite_types[slot] = 0;
    memset(g_elite_source_names[slot], 0, sizeof(g_elite_source_names[slot]));
    g_elite_rewarded[slot] = false;
    g_elite_reward_mask[slot] = 0;
    g_elite_spawn_announced[slot] = false;
    g_elite_ai_ticks[slot] = 0;
    g_elite_base_damage[slot] = 0;
    g_elite_enraged[slot] = false;
    g_elite_affix_masks[slot] = 0;
    g_elite_split_triggered[slot] = false;
}

static void set_elite_state(size_t slot, elite_rank_t rank,
                            elite_behavior_t behavior, int32_t base_damage,
                            uint32_t affix_mask, int32_t npc_type) {
    g_elite_active[slot] = true;
    g_elite_types[slot] = npc_type;
    g_elite_ranks[slot] = rank;
    g_elite_behaviors[slot] = behavior;
    g_elite_ai_ticks[slot] = 0;
    g_elite_base_damage[slot] = base_damage;
    g_elite_enraged[slot] = false;
    g_elite_rewarded[slot] = false;
    g_elite_reward_mask[slot] = 0;
    g_elite_spawn_announced[slot] = false;
    g_elite_affix_masks[slot] = affix_mask;
    g_elite_split_triggered[slot] = false;
}

static void remember_elite_instance(void *instance, elite_rank_t rank,
                                    elite_behavior_t behavior,
                                    int32_t base_damage,
                                    uint32_t affix_mask, int32_t npc_type) {
    if (!instance) return;
    size_t existing_slot = tracked_instance_index(instance);
    if (existing_slot < PROCESSED_INSTANCE_LIMIT) {
        set_elite_state(existing_slot, rank, behavior, base_damage, affix_mask,
                        npc_type);
        return;
    }
    if (g_elite_instance_count < PROCESSED_INSTANCE_LIMIT) {
        size_t slot = g_elite_instance_count;
        g_elite_instances[slot] = instance;
        set_elite_state(slot, rank, behavior, base_damage, affix_mask,
                        npc_type);
        ++g_elite_instance_count;
    } else {
        size_t slot = g_elite_count % PROCESSED_INSTANCE_LIMIT;
        g_elite_instances[slot] = instance;
        set_elite_state(slot, rank, behavior, base_damage, affix_mask,
                        npc_type);
    }
}

static elite_rank_t elite_rank_for_instance(void *instance) {
    size_t slot = elite_instance_index(instance);
    if (slot < PROCESSED_INSTANCE_LIMIT) return g_elite_ranks[slot];
    return ELITE_NORMAL;
}

static uint32_t elite_affixes_for_instance(void *instance) {
    size_t slot = elite_instance_index(instance);
    if (slot < PROCESSED_INSTANCE_LIMIT) return g_elite_affix_masks[slot];
    return 0;
}

static bool elite_has_affix(void *instance, elite_affix_t affix) {
    return (elite_affixes_for_instance(instance) & (1u << affix)) != 0;
}

static bool already_rewarded(void *instance) {
    size_t slot = elite_instance_index(instance);
    return slot < PROCESSED_INSTANCE_LIMIT && g_elite_rewarded[slot];
}

static void remember_rewarded(void *instance) {
    size_t slot = elite_instance_index(instance);
    if (slot < PROCESSED_INSTANCE_LIMIT) g_elite_rewarded[slot] = true;
}

static bool reward_component_done(void *instance, uint32_t component) {
    size_t slot = elite_instance_index(instance);
    return slot < PROCESSED_INSTANCE_LIMIT &&
           (g_elite_reward_mask[slot] & component) != 0;
}

static void remember_reward_component(void *instance, uint32_t component) {
    size_t slot = elite_instance_index(instance);
    if (slot < PROCESSED_INSTANCE_LIMIT) {
        g_elite_reward_mask[slot] |= component;
    }
}

static void complete_reward_if_ready(void *instance) {
    size_t slot = elite_instance_index(instance);
    if (slot >= PROCESSED_INSTANCE_LIMIT) return;
    if ((g_elite_reward_mask[slot] &
         (REWARD_COMPONENT_PRIMARY | REWARD_COMPONENT_SECONDARY)) ==
        (REWARD_COMPONENT_PRIMARY | REWARD_COMPONENT_SECONDARY)) {
        remember_rewarded(instance);
    }
}

static elite_world_mode_t profile_mode_for_game_mode(int game_mode) {
    if (game_mode == 1) return ELITE_MODE_EXPERT;
    if (game_mode == 2) return ELITE_MODE_MASTER;
    /* GameModeID.Creative/Journey (3) has no custom difficulty mapping. */
    return ELITE_MODE_NORMAL;
}

static uint32_t mix_seed(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

static uint32_t next_seed_value(uint32_t *state) {
    *state = mix_seed(*state + 0x9E3779B9u);
    return *state;
}

/* NPC.whoAmI is synchronized by Terraria and is therefore a better profile
 * seed than libc rand(), whose call order can differ between client/server. */
static uint32_t profile_seed(patch_handle_t instance, int32_t npc_type,
                             elite_progress_t progress,
                             elite_world_mode_t mode) {
    int32_t who_am_i = 0;
    (void)read_i32(g_field_who_am_i, instance, &who_am_i);
    uint32_t seed = 0x454C4954u;
    seed ^= (uint32_t)(who_am_i + 1) * 0x45D9F3Bu;
    seed ^= (uint32_t)(npc_type + 1) * 0x27D4EB2Du;
    seed ^= (uint32_t)(progress + 1) * 0x165667B1u;
    seed ^= (uint32_t)(mode + 1) * 0x9E3779B9u;
    return mix_seed(seed);
}

static const char *world_mode_name(elite_world_mode_t mode) {
    if (mode < ELITE_MODE_NORMAL || mode >= ELITE_MODE_COUNT) {
        return g_mode_modifiers[ELITE_MODE_NORMAL].name;
    }
    return g_mode_modifiers[mode].name;
}

static elite_profile_t make_profile(elite_progress_t progress,
                                    elite_world_mode_t mode,
                                    uint32_t seed) {
    if (progress < PROGRESS_PRE_HARDMODE || progress > PROGRESS_ENDGAME) {
        progress = PROGRESS_PRE_HARDMODE;
    }
    if (mode < ELITE_MODE_NORMAL || mode >= ELITE_MODE_COUNT) {
        mode = ELITE_MODE_NORMAL;
    }

    int rank_roll = (int)(next_seed_value(&seed) % 100u);
    size_t rank_index = 0;
    if (rank_roll < 5) {
        rank_index = 2;
    } else if (rank_roll < 30) {
        rank_index = 1;
    }

    elite_profile_t p = g_progress_profiles[progress][rank_index];
    const elite_mode_modifier_t *mode_modifier = &g_mode_modifiers[mode];
    p.health_multiplier *= mode_modifier->health_multiplier;
    p.damage_multiplier *= mode_modifier->damage_multiplier;
    p.defense_bonus += mode_modifier->defense_bonus;
    p.scale_multiplier *= mode_modifier->scale_multiplier;
    p.knockback_multiplier *= mode_modifier->knockback_multiplier;
    p.gold_multiplier *= mode_modifier->gold_multiplier;
    unsigned int affix_count = p.rank == ELITE_LEGENDARY
                                    ? 3u
                                    : (p.rank == ELITE_RARE ? 2u : 1u);
    for (unsigned int i = 0; i < affix_count; ++i) {
        for (unsigned int attempts = 0; attempts < 8u; ++attempts) {
            unsigned int candidate = next_seed_value(&seed) % 6u;
            uint32_t bit = 1u << candidate;
            if ((p.affix_mask & bit) == 0) {
                p.affix_mask |= bit;
                break;
            }
        }
    }

    if (p.affix_mask & (1u << AFFIX_FLAME)) {
        p.damage_multiplier *= 1.10f;
    }
    if (p.affix_mask & (1u << AFFIX_FROST)) {
        p.defense_bonus += 5;
        p.knockback_multiplier *= 0.75f;
    }
    if (p.affix_mask & (1u << AFFIX_VAMPIRIC)) {
        p.health_multiplier *= 1.08f;
    }
    if (p.affix_mask & (1u << AFFIX_SPLIT)) {
        p.health_multiplier *= 1.12f;
    }
    if (p.affix_mask & (1u << AFFIX_ENRAGED)) {
        p.damage_multiplier *= 1.05f;
    }
    if (p.affix_mask & (1u << AFFIX_ABYSSAL)) {
        p.health_multiplier *= 1.10f;
        p.damage_multiplier *= 1.08f;
    }
    return p;
}

/* 生成 Hook 接入后调用此函数；GameModeID: 0=普通,1=专家,2=大师,
 * 3=旅途。天顶世界由 Main.zenithWorld 覆盖为自定义传奇属性档。 */
static int elite_should_spawn(int world_mode, uint32_t seed) {
    if (world_mode < 0) world_mode = 0;
    if (world_mode >= ELITE_MODE_COUNT) world_mode = ELITE_MODE_COUNT - 1;
    return (int)(mix_seed(seed) % 100u) < g_spawn_chance_percent[world_mode];
}

static int current_world_mode(void) {
    int32_t raw_mode = ELITE_MODE_NORMAL;
    if (valid_field(g_main_game_mode_field, PATCH_INT32)) {
        (void)read_i32(g_main_game_mode_field, NULL, &raw_mode);
    } else if (g_main_game_mode_getter) {
        (void)read_static_i32_method(g_main_game_mode_getter, &raw_mode);
    }
    if (raw_mode < 0 || raw_mode > 3) raw_mode = ELITE_MODE_NORMAL;

    elite_world_mode_t mode = profile_mode_for_game_mode((int)raw_mode);
    bool zenith_world = false;
    (void)read_bool(g_main_zenith_world_field, NULL, &zenith_world);
    if (zenith_world) mode = ELITE_MODE_LEGENDARY;
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

static bool get_player_instance(int32_t player_index, patch_handle_t *out_player) {
    if (player_index < 0 || player_index > 255 || !out_player ||
        !g_main_player_field || !patchlib_is_valid(g_main_player_field)) {
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
    *out_player = player;
    return true;
}

static bool read_player_state(int32_t player_index, elite_vector2_t *position,
                              int32_t *width, int32_t *height) {
    if (!position || !g_player_position_field) return false;

    patch_handle_t player = NULL;
    if (!get_player_instance(player_index, &player)) return false;

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

static bool read_player_zone_flag(int32_t player_index, patch_handle_t field) {
    if (!valid_field(field, PATCH_BOOL)) return false;
    patch_handle_t player = NULL;
    bool value = false;
    return get_player_instance(player_index, &player) &&
           read_bool(field, player, &value) && value;
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

static bool select_rare_progress_reward(elite_progress_t progress,
                                        int32_t *item_type,
                                        int32_t *item_stack) {
    if (!item_type || !item_stack) return false;
    if (progress < PROGRESS_PRE_HARDMODE || progress > PROGRESS_ENDGAME) {
        progress = PROGRESS_PRE_HARDMODE;
    }

    const vanilla_reward_entry_t *pool = g_progress_reward_pools[progress];
    size_t entry_index = (size_t)(rand() % PROGRESS_REWARD_POOL_SIZE);
    const vanilla_reward_entry_t *entry = &pool[entry_index];
    *item_type = entry->item_type;
    *item_stack = random_range_i32(entry->min_stack, entry->max_stack);
    return *item_type > 0 && *item_stack > 0;
}

static bool select_progress_pool_reward(
    const vanilla_reward_entry_t pool[][PROGRESS_MATERIAL_POOL_SIZE],
    elite_progress_t progress, int32_t *item_type, int32_t *item_stack) {
    if (!item_type || !item_stack) return false;
    if (progress < PROGRESS_PRE_HARDMODE || progress > PROGRESS_ENDGAME) {
        progress = PROGRESS_PRE_HARDMODE;
    }
    const vanilla_reward_entry_t *entry =
        &pool[progress][(size_t)(rand() % PROGRESS_MATERIAL_POOL_SIZE)];
    *item_type = entry->item_type;
    *item_stack = random_range_i32(entry->min_stack, entry->max_stack);
    return *item_type > 0 && *item_stack > 0;
}

static bool select_progress_material_reward(elite_progress_t progress,
                                             int32_t *item_type,
                                             int32_t *item_stack) {
    return select_progress_pool_reward(g_progress_material_pools, progress,
                                        item_type, item_stack);
}

static bool select_progress_potion_reward(elite_progress_t progress,
                                          int32_t *item_type,
                                          int32_t *item_stack) {
    if (!item_type || !item_stack) return false;
    if (progress < PROGRESS_PRE_HARDMODE || progress > PROGRESS_ENDGAME) {
        progress = PROGRESS_PRE_HARDMODE;
    }
    const vanilla_reward_entry_t *entry =
        &g_progress_potion_pools[progress][(size_t)(rand() % PROGRESS_POTION_POOL_SIZE)];
    *item_type = entry->item_type;
    *item_stack = random_range_i32(entry->min_stack, entry->max_stack);
    return *item_type > 0 && *item_stack > 0;
}

static int32_t legendary_environment_crate(elite_progress_t progress) {
    if (progress == PROGRESS_PRE_HARDMODE) {
        size_t index = (size_t)(rand() %
                                PRE_HARDMODE_ENVIRONMENT_CRATE_POOL_SIZE);
        return g_pre_hardmode_environment_crates[index];
    }

    size_t index = (size_t)(rand() % HARDMODE_ENVIRONMENT_CRATE_POOL_SIZE);
    return g_hardmode_environment_crates[index];
}

static int32_t current_environment_crate(elite_progress_t progress,
                                          int32_t player_index) {
    bool hardmode = progress != PROGRESS_PRE_HARDMODE;
    if (read_player_zone_flag(player_index, g_player_zone_dungeon_field)) {
        return hardmode ? ITEM_DUNGEON_CRATE_HARD : ITEM_DUNGEON_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_corrupt_field)) {
        return hardmode ? ITEM_CORRUPT_CRATE_HARD : ITEM_CORRUPT_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_crimson_field)) {
        return hardmode ? ITEM_CRIMSON_CRATE_HARD : ITEM_CRIMSON_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_jungle_field)) {
        return hardmode ? ITEM_JUNGLE_CRATE_HARD : ITEM_JUNGLE_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_snow_field)) {
        return hardmode ? ITEM_FROZEN_CRATE_HARD : ITEM_FROZEN_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_desert_field)) {
        return hardmode ? ITEM_OASIS_CRATE_HARD : ITEM_OASIS_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_beach_field)) {
        return hardmode ? ITEM_OCEAN_CRATE_HARD : ITEM_OCEAN_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_underworld_field)) {
        return hardmode ? ITEM_LAVA_CRATE_HARD : ITEM_LAVA_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_hallow_field)) {
        return hardmode ? ITEM_HALLOWED_CRATE_HARD : ITEM_HALLOWED_CRATE;
    }
    if (read_player_zone_flag(player_index, g_player_zone_sky_field)) {
        return hardmode ? ITEM_SKY_CRATE_HARD : ITEM_SKY_CRATE;
    }
    /* If the target is unavailable during NPCLoot, retain a vanilla biome
     * fallback rather than failing the guaranteed one-crate reward. */
    return legendary_environment_crate(progress);
}

static int32_t legendary_common_crate(elite_progress_t progress) {
    return progress == PROGRESS_PRE_HARDMODE ? ITEM_GOLDEN_CRATE
                                             : ITEM_TITANIUM_CRATE;
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
    if (!instance) return;

    /* SetDefaults resets the object to a new vanilla NPC, even when Terraria
     * reuses exactly the same object pointer.  Invalidate the previous spawn's
     * name/AI/reward state before evaluating the new spawn. */
    clear_elite_instance(instance);

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
    if (read_bool(g_field_immortal, instance, &value) && value) return;
    if (read_bool(g_field_dont_take_damage, instance, &value) && value) return;
    if (read_bool(g_field_catchable, instance, &value) && value) return;

    int profile_mode_value = current_world_mode();
    if (!elite_should_spawn(
            profile_mode_value,
            profile_seed(instance, npc_type, PROGRESS_PRE_HARDMODE,
                         (elite_world_mode_t)profile_mode_value))) {
        return;
    }

    elite_progress_t progress = current_progress();
    elite_world_mode_t profile_mode = (elite_world_mode_t)profile_mode_value;
    elite_profile_t profile = make_profile(
        progress, profile_mode, profile_seed(instance, npc_type, progress,
                                              profile_mode));
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
                            elite_damage, profile.affix_mask, npc_type);

    if (changed) {
        ++g_elite_count;
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Elite NPC transformed: type=%d mode=%s progress=%s rank=%d gold=%.1fx affixes=0x%X total=%lu",
                  (int)npc_type, world_mode_name(profile_mode),
                  progress_name(progress), (int)profile.rank,
                  (double)profile.gold_multiplier,
                  (unsigned)profile.affix_mask, g_elite_count);
    }
}

/* NPCLoot runs after Terraria has processed the original loot. Normal elites
 * add a small potion bundle and a progression material; rare elites add a
 * progression material plus one useful vanilla item; legendary elites add a
 * 70% common/30% environment crate plus a progression material. NPC.value was
 * already scaled at SetDefaults, so all three ranks also drop large amounts
 * of vanilla coins. Each component is tracked independently for safe retry. */
static void npc_loot_postfix(patch_handle_t instance, void **args, void *result,
                             const patch_method_signature_t *sig_info) {
    (void)args;
    (void)result;
    (void)sig_info;
    if (!instance || !is_elite_instance(instance) || already_rewarded(instance)) {
        return;
    }

    elite_rank_t rank = elite_rank_for_instance(instance);

    if (!reward_drop_allowed()) {
        /* Clients never create the item, but still remember the completed
         * loot callback so a repeated client-side call stays harmless. */
        remember_rewarded(instance);
        return;
    }

    elite_progress_t progress = current_progress();
    if (rank == ELITE_NORMAL) {
        if (!reward_component_done(instance, REWARD_COMPONENT_PRIMARY)) {
            int32_t item_type = 0;
            int32_t item_stack = 0;
            if (select_progress_potion_reward(progress, &item_type,
                                              &item_stack) &&
                spawn_vanilla_reward(instance, item_type, item_stack)) {
                remember_reward_component(instance, REWARD_COMPONENT_PRIMARY);
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "Normal elite potion reward dropped: item=%d stack=%d progress=%s",
                          (int)item_type, (int)item_stack,
                          progress_name(progress));
            }
        }
        if (!reward_component_done(instance, REWARD_COMPONENT_SECONDARY)) {
            int32_t item_type = 0;
            int32_t item_stack = 0;
            if (select_progress_material_reward(progress, &item_type,
                                                &item_stack) &&
                spawn_vanilla_reward(instance, item_type, item_stack)) {
                remember_reward_component(instance, REWARD_COMPONENT_SECONDARY);
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "Normal elite material reward dropped: item=%d stack=%d progress=%s",
                          (int)item_type, (int)item_stack,
                          progress_name(progress));
            }
        }
        complete_reward_if_ready(instance);
        return;
    }

    if (rank == ELITE_RARE) {
        if (!reward_component_done(instance, REWARD_COMPONENT_PRIMARY)) {
            int32_t item_type = 0;
            int32_t item_stack = 0;
            if (select_progress_material_reward(progress, &item_type,
                                                &item_stack) &&
                spawn_vanilla_reward(instance, item_type, item_stack)) {
                remember_reward_component(instance, REWARD_COMPONENT_PRIMARY);
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "Rare elite material reward dropped: item=%d stack=%d progress=%s",
                          (int)item_type, (int)item_stack,
                          progress_name(progress));
            }
        }
        if (!reward_component_done(instance, REWARD_COMPONENT_SECONDARY)) {
            int32_t item_type = 0;
            int32_t item_stack = 0;
            if (select_rare_progress_reward(progress, &item_type,
                                             &item_stack) &&
                spawn_vanilla_reward(instance, item_type, item_stack)) {
                remember_reward_component(instance, REWARD_COMPONENT_SECONDARY);
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "Rare elite utility reward dropped: item=%d stack=%d progress=%s",
                          (int)item_type, (int)item_stack,
                          progress_name(progress));
            }
        }
        complete_reward_if_ready(instance);
        return;
    }

    if (rank == ELITE_LEGENDARY) {
        if (!reward_component_done(instance, REWARD_COMPONENT_PRIMARY)) {
            int32_t item_type = 0;
            const char *crate_kind = NULL;
            int roll = random_percent();
            if (roll < LEGENDARY_COMMON_CRATE_CHANCE_PERCENT) {
                item_type = legendary_common_crate(progress);
                crate_kind = progress == PROGRESS_PRE_HARDMODE ? "golden" : "titanium";
            } else {
                item_type = current_environment_crate(
                    progress, target_player_index(instance));
                crate_kind = "environment";
            }
            if (spawn_vanilla_reward(instance, item_type, 1)) {
                remember_reward_component(instance, REWARD_COMPONENT_PRIMARY);
                ELITE_LOG(
                    MOD_LOG_LEVEL_INFO,
                    "Legendary crate dropped: kind=%s item=%d distribution=%d%%/%d%% progress=%s",
                    crate_kind, (int)item_type,
                    LEGENDARY_COMMON_CRATE_CHANCE_PERCENT,
                    LEGENDARY_ENVIRONMENT_CRATE_CHANCE_PERCENT,
                    progress_name(progress));
            }
        }

        if (!reward_component_done(instance, REWARD_COMPONENT_SECONDARY)) {
            int32_t item_type = 0;
            int32_t item_stack = 0;
            if (select_progress_material_reward(progress, &item_type,
                                                &item_stack) &&
                spawn_vanilla_reward(instance, item_type, item_stack)) {
                remember_reward_component(instance, REWARD_COMPONENT_SECONDARY);
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "Legendary elite material reward dropped: item=%d stack=%d progress=%s",
                          (int)item_type, (int)item_stack,
                          progress_name(progress));
            }
        }
        complete_reward_if_ready(instance);
    }
}

static size_t append_affix_labels(char *buffer, size_t capacity,
                                  size_t offset, uint32_t affix_mask) {
    static const char *labels[6] = {
        "烈焰", "寒霜", "吸血", "分裂", "狂暴", "深渊"
    };
    for (size_t i = 0; i < 6; ++i) {
        if ((affix_mask & (1u << i)) == 0 || offset >= capacity) continue;
        int written = snprintf(buffer + offset, capacity - offset, "%s·",
                               labels[i]);
        if (written < 0) return offset;
        offset += (size_t)written;
    }
    return offset;
}

static void stage_pending_name(const char *source, const char *decorated) {
    if (!source || !decorated) return;
    (void)snprintf(g_pending_source_name, sizeof(g_pending_source_name),
                   "%s", source);
    (void)snprintf(g_pending_decorated_name, sizeof(g_pending_decorated_name),
                   "%s", decorated);
    g_pending_name_valid = true;
}

static bool elite_profile_for_type(int32_t npc_type, elite_rank_t *rank,
                                   uint32_t *affix_mask) {
    bool found = false;
    elite_rank_t best_rank = ELITE_NORMAL;
    uint32_t best_affixes = 0;
    for (size_t i = 0; i < g_elite_instance_count; ++i) {
        if (!g_elite_active[i] || g_elite_types[i] != npc_type) continue;
        if (!found || g_elite_ranks[i] > best_rank) {
            best_rank = g_elite_ranks[i];
            best_affixes = g_elite_affix_masks[i];
            found = true;
        }
    }
    if (!found) return false;
    if (rank) *rank = best_rank;
    if (affix_mask) *affix_mask = best_affixes;
    return true;
}

static void cache_elite_source_name(int32_t npc_type, const char *name) {
    if (!name || !name[0]) return;
    for (size_t i = 0; i < g_elite_instance_count; ++i) {
        if (!g_elite_active[i] || g_elite_types[i] != npc_type) continue;
        (void)snprintf(g_elite_source_names[i],
                       sizeof(g_elite_source_names[i]), "%s", name);
    }
}

static bool compose_elite_name(char *buffer, size_t capacity,
                               const char *name, elite_rank_t rank,
                               uint32_t affix_mask) {
    if (!buffer || capacity == 0 || !name) return false;
    const char *prefix = "精英·";
    if (rank == ELITE_RARE) prefix = "稀有·";
    if (rank == ELITE_LEGENDARY) prefix = "传奇·";
    int written = snprintf(buffer, capacity, "%s", prefix);
    if (written < 0 || (size_t)written >= capacity) return false;
    size_t offset = (size_t)written;
    offset = append_affix_labels(buffer, capacity, offset, affix_mask);
    if (offset >= capacity) return false;
    written = snprintf(buffer + offset, capacity - offset, "%s", name);
    return written >= 0 && (size_t)written < capacity - offset;
}

/* Add a visible marker at the NPC name source. The MouseText hook below also
 * applies a rank-specific vanilla rarity directly, so this works even when the Android build
 * does not parse [c/...] tags in the NPC hover renderer. */
static void npc_name_postfix(patch_handle_t instance, void **args, void *result,
                             const patch_method_signature_t *sig_info) {
    (void)args;
    (void)sig_info;
    if (!instance || !result || !is_elite_instance(instance)) return;

    ++g_npc_name_calls;

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

    int32_t npc_type = 0;
    if (read_i32(g_field_type, instance, &npc_type)) {
        cache_elite_source_name(npc_type, name);
    }

    char decorated[512];
    /* Do not put [c/...] into the NPC name: this Android build displays the
 * tag literally. Main.MouseText applies the rank color separately. */
    const char *prefix = "精英·";
    elite_rank_t rank = elite_rank_for_instance(instance);
    if (rank == ELITE_RARE) prefix = "稀有·";
    if (rank == ELITE_LEGENDARY) prefix = "传奇·";
    size_t offset = (size_t)snprintf(decorated, sizeof(decorated), "%s", prefix);
    offset = append_affix_labels(decorated, sizeof(decorated), offset,
                                 elite_affixes_for_instance(instance));
    if (offset < sizeof(decorated)) {
        (void)snprintf(decorated + offset, sizeof(decorated) - offset, "%s",
                       name);
    }

    /* Keep a short-lived copy for Main.MouseText. On some Android IL2CPP
     * builds the postfix result slot is read-only after the native bridge
     * returns, even though the callback itself ran successfully. MouseText
     * is the final UI boundary, so it can still receive the decorated name
     * without installing a second NPC name hook. */
    stage_pending_name(name, decorated);

    patch_handle_t replacement = patchlib_string_create(decorated);
    if (replacement && patchlib_is_valid(replacement)) {
        *(patch_handle_t *)result = replacement;
    }
    free(name);
}

/* Main NPC hover text is normally built through Lang.GetNPCNameValue(int),
 * not through NPC.FullName. This callback has no NPC instance, so it uses the
 * active elite type table populated by SetDefaults and selects the highest
 * active rank for that type. It is intentionally the only name hook used by
 * the discovery routine when this API is present. */
static void npc_language_name_postfix(patch_handle_t instance, void **args,
                                      void *result,
                                      const patch_method_signature_t *sig_info) {
    (void)instance;
    (void)sig_info;
    if (!args || !args[0] || !result) return;

    int32_t npc_type = *(int32_t *)args[0];
    elite_rank_t rank = ELITE_NORMAL;
    uint32_t affix_mask = 0;
    if (!elite_profile_for_type(npc_type, &rank, &affix_mask)) return;

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

    cache_elite_source_name(npc_type, name);

    const char *prefix = "精英·";
    if (rank == ELITE_RARE) prefix = "稀有·";
    if (rank == ELITE_LEGENDARY) prefix = "传奇·";
    char decorated[512];
    size_t offset = (size_t)snprintf(decorated, sizeof(decorated), "%s",
                                     prefix);
    offset = append_affix_labels(decorated, sizeof(decorated), offset,
                                 affix_mask);
    if (offset < sizeof(decorated)) {
        (void)snprintf(decorated + offset, sizeof(decorated) - offset, "%s",
                       name);
    }

    ++g_npc_name_calls;
    stage_pending_name(name, decorated);
    patch_handle_t replacement = patchlib_string_create(decorated);
    if (replacement && patchlib_is_valid(replacement)) {
        *(patch_handle_t *)result = replacement;
    }
    free(name);
}

static void setdefaults_postfix(patch_handle_t instance, void **args, void *result,
                                const patch_method_signature_t *sig_info) {
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
    g_field_color = patchlib_type_get_field(npc, "color");
    g_field_friendly = patchlib_type_get_field(npc, "friendly");
    g_field_town_npc = patchlib_type_get_field(npc, "townNPC");
    g_field_boss = patchlib_type_get_field(npc, "boss");
    g_field_target = patchlib_type_get_field(npc, "target");
    g_field_ai_style = patchlib_type_get_field(npc, "aiStyle");
    g_field_direction = patchlib_type_get_field(npc, "direction");
    g_field_net_update = patchlib_type_get_field(npc, "netUpdate");
    g_field_no_gravity = patchlib_type_get_field(npc, "noGravity");
    g_field_velocity = patchlib_type_get_field(npc, "velocity");
    g_field_who_am_i = patchlib_type_get_field(npc, "whoAmI");
    g_field_immortal = patchlib_type_get_field(npc, "immortal");
    g_field_dont_take_damage = patchlib_type_get_field(npc, "dontTakeDamage");
    g_field_catchable = patchlib_type_get_field(npc, "catchable");

    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (main_type && patchlib_is_valid(main_type)) {
        g_main_game_mode_field = patchlib_type_get_field(main_type, "GameMode");
        if (!g_main_game_mode_field || !patchlib_is_valid(g_main_game_mode_field)) {
            g_main_game_mode_field = patchlib_type_get_field(main_type, "gameMode");
        }
        if (!g_main_game_mode_field || !patchlib_is_valid(g_main_game_mode_field)) {
            patch_handle_t game_mode_property = patchlib_type_get_property(
                main_type, "GameMode");
            if (game_mode_property && patchlib_is_valid(game_mode_property)) {
                g_main_game_mode_getter =
                    patchlib_property_get_get_method(game_mode_property);
                if (!g_main_game_mode_getter ||
                    !patchlib_is_valid(g_main_game_mode_getter)) {
                    g_main_game_mode_getter = NULL;
                }
            }
            if (!g_main_game_mode_getter) {
                g_main_game_mode_getter = patchlib_type_get_method(
                    main_type, "get_GameMode");
                if (!g_main_game_mode_getter ||
                    !patchlib_is_valid(g_main_game_mode_getter)) {
                    g_main_game_mode_getter = NULL;
                }
            }
        }
        /* Main.zenithWorld is the vanilla flag for the Zenith/fixed-boi
         * world.  It is the only trigger for Legendary. */
        g_main_zenith_world_field = patchlib_type_get_field(
            main_type, "zenithWorld");
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
        g_player_zone_dungeon_field = patchlib_type_get_field(player_type, "ZoneDungeon");
        g_player_zone_corrupt_field = patchlib_type_get_field(player_type, "ZoneCorrupt");
        g_player_zone_crimson_field = patchlib_type_get_field(player_type, "ZoneCrimson");
        g_player_zone_jungle_field = patchlib_type_get_field(player_type, "ZoneJungle");
        g_player_zone_snow_field = patchlib_type_get_field(player_type, "ZoneSnow");
        g_player_zone_desert_field = patchlib_type_get_field(player_type, "ZoneDesert");
        g_player_zone_beach_field = patchlib_type_get_field(player_type, "ZoneBeach");
        g_player_zone_underworld_field = patchlib_type_get_field(player_type, "ZoneUnderworldHeight");
        g_player_zone_hallow_field = patchlib_type_get_field(player_type, "ZoneHallow");
        g_player_zone_sky_field = patchlib_type_get_field(player_type, "ZoneSkyHeight");
    }

    g_npc_downed_mech_field = patchlib_type_get_field(npc, "downedMechBossAny");
    g_npc_downed_plant_field = patchlib_type_get_field(npc, "downedPlantBoss");
    g_npc_downed_golem_field = patchlib_type_get_field(npc, "downedGolemBoss");
    g_npc_downed_moonlord_field = patchlib_type_get_field(npc, "downedMoonlord");

    if (!g_progress_fields_logged) {
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Progress fields: gameModeField=%d gameModeGetter=%d zenithWorld=%d hardMode=%d mech=%d plant=%d golem=%d moonlord=%d",
                  g_main_game_mode_field != NULL,
                  g_main_game_mode_getter != NULL,
                  g_main_zenith_world_field != NULL,
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
                      "NPC.NPCLoot method not found; rare/environment rewards disabled");
    }

    g_item_new_item_method = patchlib_type_get_method_by_param_count(
        item_type, "NewItem", 9);
    if (!g_item_new_item_method || !patchlib_is_valid(g_item_new_item_method)) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Item.NewItem(X,Y,Width,Height,Type,Stack,...) not found; rare/environment rewards disabled");
        g_item_new_item_method = NULL;
        return;
    }

    patch_method_signature_t item_sig = {0};
    if (!patchlib_method_get_signature(g_item_new_item_method, &item_sig)) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Item.NewItem signature unavailable; rare/environment rewards disabled");
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

static bool name_method_already_hooked(patch_handle_t method) {
    int token = patchlib_method_get_token(method);
    if (token < 0) return false;
    for (size_t i = 0; i < g_npc_name_hook_count; ++i) {
        if (g_npc_name_tokens[i] == token) return true;
    }
    return false;
}

static bool install_name_hook(patch_handle_t method, const char *name) {
    if (!method || !patchlib_is_valid(method) ||
        g_npc_name_hook_count >= NPC_NAME_HOOK_LIMIT ||
        name_method_already_hooked(method)) {
        return false;
    }

    patch_method_signature_t sig = {0};
    if (!patchlib_method_get_signature(method, &sig)) return false;
    /* System.String is reported as PATCH_OBJECT by some TEFKernel builds and
     * as PATCH_POINTER by older Android metadata adapters. Both values are
     * object handles at the callback boundary and can be passed to the string
     * helper. */
    bool supported = sig.is_instance &&
                     (sig.return_type == PATCH_OBJECT ||
                      sig.return_type == PATCH_POINTER) &&
                     tefstd_vector_size(&sig.arg_types) == 0;
    if (!supported) {
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "NPC name candidate skipped: name=%s instance=%d return=%d params=%d",
                  name ? name : "?", sig.is_instance ? 1 : 0,
                  (int)sig.return_type,
                  (int)tefstd_vector_size(&sig.arg_types));
    }
    patchlib_method_signature_free(&sig);
    if (!supported) return false;

    patch_hook_id_t hook_id = patchlib_install_prepost_hook(
        method, NULL, npc_name_postfix);
    if (hook_id == PATCH_HOOK_INVALID_ID) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC name getter hook failed: name=%s", name ? name : "?");
        return false;
    }

    size_t slot = g_npc_name_hook_count++;
    g_npc_name_hooks[slot] = hook_id;
    g_npc_name_tokens[slot] = patchlib_method_get_token(method);
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "NPC name getter hook installed: %s id=%d",
              name ? name : "?", (int)hook_id);
    return true;
}

static bool install_name_property_hook(patch_handle_t npc,
                                       const char *property_name) {
    if (!npc || !patchlib_is_valid(npc) || !property_name ||
        g_npc_name_hook_count >= NPC_NAME_HOOK_LIMIT) {
        return false;
    }

    patch_handle_t property = patchlib_type_get_property(npc, property_name);
    if (!property || !patchlib_is_valid(property)) return false;

    patch_handle_t getter = patchlib_property_get_get_method(property);
    if (!getter || !patchlib_is_valid(getter)) return false;

    /* The property API is more reliable on Android than looking up the
     * compiler-generated get_* export. install_name_hook still validates the
     * signature and enforces the one-hook safety limit. */
    return install_name_hook(getter, property_name);
}

static bool install_language_name_hook(void) {
    if (g_npc_name_hook_count >= NPC_NAME_HOOK_LIMIT) return false;

    patch_handle_t lang = patchlib_type_get_type("Terraria", "Lang");
    if (!lang || !patchlib_is_valid(lang)) return false;
    patch_handle_t method = patchlib_type_get_method_by_param_count(
        lang, "GetNPCNameValue", 1);
    if (!method || !patchlib_is_valid(method)) return false;

    patch_method_signature_t sig = {0};
    if (!patchlib_method_get_signature(method, &sig)) return false;
    bool supported = !sig.is_instance &&
                     (sig.return_type == PATCH_OBJECT ||
                      sig.return_type == PATCH_POINTER) &&
                     tefstd_vector_size(&sig.arg_types) == 1;
    patchlib_method_signature_free(&sig);
    if (!supported) return false;

    patch_hook_id_t hook_id = patchlib_install_prepost_hook(
        method, NULL, npc_language_name_postfix);
    if (hook_id == PATCH_HOOK_INVALID_ID) return false;

    g_npc_name_hooks[0] = hook_id;
    g_npc_name_tokens[0] = patchlib_method_get_token(method);
    g_npc_name_hook_count = 1;
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "NPC language name hook installed: GetNPCNameValue id=%d",
              (int)hook_id);
    return true;
}

static void discover_name_api(patch_handle_t npc) {
    /* Install exactly one name source hook. The previous build installed every
     * Name-like method returned by metadata enumeration; that worked on some
     * desktop layouts but caused SIGILL during the first Android UI frame. */
    if (install_language_name_hook()) return;

    const char *name_properties[] = {
        "FullName", "GivenOrTypeName", "TypeName", "DisplayName",
        "HoverName", "Name"
    };
    const size_t property_count =
        sizeof(name_properties) / sizeof(name_properties[0]);
    for (size_t i = 0; i < property_count; ++i) {
        if (install_name_property_hook(npc, name_properties[i])) return;
    }

    const char *name_getters[] = {
        "get_FullName", "get_GivenOrTypeName", "get_TypeName",
        "get_DisplayName", "get_HoverName", "get_Name"
    };
    const size_t known_count = sizeof(name_getters) / sizeof(name_getters[0]);
    for (size_t i = 0; i < known_count; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            npc, name_getters[i], 0);
        if (install_name_hook(method, name_getters[i])) return;
    }

    /* A few metadata adapters expose the property getter under the property
     * name itself instead of get_PropertyName. Keep this fallback single-shot
     * so it remains safe on the Android ARM64 bridge. */
    for (size_t i = 0; i < property_count; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            npc, name_properties[i], 0);
        if (install_name_hook(method, name_properties[i])) return;
    }

    /* If names were renamed by the IL2CPP export, scan metadata but still
     * choose only one supported candidate, preferring FullName-like methods. */
    tefstd_vector_t methods = {0};
    if (!tefstd_vector_init(&methods, sizeof(patch_handle_t))) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC name method vector initialization failed");
        return;
    }
    if (patchlib_type_get_methods(npc, true, &methods)) {
        patch_handle_t selected_method = NULL;
        const char *selected_name = NULL;
        int selected_score = INT_MAX;
        size_t method_count = tefstd_vector_size(&methods);
        for (size_t i = 0; i < method_count; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
            patch_handle_t method = entry ? *entry : NULL;
            if (!method || !patchlib_is_valid(method)) continue;
            const char *name = patchlib_method_get_name(method);
            if (!name || (!strstr(name, "Name") && !strstr(name, "name"))) {
                continue;
            }
            patch_method_signature_t sig = {0};
            if (!patchlib_method_get_signature(method, &sig)) continue;
            bool supported = sig.is_instance &&
                             (sig.return_type == PATCH_OBJECT ||
                              sig.return_type == PATCH_POINTER) &&
                             tefstd_vector_size(&sig.arg_types) == 0;
            patchlib_method_signature_free(&sig);
            if (!supported) continue;

            int score = 100;
            if (strstr(name, "FullName") != NULL) score = 0;
            else if (strstr(name, "GivenOrTypeName") != NULL) score = 10;
            else if (strstr(name, "TypeName") != NULL) score = 20;
            if (score < selected_score) {
                selected_method = method;
                selected_name = name;
                selected_score = score;
            }
        }
        if (selected_method) {
            (void)install_name_hook(selected_method, selected_name);
        }
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "NPC name method discovery complete: methods=%d hooks=%d",
                  (int)method_count, (int)g_npc_name_hook_count);
    } else {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC name method enumeration failed");
    }
    tefstd_vector_destroy(&methods);
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
        return true;
    }

    patch_handle_t text_handle = *(patch_handle_t *)args[0];
    if (!text_handle || !patchlib_is_valid(text_handle)) return true;
    char *text = patchlib_string_cstr(text_handle);
    if (!text) return true;

    const char *effective_text = text;
    char direct_decorated_name[512];
    bool injected_name = false;
    if (g_pending_name_valid) {
        if (strcmp(text, g_pending_source_name) == 0) {
            patch_handle_t replacement =
                patchlib_string_create(g_pending_decorated_name);
            if (replacement && patchlib_is_valid(replacement)) {
                *(patch_handle_t *)args[0] = replacement;
                effective_text = g_pending_decorated_name;
                injected_name = true;
            }
        } else {
            /* Do not let a name obtained in an unrelated UI path decorate a
             * later tooltip. */
            g_pending_name_valid = false;
        }
    }

    /* Final display fallback: compare the string passed to MouseText with
     * the vanilla name cached for each active elite type. This path works
     * even when Terraria ignores a string returned by an NPC/Lang postfix. */
    if (!injected_name &&
        strstr(effective_text, "精英·") == NULL &&
        strstr(effective_text, "稀有·") == NULL &&
        strstr(effective_text, "传奇·") == NULL) {
        for (size_t i = 0; i < g_elite_instance_count; ++i) {
            if (!g_elite_active[i] || !g_elite_source_names[i][0] ||
                strcmp(effective_text, g_elite_source_names[i]) != 0) {
                continue;
            }
            if (compose_elite_name(direct_decorated_name,
                                   sizeof(direct_decorated_name),
                                   effective_text, g_elite_ranks[i],
                                   g_elite_affix_masks[i])) {
                patch_handle_t replacement =
                    patchlib_string_create(direct_decorated_name);
                if (replacement && patchlib_is_valid(replacement)) {
                    *(patch_handle_t *)args[0] = replacement;
                    effective_text = direct_decorated_name;
                    injected_name = true;
                }
            }
            break;
        }
    }

    int rarity = -1;
    if (strstr(effective_text, "传奇·") != NULL) {
        rarity = 11;
    } else if (strstr(effective_text, "稀有·") != NULL) {
        rarity = 1;
    } else if (strstr(effective_text, "精英·") != NULL) {
        rarity = 0;
    }
    free(text);
    if (injected_name) g_pending_name_valid = false;
    if (rarity < 0) return true;

    const size_t arg_count = tefstd_vector_size(&sig_info->arg_types);
    /* MouseText(string, int, byte, ...) and
     * MouseText(string, string, int, byte, ...). */
    const size_t rare_index = (arg_count >= 10) ? 2 : 1;
    if (args[rare_index]) *(int *)args[rare_index] = rarity;
    return true;
}

/* NPC hover text is assembled through Main.MouseTextHackZoom before it
 * reaches Main.MouseText.  On the Android 1.4.5 build the latter can be
 * inlined or bypassed, so patch the small static wrapper as well.  The
 * wrapper's first argument is the same System.String handle and its second
 * argument is the vanilla rarity integer. */
static bool mouse_text_hack_prefix(patch_handle_t instance, void **args,
                                   const patch_method_signature_t *sig_info,
                                   void *result) {
    (void)instance;
    (void)result;
    if (!args || !sig_info || sig_info->is_instance ||
        tefstd_vector_size(&sig_info->arg_types) < 2 ||
        !args[0] || !args[1]) {
        return true;
    }

    patch_handle_t text_handle = *(patch_handle_t *)args[0];
    if (!text_handle || !patchlib_is_valid(text_handle)) return true;
    char *text = patchlib_string_cstr(text_handle);
    if (!text) return true;

    const char *effective_text = text;
    char decorated[512];
    bool injected_name = false;

    if (g_pending_name_valid) {
        if (strcmp(text, g_pending_source_name) == 0) {
            patch_handle_t replacement =
                patchlib_string_create(g_pending_decorated_name);
            if (replacement && patchlib_is_valid(replacement)) {
                *(patch_handle_t *)args[0] = replacement;
                effective_text = g_pending_decorated_name;
                injected_name = true;
            }
        } else {
            /* A name getter can also run for another UI element. Never carry
             * that pending replacement into a later tooltip. */
            g_pending_name_valid = false;
        }
    }

    if (!injected_name &&
        strstr(effective_text, "精英·") == NULL &&
        strstr(effective_text, "稀有·") == NULL &&
        strstr(effective_text, "传奇·") == NULL) {
        for (size_t i = 0; i < g_elite_instance_count; ++i) {
            if (!g_elite_active[i] || !g_elite_source_names[i][0] ||
                strcmp(effective_text, g_elite_source_names[i]) != 0) {
                continue;
            }
            if (compose_elite_name(decorated, sizeof(decorated),
                                   effective_text, g_elite_ranks[i],
                                   g_elite_affix_masks[i])) {
                patch_handle_t replacement = patchlib_string_create(decorated);
                if (replacement && patchlib_is_valid(replacement)) {
                    *(patch_handle_t *)args[0] = replacement;
                    effective_text = decorated;
                    injected_name = true;
                }
            }
            break;
        }
    }

    int rarity = -1;
    if (strstr(effective_text, "传奇·") != NULL) {
        rarity = 11;
    } else if (strstr(effective_text, "稀有·") != NULL) {
        rarity = 1;
    } else if (strstr(effective_text, "精英·") != NULL) {
        rarity = 0;
    }

    free(text);
    if (injected_name) g_pending_name_valid = false;
    if (rarity >= 0) *(int *)args[1] = rarity;
    return true;
}

static bool mouse_text_hack_signature_supported(
    const patch_method_signature_t *sig) {
    if (!sig || sig->is_instance || sig->return_type != PATCH_VOID) {
        return false;
    }
    size_t count = tefstd_vector_size(&sig->arg_types);
    if (count != 3 && count != 4) return false;

    patch_type_t *text_type =
        (patch_type_t *)tefstd_vector_at(&sig->arg_types, 0);
    patch_type_t *rare_type =
        (patch_type_t *)tefstd_vector_at(&sig->arg_types, 1);
    if (!text_type || !rare_type) return false;
    bool text_supported = *text_type == PATCH_OBJECT ||
                           *text_type == PATCH_POINTER;
    return text_supported && *rare_type == PATCH_INT32;
}

static void discover_mouse_text_hack_api(patch_handle_t main_type) {
    /* Depending on the Terraria build, optional arguments are either exposed
     * as one 4-parameter method or as a 3-parameter overload.  Try both, but
     * only accept the known static-void layout. */
    const int arg_counts[MOUSE_TEXT_HACK_HOOK_LIMIT] = {4, 3};
    for (size_t i = 0; i < MOUSE_TEXT_HACK_HOOK_LIMIT; ++i) {
        if (g_mouse_text_hack_hook_count >= MOUSE_TEXT_HACK_HOOK_LIMIT) {
            break;
        }
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            main_type, "MouseTextHackZoom", arg_counts[i]);
        if (!method || !patchlib_is_valid(method)) continue;

        patch_method_signature_t sig = {0};
        if (!patchlib_method_get_signature(method, &sig)) continue;
        bool supported = mouse_text_hack_signature_supported(&sig);
        patchlib_method_signature_free(&sig);
        if (!supported) continue;

        patch_hook_id_t hook_id = patchlib_install_prepost_hook(
            method, mouse_text_hack_prefix, NULL);
        if (hook_id == PATCH_HOOK_INVALID_ID) continue;
        g_mouse_text_hack_hooks[g_mouse_text_hack_hook_count++] = hook_id;
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Main.MouseTextHackZoom hook installed: params=%d id=%d",
                  arg_counts[i], (int)hook_id);
    }
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

/* Main.NewText(string) is used only for the one-time spawn notice. Keeping
 * this as a dynamically resolved optional API means the rest of the mod still
 * works on builds that omit that overload. */
static void discover_new_text_api(patch_handle_t main_type) {
    patch_handle_t method = patchlib_type_get_method_by_param_count(
        main_type, "NewText", 1);
    if (!method || !patchlib_is_valid(method)) return;

    patch_method_signature_t sig = {0};
    if (!patchlib_method_get_signature(method, &sig)) return;
    bool supported = !sig.is_instance && sig.return_type == PATCH_VOID &&
                     tefstd_vector_size(&sig.arg_types) == 1;
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Main.NewText API: found=%d static=%d return=%d params=%d",
              supported ? 1 : 0, sig.is_instance ? 0 : 1,
              (int)sig.return_type, (int)tefstd_vector_size(&sig.arg_types));
    patchlib_method_signature_free(&sig);
    if (supported) g_main_new_text_method = method;
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

static void announce_elite_spawn(void *instance, size_t index) {
    if (!instance || index >= PROCESSED_INSTANCE_LIMIT ||
        g_elite_spawn_announced[index]) {
        return;
    }

    /* UI method invocation is intentionally disabled until the exact
     * NewText overload is verified on-device. Mark the state so this optional
     * path cannot be retried every AI tick. */
    g_elite_spawn_announced[index] = true;
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

    elite_rank_t rank = g_elite_ranks[index];
    bool has_enrage = rank == ELITE_LEGENDARY ||
                      elite_has_affix(instance, AFFIX_ENRAGED);
    if (!has_enrage) return;

    int32_t life = 0;
    int32_t life_max = 0;
    if (!read_i32(g_field_life, instance, &life) ||
        !read_i32(g_field_life_max, instance, &life_max) || life <= 0 ||
        life_max <= 0) {
        return;
    }

    int enrage_percent = rank == ELITE_LEGENDARY
                             ? LEGENDARY_ENRAGE_LIFE_PERCENT
                             : 30;
    if ((int64_t)life * 100 > (int64_t)life_max * enrage_percent) {
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
              "%s elite entered enrage: damage=%.2fx life=%d/%d",
              rank == ELITE_LEGENDARY ? "Legendary" : "Affix", 
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

/* Safe, field-only affix effects. These do not replace vanilla attack logic,
 * and they continue to work on builds that do not expose projectile APIs. */
static void apply_affix_effects(patch_handle_t instance, size_t index) {
    uint32_t affix_mask = g_elite_affix_masks[index];
    uint32_t ticks = g_elite_ai_ticks[index];
    int32_t life = 0;
    int32_t life_max = 0;
    if (!read_i32(g_field_life, instance, &life) ||
        !read_i32(g_field_life_max, instance, &life_max) || life <= 0 ||
        life_max <= 0) {
        return;
    }

    if ((affix_mask & (1u << AFFIX_VAMPIRIC)) != 0 && ticks % 90u == 0u) {
        int32_t healed = life + (life_max / 100);
        if (healed <= life) healed = life + 1;
        if (healed > life_max) healed = life_max;
        if (healed != life) {
            (void)write_i32(g_field_life, instance, healed);
            request_npc_net_update(instance);
        }
    }

    /* Split is a one-time second wind: it restores 10% max life and forces a
     * short dash at half health. This keeps the mechanic reliable without
     * depending on an unstable NPC.NewNPC overload. */
    if ((affix_mask & (1u << AFFIX_SPLIT)) != 0 &&
        !g_elite_split_triggered[index] &&
        (int64_t)life * 100 <= (int64_t)life_max * 50) {
        int32_t restored = life + life_max / 10;
        if (restored > life_max) restored = life_max;
        (void)write_i32(g_field_life, instance, restored);
        g_elite_split_triggered[index] = true;
        apply_rank_dash(instance, g_elite_ranks[index]);
        request_npc_net_update(instance);
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Split affix triggered: life=%d/%d", (int)restored,
                  (int)life_max);
    }

    if ((affix_mask & (1u << AFFIX_FLAME)) != 0 && ticks % 120u == 0u) {
        apply_rank_dash(instance, g_elite_ranks[index]);
    }
    if ((affix_mask & (1u << AFFIX_ABYSSAL)) != 0 && ticks % 240u == 0u) {
        apply_rank_dash(instance, g_elite_ranks[index]);
    }
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
    announce_elite_spawn(instance, index);

    int32_t player = target_player_index(instance);
    if (player >= 0) (void)write_i32(g_field_target, instance, player);

    elite_rank_t rank = g_elite_ranks[index];
    if (!multiplayer_client()) apply_affix_effects(instance, index);
    if (rank != ELITE_LEGENDARY) {
        /* Normal elites get a readable, low-frequency burst. Rare elites
         * burst more often, so both ranks have a real combat identity instead
         * of relying on health/damage multipliers alone. */
        uint32_t skill_interval = rank == ELITE_RARE ? 150u : 240u;
        if (!multiplayer_client() &&
            g_elite_ai_ticks[index] % skill_interval == 0u) {
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
    srand((unsigned int)time(NULL) ^ 0x454C4954u);
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Loaded Android Hook probe; resolving NPC spawn API");
    discover_spawn_api();
    patch_handle_t npc = patchlib_type_get_type("Terraria", "NPC");
    if (npc && patchlib_is_valid(npc)) {
#if ENABLE_NAME_SOURCE_HOOK
        discover_name_api(npc);
#else
        /* The current Android build crashes during the first UI frame when
         * multiple NPC string getters are patched. Keep vanilla name drawing
         * enabled while a render-level name path is developed. */
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "NPC name source hooks disabled for Android stability");
#endif
        discover_ai_api(npc);
    }
    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (main_type && patchlib_is_valid(main_type)) {
        discover_mouse_text_hack_api(main_type);
        discover_mouse_text_api(main_type);
        if (npc && patchlib_is_valid(npc)) {
            patch_handle_t item_type = patchlib_type_get_type("Terraria", "Item");
            if (item_type && patchlib_is_valid(item_type)) {
                discover_reward_api(npc, item_type);
            } else {
                ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                          "Terraria.Item type not found; rare/environment rewards disabled");
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
    memset(g_npc_name_tokens, 0, sizeof(g_npc_name_tokens));
    for (size_t i = 0; i < g_mouse_text_hook_count; ++i) {
        patchlib_uninstall_hook(g_mouse_text_hooks[i]);
    }
    g_mouse_text_hook_count = 0;
    for (size_t i = 0; i < g_mouse_text_hack_hook_count; ++i) {
        patchlib_uninstall_hook(g_mouse_text_hack_hooks[i]);
    }
    g_mouse_text_hack_hook_count = 0;
    for (size_t i = 0; i < g_ai_hook_count; ++i) {
        patchlib_uninstall_hook(g_ai_hooks[i]);
    }
    g_ai_hook_count = 0;
    g_ai_method_token = -1;
    for (size_t i = 0; i < g_loot_hook_count; ++i) {
        patchlib_uninstall_hook(g_loot_hooks[i]);
    }
    g_loot_hook_count = 0;
    g_main_game_mode_getter = NULL;
    g_main_zenith_world_field = NULL;
    g_main_new_text_method = NULL;
    g_item_new_item_method = NULL;
    g_field_color = NULL;
    g_field_who_am_i = NULL;
    g_field_immortal = NULL;
    g_field_dont_take_damage = NULL;
    g_field_catchable = NULL;
    g_elite_instance_count = 0;
    memset(g_elite_instances, 0, sizeof(g_elite_instances));
    memset(g_elite_active, 0, sizeof(g_elite_active));
    memset(g_elite_types, 0, sizeof(g_elite_types));
    memset(g_elite_source_names, 0, sizeof(g_elite_source_names));
    memset(g_elite_rewarded, 0, sizeof(g_elite_rewarded));
    memset(g_elite_reward_mask, 0, sizeof(g_elite_reward_mask));
    memset(g_elite_spawn_announced, 0, sizeof(g_elite_spawn_announced));
    memset(g_elite_affix_masks, 0, sizeof(g_elite_affix_masks));
    memset(g_elite_split_triggered, 0, sizeof(g_elite_split_triggered));
    g_setdefaults_calls = 0;
    g_elite_count = 0;
    g_npc_name_calls = 0;
    g_pending_name_valid = false;
    memset(g_pending_source_name, 0, sizeof(g_pending_source_name));
    memset(g_pending_decorated_name, 0, sizeof(g_pending_decorated_name));
    ELITE_LOG(MOD_LOG_LEVEL_INFO, "Unloaded");
}

static kernel_mod_info_t g_info = {
    .pkg_id = "eternal.future.elitemonsters",
    .version_code = 2026090114,
    .api_version = 1,
    .version = "1.4.6"
};

static kernel_mod_info_t* get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t* create_kernel_mod(void) { return &g_ops; }
