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
void em_terrain_detector_initialize(const em_game_api_t *api);
void em_terrain_detector_shutdown(void);
void em_boss_modify_initialize(const em_game_api_t *api);
void em_boss_modify_shutdown(void);
void em_random_event_initialize(const em_game_api_t *api);
void em_random_event_shutdown(void);
void em_notice_initialize(const em_game_api_t *api);
void em_notice_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
