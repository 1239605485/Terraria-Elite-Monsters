#ifndef BIOME_MUTATIONS_H
#define BIOME_MUTATIONS_H

#include <stddef.h>

void biome_mutations_init(void);
void biome_mutations_cleanup(void);
void biome_mutations_on_spawn(void* npc, size_t slot);
void biome_mutations_tick(void* npc, size_t slot, int player_index);
float biome_mutations_speed_multiplier(int player_index);

#endif
