cmake_minimum_required(VERSION 3.22.1)
project("EliteMonsters")
add_terraria_mod(EliteMonsters
  ${CMAKE_SOURCE_DIR}/src/Core/HookManager.cpp
  ${CMAKE_SOURCE_DIR}/src/NPC/EliteNPC.cpp
  ${CMAKE_SOURCE_DIR}/src/World/WorldRule.cpp
  ${CMAKE_SOURCE_DIR}/src/World/TerrainDetector.cpp
  ${CMAKE_SOURCE_DIR}/src/Boss/BossModify.cpp
  ${CMAKE_SOURCE_DIR}/src/Event/RandomEvent.cpp
  ${CMAKE_SOURCE_DIR}/src/UI/Notice.cpp)
