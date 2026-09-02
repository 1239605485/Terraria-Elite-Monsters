# 1.4.0 地形播报与闪退修复

## 现象

进入世界后全局规则可以播报，但移动到其他地形时不再播报。

## 原因

旧实现通过 `Main.player[]` 取玩家实例。目标 Android IL2CPP 构建中，静态玩家数组虽然能被解析为字段，但不能稳定地通过 TEFKernel 的数组接口读取元素，因此地形检测在获取玩家对象处提前结束。

## 1.3.7 后续问题

在部分 Android IL2CPP 构建中，`Player.Update`、`Player.UpdateBiomes` 和
`Main.LocalPlayer` getter 都会在进入世界时触发 `SIGABRT`。因此 1.4.0
移除了所有 Player 方法 Hook 以及 LocalPlayer getter 调用。

## 修改

- 不再安装任何 Player 方法 Hook。
- 直接使用当前 `Player` 实例读取 Zone 属性和 getter。
- 地形仍按状态变化播报，重复帧不会重复播报。
- 保留 `Main.Update` 和 `NPC.AI` 作为兼容回退路径。
- 移除不稳定的 `Player.Update`/`Player.UpdateBiomes` Hook，避免进入世界闪退。
- 版本升级为 `1.4.0`（`2026090206`），确保 TEFManager 不会继续使用旧版本。

## 构建

本版本仍需在安装了 Android NDK 的环境中按 `BUILD.md` 构建 ARM64 动态库。当前源码包不包含可直接安装的 `.so`。
