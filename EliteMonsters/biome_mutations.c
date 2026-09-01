#include "biome_mutations.h"

#include "elite_core.h"
#include "mod_logger.h"

#include <stdint.h>

enum {
    BIOME_FOREST,
    BIOME_UNDERGROUND,
    BIOME_DESERT,
    BIOME_SNOW,
    BIOME_JUNGLE,
    BIOME_OCEAN,
    BIOME_CORRUPTION,
    BIOME_CRIMSON,
    BIOME_HALLOW,
    BIOME_DUNGEON,
    BIOME_UNDERWORLD,
    BIOME_SKY,
    BIOME_COUNT
};

enum {
    RULE_NIGHT_HASTE = 1u << 0,
    RULE_REINFORCED = 1u << 1,
    RULE_BLOOD_FEAST = 1u << 2,
    RULE_UNSTABLE = 1u << 3,
    RULE_HUNTER = 1u << 4
};

static uint32_t g_rule_mask = 0;
static int32_t g_world_id = -1;
static int g_rules_ready = 0;
static int g_initialized = 0;

static const char* biome_name(int biome) {
    static const char* names[BIOME_COUNT] = {
        "森林", "地下", "沙漠", "雪原", "丛林", "海洋",
        "腐化", "猩红", "神圣", "地牢", "地狱", "天空"
    };
    return biome >= 0 && biome < BIOME_COUNT ? names[biome] : "未知地形";
}

static int rule_count(uint32_t mask) {
    int count = 0;
    for (unsigned bit = 1u; bit <= RULE_HUNTER; bit <<= 1u) {
        if (mask & bit) ++count;
    }
    return count;
}

static void refresh_rules(void) {
    EliteContext* ctx = elite_core_context();
    int32_t world_id = 0;
    bool has_world_id = elite_core_read_i32(ctx->main_world_id, NULL, &world_id);
    if (!has_world_id) world_id = 1;
    if (g_rules_ready && g_world_id == world_id) return;

    uint32_t seed = (uint32_t)world_id * 1103515245u + 12345u;
    g_rule_mask = 0;
    while (rule_count(g_rule_mask) < 3) {
        seed = seed * 1664525u + 1013904223u;
        g_rule_mask |= 1u << ((seed >> 28u) % 5u);
    }
    g_world_id = world_id;
    g_rules_ready = 1;
    mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                     "世界变异规则已生成：worldID=%d mask=0x%X", (int)world_id,
                     (unsigned)g_rule_mask);
}

static int detect_biome(int player_index) {
    EliteContext* ctx = elite_core_context();
    if (elite_core_player_in_zone(player_index, ctx->zone_dungeon)) return BIOME_DUNGEON;
    if (elite_core_player_in_zone(player_index, ctx->zone_underworld)) return BIOME_UNDERWORLD;
    if (elite_core_player_in_zone(player_index, ctx->zone_jungle)) return BIOME_JUNGLE;
    if (elite_core_player_in_zone(player_index, ctx->zone_desert)) return BIOME_DESERT;
    if (elite_core_player_in_zone(player_index, ctx->zone_snow)) return BIOME_SNOW;
    if (elite_core_player_in_zone(player_index, ctx->zone_corrupt)) return BIOME_CORRUPTION;
    if (elite_core_player_in_zone(player_index, ctx->zone_crimson)) return BIOME_CRIMSON;
    if (elite_core_player_in_zone(player_index, ctx->zone_hallow)) return BIOME_HALLOW;
    if (elite_core_player_in_zone(player_index, ctx->zone_beach)) return BIOME_OCEAN;
    if (elite_core_player_in_zone(player_index, ctx->zone_sky)) return BIOME_SKY;

    float x = 0.0f, y = 0.0f;
    if (elite_core_player_state(player_index, &x, &y, NULL, NULL)) {
        if (y > 3000.0f) return BIOME_UNDERGROUND;
        if (y > 900.0f) return BIOME_UNDERGROUND;
    }
    return BIOME_FOREST;
}

static void apply_biome_stats(void* npc, EliteState* state, int biome) {
    EliteContext* ctx = elite_core_context();
    static const int damage_bonus[BIOME_COUNT] = {0, 2, 4, 3, 6, 2, 7, 8, 4, 6, 10, 3};
    static const int defense_bonus[BIOME_COUNT] = {0, 2, 1, 3, 2, 0, 4, 3, 4, 6, 5, 1};
    int32_t damage = state->base_damage + damage_bonus[biome];
    int32_t defense = state->base_defense + defense_bonus[biome];
    if (g_rule_mask & RULE_REINFORCED) defense += 3;
    if (g_rule_mask & RULE_UNSTABLE) damage += 2;
    (void)elite_core_write_i32(ctx->npc_damage, npc, damage);
    (void)elite_core_write_i32(ctx->npc_defense, npc, defense);
    state->biome = biome;
    mod_logger_write(MOD_LOG_LEVEL_DEBUG, "EliteMonsters",
                     "地形规则启用：%s damage+%d defense+%d", biome_name(biome),
                     damage_bonus[biome] + ((g_rule_mask & RULE_UNSTABLE) ? 2 : 0),
                     defense_bonus[biome] + ((g_rule_mask & RULE_REINFORCED) ? 3 : 0));
}

void biome_mutations_on_spawn(void* npc, size_t slot) {
    (void)npc;
    refresh_rules();
    EliteState* state = elite_core_state(slot);
    if (state) state->biome = -1;
}

void biome_mutations_tick(void* npc, size_t slot, int player_index) {
    if (!npc) return;
    refresh_rules();
    EliteState* state = elite_core_state(slot);
    if (!state || !state->active) return;
    int biome = detect_biome(player_index);
    if (state->biome != biome) apply_biome_stats(npc, state, biome);

    EliteContext* ctx = elite_core_context();
    if ((g_rule_mask & RULE_BLOOD_FEAST || biome == BIOME_CRIMSON) &&
        state->ai_ticks % 120u == 0u) {
        int32_t life = 0, life_max = 0;
        if (elite_core_read_i32(ctx->npc_life, npc, &life) &&
            elite_core_read_i32(ctx->npc_life_max, npc, &life_max) && life > 0) {
            int32_t heal = life_max / 100;
            if (heal < 1) heal = 1;
            (void)elite_core_write_i32(ctx->npc_life, npc, life + heal);
        }
    }
}

float biome_mutations_speed_multiplier(int player_index) {
    refresh_rules();
    EliteContext* ctx = elite_core_context();
    bool day = true;
    (void)elite_core_read_bool(ctx->main_day_time, NULL, &day);
    float multiplier = 1.0f;
    if ((g_rule_mask & RULE_NIGHT_HASTE) && !day) multiplier *= 1.20f;
    int biome = detect_biome(player_index);
    if (biome == BIOME_DESERT || biome == BIOME_UNDERWORLD) multiplier *= 1.12f;
    if (biome == BIOME_SNOW) multiplier *= 1.05f;
    if (biome == BIOME_SKY) multiplier *= 1.15f;
    if (g_rule_mask & RULE_HUNTER) multiplier *= 1.05f;
    return multiplier;
}

void biome_mutations_init(void) {
    if (g_initialized) return;
    refresh_rules();
    g_initialized = 1;
}

void biome_mutations_cleanup(void) {
    g_rule_mask = 0;
    g_world_id = -1;
    g_rules_ready = 0;
    g_initialized = 0;
}
