# Elite Monsters（精英怪）

Android ARM64 KernelLoader mod source and package layout.

## Build

Place this directory under the `TEFKernel-KernelLoader-Mods` source tree and add:

```cmake
add_subdirectory(EliteMonsters)
```

Build with the Android NDK using the repository's existing CMake preset. The
resulting library is `libEliteMonsters.android.arm64.so`.

## Package layout

```text
Manifest.json
Info.json
EliteMonsters.json
Resources/lib/libEliteMonsters.android.arm64.so
```

The current build is an MVP data/runtime skeleton. NPC method names and
signatures must be verified against the exact Terraria 1.4.5.6.4 binary before
installing spawn hooks.
