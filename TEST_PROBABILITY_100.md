# EliteMonsters 0.4.0：100% 概率测试修复版

本测试版将旅途、经典、专家、大师、传奇五种世界模式的精英怪生成概率都设置为 100%，并修复了 `NPC.SetDefaults` Hook 的执行时机和重载选择问题。

注意：这只代表每个符合条件的普通 NPC 都会尝试转化；友好 NPC、城镇 NPC、Boss 和无效 NPC 仍会被过滤。由于所有普通敌怪都可能变成精英怪，测试时可能出现敌怪强度明显升高的情况。

测试完成后，将 `EliteMonsters/mod.c` 中的概率数组恢复为正式配置：

```c
static const int g_spawn_chance_percent[5] = {2, 5, 10, 15, 20};
```
