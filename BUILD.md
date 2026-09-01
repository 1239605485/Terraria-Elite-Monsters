# EliteMonsters 1.4.3 正式版（liuxin 四档难度、词缀、名称显示、技能与战利品版）

This version is authored by `liuxin`. The mod filters friendly, town, and boss
NPCs, applies the configured world-mode chance, and prevents repeat
transformation of the same instance. The profile reads both game mode and
world progress. The four mod profiles are Normal, Expert, Master, and
Legendary; because Terraria has no native Legendary GameModeID, the custom
Legendary profile is enabled by Main.zenithWorld in the Zenith/fixed-boi
special-seed world. Creative/Journey remains on the Normal profile. Progress is
split into pre-hardmode, early hardmode, pre-Plantera, post-Plantera, and
endgame. Eligible enemies roll one of three ranks at every progress stage:

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
markup literally. Normal elites add a small potion bundle and one progression
material; rare elites add one progression material and one useful vanilla
equipment item; legendary elites retain the 70% common crate / 30% environment
crate roll and also add one progression material. Before hardmode the common
branch is a Golden Crate, after hardmode it is a Titanium Crate, and the
environment branch follows the target player's current biome. No custom item
or non-vanilla material is added in this version. Version 1.4.0 also hooks the
original `Terraria.NPC.AI` without replacing it.
All elites keep the local player as target. Legendary melee/charger enemies
can teleport to the player's side on a cooldown, ranged/caster enemies make
lateral repositioning bursts, flyers weave while approaching, worms perform
diagonal ambush bursts, and special enemies use evasive movement. Legendary
elites enter a one-time enrage below 35% life, increasing their contact damage
by 25%. Movement is authoritative on single-player/server; multiplayer
clients do not duplicate teleports. If a game build does not expose a safe
`velocity`, `position`, or `Main.player` field, the affected movement is
skipped while the rest of the mod remains active.

Version 1.4.3 includes six visible affixes. Flame increases damage and periodically
dashes, Frost adds defense and knockback resistance, Vampiric periodically heals,
Split triggers one half-health second wind and dash, Enraged adds the low-health
damage phase to non-legendary ranks, and Abyssal adds health, damage, and periodic
movement bursts. Profile rolls use `NPC.whoAmI` as a deterministic input for
better client/server agreement. SetDefaults remains a postfix-only hook so the
original NPC initialization order is preserved, and
reward state is committed only after `Item.NewItem` succeeds. NPC name methods
are resolved from the `FullName`/`TypeName` property getters first and then by
enumerating the current NPC method table, which covers different IL2CPP name
getter exports and Android string metadata variants.

Normal elites now perform a low-frequency burst, rare elites perform a faster
burst, and legendary elites retain their type-aware movement. The name source
uses only one best-match NPC getter, restoring the elite/affix name while
avoiding the multi-getter batch that caused the 1.4.0 Android startup crash.
Direct `NPC.color` write and `Main.NewText` UI invocation remain disabled until
their exact Android overloads are verified. Loot is split
by rank: normal elites drop a small potion bundle and a progression material;
rare elites drop a progression material and a useful vanilla equipment item;
legendary elites retain the 70% common crate / 30% environment crate roll and
also drop a progression material. NPC.value supplies the large vanilla coin
reward for every rank, with the existing rank and difficulty multipliers.

Build the ARM64 package on a machine with CMake and the Android NDK installed:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24
cmake --build build --config Release --target EliteMonsters -j2
```

The existing GitHub Actions workflow performs the same build and assembles the
installable package. Version 1.4.3 resolves the NPC name through the property
getter first, then uses the known
parameterless `NPC.AI()` dispatcher, installs an `NPC.NPCLoot` postfix for the
three rank-specific reward bundles and guaranteed legendary crate distribution
(70% common, 30% current-environment), and uses the
primitive-argument `Item.NewItem` overload to create vanilla items safely.
