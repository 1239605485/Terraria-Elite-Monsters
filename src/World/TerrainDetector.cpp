#include "../Core/ModuleRegistry.h"

#include "mod_logger.h"

static const em_game_api_t *g_api = nullptr;
static bool g_enabled = false;
static bool g_hook_installed = false;
static int g_last_terrain = -1;

#define EM_TERRAIN_LOG(level, ...) \
    do { \
        if (mod_logger_write) { \
            mod_logger_write((level), "EliteMonsters.Terrain", __VA_ARGS__); \
        } \
    } while (0)

enum terrain_id {
    TERRAIN_UNKNOWN = 0,
    TERRAIN_FOREST,
    TERRAIN_DESERT,
    TERRAIN_SNOW,
    TERRAIN_JUNGLE,
    TERRAIN_OCEAN,
    TERRAIN_CAVE,
    TERRAIN_UNDERGROUND,
    TERRAIN_DUNGEON,
    TERRAIN_CORRUPTION,
    TERRAIN_CRIMSON,
    TERRAIN_HALLOW,
    TERRAIN_UNDERWORLD,
    TERRAIN_SKY,
    TERRAIN_MUSHROOM,
    TERRAIN_SPIDER,
    TERRAIN_METEOR,
    TERRAIN_TEMPLE
};

static bool read_zone(patch_handle_t player, patch_handle_t field) {
    bool value = false;
    return em_field_read_bool(field, player, &value) && value;
}

static bool any_zone_field_available(void) {
    return em_field_valid(g_api->player_zone_dungeon, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_corrupt, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_crimson, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_jungle, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_snow, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_desert, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_beach, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_underworld, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_hallow, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_sky, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_forest, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_rock_layer, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_dirt_layer, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_glowshroom, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_spider, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_meteor, PATCH_BOOL) ||
           em_field_valid(g_api->player_zone_temple, PATCH_BOOL);
}

static int detect_terrain(patch_handle_t player) {
    if (!player || !patchlib_is_valid(player)) return TERRAIN_UNKNOWN;
    if (read_zone(player, g_api->player_zone_temple)) return TERRAIN_TEMPLE;
    if (read_zone(player, g_api->player_zone_spider)) return TERRAIN_SPIDER;
    if (read_zone(player, g_api->player_zone_underworld)) return TERRAIN_UNDERWORLD;
    if (read_zone(player, g_api->player_zone_meteor)) return TERRAIN_METEOR;
    if (read_zone(player, g_api->player_zone_sky)) return TERRAIN_SKY;
    if (read_zone(player, g_api->player_zone_glowshroom)) return TERRAIN_MUSHROOM;
    if (read_zone(player, g_api->player_zone_dungeon)) return TERRAIN_DUNGEON;
    if (read_zone(player, g_api->player_zone_corrupt)) return TERRAIN_CORRUPTION;
    if (read_zone(player, g_api->player_zone_crimson)) return TERRAIN_CRIMSON;
    if (read_zone(player, g_api->player_zone_hallow)) return TERRAIN_HALLOW;
    if (read_zone(player, g_api->player_zone_jungle)) return TERRAIN_JUNGLE;
    if (read_zone(player, g_api->player_zone_snow)) return TERRAIN_SNOW;
    if (read_zone(player, g_api->player_zone_desert)) return TERRAIN_DESERT;
    if (read_zone(player, g_api->player_zone_beach)) return TERRAIN_OCEAN;
    if (read_zone(player, g_api->player_zone_rock_layer)) return TERRAIN_CAVE;
    if (read_zone(player, g_api->player_zone_dirt_layer)) return TERRAIN_UNDERGROUND;
    if (read_zone(player, g_api->player_zone_forest)) return TERRAIN_FOREST;
    return TERRAIN_UNKNOWN;
}

static const char *terrain_name(int terrain) {
    static const char *const names[] = {
        "未知区域", "森林", "沙漠", "雪原", "丛林", "海洋", "洞穴",
        "地下", "地牢", "腐化", "猩红", "神圣", "地狱", "太空",
        "蘑菇地", "蜘蛛洞", "陨石区", "神庙"};
    return terrain >= 0 && terrain < (int)(sizeof(names) / sizeof(names[0]))
               ? names[terrain]
               : names[TERRAIN_UNKNOWN];
}

void em_terrain_detector_initialize(const em_game_api_t *api) {
    g_api = api;
    g_enabled = api && api->player_type_class &&
                patchlib_is_valid(api->player_type_class) &&
                any_zone_field_available();
    g_hook_installed = false;
    g_last_terrain = -1;
    EM_TERRAIN_LOG(g_enabled ? MOD_LOG_LEVEL_INFO : MOD_LOG_LEVEL_WARNING,
                   g_enabled ? "Terrain read-only module ready"
                              : "Terrain read-only module disabled: Player or Zone fields unavailable");
}

bool em_terrain_detector_enabled(void) { return g_enabled; }

void em_terrain_detector_set_hook_installed(bool installed) {
    g_hook_installed = installed;
    if (!installed && g_enabled) {
        g_enabled = false;
        EM_TERRAIN_LOG(MOD_LOG_LEVEL_WARNING,
                       "Terrain read-only module disabled: Player.Update hook unavailable");
    }
}

void em_terrain_detector_update(patch_handle_t instance, void **args, void *result,
                                const patch_method_signature_t *sig_info) {
    (void)args;
    (void)result;
    (void)sig_info;
    if (!g_enabled || !g_hook_installed || !g_api) return;
    int terrain = detect_terrain(instance);
    if (terrain == g_last_terrain) return;
    g_last_terrain = terrain;
    EM_TERRAIN_LOG(MOD_LOG_LEVEL_INFO, "Terrain state changed: id=%d name=%s",
                   terrain, terrain_name(terrain));
}

void em_terrain_detector_shutdown(void) {
    g_enabled = false;
    g_hook_installed = false;
    g_api = nullptr;
    g_last_terrain = -1;
}
