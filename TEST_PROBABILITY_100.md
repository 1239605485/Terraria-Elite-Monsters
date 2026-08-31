# EliteMonsters 1.0.5：100% 概率、AI与奖励测试版

本测试版将旅途、经典、专家、大师、传奇五种世界模式的精英怪生成概率都设置为 100%，并修复了 `NPC.SetDefaults` Hook 的执行时机和重载选择问题。符合条件的敌怪会随机分为三档：普通精英名称为“精英·怪物”（白色），稀有精英为“稀有·怪物”（蓝色），传奇精英为“传奇·怪物”（紫色）。颜色由 `Main.MouseText` 的原版稀有度参数设置，不再把颜色代码写进名称；生命、伤害、防御、体型和金币奖励会随游戏进度提高。稀有和传奇精英会更积极锁定玩家，并按冷却尝试冲刺。

注意：这只代表每个符合条件的普通 NPC 都会尝试转化；友好 NPC、城镇 NPC、Boss 和无效 NPC 仍会被过滤。由于所有普通敌怪都可能变成精英怪，测试时可能出现敌怪强度明显升高的情况。1.0.5 会优先直接 Hook `NPC.AI()`，并在 `NPC.NPCLoot` 后为传奇精英增加 1 个原版金匣或钛金匣；日志中应看到 `Known NPC AI hook installed`、`NPC.NPCLoot reward hook installed` 和 `Item.NewItem reward API: found=1`。

测试完成后，将 `EliteMonsters/mod.c` 中的概率数组恢复为正式配置：

```c
static const int g_spawn_chance_percent[5] = {2, 5, 10, 15, 20};
```
