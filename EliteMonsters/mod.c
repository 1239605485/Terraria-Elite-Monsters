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

/* The terrain layer is intentionally independent from Terraria's game mode.
 * A player can therefore move between rules without restarting the world. */
typedef enum terrain_rule_t {
    TERRAIN_RULE_NONE,
    TERRAIN_RULE_FOREST,
    TERRAIN_RULE_DESERT,
    TERRAIN_RULE_SNOW,
    TERRAIN_RULE_JUNGLE,
    TERRAIN_RULE_OCEAN,
    TERRAIN_RULE_UNDERGROUND,
    TERRAIN_RULE_CAVE,
    TERRAIN_RULE_CORRUPTION,
    TERRAIN_RULE_CRIMSON,
    TERRAIN_RULE_HALLOW,
    TERRAIN_RULE_DUNGEON,
    TERRAIN_RULE_UNDERWORLD,
    TERRAIN_RULE_GLOWING_MUSHROOM,
    TERRAIN_RULE_SPIDER,
    TERRAIN_RULE_TEMPLE,
    TERRAIN_RULE_SPACE,
    TERRAIN_RULE_METEOR,
    TERRAIN_RULE_ICE_CAVE,
    TERRAIN_RULE_UNDERGROUND_DESERT,
    TERRAIN_RULE_POST_PLANTERA_JUNGLE,
    TERRAIN_RULE_COUNT
} terrain_rule_t;

typedef enum global_rule_t {
    GLOBAL_RULE_SPLIT,
    GLOBAL_RULE_NIGHT_SPEED,
    GLOBAL_RULE_ELITE_BARRAGE,
    GLOBAL_RULE_TIDE,
    GLOBAL_RULE_BOSS_ENRAGE,
    GLOBAL_RULE_CRIT_SHOCKWAVE,
    GLOBAL_RULE_UNDERGROUND_SURGE,
    GLOBAL_RULE_EXTRA_CHEST,
    GLOBAL_RULE_LOW_HEALTH_DAMAGE,
    GLOBAL_RULE_RULE_SHIFT,
    GLOBAL_RULE_COUNT
} global_rule_t;

#define GLOBAL_RULE_SLOT_LIMIT 5
#define GLOBAL_RULE_MIN_COUNT 3
#define GLOBAL_RULE_MAX_COUNT 5
#define GLOBAL_RULE_SHIFT_INTERVAL 1800u

typedef struct terrain_rule_info_t {
    const char *name;
    const char *description;
} terrain_rule_info_t;

static const terrain_rule_info_t g_terrain_rule_info[TERRAIN_RULE_COUNT] = {
    {"无", ""},
    {"野性滋长", "森林敌怪在夜晚更活跃并缓慢恢复生命"},
    {"流沙猎杀", "沙漠敌怪移速提高并周期性释放沙刃"},
    {"极寒侵袭", "敌怪命中时会减缓玩家，暴雪期间效果增强"},
    {"毒性繁殖", "丛林敌怪死亡后有概率生成毒虫"},
    {"潮汐压力", "水域敌怪更快更痛，周期性出现敌潮"},
    {"回声伏击", "地下敌怪更积极追击并周期性增援"},
    {"岩层震荡", "洞穴敌怪死亡后可能引发小型敌群"},
    {"腐化蔓延", "腐化敌怪释放诅咒弹幕并留下危险余波"},
    {"血肉盛宴", "猩红敌怪造成压力时恢复生命"},
    {"棱彩反射", "神圣敌怪周期性释放分裂弹幕"},
    {"死灵复苏", "骷髅类敌怪有概率复活一次"},
    {"炼狱契约", "地狱敌怪获得火焰弹幕和更高伤害"},
    {"孢子爆发", "发光蘑菇地敌怪死亡会释放孢子余波"},
    {"蛛网领域", "玩家变慢，蜘蛛敌怪获得额外突进"},
    {"陷阱协议", "神庙敌怪周期性发射陷阱弹"},
    {"低重力猎场", "太空敌怪移动与跳跃更激进"},
    {"陨星共鸣", "陨石区敌怪死亡时可能释放陨星火花"},
    {"冰晶反射", "冰层敌怪周期性释放冰晶反击"},
    {"沙虫暴动", "地下沙漠敌怪移速更快并周期性增援"},
    {"丛林暴走", "世纪之花后丛林敌怪伤害提高并释放毒刺"}
};

static const char *g_global_rule_names[GLOBAL_RULE_COUNT] = {
    "裂变回响", "夜行猎杀", "精英弹幕", "百杀敌潮",
    "Boss狂暴", "暴击震荡", "地下增殖", "宝箱增益",
    "濒死反击", "危险轮换"
};

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
#define MAIN_UPDATE_HOOK_LIMIT 1
static patch_hook_id_t g_main_update_hooks[MAIN_UPDATE_HOOK_LIMIT];
static size_t g_main_update_hook_count = 0;
#define PLAYER_UPDATE_HOOK_LIMIT 1
static patch_hook_id_t g_player_update_hooks[PLAYER_UPDATE_HOOK_LIMIT];
static size_t g_player_update_hook_count = 0;
#define LOOT_HOOK_LIMIT 1
static patch_hook_id_t g_loot_hooks[LOOT_HOOK_LIMIT];
static size_t g_loot_hook_count = 0;
#define DEATH_HOOK_LIMIT 2
static patch_hook_id_t g_death_hooks[DEATH_HOOK_LIMIT];
static size_t g_death_hook_count = 0;
#define CHEST_HOOK_LIMIT 1
static patch_hook_id_t g_chest_hooks[CHEST_HOOK_LIMIT];
static size_t g_chest_hook_count = 0;
static unsigned long g_setdefaults_calls = 0;
static unsigned long g_elite_count = 0;
static unsigned long g_rare_reward_count = 0;
static unsigned long g_legendary_reward_count = 0;

/* World-scoped mutation state. The selection remains stable while the world
 * is open; the terrain rule is recalculated from the target player every AI
 * tick, so crossing a biome boundary needs no button or extra UI. */
static bool g_global_rules_initialized = false;
static uint32_t g_world_rule_identity = 0;
static uint32_t g_world_rule_rng = 0;
static uint32_t g_fallback_world_identity = 0;
static global_rule_t g_global_rules[GLOBAL_RULE_SLOT_LIMIT];
static size_t g_global_rule_count = 0;
static global_rule_t g_dynamic_rule = GLOBAL_RULE_COUNT;
static uint64_t g_last_game_update_count = UINT64_MAX;
static clock_t g_last_fallback_clock = 0;
static uint32_t g_rule_ticks = 0;
static uint32_t g_kill_count = 0;
static terrain_rule_t g_last_reported_terrain = TERRAIN_RULE_NONE;
static bool g_player_session_seen = false;
static bool g_player_session_active = false;
static bool g_world_notice_on_session_enter = false;
static uint32_t g_tide_cooldown = 0;
static uint32_t g_spawn_events_this_tick = 0;

static bool game_text_notice(const char *text, uint8_t red, uint8_t green,
                             uint8_t blue);

#define GLOBAL_RULE_SHIFT_INTERVAL 1800u
#define RULE_PROJECTILE_INTERVAL 150u
#define TERRAIN_ACTION_INTERVAL 120u
#define TERRAIN_REGEN_INTERVAL 90u
#define MAX_RULE_SPAWNS_PER_TICK 4u

static terrain_rule_t terrain_rule_for_player(int32_t player_index);
static terrain_rule_t terrain_rule_for_player_instance(patch_handle_t player);

/* Terraria keeps a fixed NPC object pool and reuses the same object pointer
 * for many different spawns.  State stored here therefore describes only the
 * NPC's current SetDefaults lifecycle; it must be reset every time
 * SetDefaults completes for that object. */
#define PROCESSED_INSTANCE_LIMIT 1024
static void *g_elite_instances[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_active[PROCESSED_INSTANCE_LIMIT];
static elite_rank_t g_elite_ranks[PROCESSED_INSTANCE_LIMIT];
static elite_behavior_t g_elite_behaviors[PROCESSED_INSTANCE_LIMIT];
static uint32_t g_elite_ai_ticks[PROCESSED_INSTANCE_LIMIT];
static int32_t g_elite_base_damage[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_enraged[PROCESSED_INSTANCE_LIMIT];
static bool g_elite_rewarded[PROCESSED_INSTANCE_LIMIT];
static bool g_rule_eligible[PROCESSED_INSTANCE_LIMIT];
static bool g_rule_death_handled[PROCESSED_INSTANCE_LIMIT];
static bool g_rule_revived[PROCESSED_INSTANCE_LIMIT];
static bool g_rule_boss_enraged[PROCESSED_INSTANCE_LIMIT];
static int32_t g_rule_npc_types[PROCESSED_INSTANCE_LIMIT];
static int32_t g_rule_base_damage[PROCESSED_INSTANCE_LIMIT];
static size_t g_elite_instance_count = 0;
static size_t g_rule_slot_cursor = 0;

/* Terraria 1.4.5.6.4 ItemID values from the target game's ItemID table.
 * Every reward below is an original Terraria item or environment crate. */
#define ITEM_LIFE_CRYSTAL 29
#define ITEM_GOLDEN_CRATE 2336
#define ITEM_TITANIUM_CRATE 3981
#define ITEM_MANA_CRYSTAL 109
#define ITEM_FALLEN_STAR 75
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

/* A rare elite gives one random original item from the current progression
 * tier.  Pools deliberately use materials/utility items rather than custom
 * content, and stack ranges keep the reward useful without creating a full
 * endgame inventory in one drop. */
static const vanilla_reward_entry_t
    g_progress_reward_pools[5][PROGRESS_REWARD_POOL_SIZE] = {
        {
            {ITEM_LIFE_CRYSTAL, 1, 1},
            {ITEM_MANA_CRYSTAL, 1, 1},
            {ITEM_FALLEN_STAR, 3, 5},
            {ITEM_MAGIC_MIRROR, 1, 1},
            {ITEM_HERMES_BOOTS, 1, 1},
            {ITEM_CLOUD_IN_A_BOTTLE, 1, 1},
            {ITEM_HOOK, 1, 1},
            {ITEM_HEALING_POTION, 2, 4}
        },
        {
            {ITEM_COBALT_BAR, 2, 4},
            {ITEM_MYTHRIL_BAR, 2, 4},
            {ITEM_ADAMANTITE_BAR, 2, 4},
            {ITEM_SOUL_OF_LIGHT, 3, 6},
            {ITEM_SOUL_OF_NIGHT, 3, 6},
            {ITEM_DEMON_WINGS, 1, 1},
            {ITEM_ANGEL_WINGS, 1, 1},
            {ITEM_GREATER_HEALING_POTION, 3, 6}
        },
        {
            {ITEM_HALLOWED_BAR, 2, 4},
            {ITEM_CHLOROPHYTE_ORE, 8, 16},
            {ITEM_CHLOROPHYTE_BAR, 2, 4},
            {ITEM_SOUL_OF_FRIGHT, 3, 6},
            {ITEM_SOUL_OF_MIGHT, 3, 6},
            {ITEM_SOUL_OF_SIGHT, 3, 6},
            {ITEM_LIFE_FRUIT, 1, 1},
            {ITEM_GREATER_MANA_POTION, 3, 6}
        },
        {
            {ITEM_ECTOPLASM, 2, 4},
            {ITEM_SPECTRE_BAR, 2, 4},
            {ITEM_SHROOMITE_BAR, 2, 4},
            {ITEM_TEMPLE_KEY, 1, 1},
            {ITEM_CHLOROPHYTE_BAR, 3, 6},
            {ITEM_LIHZAHRD_POWER_CELL, 1, 2},
            {ITEM_LIFE_FRUIT, 1, 1},
            {ITEM_GREATER_HEALING_POTION, 4, 8}
        },
        {
            {ITEM_LUNAR_BAR, 2, 5},
            {ITEM_CELESTIAL_SIGIL, 1, 1},
            {ITEM_ECTOPLASM, 3, 6},
            {ITEM_BEETLE_HUSK, 2, 4},
            {ITEM_SHROOMITE_BAR, 2, 5},
            {ITEM_CHLOROPHYTE_BAR, 4, 8},
            {ITEM_LIFE_FRUIT, 1, 1},
            {ITEM_GREATER_HEALING_POTION, 5, 10}
        }
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
static patch_handle_t g_main_world_name_field = NULL;
static patch_handle_t g_main_world_id_field = NULL;
static patch_handle_t g_main_game_menu_field = NULL;
static patch_handle_t g_main_day_time_field = NULL;
static patch_handle_t g_main_snow_storm_field = NULL;
static patch_handle_t g_main_update_count_field = NULL;
static patch_handle_t g_main_new_text_method = NULL;
static int g_main_new_text_arg_count = 0;
static patch_type_t g_main_new_text_color_type = PATCH_UINT8;
static bool g_main_new_text_warning_logged = false;
static patch_handle_t g_item_new_item_method = NULL;
static patch_handle_t g_npc_new_npc_method = NULL;
static int g_npc_new_npc_arg_count = 0;
static patch_handle_t g_projectile_new_projectile_method = NULL;
static patch_handle_t g_chest_open_method = NULL;
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
static patch_handle_t g_player_zone_forest_field = NULL;
static patch_handle_t g_player_zone_rock_layer_field = NULL;
static patch_handle_t g_player_zone_dirt_layer_field = NULL;
static patch_handle_t g_player_zone_glowshroom_field = NULL;
static patch_handle_t g_player_zone_spider_field = NULL;
static patch_handle_t g_player_zone_meteor_field = NULL;
static patch_handle_t g_player_zone_temple_field = NULL;
static patch_handle_t g_player_zone_ice_field = NULL;
enum player_zone_getter_index {
    ZONE_GETTER_DUNGEON,
    ZONE_GETTER_CORRUPT,
    ZONE_GETTER_CRIMSON,
    ZONE_GETTER_JUNGLE,
    ZONE_GETTER_SNOW,
    ZONE_GETTER_DESERT,
    ZONE_GETTER_BEACH,
    ZONE_GETTER_UNDERWORLD,
    ZONE_GETTER_HALLOW,
    ZONE_GETTER_SKY,
    ZONE_GETTER_FOREST,
    ZONE_GETTER_ROCK,
    ZONE_GETTER_DIRT,
    ZONE_GETTER_GLOWSHROOM,
    ZONE_GETTER_SPIDER,
    ZONE_GETTER_METEOR,
    ZONE_GETTER_TEMPLE,
    ZONE_GETTER_COUNT
};
static patch_handle_t g_player_zone_getters[ZONE_GETTER_COUNT];
static patch_handle_t g_player_velocity_field = NULL;
static patch_handle_t g_player_stat_life_field = NULL;
static patch_handle_t g_player_stat_life_max_field = NULL;
static bool g_progress_fields_logged = false;

#define LEGENDARY_ENRAGE_LIFE_PERCENT 35
#define LEGENDARY_ENRAGE_DAMAGE_MULTIPLIER 1.25f
#define LEGENDARY_TELEPORT_DISTANCE 480.0f
#define LEGENDARY_TELEPORT_OFFSET 96.0f

/* Vanilla IDs used only if the target build exposes the original factory. */
#define RULE_PROJECTILE_SAND 10
#define RULE_PROJECTILE_FIRE 17
#define RULE_PROJECTILE_ICE 27
#define RULE_PROJECTILE_CURSED 20
#define RULE_NPC_BLUE_SLIME 1
#define RULE_NPC_CAVE_BAT 49
#define RULE_NPC_HORNET 42
#define RULE_NPC_VULTURE 47

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

static bool read_u64(patch_handle_t field, patch_handle_t instance,
                     uint64_t *out) {
    if (!field || !out || !patchlib_is_valid(field)) return false;
    patch_type_t type = patchlib_field_get_type(field);
    if (type == PATCH_UINT64) return get_field_value(field, instance, out);
    if (type == PATCH_INT64) {
        int64_t signed_value = 0;
        if (!get_field_value(field, instance, &signed_value)) return false;
        *out = signed_value < 0 ? 0u : (uint64_t)signed_value;
        return true;
    }
    if (type == PATCH_INT32) {
        int32_t value = 0;
        if (!get_field_value(field, instance, &value)) return false;
        *out = value < 0 ? 0u : (uint64_t)value;
        return true;
    }
    return false;
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

static size_t rule_instance_index(void *instance) {
    return tracked_instance_index(instance);
}

static bool is_rule_instance(void *instance) {
    size_t slot = rule_instance_index(instance);
    return slot < PROCESSED_INSTANCE_LIMIT && g_rule_eligible[slot];
}

static void clear_elite_instance(void *instance) {
    size_t slot = tracked_instance_index(instance);
    if (slot >= PROCESSED_INSTANCE_LIMIT) return;
    g_elite_active[slot] = false;
    g_elite_rewarded[slot] = false;
    g_elite_ai_ticks[slot] = 0;
    g_elite_base_damage[slot] = 0;
    g_elite_enraged[slot] = false;
    g_rule_eligible[slot] = false;
    g_rule_death_handled[slot] = false;
    g_rule_revived[slot] = false;
    g_rule_boss_enraged[slot] = false;
    g_rule_npc_types[slot] = 0;
    g_rule_base_damage[slot] = 0;
}

static void set_rule_state(size_t slot, int32_t npc_type, int32_t base_damage,
                           bool eligible) {
    g_rule_eligible[slot] = eligible;
    g_elite_ai_ticks[slot] = 0;
    g_rule_death_handled[slot] = false;
    g_rule_revived[slot] = false;
    g_rule_boss_enraged[slot] = false;
    g_rule_npc_types[slot] = npc_type;
    g_rule_base_damage[slot] = base_damage;
}

static void remember_rule_instance(void *instance, int32_t npc_type,
                                   int32_t base_damage, bool eligible) {
    if (!instance) return;
    size_t slot = tracked_instance_index(instance);
    if (slot >= PROCESSED_INSTANCE_LIMIT) {
        slot = g_rule_slot_cursor++ % PROCESSED_INSTANCE_LIMIT;
        g_elite_instances[slot] = instance;
        if (slot >= g_elite_instance_count) g_elite_instance_count = slot + 1;
        g_elite_active[slot] = false;
    }
    set_rule_state(slot, npc_type, base_damage, eligible);
}

static void set_elite_state(size_t slot, elite_rank_t rank,
                            elite_behavior_t behavior, int32_t base_damage) {
    g_elite_active[slot] = true;
    g_elite_ranks[slot] = rank;
    g_elite_behaviors[slot] = behavior;
    g_elite_ai_ticks[slot] = 0;
    g_elite_base_damage[slot] = base_damage;
    g_elite_enraged[slot] = false;
    g_elite_rewarded[slot] = false;
    g_rule_eligible[slot] = true;
    g_rule_death_handled[slot] = false;
    g_rule_revived[slot] = false;
    g_rule_boss_enraged[slot] = false;
    g_rule_npc_types[slot] = 0;
    g_rule_base_damage[slot] = base_damage;
}

static void remember_elite_instance(void *instance, elite_rank_t rank,
                                    elite_behavior_t behavior,
                                    int32_t base_damage) {
    if (!instance) return;
    size_t existing_slot = tracked_instance_index(instance);
    if (existing_slot < PROCESSED_INSTANCE_LIMIT) {
        set_elite_state(existing_slot, rank, behavior, base_damage);
        return;
    }
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
    size_t slot = elite_instance_index(instance);
    if (slot < PROCESSED_INSTANCE_LIMIT) return g_elite_ranks[slot];
    return ELITE_NORMAL;
}

static bool already_rewarded(void *instance) {
    size_t slot = elite_instance_index(instance);
    return slot < PROCESSED_INSTANCE_LIMIT && g_elite_rewarded[slot];
}

static void remember_rewarded(void *instance) {
    size_t slot = elite_instance_index(instance);
    if (slot < PROCESSED_INSTANCE_LIMIT) g_elite_rewarded[slot] = true;
}

static elite_world_mode_t profile_mode_for_game_mode(int game_mode) {
    if (game_mode == 1) return ELITE_MODE_EXPERT;
    if (game_mode == 2) return ELITE_MODE_MASTER;
    /* GameModeID.Creative/Journey (3) has no custom difficulty mapping. */
    return ELITE_MODE_NORMAL;
}

static const char *world_mode_name(elite_world_mode_t mode) {
    if (mode < ELITE_MODE_NORMAL || mode >= ELITE_MODE_COUNT) {
        return g_mode_modifiers[ELITE_MODE_NORMAL].name;
    }
    return g_mode_modifiers[mode].name;
}

static elite_profile_t make_profile(elite_progress_t progress,
                                    elite_world_mode_t mode) {
    if (progress < PROGRESS_PRE_HARDMODE || progress > PROGRESS_ENDGAME) {
        progress = PROGRESS_PRE_HARDMODE;
    }
    if (mode < ELITE_MODE_NORMAL || mode >= ELITE_MODE_COUNT) {
        mode = ELITE_MODE_NORMAL;
    }

    int rank_roll = random_percent();
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

/* 生成 Hook 接入后调用此函数；GameModeID: 0=普通,1=专家,2=大师,
 * 3=旅途。天顶世界由 Main.zenithWorld 覆盖为自定义传奇属性档。 */
static int elite_should_spawn(int world_mode) {
    if (world_mode < 0) world_mode = 0;
    if (world_mode >= ELITE_MODE_COUNT) world_mode = ELITE_MODE_COUNT - 1;
    /* SetDefaults may run during world bootstrap, before Main.player and the
     * Zone* fields are valid. Keep this gate side-effect-free; terrain rules
     * are evaluated later from the safe AI path. */
    return random_percent() < g_spawn_chance_percent[world_mode];
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

static patch_handle_t zone_getter_for_field(patch_handle_t field) {
    if (field == g_player_zone_dungeon_field) {
        return g_player_zone_getters[ZONE_GETTER_DUNGEON];
    }
    if (field == g_player_zone_corrupt_field) {
        return g_player_zone_getters[ZONE_GETTER_CORRUPT];
    }
    if (field == g_player_zone_crimson_field) {
        return g_player_zone_getters[ZONE_GETTER_CRIMSON];
    }
    if (field == g_player_zone_jungle_field) {
        return g_player_zone_getters[ZONE_GETTER_JUNGLE];
    }
    if (field == g_player_zone_snow_field) {
        return g_player_zone_getters[ZONE_GETTER_SNOW];
    }
    if (field == g_player_zone_desert_field) {
        return g_player_zone_getters[ZONE_GETTER_DESERT];
    }
    if (field == g_player_zone_beach_field) {
        return g_player_zone_getters[ZONE_GETTER_BEACH];
    }
    if (field == g_player_zone_underworld_field) {
        return g_player_zone_getters[ZONE_GETTER_UNDERWORLD];
    }
    if (field == g_player_zone_hallow_field) {
        return g_player_zone_getters[ZONE_GETTER_HALLOW];
    }
    if (field == g_player_zone_sky_field) {
        return g_player_zone_getters[ZONE_GETTER_SKY];
    }
    if (field == g_player_zone_forest_field) {
        return g_player_zone_getters[ZONE_GETTER_FOREST];
    }
    if (field == g_player_zone_rock_layer_field) {
        return g_player_zone_getters[ZONE_GETTER_ROCK];
    }
    if (field == g_player_zone_dirt_layer_field) {
        return g_player_zone_getters[ZONE_GETTER_DIRT];
    }
    if (field == g_player_zone_glowshroom_field) {
        return g_player_zone_getters[ZONE_GETTER_GLOWSHROOM];
    }
    if (field == g_player_zone_spider_field) {
        return g_player_zone_getters[ZONE_GETTER_SPIDER];
    }
    if (field == g_player_zone_meteor_field) {
        return g_player_zone_getters[ZONE_GETTER_METEOR];
    }
    if (field == g_player_zone_temple_field) {
        return g_player_zone_getters[ZONE_GETTER_TEMPLE];
    }
    return NULL;
}

static bool read_player_zone_flag_instance(patch_handle_t player,
                                           patch_handle_t field) {
    bool value = false;
    if (!player || !patchlib_is_valid(player)) return false;
    /* A backing field can exist but stay stale on some IL2CPP builds. If it
     * reads false, still try the property's getter before deciding that the
     * player is outside the biome. */
    if (valid_field(field, PATCH_BOOL) &&
        read_bool(field, player, &value) && value) return true;

    patch_handle_t getter = zone_getter_for_field(field);
    if (!getter || !patchlib_is_valid(getter)) return false;
    return patchlib_method_invoke_args(getter, player, &value, NULL) && value;
}

static bool read_player_zone_flag(int32_t player_index, patch_handle_t field) {
    patch_handle_t player = NULL;
    if (!get_player_instance(player_index, &player)) return false;
    return read_player_zone_flag_instance(player, field);
}

static uint32_t hash_bytes(uint32_t hash, const unsigned char *bytes,
                           size_t length) {
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool main_bool_field(patch_handle_t field, bool fallback) {
    bool value = fallback;
    (void)read_bool(field, NULL, &value);
    return value;
}

static uint32_t current_world_rule_identity(void) {
    uint32_t hash = 2166136261u;
    bool has_identity_component = false;
    int32_t world_id = 0;
    int32_t mode = current_world_mode();
    if (read_i32(g_main_world_id_field, NULL, &world_id)) {
        has_identity_component = true;
        hash = hash_bytes(hash, (const unsigned char *)&world_id,
                          sizeof(world_id));
    }
    hash = hash_bytes(hash, (const unsigned char *)&mode, sizeof(mode));

    if (g_main_world_name_field && patchlib_is_valid(g_main_world_name_field)) {
        patch_handle_t world_name = NULL;
        if (get_field_value(g_main_world_name_field, NULL, &world_name) &&
            world_name && patchlib_is_valid(world_name)) {
            char *name = patchlib_string_cstr(world_name);
            if (name) {
                if (name[0]) has_identity_component = true;
                hash = hash_bytes(hash, (const unsigned char *)name,
                                  strlen(name));
                free(name);
            }
        }
    }
    /* A zero identity normally means the game is still at its title screen.
     * Keep a per-process fallback instead of repeatedly rerolling rules. */
    if (!has_identity_component) {
        if (g_fallback_world_identity == 0) {
            g_fallback_world_identity = (uint32_t)time(NULL) ^
                                        (uint32_t)(uintptr_t)&hash;
            if (g_fallback_world_identity == 0) g_fallback_world_identity = 1;
        }
        hash ^= g_fallback_world_identity;
    }
    return hash;
}

static uint32_t world_random_below(uint32_t maximum) {
    if (maximum == 0) return 0;
    g_world_rule_rng = g_world_rule_rng * 1664525u + 1013904223u;
    return g_world_rule_rng % maximum;
}

static bool global_rule_selected(global_rule_t rule) {
    for (size_t i = 0; i < g_global_rule_count; ++i) {
        if (g_global_rules[i] == rule) return true;
    }
    return false;
}

static bool global_rule_active(global_rule_t rule) {
    return global_rule_selected(rule) || g_dynamic_rule == rule;
}

static void announce_global_rules(void) {
    char notice[768];
    int offset = snprintf(notice, sizeof(notice),
                          "[精英变异] 本世界全局规则（%d条）：",
                          (int)g_global_rule_count);
    if (offset < 0) return;
    if ((size_t)offset >= sizeof(notice)) offset = (int)sizeof(notice) - 1;
    for (size_t i = 0; i < g_global_rule_count &&
                        (size_t)offset < sizeof(notice); ++i) {
        int written = snprintf(notice + offset, sizeof(notice) - (size_t)offset,
                               "%s%s", i == 0 ? " " : "、",
                               g_global_rule_names[g_global_rules[i]]);
        if (written < 0) break;
        if ((size_t)written >= sizeof(notice) - (size_t)offset) {
            offset = (int)sizeof(notice) - 1;
            break;
        }
        offset += written;
    }
    notice[sizeof(notice) - 1] = '\0';
    (void)game_text_notice(notice, 255, 220, 80);
    (void)game_text_notice("[精英变异] 地形规则会随玩家进入或离开区域自动切换。",
                           180, 220, 255);
}

static void initialize_world_rules(void) {
    uint32_t identity = current_world_rule_identity();
    if (g_global_rules_initialized && identity == g_world_rule_identity) return;

    g_world_rule_identity = identity;
    g_world_rule_rng = identity ^ 0xA5C31F27u;
    g_global_rule_count = GLOBAL_RULE_MIN_COUNT +
                          (size_t)world_random_below(
                              GLOBAL_RULE_MAX_COUNT - GLOBAL_RULE_MIN_COUNT + 1);
    memset(g_global_rules, 0, sizeof(g_global_rules));
    for (size_t i = 0; i < g_global_rule_count; ++i) {
        global_rule_t candidate =
            (global_rule_t)world_random_below(GLOBAL_RULE_COUNT);
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (g_global_rules[j] == candidate) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            --i;
            continue;
        }
        g_global_rules[i] = candidate;
    }
    g_dynamic_rule = GLOBAL_RULE_COUNT;
    g_rule_ticks = 0;
    g_kill_count = 0;
    g_tide_cooldown = 0;
    g_global_rules_initialized = true;

    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "World mutation rules initialized: identity=%u count=%d",
              (unsigned)g_world_rule_identity, (int)g_global_rule_count);
    for (size_t i = 0; i < g_global_rule_count; ++i) {
        ELITE_LOG(MOD_LOG_LEVEL_INFO, "Global rule %d: %s",
                  (int)i, g_global_rule_names[g_global_rules[i]]);
    }

    announce_global_rules();
}

static void advance_world_rule_clock(void) {
    uint64_t update_count = 0;
    bool has_update_count = read_u64(g_main_update_count_field, NULL,
                                     &update_count);
    if (has_update_count && g_last_game_update_count != UINT64_MAX &&
        update_count < g_last_game_update_count) {
        /* Main.GameUpdateCount starts over when a world is left/reloaded.
         * Keep the world's rule roll, but make the next active terrain emit
         * its entry notice again. */
        g_last_reported_terrain = TERRAIN_RULE_NONE;
        g_tide_cooldown = 0;
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "World session restarted; terrain notice state reset");
    }
    if (has_update_count && update_count == g_last_game_update_count) return;
    if (!has_update_count) {
        clock_t now = clock();
        clock_t minimum_step = CLOCKS_PER_SEC / 60;
        if (minimum_step < 1) minimum_step = 1;
        if (g_last_fallback_clock != 0 &&
            now - g_last_fallback_clock < minimum_step) return;
        g_last_fallback_clock = now;
    }
    if (has_update_count) g_last_game_update_count = update_count;
    g_spawn_events_this_tick = 0;
    ++g_rule_ticks;
    if (g_tide_cooldown > 0) --g_tide_cooldown;

    if (g_global_rule_count > 0 &&
        g_rule_ticks % GLOBAL_RULE_SHIFT_INTERVAL == 0u &&
        global_rule_selected(GLOBAL_RULE_RULE_SHIFT)) {
        global_rule_t next = GLOBAL_RULE_COUNT;
        for (int attempts = 0; attempts < 16 && next == GLOBAL_RULE_COUNT;
             ++attempts) {
            global_rule_t candidate =
                (global_rule_t)(rand() % (GLOBAL_RULE_COUNT - 1));
            if (candidate != GLOBAL_RULE_RULE_SHIFT) next = candidate;
        }
        g_dynamic_rule = next;
        ELITE_LOG(MOD_LOG_LEVEL_INFO, "Danger rule rotated: %s",
                  g_global_rule_names[g_dynamic_rule]);
        char notice[192];
        (void)snprintf(notice, sizeof(notice),
                       "[精英变异] 危险轮换：当前临时规则为「%s」",
                       g_global_rule_names[g_dynamic_rule]);
        (void)game_text_notice(notice, 255, 150, 80);
    }
}

static terrain_rule_t terrain_rule_for_player(int32_t player_index) {
    if (player_index < 0) return TERRAIN_RULE_NONE;
    patch_handle_t player = NULL;
    if (!get_player_instance(player_index, &player)) return TERRAIN_RULE_NONE;
    return terrain_rule_for_player_instance(player);
}

/* Player.Update runs on the actual Player object.  Use that object directly
 * for terrain detection instead of requiring Main.player[] to be readable as
 * an IL2CPP array.  The latter is exposed differently by different Android
 * metadata builds and was the reason terrain notices stopped after the first
 * global announcement. */
static terrain_rule_t terrain_rule_for_player_instance(patch_handle_t player) {
    if (!player || !patchlib_is_valid(player)) return TERRAIN_RULE_NONE;
    bool underworld = read_player_zone_flag_instance(
        player, g_player_zone_underworld_field);
    bool rock = read_player_zone_flag_instance(player,
                                               g_player_zone_rock_layer_field);
    bool dirt = read_player_zone_flag_instance(player,
                                               g_player_zone_dirt_layer_field);
    bool snow = read_player_zone_flag_instance(player, g_player_zone_snow_field);
    bool desert = read_player_zone_flag_instance(player,
                                                 g_player_zone_desert_field);
    bool jungle = read_player_zone_flag_instance(player,
                                                 g_player_zone_jungle_field);

    if (read_player_zone_flag_instance(player, g_player_zone_temple_field)) {
        return TERRAIN_RULE_TEMPLE;
    }
    if (read_player_zone_flag_instance(player, g_player_zone_spider_field)) {
        return TERRAIN_RULE_SPIDER;
    }
    if (underworld) return TERRAIN_RULE_UNDERWORLD;
    if (read_player_zone_flag_instance(player, g_player_zone_meteor_field)) {
        return TERRAIN_RULE_METEOR;
    }
    if (read_player_zone_flag_instance(player, g_player_zone_sky_field)) {
        return TERRAIN_RULE_SPACE;
    }
    if (read_player_zone_flag_instance(player, g_player_zone_glowshroom_field)) {
        return TERRAIN_RULE_GLOWING_MUSHROOM;
    }
    if (snow && rock) return TERRAIN_RULE_ICE_CAVE;
    if (desert && (rock || dirt)) return TERRAIN_RULE_UNDERGROUND_DESERT;
    if (jungle && current_progress() >= PROGRESS_POST_PLANTERA) {
        return TERRAIN_RULE_POST_PLANTERA_JUNGLE;
    }
    if (read_player_zone_flag_instance(player, g_player_zone_dungeon_field)) {
        return TERRAIN_RULE_DUNGEON;
    }
    if (read_player_zone_flag_instance(player, g_player_zone_corrupt_field)) {
        return TERRAIN_RULE_CORRUPTION;
    }
    if (read_player_zone_flag_instance(player, g_player_zone_crimson_field)) {
        return TERRAIN_RULE_CRIMSON;
    }
    if (read_player_zone_flag_instance(player, g_player_zone_hallow_field)) {
        return TERRAIN_RULE_HALLOW;
    }
    if (jungle) return TERRAIN_RULE_JUNGLE;
    if (snow) return TERRAIN_RULE_SNOW;
    if (desert) return TERRAIN_RULE_DESERT;
    if (read_player_zone_flag_instance(player, g_player_zone_beach_field)) {
        return TERRAIN_RULE_OCEAN;
    }
    if (rock) return TERRAIN_RULE_CAVE;
    if (dirt) return TERRAIN_RULE_UNDERGROUND;
    if (read_player_zone_flag(player_index, g_player_zone_forest_field)) {
        return TERRAIN_RULE_FOREST;
    }
    return TERRAIN_RULE_FOREST;
}

static const char *terrain_display_name(terrain_rule_t terrain) {
    static const char *names[TERRAIN_RULE_COUNT] = {
        "未知区域", "森林", "沙漠", "雪原", "丛林", "海洋",
        "地下", "洞穴", "腐化之地", "猩红之地", "神圣之地", "地牢",
        "地狱", "发光蘑菇地", "蜘蛛洞", "蜥蜴神庙", "太空", "陨石区域",
        "洞穴冰层", "沙漠地下", "世纪之花后丛林"
    };
    if (terrain < TERRAIN_RULE_NONE || terrain >= TERRAIN_RULE_COUNT) {
        return names[TERRAIN_RULE_NONE];
    }
    return names[terrain];
}

static void report_terrain_transition(terrain_rule_t terrain) {
    /* A terrain notice is an edge-triggered event.  The old global cooldown
     * could swallow a legitimate transition when the player crossed two
     * biome boundaries quickly, making the feature look like a one-shot
     * announcement.  The cached terrain already debounces repeated ticks. */
    if (terrain == g_last_reported_terrain) return;
    terrain_rule_t previous = g_last_reported_terrain;
    g_last_reported_terrain = terrain;
    if (terrain > TERRAIN_RULE_NONE && terrain < TERRAIN_RULE_COUNT) {
        ELITE_LOG(MOD_LOG_LEVEL_INFO, "Terrain rule enabled: %s - %s",
                  g_terrain_rule_info[terrain].name,
                  g_terrain_rule_info[terrain].description);
        char notice[384];
        if (previous > TERRAIN_RULE_NONE && previous < TERRAIN_RULE_COUNT) {
            (void)snprintf(notice, sizeof(notice),
                           "[精英变异] 地形规则切换：%s → %s",
                           terrain_display_name(previous),
                           terrain_display_name(terrain));
        } else {
            (void)snprintf(notice, sizeof(notice), "[精英变异] 进入地形：%s",
                           terrain_display_name(terrain));
        }
        (void)game_text_notice(notice, 140, 255, 170);
        (void)snprintf(notice, sizeof(notice), "专属规则：%s——%s",
                       g_terrain_rule_info[terrain].name,
                       g_terrain_rule_info[terrain].description);
        (void)game_text_notice(notice, 140, 220, 255);
    } else {
        ELITE_LOG(MOD_LOG_LEVEL_INFO, "Terrain rule disabled");
        if (previous > TERRAIN_RULE_NONE && previous < TERRAIN_RULE_COUNT) {
            char notice[192];
            (void)snprintf(notice, sizeof(notice),
                           "[精英变异] 已离开%s，专属规则已关闭",
                           terrain_display_name(previous));
            (void)game_text_notice(notice, 180, 220, 255);
        }
    }
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

static bool multiplayer_client(void);
static void request_npc_net_update(patch_handle_t instance);

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

static bool spawn_vanilla_reward_at(int32_t x, int32_t y, int32_t item_type,
                                    int32_t item_stack) {
    if (multiplayer_client() || !g_item_new_item_method || item_type <= 0 ||
        item_stack <= 0) return false;
    int32_t width = 16;
    int32_t height = 16;
    int32_t type = item_type;
    int32_t stack = item_stack;
    bool no_broadcast = false;
    int32_t prefix = 0;
    bool no_grab_delay = false;
    int32_t spawned_item = -1;
    void *args[9] = {&x, &y, &width, &height, &type,
                      &stack, &no_broadcast, &prefix, &no_grab_delay};
    if (!patchlib_method_invoke_args(g_item_new_item_method, PATCH_NULL,
                                     &spawned_item, args)) return false;
    return spawned_item >= 0;
}

static bool signature_arg_is(const patch_method_signature_t *sig, size_t index,
                             patch_type_t expected) {
    if (!sig || index >= tefstd_vector_size(&sig->arg_types)) return false;
    patch_type_t *actual = (patch_type_t *)tefstd_vector_at(
        (tefstd_vector_t *)&sig->arg_types, index);
    return actual && *actual == expected;
}

static bool signature_arg_is_text(const patch_method_signature_t *sig,
                                  size_t index) {
    return signature_arg_is(sig, index, PATCH_OBJECT) ||
           signature_arg_is(sig, index, PATCH_POINTER);
}

static bool game_text_notice(const char *text, uint8_t red, uint8_t green,
                             uint8_t blue) {
    if (!text || !text[0] || !g_main_new_text_method ||
        !patchlib_is_valid(g_main_new_text_method)) {
        if (!g_main_new_text_warning_logged) {
            ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                      "Main.NewText unavailable; in-game rule notices disabled");
            g_main_new_text_warning_logged = true;
        }
        return false;
    }

    patch_handle_t message = patchlib_string_create(text);
    if (!message || !patchlib_is_valid(message)) return false;

    void *args[4] = {&message, NULL, NULL, NULL};
    int32_t red_i = red;
    int32_t green_i = green;
    int32_t blue_i = blue;
    if (g_main_new_text_arg_count == 4) {
        if (g_main_new_text_color_type == PATCH_UINT8) {
            args[1] = &red;
            args[2] = &green;
            args[3] = &blue;
        } else {
            args[1] = &red_i;
            args[2] = &green_i;
            args[3] = &blue_i;
        }
    }

    uint64_t ignored_return = 0;
    if (patchlib_method_invoke_args(g_main_new_text_method, PATCH_NULL,
                                    &ignored_return, args)) {
        return true;
    }
    if (!g_main_new_text_warning_logged) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Main.NewText invocation failed; in-game rule notices disabled");
        g_main_new_text_warning_logged = true;
    }
    return false;
}

static bool spawn_rule_npc_at(int32_t x, int32_t y, int32_t npc_type) {
    if (multiplayer_client() || g_spawn_events_this_tick >= MAX_RULE_SPAWNS_PER_TICK ||
        !g_npc_new_npc_method || npc_type <= 0) {
        return false;
    }
    int32_t start = 0;
    int32_t spawned = -1;
    float ai0 = 0.0f;
    float ai1 = 0.0f;
    float ai2 = 0.0f;
    float ai3 = 0.0f;
    float ai4 = 0.0f;
    float ai5 = 0.0f;
    void *args[10] = {&x, &y, &npc_type, &start, &ai0,
                      &ai1, &ai2, &ai3, &ai4, &ai5};
    void *short_args[4] = {&x, &y, &npc_type, &start};
    void **call_args = g_npc_new_npc_arg_count == 4 ? short_args : args;
    if (!patchlib_method_invoke_args(g_npc_new_npc_method, PATCH_NULL,
                                     &spawned, call_args) || spawned < 0) {
        return false;
    }
    ++g_spawn_events_this_tick;
    return true;
}

static bool spawn_rule_npc_near(patch_handle_t instance, int32_t npc_type,
                                int32_t offset_x, int32_t offset_y) {
    int32_t x = 0;
    int32_t y = 0;
    if (!read_npc_position(instance, &x, &y)) return false;
    return spawn_rule_npc_at(x + offset_x, y + offset_y, npc_type);
}

static bool spawn_rule_projectile_at(elite_vector2_t position,
                                     elite_vector2_t velocity, int32_t type,
                                     int32_t damage, int32_t owner) {
    if (multiplayer_client() || !g_projectile_new_projectile_method ||
        type <= 0 || damage <= 0) {
        return false;
    }
    float x = position.x;
    float y = position.y;
    float speed_x = velocity.x;
    float speed_y = velocity.y;
    int32_t projectile_type = type;
    int32_t projectile_damage = damage;
    float knockback = 2.0f;
    int32_t projectile_owner = owner;
    float ai0 = 0.0f;
    float ai1 = 0.0f;
    int32_t spawned = -1;
    void *args[10] = {&x, &y, &speed_x, &speed_y, &projectile_type,
                      &projectile_damage, &knockback, &projectile_owner,
                      &ai0, &ai1};
    if (!patchlib_method_invoke_args(g_projectile_new_projectile_method,
                                     PATCH_NULL, &spawned, args) ||
        spawned < 0) {
        return false;
    }
    return true;
}

static bool spawn_rule_projectile_from_npc(patch_handle_t instance,
                                           int32_t type, float direction_x,
                                           float direction_y, float speed,
                                           int32_t damage) {
    elite_vector2_t position = {0.0f, 0.0f};
    if (!read_vector2_field(g_field_position, instance, &position)) return false;
    int32_t owner = -1;
    return spawn_rule_projectile_at(
        position,
        (elite_vector2_t){direction_x * speed, direction_y * speed}, type,
        damage, owner);
}

static bool rule_npc_is_skeleton(int32_t npc_type) {
    /* Skeleton, Angry Bones and dungeon skeleton variants in the target
     * Terraria build. Unknown types simply do not receive resurrection. */
    static const int32_t skeleton_types[] = {
        21, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90
    };
    for (size_t i = 0; i < sizeof(skeleton_types) / sizeof(skeleton_types[0]);
         ++i) {
        if (npc_type == skeleton_types[i]) return true;
    }
    return false;
}

static bool player_is_low_health(int32_t player_index) {
    patch_handle_t player = NULL;
    int32_t life = 0;
    int32_t life_max = 0;
    if (!get_player_instance(player_index, &player) ||
        !read_i32(g_player_stat_life_field, player, &life) ||
        !read_i32(g_player_stat_life_max_field, player, &life_max) ||
        life_max <= 0) {
        return false;
    }
    return (int64_t)life * 100 < (int64_t)life_max * 30;
}

static void apply_player_slow(int32_t player_index, float multiplier) {
    patch_handle_t player = NULL;
    elite_vector2_t velocity = {0.0f, 0.0f};
    if (!get_player_instance(player_index, &player) ||
        !read_vector2_field(g_player_velocity_field, player, &velocity)) {
        return;
    }
    velocity.x *= multiplier;
    if (float_abs(velocity.y) < 0.5f) velocity.y = 0.0f;
    (void)write_vector2_field(g_player_velocity_field, player, &velocity);
}

static void apply_rule_damage(patch_handle_t instance, size_t index,
                              terrain_rule_t terrain, int32_t player_index) {
    int32_t base_damage = is_elite_instance(instance)
                              ? g_elite_base_damage[index]
                              : g_rule_base_damage[index];
    if (base_damage <= 0) return;

    float multiplier = 1.0f;
    switch (terrain) {
        case TERRAIN_RULE_DESERT:
        case TERRAIN_RULE_UNDERGROUND_DESERT:
            multiplier *= 1.10f;
            break;
        case TERRAIN_RULE_OCEAN:
            multiplier *= 1.15f;
            break;
        case TERRAIN_RULE_UNDERWORLD:
            multiplier *= 1.20f;
            break;
        case TERRAIN_RULE_POST_PLANTERA_JUNGLE:
            multiplier *= 1.25f;
            break;
        case TERRAIN_RULE_CRIMSON:
            multiplier *= 1.08f;
            break;
        default:
            break;
    }
    if (global_rule_active(GLOBAL_RULE_LOW_HEALTH_DAMAGE) &&
        player_index >= 0 && player_is_low_health(player_index)) {
        multiplier *= 1.25f;
    }
    if (g_rule_boss_enraged[index]) multiplier *= 1.50f;
    (void)write_i32(g_field_damage, instance,
                    scaled_i32(base_damage, multiplier));
}

static void apply_terrain_rule(patch_handle_t instance, size_t index,
                               terrain_rule_t terrain, int32_t player_index) {
    if (!is_rule_instance(instance) || terrain <= TERRAIN_RULE_NONE ||
        terrain >= TERRAIN_RULE_COUNT) return;

    apply_rule_damage(instance, index, terrain, player_index);

    elite_vector2_t velocity = {0.0f, 0.0f};
    bool has_velocity = read_vector2_field(g_field_velocity, instance, &velocity);
    float speed_multiplier = 1.0f;
    if (global_rule_active(GLOBAL_RULE_NIGHT_SPEED) &&
        main_bool_field(g_main_day_time_field, true) == false) {
        speed_multiplier *= 1.25f;
    }
    switch (terrain) {
        case TERRAIN_RULE_DESERT:
        case TERRAIN_RULE_UNDERGROUND_DESERT:
            speed_multiplier *= terrain == TERRAIN_RULE_UNDERGROUND_DESERT
                                    ? 1.40f : 1.25f;
            break;
        case TERRAIN_RULE_OCEAN:
            speed_multiplier *= 1.20f;
            break;
        case TERRAIN_RULE_SPIDER:
            speed_multiplier *= 1.15f;
            if (player_index >= 0 && g_elite_ai_ticks[index] % 30u == 0u) {
                apply_player_slow(player_index, 0.82f);
            }
            break;
        case TERRAIN_RULE_SNOW:
            if (main_bool_field(g_main_snow_storm_field, false)) {
                speed_multiplier *= 1.15f;
            }
            break;
        case TERRAIN_RULE_SPACE:
            speed_multiplier *= 1.22f;
            if (has_velocity) velocity.y *= 1.35f;
            break;
        case TERRAIN_RULE_UNDERWORLD:
        case TERRAIN_RULE_POST_PLANTERA_JUNGLE:
            speed_multiplier *= 1.10f;
            break;
        default:
            break;
    }
    if (has_velocity && speed_multiplier != 1.0f) {
        velocity.x *= speed_multiplier;
        velocity.y *= speed_multiplier;
        (void)write_vector2_field(g_field_velocity, instance, &velocity);
    }

    if (terrain == TERRAIN_RULE_FOREST &&
        !main_bool_field(g_main_day_time_field, true) &&
        g_elite_ai_ticks[index] % TERRAIN_REGEN_INTERVAL == 0u) {
        int32_t life = 0;
        int32_t life_max = 0;
        if (read_i32(g_field_life, instance, &life) &&
            read_i32(g_field_life_max, instance, &life_max) && life > 0 &&
            life_max > 0) {
            int32_t regen = life_max / 60;
            if (regen < 1) regen = 1;
            if (life > INT32_MAX - regen) life = INT32_MAX;
            else life += regen;
            if (life > life_max) life = life_max;
            (void)write_i32(g_field_life, instance, life);
        }
    }

    if (g_elite_ai_ticks[index] % TERRAIN_ACTION_INTERVAL != 0u) return;
    int32_t action_damage = g_rule_base_damage[index] > 0
                                ? scaled_i32(g_rule_base_damage[index], 0.35f)
                                : 8;
    switch (terrain) {
        case TERRAIN_RULE_DESERT:
        case TERRAIN_RULE_UNDERGROUND_DESERT:
            (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_SAND,
                                                  1.0f, -0.15f, 6.0f,
                                                  action_damage);
            (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_SAND,
                                                  1.0f, 0.15f, 6.0f,
                                                  action_damage);
            break;
        case TERRAIN_RULE_SNOW:
        case TERRAIN_RULE_ICE_CAVE:
            (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_ICE,
                                                  0.0f, -1.0f, 5.0f,
                                                  action_damage);
            if (player_index >= 0) {
                apply_player_slow(
                    player_index,
                    main_bool_field(g_main_snow_storm_field, false) ? 0.75f
                                                                     : 0.90f);
            }
            break;
        case TERRAIN_RULE_CORRUPTION:
            (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_CURSED,
                                                  1.0f, -0.20f, 5.5f,
                                                  action_damage);
            break;
        case TERRAIN_RULE_HALLOW:
        case TERRAIN_RULE_POST_PLANTERA_JUNGLE:
            (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_ICE,
                                                  0.85f, -0.35f, 5.0f,
                                                  action_damage);
            (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_ICE,
                                                  0.85f, 0.35f, 5.0f,
                                                  action_damage);
            break;
        case TERRAIN_RULE_UNDERWORLD:
        case TERRAIN_RULE_TEMPLE:
            (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_FIRE,
                                                  1.0f, -0.20f, 5.5f,
                                                  action_damage);
            break;
        case TERRAIN_RULE_OCEAN:
            if (g_tide_cooldown == 0) {
                (void)spawn_rule_npc_near(instance, RULE_NPC_VULTURE, 160, -80);
                (void)spawn_rule_npc_near(instance, RULE_NPC_VULTURE, -160, -80);
                g_tide_cooldown = 180;
            }
            break;
        case TERRAIN_RULE_UNDERGROUND:
        case TERRAIN_RULE_CAVE:
            if (terrain == TERRAIN_RULE_CAVE ||
                global_rule_active(GLOBAL_RULE_UNDERGROUND_SURGE)) {
                (void)spawn_rule_npc_near(instance, RULE_NPC_CAVE_BAT, 120, -40);
            }
            break;
        case TERRAIN_RULE_SPIDER:
            if (has_velocity && velocity.y > -1.0f) {
                velocity.y = -7.0f;
                (void)write_vector2_field(g_field_velocity, instance, &velocity);
            }
            break;
        case TERRAIN_RULE_GLOWING_MUSHROOM:
        case TERRAIN_RULE_METEOR:
            (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_FIRE,
                                                  0.0f, 1.0f, 3.5f,
                                                  action_damage);
            break;
        case TERRAIN_RULE_CRIMSON:
            if (read_i32(g_field_life, instance, &action_damage)) {
                int32_t max_life = 0;
                (void)read_i32(g_field_life_max, instance, &max_life);
                action_damage += max_life / 120;
                if (max_life > 0 && action_damage > max_life)
                    action_damage = max_life;
                (void)write_i32(g_field_life, instance, action_damage);
            }
            break;
        default:
            break;
    }

    if (global_rule_active(GLOBAL_RULE_ELITE_BARRAGE) &&
        is_elite_instance(instance)) {
        (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_FIRE,
                                              1.0f, -0.35f, 5.5f,
                                              action_damage);
    }
    if (global_rule_active(GLOBAL_RULE_CRIT_SHOCKWAVE) &&
        is_elite_instance(instance)) {
        /* The native damage/crit callback is not exported consistently on
         * Android. A periodic three-way pulse preserves the intended risk
         * without replacing Terraria's strike calculation. */
        (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_FIRE,
                                              0.70f, -0.70f, 4.0f,
                                              action_damage);
        (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_FIRE,
                                              1.00f, 0.00f, 4.0f,
                                              action_damage);
        (void)spawn_rule_projectile_from_npc(instance, RULE_PROJECTILE_FIRE,
                                              0.70f, 0.70f, 4.0f,
                                              action_damage);
    }
}

static void handle_rule_death(patch_handle_t instance) {
    if (!instance || !is_elite_instance(instance)) return;
    size_t index = rule_instance_index(instance);
    if (index >= PROCESSED_INSTANCE_LIMIT || g_rule_death_handled[index]) {
        return;
    }
    g_rule_death_handled[index] = true;

    bool boss = false;
    (void)read_bool(g_field_boss, instance, &boss);
    int32_t npc_type = g_rule_npc_types[index];
    int32_t player = target_player_index(instance);
    terrain_rule_t terrain = terrain_rule_for_player(player);

    if (!boss) {
        ++g_kill_count;
        if (global_rule_active(GLOBAL_RULE_TIDE) &&
            g_kill_count % 100u == 0u && g_tide_cooldown == 0) {
            (void)spawn_rule_npc_near(instance, RULE_NPC_BLUE_SLIME, 180, -40);
            (void)spawn_rule_npc_near(instance, RULE_NPC_BLUE_SLIME, -180, -40);
            (void)spawn_rule_npc_near(instance, RULE_NPC_BLUE_SLIME, 0, -120);
            g_tide_cooldown = 300;
            ELITE_LOG(MOD_LOG_LEVEL_INFO,
                      "Global enemy tide triggered: kills=%u",
                      (unsigned)g_kill_count);
        }
        if (global_rule_active(GLOBAL_RULE_SPLIT) && random_percent() < 20) {
            (void)spawn_rule_npc_near(instance, npc_type, 72, -24);
        }
    }

    if (terrain == TERRAIN_RULE_JUNGLE ||
        terrain == TERRAIN_RULE_POST_PLANTERA_JUNGLE) {
        if (random_percent() < 30) {
            (void)spawn_rule_npc_near(instance, RULE_NPC_HORNET, 64, -48);
        }
    } else if (terrain == TERRAIN_RULE_CAVE ||
               terrain == TERRAIN_RULE_UNDERGROUND ||
               terrain == TERRAIN_RULE_UNDERGROUND_DESERT) {
        if (random_percent() < 20) {
            (void)spawn_rule_npc_near(instance, RULE_NPC_CAVE_BAT, -72, -32);
        }
    }

    if (terrain == TERRAIN_RULE_GLOWING_MUSHROOM ||
        terrain == TERRAIN_RULE_METEOR) {
        int32_t damage = g_rule_base_damage[index] > 0
                             ? scaled_i32(g_rule_base_damage[index], 0.30f)
                             : 8;
        (void)spawn_rule_projectile_from_npc(
            instance,
            terrain == TERRAIN_RULE_METEOR ? RULE_PROJECTILE_FIRE
                                           : RULE_PROJECTILE_ICE,
            0.0f, -1.0f, 2.5f, damage);
    }
}

static void apply_elite_profile(patch_handle_t instance) {
    if (!instance) return;

    /* SetDefaults resets the object to a new vanilla NPC, even when Terraria
     * reuses exactly the same object pointer.  Invalidate the previous spawn's
     * name/AI/reward state before evaluating the new spawn. */
    clear_elite_instance(instance);

    int32_t npc_type = 0;
    int32_t life_max = 0;
    int32_t base_damage = 0;
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

    /* Do not register ordinary NPCs here. SetDefaults is also used by world
     * generation and by several internal templates; keeping the mutation
     * layer limited to successful elite conversions prevents it from
     * changing Terraria's vanilla spawn/AI path. */
    (void)read_i32(g_field_damage, instance, &base_damage);
    if (read_bool(g_field_boss, instance, &value) && value) return;

    int profile_mode_value = current_world_mode();
    if (!elite_should_spawn(profile_mode_value)) return;

    remember_rule_instance(instance, npc_type, base_damage, true);

    elite_progress_t progress = current_progress();
    elite_world_mode_t profile_mode = (elite_world_mode_t)profile_mode_value;
    elite_profile_t profile = make_profile(progress, profile_mode);
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
    {
        size_t state_slot = elite_instance_index(instance);
        if (state_slot < PROCESSED_INSTANCE_LIMIT) {
            g_rule_npc_types[state_slot] = npc_type;
            g_rule_base_damage[state_slot] =
                base_damage > 0 ? base_damage : elite_damage;
        }
    }
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

/* NPCLoot runs after Terraria has processed the original loot. Rare elites
 * add one random current-progress vanilla item; legendary elites always add
 * exactly one crate: a 70% common crate or a 30% environment crate. The
 * instance guard prevents a repeated NPCLoot call from duplicating rewards. */
static void npc_loot_postfix(patch_handle_t instance, void **args, void *result,
                             const patch_method_signature_t *sig_info) {
    (void)args;
    (void)result;
    (void)sig_info;
    if (!instance) return;
    handle_rule_death(instance);
    if (!is_elite_instance(instance) || already_rewarded(instance)) return;

    elite_rank_t rank = elite_rank_for_instance(instance);
    if (rank != ELITE_RARE && rank != ELITE_LEGENDARY) return;

    remember_rewarded(instance);
    if (!reward_drop_allowed()) return;

    elite_progress_t progress = current_progress();
    if (rank == ELITE_RARE) {
        int32_t item_type = 0;
        int32_t item_stack = 0;
        if (select_rare_progress_reward(progress, &item_type, &item_stack) &&
            spawn_vanilla_reward(instance, item_type, item_stack)) {
            ++g_rare_reward_count;
            ELITE_LOG(
                MOD_LOG_LEVEL_INFO,
                "Rare progress reward dropped: item=%d stack=%d progress=%s total=%lu",
                (int)item_type, (int)item_stack, progress_name(progress),
                g_rare_reward_count);
        } else {
            ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                      "Rare progress reward could not be spawned: progress=%s",
                      progress_name(progress));
        }
        return;
    }

    int32_t item_type = 0;
    const char *crate_kind = NULL;
    int roll = random_percent();
    if (roll < LEGENDARY_COMMON_CRATE_CHANCE_PERCENT) {
        item_type = legendary_common_crate(progress);
        crate_kind = progress == PROGRESS_PRE_HARDMODE ? "golden" : "titanium";
    } else {
        item_type = current_environment_crate(progress, target_player_index(instance));
        crate_kind = "environment";
    }
    if (spawn_vanilla_reward(instance, item_type, 1)) {
        ++g_legendary_reward_count;
        ELITE_LOG(
            MOD_LOG_LEVEL_INFO,
            "Legendary crate dropped: kind=%s item=%d distribution=%d%%/%d%% progress=%s total=%lu",
            crate_kind, (int)item_type, LEGENDARY_COMMON_CRATE_CHANCE_PERCENT,
            LEGENDARY_ENVIRONMENT_CRATE_CHANCE_PERCENT, progress_name(progress),
            g_legendary_reward_count);
    } else {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Legendary crate could not be spawned: kind=%s item=%d progress=%s",
                  crate_kind, (int)item_type, progress_name(progress));
    }
}

static bool npc_check_dead_prefix(patch_handle_t instance, void **args,
                                  const patch_method_signature_t *sig_info,
                                  void *result) {
    (void)args;
    if (!instance || !is_elite_instance(instance)) return true;
    size_t index = rule_instance_index(instance);
    if (index >= PROCESSED_INSTANCE_LIMIT || g_rule_revived[index]) return true;
    if (terrain_rule_for_player(target_player_index(instance)) !=
            TERRAIN_RULE_DUNGEON ||
        !rule_npc_is_skeleton(g_rule_npc_types[index]) ||
        random_percent() >= 35) {
        return true;
    }

    int32_t life_max = 0;
    if (!read_i32(g_field_life_max, instance, &life_max) || life_max <= 0) {
        return true;
    }
    int32_t revived_life = scaled_i32(life_max, 0.45f);
    g_rule_revived[index] = true;
    (void)write_i32(g_field_life, instance, revived_life);
    request_npc_net_update(instance);
    ELITE_LOG(MOD_LOG_LEVEL_INFO, "Dungeon skeleton revived: type=%d life=%d",
              (int)g_rule_npc_types[index], (int)revived_life);

    /* CheckDead is a bool in some exports and void in others. When it is a
     * bool, false tells the caller that the death transition was handled. */
    if (sig_info && sig_info->return_type == PATCH_BOOL && result) {
        *(bool *)result = false;
    }
    return false;
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

static patch_handle_t discover_bool_getter(patch_handle_t type,
                                           const char *name) {
    patch_handle_t getter = NULL;
    patch_handle_t property = patchlib_type_get_property(type, name);
    if (property && patchlib_is_valid(property)) {
        getter = patchlib_property_get_get_method(property);
    }
    if (!getter || !patchlib_is_valid(getter)) {
        char getter_name[96];
        (void)snprintf(getter_name, sizeof(getter_name), "get_%s", name);
        getter = patchlib_type_get_method(type, getter_name);
    }
    if (!getter || !patchlib_is_valid(getter)) return NULL;

    patch_method_signature_t sig = {0};
    if (!patchlib_method_get_signature(getter, &sig)) return NULL;
    bool supported = sig.is_instance && sig.return_type == PATCH_BOOL &&
                     tefstd_vector_size(&sig.arg_types) == 0;
    patchlib_method_signature_free(&sig);
    return supported ? getter : NULL;
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
        g_main_world_name_field = patchlib_type_get_field(main_type, "worldName");
        g_main_world_id_field = patchlib_type_get_field(main_type, "worldID");
        g_main_game_menu_field = patchlib_type_get_field(main_type, "gameMenu");
        g_main_day_time_field = patchlib_type_get_field(main_type, "dayTime");
        g_main_snow_storm_field = patchlib_type_get_field(main_type, "snowMoon");
        g_main_update_count_field = patchlib_type_get_field(main_type,
                                                            "GameUpdateCount");
        if (!g_main_update_count_field ||
            !patchlib_is_valid(g_main_update_count_field)) {
            g_main_update_count_field = patchlib_type_get_field(
                main_type, "gameUpdateCount");
        }
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
        g_player_zone_forest_field = patchlib_type_get_field(player_type, "ZoneForest");
        g_player_zone_rock_layer_field = patchlib_type_get_field(
            player_type, "ZoneRockLayerHeight");
        g_player_zone_dirt_layer_field = patchlib_type_get_field(
            player_type, "ZoneDirtLayerHeight");
        g_player_zone_glowshroom_field = patchlib_type_get_field(
            player_type, "ZoneGlowshroom");
        g_player_zone_spider_field = patchlib_type_get_field(player_type, "ZoneSpider");
        g_player_zone_meteor_field = patchlib_type_get_field(player_type, "ZoneMeteor");
        g_player_zone_temple_field = patchlib_type_get_field(
            player_type, "ZoneLihzhardian");
        g_player_zone_ice_field = patchlib_type_get_field(player_type, "ZoneSnow");
        g_player_zone_getters[ZONE_GETTER_DUNGEON] =
            discover_bool_getter(player_type, "ZoneDungeon");
        g_player_zone_getters[ZONE_GETTER_CORRUPT] =
            discover_bool_getter(player_type, "ZoneCorrupt");
        g_player_zone_getters[ZONE_GETTER_CRIMSON] =
            discover_bool_getter(player_type, "ZoneCrimson");
        g_player_zone_getters[ZONE_GETTER_JUNGLE] =
            discover_bool_getter(player_type, "ZoneJungle");
        g_player_zone_getters[ZONE_GETTER_SNOW] =
            discover_bool_getter(player_type, "ZoneSnow");
        g_player_zone_getters[ZONE_GETTER_DESERT] =
            discover_bool_getter(player_type, "ZoneDesert");
        g_player_zone_getters[ZONE_GETTER_BEACH] =
            discover_bool_getter(player_type, "ZoneBeach");
        g_player_zone_getters[ZONE_GETTER_UNDERWORLD] =
            discover_bool_getter(player_type, "ZoneUnderworldHeight");
        g_player_zone_getters[ZONE_GETTER_HALLOW] =
            discover_bool_getter(player_type, "ZoneHallow");
        g_player_zone_getters[ZONE_GETTER_SKY] =
            discover_bool_getter(player_type, "ZoneSkyHeight");
        g_player_zone_getters[ZONE_GETTER_FOREST] =
            discover_bool_getter(player_type, "ZoneForest");
        g_player_zone_getters[ZONE_GETTER_ROCK] =
            discover_bool_getter(player_type, "ZoneRockLayerHeight");
        g_player_zone_getters[ZONE_GETTER_DIRT] =
            discover_bool_getter(player_type, "ZoneDirtLayerHeight");
        g_player_zone_getters[ZONE_GETTER_GLOWSHROOM] =
            discover_bool_getter(player_type, "ZoneGlowshroom");
        g_player_zone_getters[ZONE_GETTER_SPIDER] =
            discover_bool_getter(player_type, "ZoneSpider");
        g_player_zone_getters[ZONE_GETTER_METEOR] =
            discover_bool_getter(player_type, "ZoneMeteor");
        g_player_zone_getters[ZONE_GETTER_TEMPLE] =
            discover_bool_getter(player_type, "ZoneLihzhardian");
        g_player_velocity_field = patchlib_type_get_field(player_type, "velocity");
        g_player_stat_life_field = patchlib_type_get_field(player_type, "statLife");
        g_player_stat_life_max_field = patchlib_type_get_field(player_type,
                                                               "statLifeMax2");
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

static void discover_rule_factory_api(patch_handle_t npc) {
    const int candidate_counts[2] = {4, 10};
    for (size_t candidate_index = 0; candidate_index < 2; ++candidate_index) {
        patch_handle_t new_npc = patchlib_type_get_method_by_param_count(
            npc, "NewNPC", candidate_counts[candidate_index]);
        if (!new_npc || !patchlib_is_valid(new_npc)) continue;
        patch_method_signature_t sig = {0};
        if (patchlib_method_get_signature(new_npc, &sig) &&
            !sig.is_instance && sig.return_type == PATCH_INT32 &&
            tefstd_vector_size(&sig.arg_types) == (size_t)candidate_counts[candidate_index] &&
            signature_arg_is(&sig, 0, PATCH_INT32) &&
            signature_arg_is(&sig, 1, PATCH_INT32) &&
            signature_arg_is(&sig, 2, PATCH_INT32) &&
            signature_arg_is(&sig, 3, PATCH_INT32) &&
            (candidate_counts[candidate_index] == 4 ||
             (signature_arg_is(&sig, 4, PATCH_FLOAT) &&
              signature_arg_is(&sig, 5, PATCH_FLOAT) &&
              signature_arg_is(&sig, 6, PATCH_FLOAT) &&
              signature_arg_is(&sig, 7, PATCH_FLOAT) &&
              signature_arg_is(&sig, 8, PATCH_FLOAT) &&
              signature_arg_is(&sig, 9, PATCH_FLOAT)))) {
            g_npc_new_npc_method = new_npc;
            g_npc_new_npc_arg_count = candidate_counts[candidate_index];
            ELITE_LOG(MOD_LOG_LEVEL_INFO,
                      "NPC.NewNPC factory available for split/tide rules: params=%d",
                      g_npc_new_npc_arg_count);
        }
        patchlib_method_signature_free(&sig);
        if (g_npc_new_npc_method) break;
    }

    patch_handle_t projectile_type = patchlib_type_get_type(
        "Terraria", "Projectile");
    if (projectile_type && patchlib_is_valid(projectile_type)) {
        patch_handle_t new_projectile =
            patchlib_type_get_method_by_param_count(projectile_type,
                                                    "NewProjectile", 10);
        if (new_projectile && patchlib_is_valid(new_projectile)) {
            patch_method_signature_t sig = {0};
            if (patchlib_method_get_signature(new_projectile, &sig) &&
                !sig.is_instance && sig.return_type == PATCH_INT32 &&
                tefstd_vector_size(&sig.arg_types) == 10 &&
                signature_arg_is(&sig, 0, PATCH_FLOAT) &&
                signature_arg_is(&sig, 1, PATCH_FLOAT) &&
                signature_arg_is(&sig, 2, PATCH_FLOAT) &&
                signature_arg_is(&sig, 3, PATCH_FLOAT) &&
                signature_arg_is(&sig, 4, PATCH_INT32) &&
                signature_arg_is(&sig, 5, PATCH_INT32) &&
                signature_arg_is(&sig, 6, PATCH_FLOAT) &&
                signature_arg_is(&sig, 7, PATCH_INT32) &&
                signature_arg_is(&sig, 8, PATCH_FLOAT) &&
                signature_arg_is(&sig, 9, PATCH_FLOAT)) {
                g_projectile_new_projectile_method = new_projectile;
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "Projectile.NewProjectile factory available for terrain rules");
            }
            patchlib_method_signature_free(&sig);
        }
    }
}

static void discover_death_api(patch_handle_t npc) {
    const char *names[2] = {"CheckDead", "checkDead"};
    for (size_t i = 0; i < 2 && g_death_hook_count < DEATH_HOOK_LIMIT; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            npc, names[i], 0);
        if (!method || !patchlib_is_valid(method)) continue;
        patch_method_signature_t sig = {0};
        if (!patchlib_method_get_signature(method, &sig)) continue;
        bool supported = sig.is_instance &&
                         (sig.return_type == PATCH_BOOL ||
                          sig.return_type == PATCH_VOID) &&
                         tefstd_vector_size(&sig.arg_types) == 0;
        if (supported) {
            patch_hook_id_t hook_id = patchlib_install_prepost_hook(
                method, npc_check_dead_prefix, NULL);
            if (hook_id != PATCH_HOOK_INVALID_ID) {
                g_death_hooks[g_death_hook_count++] = hook_id;
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "NPC death hook installed: %s", names[i]);
                patchlib_method_signature_free(&sig);
                return;
            }
        }
        patchlib_method_signature_free(&sig);
    }
    ELITE_LOG(MOD_LOG_LEVEL_WARNING,
              "NPC CheckDead hook unavailable; dungeon resurrection disabled");
}

static void chest_open_postfix(patch_handle_t instance, void **args,
                               void *result,
                               const patch_method_signature_t *sig_info) {
    (void)instance;
    (void)result;
    initialize_world_rules();
    if (!args || !sig_info || !global_rule_active(GLOBAL_RULE_EXTRA_CHEST) ||
        tefstd_vector_size(&sig_info->arg_types) < 2 || !args[0] || !args[1] ||
        random_percent() >= 20) return;

    int32_t tile_x = *(int32_t *)args[0];
    int32_t tile_y = *(int32_t *)args[1];
    int32_t item_type = 0;
    int32_t item_stack = 0;
    if (!select_rare_progress_reward(current_progress(), &item_type,
                                     &item_stack)) return;
    if (spawn_vanilla_reward_at(tile_x * 16, tile_y * 16, item_type,
                                item_stack)) {
        ELITE_LOG(MOD_LOG_LEVEL_INFO,
                  "Global chest bonus spawned: item=%d stack=%d",
                  (int)item_type, (int)item_stack);
    }
}

static void discover_chest_api(void) {
    patch_handle_t chest = patchlib_type_get_type("Terraria", "Chest");
    if (!chest || !patchlib_is_valid(chest)) return;
    const char *names[2] = {"OpenChest", "Open"};
    for (size_t i = 0; i < 2; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            chest, names[i], 3);
        if (!method || !patchlib_is_valid(method)) continue;
        patch_method_signature_t sig = {0};
        if (!patchlib_method_get_signature(method, &sig)) continue;
        bool supported = !sig.is_instance && sig.return_type == PATCH_VOID &&
                         tefstd_vector_size(&sig.arg_types) == 3 &&
                         signature_arg_is(&sig, 0, PATCH_INT32) &&
                         signature_arg_is(&sig, 1, PATCH_INT32);
        if (supported) {
            patch_hook_id_t hook_id = patchlib_install_prepost_hook(
                method, NULL, chest_open_postfix);
            if (hook_id != PATCH_HOOK_INVALID_ID) {
                g_chest_open_method = method;
                g_chest_hooks[g_chest_hook_count++] = hook_id;
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "Chest open hook installed: %s", names[i]);
                patchlib_method_signature_free(&sig);
                return;
            }
        }
        patchlib_method_signature_free(&sig);
    }
    ELITE_LOG(MOD_LOG_LEVEL_WARNING,
              "Chest open hook unavailable; chest bonus rule will be inert");
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

/* Main.NewText is the vanilla chat/toast path. Resolve only signatures that
 * are safe to invoke through the public argument-array API. One-argument
 * overloads use Terraria's default white text; the four-argument overload is
 * preferred when it exposes byte or int RGB parameters. */
static void discover_main_text_api(patch_handle_t main_type) {
    const int arg_counts[2] = {4, 1};
    for (size_t i = 0; i < 2; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            main_type, "NewText", arg_counts[i]);
        if (!method || !patchlib_is_valid(method)) continue;

        patch_method_signature_t sig = {0};
        if (!patchlib_method_get_signature(method, &sig)) continue;
        bool supported = !sig.is_instance && sig.return_type == PATCH_VOID &&
                         tefstd_vector_size(&sig.arg_types) ==
                             (size_t)arg_counts[i] &&
                         signature_arg_is_text(&sig, 0);
        patch_type_t color_type = PATCH_UINT8;
        if (supported && arg_counts[i] == 4) {
            bool byte_color = signature_arg_is(&sig, 1, PATCH_UINT8) &&
                              signature_arg_is(&sig, 2, PATCH_UINT8) &&
                              signature_arg_is(&sig, 3, PATCH_UINT8);
            bool int_color = signature_arg_is(&sig, 1, PATCH_INT32) &&
                             signature_arg_is(&sig, 2, PATCH_INT32) &&
                             signature_arg_is(&sig, 3, PATCH_INT32);
            supported = byte_color || int_color;
            if (int_color) color_type = PATCH_INT32;
        }
        if (supported) {
            g_main_new_text_method = method;
            g_main_new_text_arg_count = arg_counts[i];
            g_main_new_text_color_type = color_type;
            ELITE_LOG(MOD_LOG_LEVEL_INFO,
                      "Main.NewText notice API available: params=%d colorType=%d",
                      arg_counts[i], (int)color_type);
            patchlib_method_signature_free(&sig);
            return;
        }
        patchlib_method_signature_free(&sig);
    }

    /* Some IL2CPP metadata tables do not expose overloads through the
     * parameter-count lookup even though the name lookup still works. Retry
     * the named method before disabling notices. */
    patch_handle_t fallback = patchlib_type_get_method(main_type, "NewText");
    if (fallback && patchlib_is_valid(fallback)) {
        patch_method_signature_t sig = {0};
        if (patchlib_method_get_signature(fallback, &sig)) {
            size_t count = tefstd_vector_size(&sig.arg_types);
            bool supported = !sig.is_instance && sig.return_type == PATCH_VOID &&
                             (count == 1 || count == 4) &&
                             signature_arg_is_text(&sig, 0);
            patch_type_t color_type = PATCH_UINT8;
            if (supported && count == 4) {
                bool byte_color = signature_arg_is(&sig, 1, PATCH_UINT8) &&
                                  signature_arg_is(&sig, 2, PATCH_UINT8) &&
                                  signature_arg_is(&sig, 3, PATCH_UINT8);
                bool int_color = signature_arg_is(&sig, 1, PATCH_INT32) &&
                                 signature_arg_is(&sig, 2, PATCH_INT32) &&
                                 signature_arg_is(&sig, 3, PATCH_INT32);
                supported = byte_color || int_color;
                if (int_color) color_type = PATCH_INT32;
            }
            if (supported) {
                g_main_new_text_method = fallback;
                g_main_new_text_arg_count = (int)count;
                g_main_new_text_color_type = color_type;
                ELITE_LOG(MOD_LOG_LEVEL_INFO,
                          "Main.NewText named fallback available: params=%d colorType=%d",
                          (int)count, (int)color_type);
                patchlib_method_signature_free(&sig);
                return;
            }
            patchlib_method_signature_free(&sig);
        }
    }
    ELITE_LOG(MOD_LOG_LEVEL_WARNING,
              "Main.NewText notice API not found; rule notices will be log-only");
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

/* Common notification path for both Main.Update and Player.Update.  The
 * Player.Update hook supplies the real Player object, which avoids the
 * fragile Main.player[] array access on Android IL2CPP builds. */
static void update_world_rule_notices_for_instance(patch_handle_t player_instance) {
    if (!player_instance || !patchlib_is_valid(player_instance)) return;
    bool game_menu = false;
    if (valid_field(g_main_game_menu_field, PATCH_BOOL) &&
        read_bool(g_main_game_menu_field, NULL, &game_menu) && game_menu) {
        if (g_player_session_active) {
            g_player_session_active = false;
            g_last_reported_terrain = TERRAIN_RULE_NONE;
            g_world_notice_on_session_enter = true;
        }
        return;
    }

    bool active = true;
    bool dead = false;
    if (valid_field(g_player_active_field, PATCH_BOOL)) {
        (void)read_bool(g_player_active_field, player_instance, &active);
    }
    if (valid_field(g_player_dead_field, PATCH_BOOL)) {
        (void)read_bool(g_player_dead_field, player_instance, &dead);
    }
    if (!active || dead) {
        if (g_player_session_active) {
            g_player_session_active = false;
            g_last_reported_terrain = TERRAIN_RULE_NONE;
            g_world_notice_on_session_enter = true;
        }
        return;
    }

    if (!g_player_session_seen || !g_player_session_active) {
        if (g_player_session_seen) g_world_notice_on_session_enter = true;
        g_player_session_seen = true;
        g_player_session_active = true;
        g_last_reported_terrain = TERRAIN_RULE_NONE;
    }
    initialize_world_rules();
    if (g_world_notice_on_session_enter) {
        announce_global_rules();
        g_world_notice_on_session_enter = false;
    }
    advance_world_rule_clock();
    report_terrain_transition(terrain_rule_for_player_instance(player_instance));
}

/* This remains as the Main.Update/AI compatibility path. */
static void update_world_rule_notices(void) {
    int32_t local_player = -1;
    if (!read_i32(g_main_my_player_field, NULL, &local_player) ||
        local_player < 0 || local_player > 255) {
        local_player = 0;
    }

    patch_handle_t player_instance = NULL;
    if (!get_player_instance(local_player, &player_instance)) return;
    update_world_rule_notices_for_instance(player_instance);
}

/* Terraria normally calls Player.Update(int playerIndex) once per active
 * player every frame.  Hooking this point makes terrain announcements
 * independent from Main.player[] and also independent from whether any NPC
 * exists in the world. */
static void player_update_postfix(patch_handle_t instance, void **args,
                                  void *result,
                                  const patch_method_signature_t *sig_info) {
    (void)result;
    int32_t player_index = -1;
    if (args && sig_info && tefstd_vector_size(&sig_info->arg_types) > 0 &&
        args[0] && signature_arg_is(sig_info, 0, PATCH_INT32)) {
        player_index = *(int32_t *)args[0];
    }

    int32_t local_player = -1;
    (void)read_i32(g_main_my_player_field, NULL, &local_player);
    if (local_player >= 0 && local_player <= 255 &&
        player_index >= 0 && player_index != local_player) {
        return;
    }
    update_world_rule_notices_for_instance(instance);
}

/* NPC.AI is not a reliable world/session clock: it may not run while the
 * world is loading, and a world with no active NPCs has no AI callback at
 * all.  Drive the notification state from Main.Update instead.  The AI
 * callback remains as a compatibility fallback for builds that do not expose
 * Main.Update through metadata. */
static void main_update_postfix(patch_handle_t instance, void **args, void *result,
                                const patch_method_signature_t *sig_info) {
    (void)instance;
    (void)args;
    (void)result;
    (void)sig_info;
    update_world_rule_notices();
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
    /* Initialise from the first safe NPC-AI callback as a fallback. If this
     * callback happens before world data is populated, the stable fallback
     * identity is replaced once Main.worldID/worldName becomes available. */
    update_world_rule_notices();
    initialize_world_rules();
    advance_world_rule_clock();
    if (!instance || !is_elite_instance(instance)) return;

    initialize_world_rules();
    advance_world_rule_clock();

    size_t index = rule_instance_index(instance);
    if (index >= PROCESSED_INSTANCE_LIMIT) return;
    ++g_elite_ai_ticks[index];

    int32_t player = target_player_index(instance);
    if (player >= 0) (void)write_i32(g_field_target, instance, player);

    terrain_rule_t terrain = terrain_rule_for_player(player);
    apply_terrain_rule(instance, index, terrain, player);

    bool boss = false;
    (void)read_bool(g_field_boss, instance, &boss);
    if (boss && global_rule_active(GLOBAL_RULE_BOSS_ENRAGE) &&
        !g_rule_boss_enraged[index]) {
        int32_t life = 0;
        int32_t life_max = 0;
        if (read_i32(g_field_life, instance, &life) &&
            read_i32(g_field_life_max, instance, &life_max) && life_max > 0 &&
            (int64_t)life * 100 <= (int64_t)life_max * 50) {
            g_rule_boss_enraged[index] = true;
            ELITE_LOG(MOD_LOG_LEVEL_INFO,
                      "Global Boss enrage activated: type=%d",
                      (int)g_rule_npc_types[index]);
        }
    }

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

static bool install_main_update_hook(patch_handle_t method,
                                      const char *name) {
    if (!method || !patchlib_is_valid(method) ||
        g_main_update_hook_count >= MAIN_UPDATE_HOOK_LIMIT) {
        return false;
    }

    patch_method_signature_t sig = {0};
    if (!patchlib_method_get_signature(method, &sig)) return false;
    bool supported = sig.is_instance && sig.return_type == PATCH_VOID &&
                     tefstd_vector_size(&sig.arg_types) <= 2;
    if (!supported) {
        patchlib_method_signature_free(&sig);
        return false;
    }

    patch_hook_id_t hook_id = patchlib_install_prepost_hook(
        method, NULL, main_update_postfix);
    if (hook_id == PATCH_HOOK_INVALID_ID) {
        patchlib_method_signature_free(&sig);
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Main update notification hook failed: name=%s", name);
        return false;
    }

    g_main_update_hooks[g_main_update_hook_count++] = hook_id;
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Main update notification hook installed: name=%s params=%d id=%d",
              name, (int)tefstd_vector_size(&sig.arg_types), (int)hook_id);
    patchlib_method_signature_free(&sig);
    return true;
}

/* Resolve the per-frame world update entry point.  Terraria versions expose
 * either Main.Update(GameTime) or a parameterless DoUpdate dispatcher. */
static void discover_main_update_api(patch_handle_t main_type) {
    const char *names[2] = {"Update", "DoUpdate"};
    for (size_t name_index = 0; name_index < 2; ++name_index) {
        for (int args_count = 0; args_count <= 2; ++args_count) {
            patch_handle_t method = patchlib_type_get_method_by_param_count(
                main_type, names[name_index], args_count);
            if (install_main_update_hook(method, names[name_index])) return;
        }
    }

    /* Some IL2CPP metadata tables do not expose overloads through the
     * parameter-count lookup.  Retry the exact names before falling back to
     * the AI-driven compatibility path. */
    for (size_t name_index = 0; name_index < 2; ++name_index) {
        patch_handle_t method = patchlib_type_get_method(
            main_type, names[name_index]);
        if (install_main_update_hook(method, names[name_index])) return;
    }
    ELITE_LOG(MOD_LOG_LEVEL_WARNING,
              "Main update notification hook unavailable; using NPC AI fallback");
}

static bool install_player_update_hook(patch_handle_t method,
                                       const char *name) {
    if (!method || !patchlib_is_valid(method) ||
        g_player_update_hook_count >= PLAYER_UPDATE_HOOK_LIMIT) {
        return false;
    }

    patch_method_signature_t sig = {0};
    if (!patchlib_method_get_signature(method, &sig)) return false;
    size_t arg_count = tefstd_vector_size(&sig.arg_types);
    bool supported = sig.is_instance && sig.return_type == PATCH_VOID &&
                     (arg_count == 0 ||
                      (arg_count == 1 && signature_arg_is(&sig, 0,
                                                           PATCH_INT32)));
    if (!supported) {
        patchlib_method_signature_free(&sig);
        return false;
    }

    patch_hook_id_t hook_id = patchlib_install_prepost_hook(
        method, NULL, player_update_postfix);
    if (hook_id == PATCH_HOOK_INVALID_ID) {
        patchlib_method_signature_free(&sig);
        return false;
    }
    g_player_update_hooks[g_player_update_hook_count++] = hook_id;
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Player update terrain hook installed: name=%s params=%d id=%d",
              name, (int)arg_count, (int)hook_id);
    patchlib_method_signature_free(&sig);
    return true;
}

static void discover_player_update_api(patch_handle_t player_type) {
    if (!player_type || !patchlib_is_valid(player_type)) return;

    /* Current Terraria builds expose Player.Update(int). Keep the parameterless
     * form as a compatibility option for builds that dispatch the index
     * elsewhere. */
    const int arg_counts[2] = {1, 0};
    for (size_t i = 0; i < 2; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            player_type, "Update", arg_counts[i]);
        if (install_player_update_hook(method, "Update")) return;
    }

    patch_handle_t method = patchlib_type_get_method(player_type, "Update");
    if (install_player_update_hook(method, "Update")) return;

    /* Metadata lookup can omit overloads. Enumerate the actual method table so
     * a renamed overload is still found without guessing its address. */
    tefstd_vector_t methods = {0};
    if (!tefstd_vector_init(&methods, sizeof(patch_handle_t))) {
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Player.Update terrain hook method vector unavailable");
        return;
    }
    if (!patchlib_type_get_methods(player_type, true, &methods)) {
        tefstd_vector_destroy(&methods);
        ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                  "Player.Update terrain hook unavailable");
        return;
    }
    size_t method_count = tefstd_vector_size(&methods);
    for (size_t i = 0; i < method_count; ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
        patch_handle_t candidate = entry ? *entry : NULL;
        const char *candidate_name = candidate && patchlib_is_valid(candidate)
                                         ? patchlib_method_get_name(candidate)
                                         : NULL;
        if (candidate_name && strcmp(candidate_name, "Update") == 0 &&
            install_player_update_hook(candidate, candidate_name)) {
            tefstd_vector_destroy(&methods);
            return;
        }
    }
    tefstd_vector_destroy(&methods);
    ELITE_LOG(MOD_LOG_LEVEL_WARNING,
              "Player.Update terrain hook unavailable");
}

static void init_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    srand((unsigned)(time(NULL) ^ (time_t)(uintptr_t)handle));
    ELITE_LOG(MOD_LOG_LEVEL_INFO,
              "Loaded Android Hook probe; resolving NPC spawn API");
    discover_spawn_api();
    patch_handle_t npc = patchlib_type_get_type("Terraria", "NPC");
    if (npc && patchlib_is_valid(npc)) {
        discover_name_api(npc);
        discover_ai_api(npc);
        discover_rule_factory_api(npc);
        discover_death_api(npc);
    }
    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    if (main_type && patchlib_is_valid(main_type)) {
        discover_main_text_api(main_type);
        discover_main_update_api(main_type);
        discover_mouse_text_api(main_type);
        if (npc && patchlib_is_valid(npc)) {
            patch_handle_t item_type = patchlib_type_get_type("Terraria", "Item");
            if (item_type && patchlib_is_valid(item_type)) {
                discover_reward_api(npc, item_type);
                discover_chest_api();
            } else {
                ELITE_LOG(MOD_LOG_LEVEL_WARNING,
                          "Terraria.Item type not found; rare/environment rewards disabled");
            }
        }
    }
    patch_handle_t player_type = patchlib_type_get_type("Terraria", "Player");
    if (player_type && patchlib_is_valid(player_type)) {
        discover_player_update_api(player_type);
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
    for (size_t i = 0; i < g_main_update_hook_count; ++i) {
        patchlib_uninstall_hook(g_main_update_hooks[i]);
    }
    g_main_update_hook_count = 0;
    for (size_t i = 0; i < g_player_update_hook_count; ++i) {
        patchlib_uninstall_hook(g_player_update_hooks[i]);
    }
    g_player_update_hook_count = 0;
    for (size_t i = 0; i < g_loot_hook_count; ++i) {
        patchlib_uninstall_hook(g_loot_hooks[i]);
    }
    g_loot_hook_count = 0;
    for (size_t i = 0; i < g_death_hook_count; ++i) {
        patchlib_uninstall_hook(g_death_hooks[i]);
    }
    g_death_hook_count = 0;
    for (size_t i = 0; i < g_chest_hook_count; ++i) {
        patchlib_uninstall_hook(g_chest_hooks[i]);
    }
    g_chest_hook_count = 0;
    g_main_game_mode_getter = NULL;
    g_main_zenith_world_field = NULL;
    g_main_new_text_method = NULL;
    g_main_new_text_arg_count = 0;
    g_main_new_text_color_type = PATCH_UINT8;
    g_main_new_text_warning_logged = false;
    memset(g_player_zone_getters, 0, sizeof(g_player_zone_getters));
    g_item_new_item_method = NULL;
    g_npc_new_npc_method = NULL;
    g_npc_new_npc_arg_count = 0;
    g_projectile_new_projectile_method = NULL;
    g_chest_open_method = NULL;
    g_elite_instance_count = 0;
    g_rule_slot_cursor = 0;
    memset(g_elite_instances, 0, sizeof(g_elite_instances));
    memset(g_elite_active, 0, sizeof(g_elite_active));
    memset(g_elite_rewarded, 0, sizeof(g_elite_rewarded));
    memset(g_rule_eligible, 0, sizeof(g_rule_eligible));
    memset(g_rule_death_handled, 0, sizeof(g_rule_death_handled));
    memset(g_rule_revived, 0, sizeof(g_rule_revived));
    memset(g_rule_boss_enraged, 0, sizeof(g_rule_boss_enraged));
    memset(g_rule_npc_types, 0, sizeof(g_rule_npc_types));
    memset(g_rule_base_damage, 0, sizeof(g_rule_base_damage));
    g_setdefaults_calls = 0;
    g_elite_count = 0;
    g_rare_reward_count = 0;
    g_legendary_reward_count = 0;
    g_global_rules_initialized = false;
    g_global_rule_count = 0;
    g_dynamic_rule = GLOBAL_RULE_COUNT;
    g_last_game_update_count = UINT64_MAX;
    g_last_fallback_clock = 0;
    g_last_reported_terrain = TERRAIN_RULE_NONE;
    g_player_session_seen = false;
    g_player_session_active = false;
    g_world_notice_on_session_enter = false;
    ELITE_LOG(MOD_LOG_LEVEL_INFO, "Unloaded");
}

static kernel_mod_info_t g_info = {
    .pkg_id = "eternal.future.elitemonsters",
    .version_code = 2026090203,
    .api_version = 1,
    .version = "1.3.7"
};

static kernel_mod_info_t* get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t* create_kernel_mod(void) { return &g_ops; }
