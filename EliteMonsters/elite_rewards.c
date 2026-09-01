#include "elite_core.h"
#include "mod_logger.h"

#include <stdint.h>
#include <stdlib.h>

static patch_handle_t g_new_item = NULL;
static patch_hook_id_t g_loot_hook = PATCH_HOOK_INVALID_ID;

enum {
    ITEM_GOLDEN_CRATE = 2336, ITEM_TITANIUM_CRATE = 3981,
    ITEM_LIFE_CRYSTAL = 29, ITEM_MANA_CRYSTAL = 109, ITEM_FALLEN_STAR = 75,
    ITEM_MAGIC_MIRROR = 50, ITEM_HERMES_BOOTS = 54, ITEM_HOOK = 118,
    ITEM_COBALT_BAR = 381, ITEM_MYTHRIL_BAR = 382, ITEM_ADAMANTITE_BAR = 391,
    ITEM_SOUL_LIGHT = 520, ITEM_SOUL_NIGHT = 521, ITEM_LIFE_FRUIT = 1291,
    ITEM_ECTOPLASM = 1508, ITEM_LUNAR_BAR = 3467,
    ITEM_CORRUPT_CRATE = 3203, ITEM_CRIMSON_CRATE = 3204,
    ITEM_DUNGEON_CRATE = 3205, ITEM_SKY_CRATE = 3206,
    ITEM_HALLOWED_CRATE = 3207, ITEM_JUNGLE_CRATE = 3208,
    ITEM_FROZEN_CRATE = 4405, ITEM_OASIS_CRATE = 4407,
    ITEM_LAVA_CRATE = 4877, ITEM_OCEAN_CRATE = 5002,
    ITEM_CORRUPT_HARD = 3982, ITEM_CRIMSON_HARD = 3983,
    ITEM_DUNGEON_HARD = 3984, ITEM_SKY_HARD = 3985,
    ITEM_HALLOWED_HARD = 3986, ITEM_JUNGLE_HARD = 3987,
    ITEM_FROZEN_HARD = 4406, ITEM_OASIS_HARD = 4408,
    ITEM_LAVA_HARD = 4878, ITEM_OCEAN_HARD = 5003
};

static int random_item(elite_progress_t progress) {
    static const int pools[5][8] = {
        {ITEM_LIFE_CRYSTAL, ITEM_MANA_CRYSTAL, ITEM_FALLEN_STAR, ITEM_MAGIC_MIRROR, ITEM_HERMES_BOOTS, ITEM_HOOK, 188, 188},
        {ITEM_COBALT_BAR, ITEM_MYTHRIL_BAR, ITEM_ADAMANTITE_BAR, ITEM_SOUL_LIGHT, ITEM_SOUL_NIGHT, 492, 493, 499},
        {1225, 947, 1006, 547, 548, 549, ITEM_LIFE_FRUIT, 500},
        {ITEM_ECTOPLASM, 3261, 1552, 1141, 1006, 1293, ITEM_LIFE_FRUIT, 499},
        {ITEM_LUNAR_BAR, 3601, ITEM_ECTOPLASM, 2218, 1552, 1006, ITEM_LIFE_FRUIT, 499}
    };
    if (progress < 0 || progress > PROGRESS_ENDGAME) progress = PROGRESS_PRE_HARDMODE;
    return pools[progress][rand() % 8];
}

static int environment_crate(int player, elite_progress_t progress) {
    bool hard = progress != PROGRESS_PRE_HARDMODE;
    EliteContext* ctx = elite_core_context();
    if (elite_core_player_in_zone(player, ctx->zone_dungeon)) return hard ? ITEM_DUNGEON_HARD : ITEM_DUNGEON_CRATE;
    if (elite_core_player_in_zone(player, ctx->zone_corrupt)) return hard ? ITEM_CORRUPT_HARD : ITEM_CORRUPT_CRATE;
    if (elite_core_player_in_zone(player, ctx->zone_crimson)) return hard ? ITEM_CRIMSON_HARD : ITEM_CRIMSON_CRATE;
    if (elite_core_player_in_zone(player, ctx->zone_jungle)) return hard ? ITEM_JUNGLE_HARD : ITEM_JUNGLE_CRATE;
    if (elite_core_player_in_zone(player, ctx->zone_snow)) return hard ? ITEM_FROZEN_HARD : ITEM_FROZEN_CRATE;
    if (elite_core_player_in_zone(player, ctx->zone_desert)) return hard ? ITEM_OASIS_HARD : ITEM_OASIS_CRATE;
    if (elite_core_player_in_zone(player, ctx->zone_beach)) return hard ? ITEM_OCEAN_HARD : ITEM_OCEAN_CRATE;
    if (elite_core_player_in_zone(player, ctx->zone_underworld)) return hard ? ITEM_LAVA_HARD : ITEM_LAVA_CRATE;
    if (elite_core_player_in_zone(player, ctx->zone_hallow)) return hard ? ITEM_HALLOWED_HARD : ITEM_HALLOWED_CRATE;
    return progress == PROGRESS_PRE_HARDMODE ? ITEM_GOLDEN_CRATE : ITEM_TITANIUM_CRATE;
}

static bool spawn_item(void* npc, int type, int stack) {
    EliteContext* ctx = elite_core_context();
    int32_t x = 0, y = 0, width = 0, height = 0, item = type, count = stack;
    bool no_broadcast = false, no_grab_delay = false;
    int32_t prefix = 0, result = -1;
    float position[2] = {0.0f, 0.0f};
    if (!g_new_item || !elite_core_read_vector2(ctx->npc_position, npc, &position[0], &position[1])) return false;
    x = (int32_t)position[0]; y = (int32_t)position[1];
    (void)elite_core_read_i32(ctx->npc_width, npc, &width);
    (void)elite_core_read_i32(ctx->npc_height, npc, &height);
    void* args[9] = {&x, &y, &width, &height, &item, &count, &no_broadcast, &prefix, &no_grab_delay};
    return patchlib_method_invoke_args(g_new_item, NULL, &result, args) && result >= 0;
}

static void loot_postfix(patch_handle_t instance, void** args, void* result,
                         const patch_method_signature_t* sig) {
    (void)args; (void)result; (void)sig;
    if (!instance || !elite_core_is_elite(instance)) return;
    size_t slot = elite_core_slot(instance);
    EliteState* state = elite_core_state(slot);
    if (!state || state->rewarded || state->rank == ELITE_NORMAL) return;
    state->rewarded = true;
    if (!elite_core_reward_allowed()) return;
    elite_progress_t progress = elite_core_progress();
    int item = state->rank == ELITE_RARE ? random_item(progress)
                                         : environment_crate(elite_core_target_player(instance), progress);
    if (spawn_item(instance, item, state->rank == ELITE_RARE ? 2 : 1)) {
        mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters",
                         "精英奖励生成：item=%d rank=%d progress=%s", item,
                         (int)state->rank, elite_core_progress_name(progress));
    }
}

void elite_rewards_init(void) {
    patch_handle_t npc = patchlib_type_get_type("Terraria", "NPC");
    patch_handle_t item = patchlib_type_get_type("Terraria", "Item");
    if (!npc || !item) { if (npc) patchlib_free(npc); if (item) patchlib_free(item); return; }
    patch_handle_t loot = patchlib_type_get_method_by_param_count(npc, "NPCLoot", 0);
    g_new_item = patchlib_type_get_method_by_param_count(item, "NewItem", 9);
    if (loot) { g_loot_hook = patchlib_install_prepost_hook(loot, NULL, loot_postfix); patchlib_free(loot); }
    patchlib_free(npc); patchlib_free(item);
    if (g_loot_hook != PATCH_HOOK_INVALID_ID)
        mod_logger_write(MOD_LOG_LEVEL_INFO, "EliteMonsters", "NPC.NPCLoot reward Hook installed");
}

void elite_rewards_cleanup(void) {
    if (g_loot_hook != PATCH_HOOK_INVALID_ID) patchlib_uninstall_hook(g_loot_hook);
    if (g_new_item) patchlib_free(g_new_item);
    g_loot_hook = PATCH_HOOK_INVALID_ID;
    g_new_item = NULL;
}
