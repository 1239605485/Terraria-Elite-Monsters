# EliteMonsters 2.0.0-alpha4.3-safe-noui-terrain-hookfix 模块化增量验证版

本版本不是完整功能版，而是重构后的第一阶段验证包。它以原版代码为
参考，保留旧的 `EliteMonsters/mod.c` 作为迁移参考，但 CMake 不再编译它。
实际编译入口位于 `src/`。

## 当前启用范围

当前启用 Core、基础 NPC 属性增强和被动 WorldRule 状态层：普通敌怪有 20% 概率
获得生命 ×1.4、伤害 ×1.15、防御 +4；友好 NPC、城镇 NPC 和 Boss 会跳过。进入
世界后，WorldRule 仅依据 `Main.worldID` 确定性抽取 3～5 条规则并记录日志，不执行
任何规则效果。地形检测、聊天播报、Boss、AI、随机事件、奖励和投射物模块均关闭，
待真实 Android 设备完成启动、进入世界、退出世界、再次进入世界测试后逐项恢复。

下方 1.3.x 功能说明是历史资料，不代表 2.0.0-alpha3 已启用的功能。

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
markup literally. Rare elites add one random original item from the current
progress tier. Legendary elites have a 30% chance to add one random original
environment crate; before hardmode the common branch is a Golden Crate, after
hardmode it is a Titanium Crate, and the environment branch follows the target
player's current biome. No custom item or non-vanilla material is added in this
version. Version 1.3.6 also resolves the vanilla `Terraria.Main.NewText`
overload for world, terrain, and rotating-rule announcements, and drives the
notification state from the `Terraria.Main.Update` game loop. The original
`Terraria.NPC.AI` hook remains as a compatibility fallback without replacing
vanilla AI behavior.
Version 2.0.0-alpha4.3-safe-noui-terrain-hookfix installs the minimal NPC `SetDefaults` hook plus
one signature-checked `Main.Update` hook for the passive WorldRule state layer.
It additionally installs one signature-checked `Player.Update` hook for read-only
Zone state logging; no terrain rule effect is executed.
The entire UI/NewText path is disabled: startup does not discover `Main.NewText`,
and the update callback emits no probe or world-entry message. It does not install
AI, boss, event, reward, or other feature hooks. This is
intentional: each later module will be enabled and tested independently.

The alpha3.1 correction reads `Main.gameMenu` as a validated static boolean
field; `Main.worldID` remains a validated static 32-bit integer field.
Alpha3.2 discovers the lifecycle hook from `Terraria.Main` independently of
those fields, then validates the fields inside the callback.
The earlier alpha4.x `Main.NewText` overload discovery and invocation experiment
is retained only in the historical notes/old reference source; it is not part of
the active CMake sources in this safe rollback.

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

The existing GitHub Actions workflow performs the same build and assembles the
installable package. Version 1.3.6 hooks `Main.Update` for reliable world
notifications and directly asks TEFKernel to hook the known parameterless
`NPC.AI()` dispatcher as a fallback, installs an `NPC.NPCLoot` postfix for the
rare progress reward and guaranteed legendary crate distribution (70% common,
30% current-environment), and uses the
primitive-argument `Item.NewItem` overload to create vanilla items safely.

## 双层变异实现

Version 1.3.6 keeps a world-scoped list of 3--5 unique global rules and a
per-tick terrain resolver driven by the target player's vanilla `Zone*`
flags. The resolver prioritizes special sub-biomes (temple, spider,
underworld, meteor, sky, mushroom, ice cave and underground desert) before
falling back to normal surface/underground biomes. The active terrain is
automatically enabled on entry and replaced on exit without an input binding.

The AI postfix applies movement, damage, regeneration, player slow, periodic
reinforcements and rule projectiles. `NPCLoot` handles split/tide/death
effects, while an optional `NPC.CheckDead` hook implements one-time dungeon
skeleton resurrection. An optional `Chest.OpenChest`/`Chest.Open` hook
implements the extra reward rule. `NPC.NewNPC`, `Projectile.NewProjectile`
and chest APIs are signature-checked at startup; unsupported overloads are
logged and safely disabled so the Android build remains stable.
