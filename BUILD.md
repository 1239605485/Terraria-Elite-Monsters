# EliteMonsters 1.3.3（Android NPC 字段读取测试版）

This version is authored by `liuxin`. The mod keeps the four difficulty profiles,
progress-scaled elite NPCs, original rewards and legendary AI. It also adds a
modular biome mutation layer. Each world deterministically receives three global
rules from its worldID, and each target player's current biome applies a unique
elite damage, defense, healing or movement modifier.

- normal elite: `精英·怪物`, white name; from 1.4x/1.15x/+4 defense and
  10x coins in pre-hardmode to 3x/2.1x/+26 defense and 60x coins in endgame;
- rare elite: `稀有·怪物`, blue name; from 2x/1.4x/+8 defense and 25x coins
  to 5.5x/3.2x/+45 defense and 150x coins;
- legendary elite: `传奇·怪物`, purple name; from 3x/1.8x/+12 defense and
  50x coins to 9x/4.8x/+75 defense and 320x coins.

The mode modifiers applied on top of those values are:

- Normal: health x1.00, damage x1.00, defense +0, size x1.00, coins x1.00;
- Expert: health x1.15, damage x1.10, defense +4, size x1.02, coins x1.50;
- Master: health x1.35, damage x1.25, defense +8, size x1.05, coins x2.25;
- Legendary: health x1.60, damage x1.45, defense +12, size x1.08, coins x3.25.

Normal and rare ranks also increase size and knockback resistance. Legendary
elites are completely immune to knockback. The rank color is
assigned through the vanilla `Main.MouseText` rarity argument; no `[c/...]`
markup is written into the NPC name, because the Android build displays that
markup literally. Rare elites add one random original item from the current
progress tier. Legendary elites have a 30% chance to add one random original
environment crate; before hardmode the common branch is a Golden Crate, after
hardmode it is a Titanium Crate, and the environment branch follows the target
player's current biome. No custom item or non-vanilla material is added in this
version. Version 1.2.2 also hooks the original `Terraria.NPC.AI` without replacing it.
All elites keep the local player as target. Legendary melee/charger enemies
can teleport to the player's side on a cooldown, ranged/caster enemies make
lateral repositioning bursts, flyers weave while approaching, worms perform
diagonal ambush bursts, and special enemies use evasive movement. Legendary
elites enter a one-time enrage below 35% life, increasing their contact damage
by 25%. Movement is authoritative on single-player/server; multiplayer
clients do not duplicate teleports. If a game build does not expose a safe
`velocity`, `position`, or `Main.player` field, the affected movement is
skipped while the rest of the mod remains active.

Build the ARM64 package on a machine with CMake and the Android NDK installed:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24
cmake --build build --config Release --target EliteMonsters -j2
```

The source is split into `elite_core.c`, `elite_ai.c`, `elite_rewards.c` and
`biome_mutations.c`. The existing GitHub Actions workflow builds and assembles
the installable ARM64 package.
