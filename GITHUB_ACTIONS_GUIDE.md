# GitHub Actions 使用说明

1. 下载源码 ZIP 并解压。
2. 将解压后的全部内容上传到仓库根目录，保留 `.github/workflows/android-arm64.yml`。
3. 打开仓库的 **Actions** 页面，选择 **Build EliteMonsters Android ARM64**。
4. 点击 **Run workflow**，等待构建完成。
5. 在运行结果底部下载 `EliteMonsters-android-arm64-installable` 工件。
6. 将下载得到的 ZIP 导入 TEFManager，确认安装的是 0.4.0 测试修复版。

本版本的旅途、经典、专家、大师、传奇模式概率均为 100%，鼠标悬停精英怪时名称会显示红色“【精英】”标识。测试完成后，将 `EliteMonsters/mod.c` 中的概率数组改回正式值并重新运行 Actions：

```c
static const int g_spawn_chance_percent[5] = {2, 5, 10, 15, 20};
```
