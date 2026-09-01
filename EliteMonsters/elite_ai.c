#include "elite_core.h"
#include "biome_mutations.h"
#include "mod_logger.h"

#include <stdint.h>

static patch_hook_id_t g_ai_hook = PATCH_HOOK_INVALID_ID;

/* Diagnostic gate: restore only one harmless field read first. Do not enable
 * transformation or per-frame writes until this probe is stable on Android. */
#define ELITEMONSTERS_ENABLE_AI_BEHAVIOR 1

static void ai_postfix(patch_handle_t instance, void** args, void* result,
                       const patch_method_signature_t* sig) {
#if !ELITEMONSTERS_ENABLE_AI_BEHAVIOR
    (void)instance; (void)args; (void)result; (void)sig;
    return;
#else
    (void)args; (void)result; (void)sig;
    EliteContext* ctx = elite_core_context();
    int32_t type = 0;
    (void)elite_core_read_i32(ctx->npc_type, instance, &type);
    (void)type;
    return;

#if 0
    (void)args; (void)result; (void)sig;
    if (!instance) return;
    elite_core_try_apply(instance);
    if (!elite_core_is_elite(instance)) return;
    size_t slot = elite_core_slot(instance);
    EliteState* state = elite_core_state(slot);
    if (!state) return;
    ++state->ai_ticks;

    int player = elite_core_target_player(instance);
    EliteContext* ctx = elite_core_context();
    if (player >= 0) {
        (void)elite_core_write_i32(ctx->npc_target, instance, player);
        biome_mutations_tick(instance, slot, player);
    }

    int32_t life = 0, life_max = 0;
    if (!state->enraged && state->rank == ELITE_LEGENDARY &&
        elite_core_read_i32(ctx->npc_life, instance, &life) &&
        elite_core_read_i32(ctx->npc_life_max, instance, &life_max) &&
        life_max > 0 && (int64_t)life * 100 <= (int64_t)life_max * 35) {
        (void)elite_core_write_i32(ctx->npc_damage, instance,
                                   (int32_t)((float)state->base_damage * 1.25f));
        state->enraged = true;
        (void)elite_core_write_bool(ctx->npc_net_update, instance, true);
        ELITEMONSTERS_LOG(MOD_LOG_LEVEL_INFO, "传奇精英进入狂暴状态");
    }
    if (state->rank != ELITE_LEGENDARY) return;

    bool client = false;
    int32_t net_mode = 0;
    (void)elite_core_read_i32(ctx->main_net_mode, NULL, &net_mode);
    client = net_mode == 1;
    (void)elite_core_write_float(ctx->npc_knockback_resist, instance, 0.0f);
    if (client || player < 0 || state->ai_ticks % 8u != 0u) return;

    float px = 0.0f, py = 0.0f, nx = 0.0f, ny = 0.0f;
    if (!elite_core_player_state(player, &px, &py, NULL, NULL) ||
        !elite_core_read_vector2(ctx->npc_position, instance, &nx, &ny)) return;
    float vx = 0.0f, vy = 0.0f;
    (void)elite_core_read_vector2(ctx->npc_velocity, instance, &vx, &vy);
    float speed = biome_mutations_speed_multiplier(player);
    if (state->ai_ticks % 64u == 0u && (px - nx > 480.0f || nx - px > 480.0f)) {
        nx = px + (px >= nx ? -120.0f : 120.0f);
        ny = py;
        vx = 0.0f;
        vy = 0.0f;
    } else {
        vx = (px >= nx ? 6.0f : -6.0f) * speed;
        if (py < ny - 80.0f) vy = -5.0f * speed;
        if (py > ny + 80.0f) vy = 5.0f * speed;
    }
    (void)elite_core_write_vector2(ctx->npc_position, instance, nx, ny);
    (void)elite_core_write_vector2(ctx->npc_velocity, instance, vx, vy);
    (void)elite_core_write_bool(ctx->npc_net_update, instance, true);
#endif
#endif
}

void elite_ai_init(void) {
    if (g_ai_hook != PATCH_HOOK_INVALID_ID) return;
    EliteContext* ctx = elite_core_context();
    patch_handle_t npc = patchlib_type_get_type("Terraria", "NPC");
    if (!npc) return;
    patch_handle_t method = patchlib_type_get_method_by_param_count(npc, "AI", 0);
    if (!method) method = patchlib_type_get_method(npc, "AI");
    bool supported = false;
    if (method) {
        patch_method_signature_t signature = {0};
        if (patchlib_method_get_signature(method, &signature)) {
            supported = signature.is_instance && signature.return_type == PATCH_VOID &&
                        tefstd_vector_size(&signature.arg_types) == 0;
            patchlib_method_signature_free(&signature);
        }
    }
    if (method && supported) {
        g_ai_hook = patchlib_install_prepost_hook(method, NULL, ai_postfix);
    }
    if (method) patchlib_free(method);
    patchlib_free(npc);
    if (g_ai_hook != PATCH_HOOK_INVALID_ID)
        ELITEMONSTERS_LOG(MOD_LOG_LEVEL_INFO, "NPC.AI Hook installed");
    (void)ctx;
}

void elite_ai_cleanup(void) {
    if (g_ai_hook != PATCH_HOOK_INVALID_ID) patchlib_uninstall_hook(g_ai_hook);
    g_ai_hook = PATCH_HOOK_INVALID_ID;
}
