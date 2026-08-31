#include "mod_core.h"
#include "mod_logger.h"
#include "tefkernel/patchlib/type.h"
#include "tefkernel/patchlib/method.h"

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

static int random_percent(void) {
    return rand() % 100;
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
    const char *names[] = { "NewNPC", "SpawnNPC", "SetDefaults" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        patch_handle_t method = patchlib_type_get_method(npc, names[i]);
        if (method && patchlib_is_valid(method)) {
            mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                             "Spawn API candidate: Terraria.NPC.%s (params=%d)",
                             names[i], patchlib_method_get_param_count(method));
        }
    }
}

static void init_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    srand(0x454C4954u);
    mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                     "Loaded Android MVP; resolving NPC spawn API");
    discover_spawn_api();
    (void)elite_should_spawn;
    (void)make_profile;
}

static void cleanup_mod(kernel_mod_handle_t* handle) {
    (void)handle;
    mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters", "Unloaded");
}

static kernel_mod_info_t g_info = {
    .pkg_id = "eternal.future.elitemonsters",
    .version_code = 20260830,
    .api_version = 1,
    .version = "0.1.0"
};

static kernel_mod_info_t* get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t* create_kernel_mod(void) { return &g_ops; }
