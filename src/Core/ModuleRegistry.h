#ifndef ELITE_MONSTERS_MODULE_REGISTRY_H
#define ELITE_MONSTERS_MODULE_REGISTRY_H

#include "GameApi.h"

#ifdef __cplusplus
extern "C" {
#endif

void em_elite_npc_initialize(const em_game_api_t *api);
void em_elite_npc_shutdown(void);
void em_world_rule_initialize(const em_game_api_t *api);
void em_world_rule_shutdown(void);
bool em_world_rule_enabled(void);
void em_world_rule_set_hook_installed(bool installed);
bool em_world_rule_active(int rule_id);
bool em_world_rule_night_hunt_test_mode(void);
void em_world_rule_update(patch_handle_t instance, void **args, void *result,
                          const patch_method_signature_t *sig_info);
void em_terrain_detector_initialize(const em_game_api_t *api);
void em_terrain_detector_shutdown(void);
bool em_terrain_detector_enabled(void);
void em_terrain_detector_set_hook_installed(bool installed);
void em_terrain_detector_update(patch_handle_t instance, void **args, void *result,
                                const patch_method_signature_t *sig_info);
void em_boss_modify_initialize(const em_game_api_t *api);
void em_boss_modify_shutdown(void);
void em_random_event_initialize(const em_game_api_t *api);
void em_random_event_shutdown(void);
#ifdef __cplusplus
}
#endif

#endif
