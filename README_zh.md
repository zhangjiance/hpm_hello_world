# hpm_hello_world

面向 HPMicro RISC-V MCU 的**示例应用程序（APP）**，需与 [`hpm_dfu_boot`](../hpm_dfu_boot) DFU 引导程序配合使用。它作为 bootloader 跳转进入的用户固件运行，并额外提供 **DFU runtime** 接口，使主机可以请求其重启回到 bootloader 进行固件升级。

> English overview: [README.md](./README.md). 编译指导： [README_build.md](./README_build.md)（英文）/ [README_build_zh.md](./README_build_zh.md)（中文）。

---

## 工程作用

这是一个在 "hello world" 基础上扩展 DFU runtime 支持的示例：

- 通过 UART 串口打印 `hello world` 并闪烁 LED。
- 以复合 USB 设备形式初始化 USB（`src/usb_desc.c`），包含：
  - **DFU runtime** 接口：响应 `DFU_DETACH`（即 `dfu-util -e`），将 DFU 触发魔数写入保留寄存器（BGPR / PDGO）后重启进入 bootloader；
  - **CDC ACM VCOM** 接口（虚拟串口）：将收到的数据回传（loopback）。
- 可选地，长按用户按键约 500 ms（连续 5 次检测）也会触发 `hpm_dfu_reboot_to_dfu()`。

本工程用于与 `hpm_dfu_boot` bootloader 配对：bootloader 占据 Flash 前 128K，本 APP 链接在其之后运行。

### 内存布局

| 项目 | 值 |
| --- | --- |
| Bootloader 预留区 | 前 128K（`_dfu_bl_length=0x20000`） |
| APP 加载/起始地址 | `0x80020000` |
| 链接脚本 | HPM SDK `flash_dfu.ld`（`HPM_BUILD_TYPE=flash_dfu`） |

> APP 起始地址（`0x80020000`）与 bootloader 的 `USBD_DFU_APP_DEFAULT_ADD` 一致，因此 bootloader 在校验完 4 字节 DFU 签名后可直接跳转至此。

---

## 目录结构

```
hpm_hello_world/
├── CMakeLists.txt          # 工程构建脚本（APP：flash_dfu，预留 128K 给 bootloader）
├── CMakePresets.json       # 各 board 的 Release/Debug 预设
├── boards/                 # 各目标板的板级配置（BOARD_SEARCH_PATH）
├── src/
│   ├── hello_world.c       # 主函数：串口打印、LED 闪烁、按键进入 bootloader
│   ├── hpm_dfu_trigger.c   # 触发检测 / 重启到 DFU（与 bootloader 共用）
│   ├── hpm_dfu_trigger.h
│   ├── usb_desc.c          # 复合 USB 设备：DFU runtime（DETACH -> 进入 boot）+ CDC ACM VCOM（loopback）
│   └── usb_config.h        # 本地 CherryUSB 配置
├── README.md               # 工程说明（英文）
├── README_zh.md            # 工程说明（中文，本文件）
├── README_build.md         # 编译指导（英文）
└── README_build_zh.md      # 编译指导（中文）
```

---

## 如何编译

详见 **[README_build.md](./README_build.md)**（英文）/ **[README_build_zh.md](./README_build_zh.md)**（中文）。

```bash
cmake --preset hpm5301evklite-release
cmake --build --preset hpm5301evklite-release
```

产物位于 `build/<preset>/output/hello_world.elf` 与 `hello_world.hex`。

---

## 典型使用场景（配合 hpm_dfu_boot）

1. 编译并将 `hpm_dfu_boot` 烧录到 Flash 起始位置（前 128K）。
2. 编译本 APP 并烧录到 `0x80020000`（例如进入 bootloader 模式后通过 DFU 下载）。
3. 复位后，bootloader 检测到有效 APP 签名并跳转至本应用；串口打印 `hello world`，LED 闪烁。
4. 如需升级固件：在主机执行 `dfu-util -e`（或长按用户按键约 500 ms）——APP 会重启进入 bootloader，随后即可通过 DFU 下载新固件。
