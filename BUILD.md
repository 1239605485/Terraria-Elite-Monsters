# EliteMonsters 0.7.0（原版金币奖励版）

This revision connects the existing `Terraria.NPC.SetDefaults` hook to the
elite profile generator. It filters friendly/town/boss NPCs, applies the
configured world-mode chance, scales supported NPC fields, and prevents repeat
transformation of the same instance. Elite NPC names receive a `【精英】`
marker; the `Main.MouseText` hover path also forces rarity 10 so the marker is
red even when Android does not parse chat color tags in NPC names. Vanilla
NPC coin values are multiplied by 2 for normal elites, 5 for rare elites, and
10 for legendary elites.

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
