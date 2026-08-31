# Elite Monsters（正式版）

## 1.2.4 更新

- 修复 Terraria 复用 NPC 对象槽位后，旧精英身份和“已发放奖励”状态残留，导致后续传奇精英死亡时跳过宝匣判定的问题。
- 每次 `NPC.SetDefaults` 完成时都会开始一个新的 NPC 生命周期，并重置该槽位的精英、AI 和奖励状态。
- 传奇精英现在必掉 1 个宝匣：困难模式前 70% 金匣、困难模式后 70% 钛金匣，另有 30% 按击杀时目标玩家当前环境选择环境匣。
- 精英生成概率按难度递增：普通 20%、专家 30%、大师 40%、传奇 50%。
- 当前版本已标记为正式版：稳定验证 `stableVerified=true`，实验标记 `experimental=false`。

一个运行于 Terraria Android ARM64 的分级精英怪模组。

作者：**liuxin**  
当前版本：**1.2.4**

## 模组功能

Elite Monsters 会将符合条件的普通敌怪转化为精英怪，让探索过程中出现更有挑战性的战斗。

### 精英等级、游戏模式与游戏进度

下面的进度表是普通模式基础值，数值格式为“生命 / 伤害 / 防御增加 / 体型 / 金币倍率”。

| 游戏阶段 | 普通精英（白色） | 稀有精英（蓝色） | 传奇精英（紫色） |
| --- | --- | --- | --- |
| 前期 | ×1.4 / ×1.15 / +4 / ×1.05 / ×10 | ×2.0 / ×1.4 / +8 / ×1.12 / ×25 | ×3.0 / ×1.8 / +12 / ×1.20 / ×50 |
| 困难模式前期 | ×1.7 / ×1.35 / +8 / ×1.08 / ×15 | ×2.6 / ×1.8 / +15 / ×1.18 / ×40 | ×4.2 / ×2.4 / +24 / ×1.30 / ×80 |
| 世纪之花前 | ×2.0 / ×1.55 / +12 / ×1.10 / ×25 | ×3.4 / ×2.15 / +22 / ×1.22 / ×60 | ×5.5 / ×3.0 / +36 / ×1.38 / ×130 |
| 世纪之花后 | ×2.4 / ×1.8 / +18 / ×1.12 / ×40 | ×4.2 / ×2.6 / +32 / ×1.28 / ×100 | ×7.0 / ×3.8 / +52 / ×1.50 / ×220 |
| 星魂/终局阶段 | ×3.0 / ×2.1 / +26 / ×1.15 / ×60 | ×5.5 / ×3.2 / +45 / ×1.32 / ×150 | ×9.0 / ×4.8 / +75 / ×1.60 / ×320 |

模式会在上表基础值上继续叠加：

| 游戏模式 | 生命 | 伤害 | 防御增加 | 体型 | 金币 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 普通 | ×1.00 | ×1.00 | +0 | ×1.00 | ×1.00 |
| 专家 | ×1.15 | ×1.10 | +4 | ×1.02 | ×1.50 |
| 大师 | ×1.35 | ×1.25 | +8 | ×1.05 | ×2.25 |
| 传奇 | ×1.60 | ×1.45 | +12 | ×1.08 | ×3.25 |

原版没有独立的“传奇模式”。本模组检测原版 `Main.zenithWorld`：天顶世界（Zenith/fixed-boi 特殊种子）启用自定义传奇属性档；普通、专家、大师按原版难度对应，旅途/Creative 保持普通属性。普通和稀有精英会增加体型和击退抗性；传奇精英的击退抗性为 0，完全免疫击退。名称颜色使用 Terraria 原版稀有度参数设置，不会把 `[c/颜色代码]` 写入名称。

### AI 强化

- 所有精英怪会更积极锁定当前玩家目标。
- 稀有精英保留原有的目标锁定和短距离冲刺。
- 传奇近战/冲锋怪：距离较远时按冷却瞬移到玩家侧后方，并进行短突进。
- 传奇远程/法师怪：周期性横向拉扯和换位，降低玩家站桩输出的收益。
- 传奇飞行怪：加入侧向偏移的蛇形追击，不再沿直线靠近。
- 传奇蠕虫/特殊怪：周期性斜向突袭或规避移动。
- 传奇生命低于 35% 时只触发一次狂暴，接触伤害提高 25%。
- 传奇 AI 只在单机或服务器侧改变位置/速度；多人客户端不重复触发瞬移。
- 所有行为仍叠加在原版 `NPC.AI` 之后，不替换原版攻击和投射物逻辑。

### 击杀奖励

- 三档精英都会大幅提高原版 `NPC.value`，金币倍率随游戏模式、游戏进度和精英等级叠加。
- 稀有精英在原版掉落处理完成后，随机额外掉落 1 种当前进度的原版道具；材料和药水会按进度给出合理数量。
- 传奇精英在原版掉落处理完成后必定额外掉落 1 个宝匣：困难模式前 70% 金匣、困难模式后 70% 钛金匣，30% 按当前环境选择环境匣。
- 奖励通过原版 `Item.NewItem` 生成，并在多人客户端跳过，避免客户端重复生成。

## 其他规则

- 友好 NPC、城镇 NPC 和 Boss 不会被转化。
- 同一个 NPC 实例不会被重复强化。
- 金币奖励完全使用 Terraria 原版金币机制。
- 本版本不添加自定义物品或虚构材料；稀有奖励和环境匣全部使用 Terraria 原版物品。
- 当前源码按难度递增设置精英生成概率：普通 20%、专家 30%、大师 40%、传奇 50%。
- 游戏进度通过困难模式、机械 Boss、世纪之花、石巨人和月亮领主进度判断。

## 生成概率

`EliteMonsters/mod.c` 中的配置如下：

```c
static const int g_spawn_chance_percent[ELITE_MODE_COUNT] = {
    20, 30, 40, 50
};
```

数组顺序为：普通、专家、大师、传奇（天顶世界特殊种子）。旅途/Creative 档仍使用普通档。

每个已经转化为精英的敌怪还会随机获得一个等级：

- 普通精英：70%
- 稀有精英：25%
- 传奇精英：5%

## 使用 GitHub Actions 编译

1. 将本项目全部文件上传到 GitHub 仓库根目录。
2. 打开仓库的 **Actions** 页面。
3. 选择 **Build EliteMonsters Android ARM64**。
4. 点击 **Run workflow**，等待编译完成。
5. 在运行结果底部下载 `EliteMonsters-android-arm64-installable`。
6. 将下载的 ZIP 导入 TEFManager 安装。

仓库中已经包含 `.github/workflows/android-arm64.yml`，GitHub Actions 会自动配置 Android NDK、编译 ARM64 动态库并组装可安装的模组 ZIP。

## 本地编译

需要安装 CMake 和 Android NDK：

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24

cmake --build build --config Release --target EliteMonsters -j2
```

## 项目结构

```text
.
├── .github/workflows/android-arm64.yml  # GitHub Actions 编译流程
├── EliteMonsters/
│   ├── mod.c                            # 模组核心代码
│   ├── Info.json                        # 模组信息、作者和功能介绍
│   ├── Manifest.json                    # 模组清单
│   └── EliteMonsters.json               # 动态库名称配置
├── mod-api/                             # TEFKernel 模组 API
├── BUILD.md                             # 编译说明
└── GITHUB_ACTIONS_GUIDE.md              # GitHub Actions 使用说明
```

## 日志检查

安装并启动游戏后，可以在 TEFManager 日志中确认模组是否加载成功。正常情况下应看到类似信息：

```text
Loaded 1 mods successfully
Initialized 1 mods successfully
Kernel runtime started successfully
```

如果只看到 TEFKernel 启动成功，但没有 `Initialized 1 mods successfully`，通常需要检查模组是否启用、安装包是否完整，以及 GitHub Actions 是否编译出了 ARM64 动态库。

1.2.3 会优先直接获取当前游戏的 `Terraria.NPC.AI()`，并让 TEFKernel
直接校验和安装这个已确认的主方法；获取失败时再枚举实际 AI 方法。
同时应看到奖励 Hook、原版物品生成 API 和进度字段已找到：

```text
Known NPC AI hook installed: name=AI id=...
NPC.NPCLoot reward hook installed: id=...
Item.NewItem reward API: found=1 ...
Progress fields: gameModeField=0 gameModeGetter=1 zenithWorld=1 hardMode=1 mech=1 plant=1 golem=1 moonlord=1
```

如果没有找到 AI 方法，模组仍会正常加载，但只使用属性、名称和原版颜色功能。
