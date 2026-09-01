#include "elite_core.h"

#include "mod_logger.h"
#include "tefkernel/patchlib/property.h"
#include "tefkernel/patchlib/struct/array.h"
#include "tefkernel/patchlib/struct/string.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void biome_mutations_on_spawn(void* npc, size_t slot);

static EliteContext g_ctx;
static EliteState g_states[ELITE_TRACK_LIMIT];
static void* g_processed_instances[ELITE_TRACK_LIMIT];
static size_t g_processed_count = 0;
static size_t g_state_count = 0;
static unsigned long g_elite_count = 0;
static bool g_initialized = false;

/* The Android 1.4.5.6.4 UI overloads are not needed for gameplay. Keep them
 * opt-in until the target build's string/rarity ABI has been verified. */
#define ELITEMONSTERS_ENABLE_UI_HOOKS 0
#define ELITEMONSTERS_ENABLE_SETDEFAULTS_HOOK 0

#define LOG(level, ...) do { if (mod_logger_write) mod_logger_write(level, "EliteMonsters", __VA_ARGS__); } while (0)

static bool valid(patch_handle_t handle) {
    return handle && patchlib_is_valid(handle);
}

static bool method_is_instance(patch_handle_t method, patch_type_t return_type,
                               int parameter_count, bool check_parameter_count) {
    if (!valid(method)) return false;
    patch_method_signature_t signature = {0};
    if (!patchlib_method_get_signature(method, &signature)) return false;
    bool supported = signature.is_instance && signature.return_type == return_type;
    if (check_parameter_count) {
        supported = supported &&
                    (int)tefstd_vector_size(&signature.arg_types) == parameter_count;
    }
    patchlib_method_signature_free(&signature);
    return supported;
}

static bool method_is_static(patch_handle_t method, patch_type_t return_type,
                             int parameter_count) {
    if (!valid(method)) return false;
    patch_method_signature_t signature = {0};
    if (!patchlib_method_get_signature(method, &signature)) return false;
    bool supported = !signature.is_instance && signature.return_type == return_type &&
                     (int)tefstd_vector_size(&signature.arg_types) == parameter_count;
    patchlib_method_signature_free(&signature);
    return supported;
}

bool elite_core_valid_field(patch_handle_t field, patch_type_t type) {
    return valid(field) && patchlib_field_get_type(field) == type;
}

static bool get_value(patch_handle_t field, patch_handle_t instance, void* out) {
    if (!valid(field) || !out) return false;
#if defined(__ANDROID__)
    if (!instance && patchlib_field_is_static(field)) {
        void* raw = patchlib_field_get_pointer(field, NULL);
        if (!raw) return false;
        memcpy(out, raw, patchlib_field_get_size(field));
        return true;
    }
#endif
    patchlib_field_get_value(field, instance, out);
    return true;
}

static bool set_value(patch_handle_t field, patch_handle_t instance, void* value) {
    if (!valid(field) || !value) return false;
#if defined(__ANDROID__)
    if (!instance && patchlib_field_is_static(field)) {
        void* raw = patchlib_field_get_pointer(field, NULL);
        if (!raw) return false;
        memcpy(raw, value, patchlib_field_get_size(field));
        return true;
    }
#endif
    patchlib_field_set_value(field, instance, value);
    return true;
}

bool elite_core_read_i32(patch_handle_t field, patch_handle_t instance, int32_t* out) {
    return out && elite_core_valid_field(field, PATCH_INT32) && get_value(field, instance, out);
}

bool elite_core_read_bool(patch_handle_t field, patch_handle_t instance, bool* out) {
    return out && elite_core_valid_field(field, PATCH_BOOL) && get_value(field, instance, out);
}

bool elite_core_read_float(patch_handle_t field, patch_handle_t instance, float* out) {
    return out && elite_core_valid_field(field, PATCH_FLOAT) && get_value(field, instance, out);
}

bool elite_core_write_i32(patch_handle_t field, patch_handle_t instance, int32_t value) {
    return elite_core_valid_field(field, PATCH_INT32) && set_value(field, instance, &value);
}

bool elite_core_write_float(patch_handle_t field, patch_handle_t instance, float value) {
    return elite_core_valid_field(field, PATCH_FLOAT) && set_value(field, instance, &value);
}

bool elite_core_write_bool(patch_handle_t field, patch_handle_t instance, bool value) {
    return elite_core_valid_field(field, PATCH_BOOL) && set_value(field, instance, &value);
}

bool elite_core_read_vector2(patch_handle_t field, patch_handle_t instance, float* x, float* y) {
    if (!x || !y || !valid(field) || patchlib_field_get_size(field) != 8) return false;
    patch_type_t type = patchlib_field_get_type(field);
    if (type != PATCH_POINTER && type != PATCH_OBJECT) return false;
    float value[2] = {0.0f, 0.0f};
    if (!get_value(field, instance, value)) return false;
    *x = value[0];
    *y = value[1];
    return true;
}

bool elite_core_write_vector2(patch_handle_t field, patch_handle_t instance, float x, float y) {
    if (!valid(field) || patchlib_field_get_size(field) != 8) return false;
    patch_type_t type = patchlib_field_get_type(field);
    if (type != PATCH_POINTER && type != PATCH_OBJECT) return false;
    float value[2] = {x, y};
    return set_value(field, instance, value);
}

EliteContext* elite_core_context(void) { return &g_ctx; }

size_t elite_core_slot(void* instance) {
    if (!instance) return ELITE_TRACK_LIMIT;
    for (size_t i = 0; i < g_state_count; ++i) {
        if (g_states[i].instance == instance) return i;
    }
    return ELITE_TRACK_LIMIT;
}

EliteState* elite_core_state(size_t slot) {
    return slot < g_state_count ? &g_states[slot] : NULL;
}

bool elite_core_is_elite(void* instance) {
    size_t slot = elite_core_slot(instance);
    return slot < g_state_count && g_states[slot].active;
}

static bool processed_instance(void* instance) {
    for (size_t i = 0; i < g_processed_count; ++i) {
        if (g_processed_instances[i] == instance) return true;
    }
    return false;
}

static void remember_processed_instance(void* instance) {
    if (!instance || processed_instance(instance)) return;
    if (g_processed_count < ELITE_TRACK_LIMIT) {
        g_processed_instances[g_processed_count++] = instance;
        return;
    }
    g_processed_instances[g_elite_count % ELITE_TRACK_LIMIT] = instance;
}

void elite_core_clear(void* instance) {
    size_t slot = elite_core_slot(instance);
    if (slot >= g_state_count) return;
    g_states[slot].active = false;
    g_states[slot].rewarded = false;
    g_states[slot].enraged = false;
    g_states[slot].ai_ticks = 0;
    g_states[slot].base_damage = 0;
    g_states[slot].base_defense = 0;
    g_states[slot].biome = -1;
}

void elite_core_mark(void* instance, elite_rank_t rank, int32_t damage, int32_t defense) {
    if (!instance) return;
    size_t slot = elite_core_slot(instance);
    if (slot >= g_state_count) {
        if (g_state_count < ELITE_TRACK_LIMIT) {
            slot = g_state_count++;
            g_states[slot].instance = instance;
        } else {
            slot = g_elite_count % ELITE_TRACK_LIMIT;
            g_states[slot].instance = instance;
        }
    }
    g_states[slot].active = true;
    g_states[slot].rewarded = false;
    g_states[slot].enraged = false;
    g_states[slot].rank = rank;
    g_states[slot].base_damage = damage;
    g_states[slot].base_defense = defense;
    g_states[slot].ai_ticks = 0;
    g_states[slot].biome = -1;
    biome_mutations_on_spawn(instance, slot);
}

elite_rank_t elite_core_rank(void* instance) {
    size_t slot = elite_core_slot(instance);
    return slot < g_state_count && g_states[slot].active ? g_states[slot].rank : ELITE_NORMAL;
}

int elite_core_target_player(void* npc) {
    int32_t target = -1, local = -1, net = 0;
    (void)elite_core_read_i32(g_ctx.npc_target, npc, &target);
    (void)elite_core_read_i32(g_ctx.main_my_player, NULL, &local);
    (void)elite_core_read_i32(g_ctx.main_net_mode, NULL, &net);
    if (net == 0 && local >= 0 && local < 256) return local;
    if (target >= 0 && target < 256) return target;
    return local >= 0 && local < 256 ? local : -1;
}

bool elite_core_player_state(int index, float* x, float* y, int32_t* width, int32_t* height) {
    if (index < 0 || index >= 256 || !x || !y) return false;
    patch_handle_t players = NULL, player = NULL;
    if (!get_value(g_ctx.main_player, NULL, &players) || !valid(players) ||
        (size_t)index >= patchlib_array_length(players) ||
        !patchlib_array_at(players, (size_t)index, &player) || !valid(player)) return false;
    bool active = true, dead = false;
    (void)elite_core_read_bool(g_ctx.player_active, player, &active);
    (void)elite_core_read_bool(g_ctx.player_dead, player, &dead);
    if (!active || dead || !elite_core_read_vector2(g_ctx.player_position, player, x, y)) return false;
    if (width) { *width = 20; (void)elite_core_read_i32(g_ctx.player_width, player, width); }
    if (height) { *height = 40; (void)elite_core_read_i32(g_ctx.player_height, player, height); }
    return true;
}

bool elite_core_player_in_zone(int index, patch_handle_t zone_field) {
    if (!elite_core_valid_field(zone_field, PATCH_BOOL)) return false;
    patch_handle_t players = NULL, player = NULL;
    bool value = false;
    if (!get_value(g_ctx.main_player, NULL, &players) || !valid(players) ||
        index < 0 || (size_t)index >= patchlib_array_length(players) ||
        !patchlib_array_at(players, (size_t)index, &player) || !valid(player)) return false;
    return elite_core_read_bool(zone_field, player, &value) && value;
}

bool elite_core_reward_allowed(void) {
    int32_t net = 0;
    (void)elite_core_read_i32(g_ctx.main_net_mode, NULL, &net);
    return net != 1;
}

static int scaled(int value, float multiplier) {
    double result = (double)value * (double)multiplier;
    if (result < 1.0) return 1;
    if (result > INT_MAX) return INT_MAX;
    return (int)(result + 0.5);
}

static elite_progress_t progress_internal(void) {
    bool hard = false, mech = false, plant = false, golem = false, moon = false;
    (void)elite_core_read_bool(g_ctx.main_hard_mode, NULL, &hard);
    (void)elite_core_read_bool(g_ctx.downed_mech, NULL, &mech);
    (void)elite_core_read_bool(g_ctx.downed_plant, NULL, &plant);
    (void)elite_core_read_bool(g_ctx.downed_golem, NULL, &golem);
    (void)elite_core_read_bool(g_ctx.downed_moonlord, NULL, &moon);
    if (moon) return PROGRESS_ENDGAME;
    if (plant || golem) return PROGRESS_POST_PLANTERA;
    if (mech) return PROGRESS_PRE_PLANTERA;
    return hard ? PROGRESS_HARDMODE_EARLY : PROGRESS_PRE_HARDMODE;
}

elite_progress_t elite_core_progress(void) { return progress_internal(); }

const char* elite_core_progress_name(elite_progress_t progress) {
    static const char* names[] = {"前期", "困难模式前期", "机械 Boss 后", "世纪之花后", "终局"};
    if (progress < PROGRESS_PRE_HARDMODE || progress > PROGRESS_ENDGAME) return names[0];
    return names[progress];
}

static int world_mode(void) {
    int32_t game_mode = 0;
    bool zenith = false;
    if (!elite_core_read_i32(g_ctx.main_game_mode, NULL, &game_mode) && g_ctx.main_game_mode_getter) {
        patchlib_method_invoke_args(g_ctx.main_game_mode_getter, NULL, &game_mode, NULL);
    }
    if (game_mode < 0 || game_mode > 3) game_mode = 0;
    (void)elite_core_read_bool(g_ctx.main_zenith_world, NULL, &zenith);
    return zenith ? 3 : (int)game_mode;
}

static int random_percent(void) { return rand() % 100; }

static void apply_profile(void* npc) {
    if (!npc) return;
    elite_core_clear(npc);
    int32_t type = 0, life_max = 0, life = 0, damage = 0, defense = 0;
    bool value = false;
    if (!elite_core_read_i32(g_ctx.npc_type, npc, &type) ||
        !elite_core_read_i32(g_ctx.npc_life_max, npc, &life_max) || life_max <= 0) return;
    if (elite_core_read_bool(g_ctx.npc_friendly, npc, &value) && value) return;
    if (elite_core_read_bool(g_ctx.npc_town, npc, &value) && value) return;
    if (elite_core_read_bool(g_ctx.npc_boss, npc, &value) && value) return;

    static const int chance[] = {20, 30, 40, 50};
    int mode = world_mode();
    if (random_percent() >= chance[mode]) return;
    elite_progress_t progress = progress_internal();
    int rank_roll = random_percent();
    int rank_index = rank_roll < 5 ? 2 : (rank_roll < 30 ? 1 : 0);
    static const float hp[5][3] = {{1.4f,2.0f,3.0f},{1.7f,2.6f,4.2f},{2.0f,3.4f,5.5f},{2.4f,4.2f,7.0f},{3.0f,5.5f,9.0f}};
    static const float dmg[5][3] = {{1.15f,1.4f,1.8f},{1.35f,1.8f,2.4f},{1.55f,2.15f,3.0f},{1.8f,2.6f,3.8f},{2.1f,3.2f,4.8f}};
    static const int def[5][3] = {{4,8,12},{8,15,24},{12,22,36},{18,32,52},{26,45,75}};
    static const float gold[5][3] = {{10,25,50},{15,40,80},{25,60,130},{40,100,220},{60,150,320}};
    static const float mode_hp[] = {1.0f,1.15f,1.35f,1.60f};
    static const float mode_dmg[] = {1.0f,1.10f,1.25f,1.45f};
    static const int mode_def[] = {0,4,8,12};
    static const float mode_gold[] = {1.0f,1.5f,2.25f,3.25f};
    float hp_mult = hp[progress][rank_index] * mode_hp[mode];
    float dmg_mult = dmg[progress][rank_index] * mode_dmg[mode];
    int defense_bonus = def[progress][rank_index] + mode_def[mode];
    if (!elite_core_read_i32(g_ctx.npc_life, npc, &life)) life = life_max;
    (void)elite_core_read_i32(g_ctx.npc_damage, npc, &damage);
    (void)elite_core_read_i32(g_ctx.npc_defense, npc, &defense);
    (void)elite_core_write_i32(g_ctx.npc_life_max, npc, scaled(life_max, hp_mult));
    (void)elite_core_write_i32(g_ctx.npc_life, npc, scaled(life, hp_mult));
    (void)elite_core_write_i32(g_ctx.npc_damage, npc, scaled(damage, dmg_mult));
    (void)elite_core_write_i32(g_ctx.npc_defense, npc, defense + defense_bonus);
    if (elite_core_valid_field(g_ctx.npc_knockback_resist, PATCH_FLOAT)) {
        (void)elite_core_write_float(g_ctx.npc_knockback_resist, npc,
                                     rank_index == 2 ? 0.0f : 0.9f);
    }
    float scale = 1.0f;
    if (elite_core_valid_field(g_ctx.npc_scale, PATCH_FLOAT)) {
        (void)elite_core_read_float(g_ctx.npc_scale, npc, &scale);
        (void)elite_core_write_float(g_ctx.npc_scale, npc, scale * (1.0f + 0.05f * rank_index));
    }
    float value_float = 0.0f;
    if (elite_core_valid_field(g_ctx.npc_value, PATCH_FLOAT) && elite_core_read_float(g_ctx.npc_value, npc, &value_float)) {
        (void)elite_core_write_float(g_ctx.npc_value, npc, value_float * gold[progress][rank_index] * mode_gold[mode]);
    }
    elite_rank_t rank = rank_index == 2 ? ELITE_LEGENDARY : (rank_index == 1 ? ELITE_RARE : ELITE_NORMAL);
    elite_core_mark(npc, rank, scaled(damage, dmg_mult), defense + defense_bonus);
    ++g_elite_count;
    LOG(MOD_LOG_LEVEL_INFO, "Elite NPC transformed: type=%d rank=%d progress=%s total=%lu",
        (int)type, (int)rank, elite_core_progress_name(progress), g_elite_count);
}

void elite_core_try_apply(void* instance) {
    if (!instance || processed_instance(instance)) return;
    remember_processed_instance(instance);
    if (!elite_core_is_elite(instance)) apply_profile(instance);
}

static void __attribute__((unused)) setdefaults_postfix(
    patch_handle_t instance, void** args, void* result,
    const patch_method_signature_t* sig) {
    (void)args; (void)result; (void)sig;
    apply_profile(instance);
}

static void cache_fields(patch_handle_t npc) {
    g_ctx.npc_type = patchlib_type_get_field(npc, "type");
    g_ctx.npc_position = patchlib_type_get_field(npc, "position");
    g_ctx.npc_life = patchlib_type_get_field(npc, "life");
    g_ctx.npc_life_max = patchlib_type_get_field(npc, "lifeMax");
    g_ctx.npc_damage = patchlib_type_get_field(npc, "damage");
    g_ctx.npc_defense = patchlib_type_get_field(npc, "defense");
    g_ctx.npc_knockback_resist = patchlib_type_get_field(npc, "knockBackResist");
    g_ctx.npc_width = patchlib_type_get_field(npc, "width");
    g_ctx.npc_height = patchlib_type_get_field(npc, "height");
    g_ctx.npc_scale = patchlib_type_get_field(npc, "scale");
    g_ctx.npc_value = patchlib_type_get_field(npc, "value");
    g_ctx.npc_friendly = patchlib_type_get_field(npc, "friendly");
    g_ctx.npc_town = patchlib_type_get_field(npc, "townNPC");
    g_ctx.npc_boss = patchlib_type_get_field(npc, "boss");
    g_ctx.npc_target = patchlib_type_get_field(npc, "target");
    g_ctx.npc_ai_style = patchlib_type_get_field(npc, "aiStyle");
    g_ctx.npc_direction = patchlib_type_get_field(npc, "direction");
    g_ctx.npc_net_update = patchlib_type_get_field(npc, "netUpdate");
    g_ctx.npc_no_gravity = patchlib_type_get_field(npc, "noGravity");
    g_ctx.npc_velocity = patchlib_type_get_field(npc, "velocity");

    patch_handle_t main = patchlib_type_get_type("Terraria", "Main");
    if (main) {
        g_ctx.main_game_mode = patchlib_type_get_field(main, "GameMode");
        if (!valid(g_ctx.main_game_mode)) g_ctx.main_game_mode = patchlib_type_get_field(main, "gameMode");
        if (!valid(g_ctx.main_game_mode)) {
            patch_handle_t prop = patchlib_type_get_property(main, "GameMode");
            if (prop) g_ctx.main_game_mode_getter = patchlib_property_get_get_method(prop);
            if (prop) patchlib_free(prop);
        }
        if (g_ctx.main_game_mode_getter &&
            !method_is_static(g_ctx.main_game_mode_getter, PATCH_INT32, 0)) {
            patchlib_free(g_ctx.main_game_mode_getter);
            g_ctx.main_game_mode_getter = NULL;
        }
        if (!g_ctx.main_game_mode && !g_ctx.main_game_mode_getter) {
            patch_handle_t getter = patchlib_type_get_method(main, "get_GameMode");
            if (getter && method_is_static(getter, PATCH_INT32, 0)) {
                g_ctx.main_game_mode_getter = getter;
            } else if (getter) {
                patchlib_free(getter);
            }
        }
        g_ctx.main_zenith_world = patchlib_type_get_field(main, "zenithWorld");
        g_ctx.main_hard_mode = patchlib_type_get_field(main, "hardMode");
        if (!valid(g_ctx.main_hard_mode)) g_ctx.main_hard_mode = patchlib_type_get_field(main, "HardMode");
        g_ctx.main_net_mode = patchlib_type_get_field(main, "netMode");
        g_ctx.main_player = patchlib_type_get_field(main, "player");
        g_ctx.main_my_player = patchlib_type_get_field(main, "myPlayer");
        g_ctx.main_world_id = patchlib_type_get_field(main, "worldID");
        g_ctx.main_day_time = patchlib_type_get_field(main, "dayTime");
        patchlib_free(main);
    }

    patch_handle_t player = patchlib_type_get_type("Terraria", "Player");
    if (player) {
        g_ctx.player_position = patchlib_type_get_field(player, "position");
        g_ctx.player_width = patchlib_type_get_field(player, "width");
        g_ctx.player_height = patchlib_type_get_field(player, "height");
        g_ctx.player_active = patchlib_type_get_field(player, "active");
        g_ctx.player_dead = patchlib_type_get_field(player, "dead");
        g_ctx.zone_dungeon = patchlib_type_get_field(player, "ZoneDungeon");
        g_ctx.zone_corrupt = patchlib_type_get_field(player, "ZoneCorrupt");
        g_ctx.zone_crimson = patchlib_type_get_field(player, "ZoneCrimson");
        g_ctx.zone_jungle = patchlib_type_get_field(player, "ZoneJungle");
        g_ctx.zone_snow = patchlib_type_get_field(player, "ZoneSnow");
        g_ctx.zone_desert = patchlib_type_get_field(player, "ZoneDesert");
        g_ctx.zone_beach = patchlib_type_get_field(player, "ZoneBeach");
        g_ctx.zone_underworld = patchlib_type_get_field(player, "ZoneUnderworldHeight");
        g_ctx.zone_hallow = patchlib_type_get_field(player, "ZoneHallow");
        g_ctx.zone_sky = patchlib_type_get_field(player, "ZoneSkyHeight");
        patchlib_free(player);
    }
    g_ctx.downed_mech = patchlib_type_get_field(npc, "downedMechBossAny");
    g_ctx.downed_plant = patchlib_type_get_field(npc, "downedPlantBoss");
    g_ctx.downed_golem = patchlib_type_get_field(npc, "downedGolemBoss");
    g_ctx.downed_moonlord = patchlib_type_get_field(npc, "downedMoonlord");
}

static void name_postfix(patch_handle_t instance, void** args, void* result,
                         const patch_method_signature_t* sig) {
    (void)args; (void)sig;
    if (!instance || !result || !elite_core_is_elite(instance)) return;
    patch_handle_t original = *(patch_handle_t*)result;
    if (!valid(original)) return;
    char* old = patchlib_string_cstr(original);
    if (!old || strstr(old, "精英·") || strstr(old, "稀有·") || strstr(old, "传奇·")) { free(old); return; }
    const char* prefix = elite_core_rank(instance) == ELITE_LEGENDARY ? "传奇·" :
                         elite_core_rank(instance) == ELITE_RARE ? "稀有·" : "精英·";
    char decorated[512];
    snprintf(decorated, sizeof(decorated), "%s%s", prefix, old ? old : "敌怪");
    patch_handle_t replacement = patchlib_string_create(decorated);
    if (replacement) *(patch_handle_t*)result = replacement;
    free(old);
}

static bool mouse_prefix(patch_handle_t instance, void** args,
                         const patch_method_signature_t* sig, void* result) {
    (void)instance; (void)result;
    if (!args || !sig || tefstd_vector_size(&sig->arg_types) < 3 || !args[0]) return false;
    patch_handle_t handle = *(patch_handle_t*)args[0];
    if (!valid(handle)) return false;
    char* text = patchlib_string_cstr(handle);
    if (!text) return false;
    int rarity = strstr(text, "传奇·") ? 11 : strstr(text, "稀有·") ? 1 : strstr(text, "精英·") ? 0 : -1;
    free(text);
    if (rarity < 0) return false;
    size_t count = tefstd_vector_size(&sig->arg_types);
    size_t index = count >= 10 ? 2 : 1;
    if (args[index]) *(int*)args[index] = rarity;
    return false;
}

static void install_ui_hooks(patch_handle_t npc, patch_handle_t main) {
    const char* names[] = {"get_FullName", "get_TypeName", "get_GivenOrTypeName"};
    for (size_t i = 0; i < 3; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(npc, names[i], 0);
        if (method && method_is_instance(method, PATCH_OBJECT, 0, true)) {
            patch_hook_id_t hook = patchlib_install_prepost_hook(method, NULL, name_postfix);
            if (hook != PATCH_HOOK_INVALID_ID && g_ctx.name_hook_count < 3)
                g_ctx.name_hooks[g_ctx.name_hook_count++] = hook;
            patchlib_free(method);
        } else if (method) {
            patchlib_free(method);
        }
    }
    const int counts[] = {8, 10};
    for (size_t i = 0; i < 2; ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(main, "MouseText", counts[i]);
        if (method && method_is_instance(method, PATCH_VOID, counts[i], true)) {
            patch_hook_id_t hook = patchlib_install_prepost_hook(method, mouse_prefix, NULL);
            if (hook != PATCH_HOOK_INVALID_ID && g_ctx.mouse_hook_count < 2)
                g_ctx.mouse_hooks[g_ctx.mouse_hook_count++] = hook;
            patchlib_free(method);
        } else if (method) {
            patchlib_free(method);
        }
    }
}

void elite_core_init(void) {
    if (g_initialized) return;
    srand(0x454C4954u);
    patch_handle_t npc = patchlib_type_get_type("Terraria", "NPC");
    if (!npc) { LOG(MOD_LOG_LEVEL_ERROR, "Terraria.NPC not found"); return; }
    cache_fields(npc);
    for (int count = 0; count <= 4; ++count) {
#if ELITEMONSTERS_ENABLE_SETDEFAULTS_HOOK
        patch_handle_t method = patchlib_type_get_method_by_param_count(npc, "SetDefaults", count);
        if (!method) continue;
        patch_method_signature_t signature = {0};
        bool supported = patchlib_method_get_signature(method, &signature) &&
                         signature.is_instance;
        if (supported) patchlib_method_signature_free(&signature);
        if (!supported) {
            if (signature.arg_types.data) patchlib_method_signature_free(&signature);
            patchlib_free(method);
            continue;
        }
        patch_hook_id_t hook = patchlib_install_prepost_hook(method, NULL, setdefaults_postfix);
        if (hook != PATCH_HOOK_INVALID_ID && g_ctx.setdefaults_hook_count < 8)
            g_ctx.setdefaults_hooks[g_ctx.setdefaults_hook_count++] = hook;
        patchlib_free(method);
#else
        (void)count;
#endif
    }
    patch_handle_t main = patchlib_type_get_type("Terraria", "Main");
    if (main) {
        if (ELITEMONSTERS_ENABLE_UI_HOOKS) install_ui_hooks(npc, main);
        patchlib_free(main);
    }
    patchlib_free(npc);
    g_initialized = true;
    LOG(MOD_LOG_LEVEL_INFO, "Elite core initialized; biome rules can use 10 vanilla zone flags");
}

static void free_field(patch_handle_t* field) {
    if (*field) patchlib_free(*field);
    *field = NULL;
}

void elite_core_cleanup(void) {
    for (size_t i = 0; i < g_ctx.setdefaults_hook_count; ++i) patchlib_uninstall_hook(g_ctx.setdefaults_hooks[i]);
    for (size_t i = 0; i < g_ctx.name_hook_count; ++i) patchlib_uninstall_hook(g_ctx.name_hooks[i]);
    for (size_t i = 0; i < g_ctx.mouse_hook_count; ++i) patchlib_uninstall_hook(g_ctx.mouse_hooks[i]);
    free_field(&g_ctx.main_game_mode); free_field(&g_ctx.main_game_mode_getter);
    free_field(&g_ctx.main_zenith_world); free_field(&g_ctx.main_hard_mode);
    free_field(&g_ctx.main_net_mode); free_field(&g_ctx.main_player);
    free_field(&g_ctx.main_my_player); free_field(&g_ctx.main_world_id);
    free_field(&g_ctx.main_day_time); free_field(&g_ctx.npc_type);
    free_field(&g_ctx.npc_position); free_field(&g_ctx.npc_life);
    free_field(&g_ctx.npc_life_max); free_field(&g_ctx.npc_damage);
    free_field(&g_ctx.npc_defense); free_field(&g_ctx.npc_knockback_resist);
    free_field(&g_ctx.npc_width); free_field(&g_ctx.npc_height);
    free_field(&g_ctx.npc_scale); free_field(&g_ctx.npc_value);
    free_field(&g_ctx.npc_friendly); free_field(&g_ctx.npc_town);
    free_field(&g_ctx.npc_boss); free_field(&g_ctx.npc_target);
    free_field(&g_ctx.npc_ai_style); free_field(&g_ctx.npc_direction);
    free_field(&g_ctx.npc_net_update); free_field(&g_ctx.npc_no_gravity);
    free_field(&g_ctx.npc_velocity); free_field(&g_ctx.player_position);
    free_field(&g_ctx.player_width); free_field(&g_ctx.player_height);
    free_field(&g_ctx.player_active); free_field(&g_ctx.player_dead);
    free_field(&g_ctx.zone_dungeon); free_field(&g_ctx.zone_corrupt);
    free_field(&g_ctx.zone_crimson); free_field(&g_ctx.zone_jungle);
    free_field(&g_ctx.zone_snow); free_field(&g_ctx.zone_desert);
    free_field(&g_ctx.zone_beach); free_field(&g_ctx.zone_underworld);
    free_field(&g_ctx.zone_hallow); free_field(&g_ctx.zone_sky);
    free_field(&g_ctx.downed_mech); free_field(&g_ctx.downed_plant);
    free_field(&g_ctx.downed_golem); free_field(&g_ctx.downed_moonlord);
    memset(&g_ctx, 0, sizeof(g_ctx));
    memset(g_states, 0, sizeof(g_states));
    memset(g_processed_instances, 0, sizeof(g_processed_instances));
    g_state_count = 0; g_elite_count = 0; g_initialized = false;
    g_processed_count = 0;
}
