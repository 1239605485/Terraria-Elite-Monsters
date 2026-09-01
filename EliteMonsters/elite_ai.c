#include "elite_core.h"
#include "biome_mutations.h"
#include "mod_logger.h"

#include <stdint.h>

static patch_hook_id_t g_ai_hook = PATCH_HOOK_INVALID_ID;

static void ai_postfix(patch_handle_t instance, void** args, void* result,
                       const patch_method_signature_t* sig) {
    (void)args; (void)result; (void)sig;
    if (!instance) return;
    elite_core_try_apply(instance);
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
