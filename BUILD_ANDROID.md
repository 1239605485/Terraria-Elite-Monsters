# Android ARM64 构建与安装

## 需要的东西

- GitHub Actions（推荐）或本机 Android NDK + CMake。
- Android NDK 路径通过 `ANDROID_NDK_HOME` 或 `ANDROID_NDK_ROOT` 提供。

## GitHub Actions

仓库根目录保留 `.github/workflows/android-arm64.yml`，运行
`Build Origin Rewrite Android ARM64`。下载 artifact
`OriginRewrite-android-arm64-installable`，直接导入 TEFManager。

## 本机构建

```bash
ANDROID_NDK_HOME=/path/to/ndk bash scripts/package_android_arm64.sh
```

成功后在工程根目录得到 `OriginRewrite-android-arm64.zip`：

```text
OriginRewrite-android-arm64.zip
├─ Manifest.json
├─ Info.json
├─ OriginRewrite.json
└─ Resources/
   └─ lib/
      └─ libOriginRewrite.android.arm64.so
```

## 手机报“无法安装”排查

1. 导入的必须是上面这种根目录 ZIP，不能是源码目录打包的 ZIP。
2. ZIP 根目录必须有 `Manifest.json`，内容 `type=Mod`、
   `file=OriginRewrite.json`、`parentLoader=eternal.future.kernelloader`。
3. `Resources/lib/` 下必须有编译好的
   `libOriginRewrite.android.arm64.so`，不能放源码或空目录。
4. 版本、ABI 和 `targetGameVersion` 需与 TEFManager/Terraria 匹配。

## 当前框架顺序

```text
NPC.SetDefaults Postfix
   只记录 PendingInit + 基准
        ↓
NPC.AI() 首次 active=true
   执行 SpawnCommitted（一次 roll + 一次属性应用）
        ↓
stat_write / readbackLifeMax 回读验证
        ↓
（下一层：AI 状态机、掉落）
```

禁止回到“SetDefaults 里直接提交并宣称精英已生效”的旧写法。
