# GitHub Actions 使用说明

1. 下载源码 ZIP 并解压。
2. 将解压后的全部内容上传到仓库根目录，保留 `.github/workflows/android-arm64.yml`。
3. 打开仓库的 **Actions** 页面，选择 **Build EliteMonsters Android ARM64**。
4. 点击 **Run workflow**，等待构建完成。
5. 在运行结果底部下载 `EliteMonsters-android-arm64-installable` 工件。
6. 将下载得到的 ZIP 导入 TEFManager，确认安装的是 1.3.2 四档难度、词缀与进度奖励版（作者：liuxin）。

本版本按普通、专家、大师、传奇四档计算精英属性，生成概率为 20%/30%/40%/50%。传奇档只在天顶世界（Zenith/fixed-boi 特殊种子）启用，旅途/Creative 档保持普通属性。符合条件的敌怪会随机成为普通、稀有或传奇精英，名称分别为白色“精英·”、蓝色“稀有·”、紫色“传奇·”，并根据游戏模式、游戏进度和精英等级提高生命、伤害、防御、体型及金币。六种词缀会显示在名称中并产生对应的属性或 AI 效果；不可伤害、无敌和可捕捉 NPC 会被过滤。稀有精英随机掉落当前进度的原版道具；传奇精英免疫击退，保留分类型挑战 AI，击杀时必掉 1 个宝匣：困难模式前 70% 金匣、困难模式后 70% 钛金匣，30% 当前环境匣。1.3.2 保留兼容性更好的 `SetDefaults` Postfix，使用自动枚举的 NPC 名称 Getter，并优先 Hook 当前游戏的 `NPC.AI()`，同时安装 `NPC.NPCLoot` 奖励 Hook：

```c
static const int g_spawn_chance_percent[ELITE_MODE_COUNT] = {
    20, 30, 40, 50
};
```
