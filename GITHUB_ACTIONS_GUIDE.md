# GitHub Actions 使用说明

1. 下载源码 ZIP 并解压。
2. 将解压后的全部内容上传到仓库根目录，保留 `.github/workflows/android-arm64.yml`。
3. 打开仓库的 **Actions** 页面，选择 **Build EliteMonsters Android ARM64**。
4. 点击 **Run workflow**，等待构建完成。
5. 在运行结果底部下载 `EliteMonsters-android-arm64-installable` 工件。
6. 将下载得到的 ZIP 导入 TEFManager，确认安装的是 1.4.3 名称显示、启动稳定、词缀、技能与三档战利品版（作者：liuxin）。

本版本按普通、专家、大师、传奇四档计算精英属性，生成概率为 20%/30%/40%/50%。传奇档只在天顶世界（Zenith/fixed-boi 特殊种子）启用，旅途/Creative 档保持普通属性。符合条件的敌怪会随机成为普通、稀有或传奇精英，名称分别为白色“精英·”、蓝色“稀有·”、紫色“传奇·”，并根据游戏模式、游戏进度和精英等级提高生命、伤害、防御、体型及金币。六种词缀会显示在名称中并产生对应的属性或 AI 效果；不可伤害、无敌和可捕捉 NPC 会被过滤。普通精英掉落少量药水和进度材料；稀有精英掉落进度材料与实用原版装备；传奇精英保留 70% 普通宝匣/30% 当前环境匣概率，并额外掉落进度材料。1.4.3 优先通过 NPC.FullName 等属性 Getter 注入名称，并在 Main.MouseText 做安全兜底；只安装一个名称 Hook，避免批量名称 Hook 引起启动崩溃；保留普通/稀有冲刺技能和传奇挑战 AI、兼容性更好的 `SetDefaults` Postfix、稳定名称颜色 Hook 和 `NPC.NPCLoot` 奖励 Hook：

```c
static const int g_spawn_chance_percent[ELITE_MODE_COUNT] = {
    20, 30, 40, 50
};
```
