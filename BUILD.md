# EliteMonsters 0.5.0（100% 概率和彩色名称测试版）

This revision connects the existing `Terraria.NPC.SetDefaults` hook to the
elite profile generator. It filters friendly/town/boss/inactive NPCs, applies
the configured world-mode chance, scales supported NPC fields, and prevents
repeat transformation of the same instance.

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
