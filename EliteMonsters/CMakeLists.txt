cmake_minimum_required(VERSION 3.22.1)
project("EliteMonsters")
add_terraria_mod(EliteMonsters
  mod.c
  elite_core.c
  elite_ai.c
  elite_rewards.c
  biome_mutations.c
)
