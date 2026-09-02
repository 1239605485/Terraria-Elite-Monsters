# 模块化重构说明

## 当前阶段：2.0.0-alpha4.3-safe-noui-terrain-notice-probe

本阶段以原版工程为迁移参考，旧版单文件 `EliteMonsters/mod.c` 保留但不参与
CMake 编译。新的编译入口是 `src/`。

当前只启用：

- `Core/HookManager.cpp`：保存 API 句柄、安装和卸载 Hook。
- `NPC/EliteNPC.cpp`：只处理普通 NPC 的基础精英属性。
- `World/WorldRule.cpp`：只检测世界生命周期，并按 `Main.worldID` 确定性抽取
  3～5 条规则写入模块状态和日志；规则效果尚未启用。
- `World/TerrainDetector.cpp`：只通过 `Player.Update` 读取可用的 `Zone*` 布尔字段，
  在地形状态变化时写入日志；不执行地形规则效果。
- `UI/Notice.cpp`：只执行一次受控世界进入测试播报；不播报地形和规则内容。

alpha3.1 修正 `Main.gameMenu` 按 `bool` 字段读取的问题；该字段不是 `int32`。
alpha3.2 进一步将 `Main.Update` Hook 发现与这两个字段的可用性解耦，便于
单独验证生命周期 Hook；字段不可用时只暂停 WorldRule 状态更新。
alpha4～alpha4.3 的 UI 探针和复杂 `Main.NewText` 枚举已回退；当前只按两个已知
签名形状检查一个 `Main.NewText` 重载，并在首次进入世界时发送一条测试文本。

暂时不启用：

- 世界规则效果和地形读取；
- 游戏文字播报；
- Boss 修改；
- 随机事件、AI、奖励和投射物。

## 恢复顺序

每次只恢复一个模块，并在真实设备上完成“启动游戏、进入世界、退出世界、
再次进入世界”测试后再继续。任何模块都不能直接安装其他模块的 Hook，跨模块
通信只能经过 Core 定义的窄接口。

## 稳定性约束

- Hook 回调不得调用未验证签名的游戏方法。
- 类型句柄和实例字段句柄必须分开保存，禁止把 `Terraria.NPC` 类型句柄当作
  `NPC.type` 字段传给字段读写 API。
- 模块初始化失败时必须保持关闭，不影响其他模块。
- 高风险功能默认关闭。
- WorldRule 当前不读写 NPC、玩家、地形、物品、投射物或 Boss 状态。
- Core 不包含 NPC、地形或规则业务逻辑。
