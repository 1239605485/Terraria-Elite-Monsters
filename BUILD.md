# EliteMonsters 0.8.0（liuxin 分级精英怪版）

This version is authored by `liuxin`. The mod filters friendly, town, and boss
NPCs, applies the configured world-mode chance, and prevents repeat
transformation of the same instance. Eligible enemies roll one of three ranks:

- normal elite: `精英·怪物`, white name, 1.5x health, 1.25x damage, 1.1x defense;
- rare elite: `稀有·怪物`, blue name, 2.5x health, 1.75x damage, 1.3x defense;
- legendary elite: `传奇·怪物`, purple name, 5x health, 2.5x damage, 1.6x defense.

All three ranks also increase size and knockback resistance. The rank color is
assigned through the vanilla `Main.MouseText` rarity argument; no `[c/...]`
markup is written into the NPC name, because the Android build displays that
markup literally. Vanilla NPC coin values are multiplied by 2 for normal
elites, 5 for rare elites, and 10 for legendary elites. No custom item or
non-vanilla material is added in this version.

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
installable package.
