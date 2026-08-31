# EliteMonsters 1.0.5（liuxin AI强化与原版奖励版）

This version is authored by `liuxin`. The mod filters friendly, town, and boss
NPCs, applies the configured world-mode chance, and prevents repeat
transformation of the same instance. The profile now also reads world
progress: pre-hardmode, early hardmode, pre-Plantera, post-Plantera, and
endgame. Eligible enemies roll one of three ranks at every progress stage:

- normal elite: `精英·怪物`, white name; from 1.4x/1.15x/+4 defense in
  pre-hardmode to 3x/2.1x/+26 defense in endgame;
- rare elite: `稀有·怪物`, blue name; from 2x/1.4x/+8 defense to
  5.5x/3.2x/+45 defense;
- legendary elite: `传奇·怪物`, purple name; from 3x/1.8x/+12 defense to
  9x/4.8x/+75 defense.

All three ranks also increase size and knockback resistance. The rank color is
assigned through the vanilla `Main.MouseText` rarity argument; no `[c/...]`
markup is written into the NPC name, because the Android build displays that
markup literally. Vanilla NPC coin values scale with progress from 2/4/10
in pre-hardmode to 6/12/30 in endgame. A legendary elite additionally drops
one original Golden Crate before hardmode or Golden Crate Hard (Titanium Crate)
after hardmode. No custom item or non-vanilla material is added in this version.
Version 1.0.5 also hooks the original
`Terraria.NPC.AI`: all elites more actively keep the local player as target,
while rare and legendary elites periodically attempt a dash. If a game build
does not expose a safe `velocity` field, the target enhancement remains active
and the dash is skipped.

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
installable package. Version 1.0.5 directly asks TEFKernel to hook the known
parameterless `NPC.AI()` dispatcher, installs an `NPC.NPCLoot` postfix for the
legendary crate reward, and uses the primitive-argument `Item.NewItem` overload
to create the vanilla item safely.
