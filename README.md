# CustomMCUAlgorithom

鑫工 T48 编程器（Xgpro 上位机）的定制算法 DLL。烧录时通过 DLL 回调接口，自动完成 **MAC 地址生成与注入、序列号维护、烧录日志记录**，并把烧录结果上报给上位机，实现批量产测的自动化。

产物名为 **`5620GEN.dll`**。

## 功能特性

- **MAC 自动生成与注入**
  - 采用 MTK 标准：MAC 后 24 位每次 **+8 递增**，末尾恒为 `0` 或 `8`，同一批次不重复。
  - 首次（无历史）随机生成基址并末尾对齐 `0`；之后基于上次值 `+8`。
  - 通过配置的 `mac_offset` 将 6 字节 MAC 覆盖写入固件指定地址。
- **序列号维护**：烧录前后对 `pSerial` 做 `+1` 自增，供下一台设备使用。
- **烧录日志**：每次成功烧录追加一行记录，支持**中文路径**与自动创建目录。
- **独立配置文件**：所有参数集中在 `5620GEN.json`，支持独立配置与状态回写。
- **固件加载**：从配置的 `firmware` 路径读取源固件填充缓冲区。
- **结果上报**：向 Xgpro 上报本次修改的区域（CODE 区、地址、长度），供上位机追溯。
- **多目录定位配置**：`5620GEN.json` 按以下优先级搜索，方便放置在上位机目录随程序分发：
  1. DLL 同目录
  2. 上位机（Xgpro.exe）所在目录
  3. 当前工作目录

## 使用步骤

1. 将 `5620GEN.dll` 放入 Xgpro 插件的算法目录（或用上位机选择该 DLL）。
2. 将 `5620GEN.json` 放置于上位机目录或 DLL 同目录。
3. 按需编辑 `5620GEN.json` 配置（见下）。
4. 启动 Xgpro，选择对应芯片并执行烧录。烧录完成后可查看日志文件确认 MAC、序号等。

> 从 Xgpro 目录随程序分发时，把 `5620GEN.json` 放到与 `Xgpro.exe` 同级即可被自动识别。

## 配置文件 `5620GEN.json`

| 字段 | 说明 | 示例 |
|------|------|------|
| `firmware` | 源固件文件路径（用作基础数据加载） | `"D:/Xgpro/FW_EN/ArcherC5v4.bin"` |
| `mac_offset` | 主 MAC 注入的固件偏移地址 | `"0x7DF100"` |
| `mac_prefix` | MAC 前缀，3 字节 OUI | `"78:44:FD"` |
| `last_mac` | 上次使用的 MAC（自动维护，用于下次 +8） | `"78:44:FD:5A:4C:F0"` |
| `log_file` | 烧录日志文件路径（支持中文目录） | `"D:/E程序/5620EN.txt"` |

示例：

```json
{
  "firmware": "D:/Xgpro/FW_EN/ArcherC5v4.bin",
  "mac_offset": "0x7DF100",
  "mac_prefix": "78:44:FD",
  "last_mac": "78:44:FD:5A:4C:F0",
  "log_file": "D:/E程序/5620EN.txt"
}
```

> 说明：`last_mac` 由 DLL 烧录时自动回写，无需手动维护。`mac_offset` 默认 `0x7DF100`；`mac_prefix` 默认 `78:44:FD`。

## 日志格式

日志为文本文件，每烧一台追加一行，`;` 分隔：

```
序号;MAC(去冒号,12位);序列号;wifi名;wifi5g名
```

示例：

```
1;7844FD5A4CF0;117C792123456;TP-Link_4CF0;TP-Link_4CF0_5G
2;7844FD5A4CF8;117C792123457;TP-Link_4CF8;TP-Link_4CF8_5G
```

- `序号`：日志已有行数 + 1，从 1 开始。
- `MAC`：6 字节去冒号的大写十六进制（12 位）。
- `wifi 名`：`TP-Link_` + MAC 最后两位字节；`wifi5g 名` 在此基础上追加 `_5G`。

## 构建

本项目由 GitHub Actions 云端编译，编译目标为 **32 位（x86）Release** DLL。手动本地构建时：

- 使用 Visual Studio（含 C++ 桌面开发）打开 `CustomMCUAlgorithom.sln`。
- 选择 **Release | x86** 配置进行编译，产物即为 `5620GEN.dll`。

## 工程结构

```
CustomMCUAlgorithom/
├── .github/workflows/build.yml     # 云端编译工作流
├── CustomMCUAlgorithom.sln
└── CustomMCUAlgorithom/
    ├── 5620GEN.json                # 独立配置文件
    ├── CustomMCUAlgo.cpp/.h        # 核心实现
    ├── CustomMCUAlgo.def           # DLL 导出定义
    └── CustomMCUAlgorithom.vcxproj
```

## 注意事项

- 配置中的 `firmware`、`log_file` 路径支持**中文目录**，内部已按宽字符处理（兼容 UTF-8 与本地 ANSI 编码）。
- 写入配置/日志时会自动创建缺失的父目录。
- 请保持 `5620GEN.json` 唯一（只放在一个位置），避免多副本导致写入路径不一致。