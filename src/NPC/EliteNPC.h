#ifndef ELITE_MONSTERS_ELITE_NPC_H
#define ELITE_MONSTERS_ELITE_NPC_H

#include "../Core/GameApi.h"

#ifdef __cplusplus
extern "C" {
#endif

void em_elite_npc_initialize(const em_game_api_t *api);
void em_elite_npc_shutdown(void);
bool em_elite_npc_enabled(void);
void em_elite_npc_postfix(patch_handle_t instance, void **args, void *result,
                          const patch_method_signature_t *sig_info);

#ifdef __cplusplus
}
#endif

#endif
