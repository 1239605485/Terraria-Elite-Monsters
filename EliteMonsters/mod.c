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
static patch_hook_id_t g_setdefaults_hook = PATCH_HOOK_INVALID_ID;
static unsigned long g_setdefaults_calls = 0;

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
    .version_code = 20260831,
    .api_version = 1,
    .version = "0.2.0"
};

static kernel_mod_info_t* get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t* create_kernel_mod(void) { return &g_ops; }
