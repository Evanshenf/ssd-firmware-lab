<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# NVMe FWLab：开源 SSD 固件开发与仿真实验室

[English](README.md) · [简体中文](README.zh-CN.md) ·
[项目主页](https://evanshenf.github.io/ssd-firmware-lab/#chinese) ·
[入门说明](docs/getting-started.md) ·
[预览版本](https://github.com/Evanshenf/ssd-firmware-lab/releases/tag/v0.1.0-spine-preview.1)

**NVMe FWLab（仓库名 `ssd-firmware-lab`）用于在 Linux 中学习、开发和验证 NVMe SSD 固件。**
项目包含可修改的 C 固件源码、FTL 闪存地址转换层、GC 垃圾回收、NFC NAND 闪存控制器模型，以及持久化 NAND 页、OOB 和恢复记录。
初步软件实验不需要专用 FPGA 或 SSD 开发板。

当前已打通实验性的原生 Linux NVMe 数据路径，也完成同一软件 PCI function 在宿主与 QEMU 客户机之间顺序、独占切换的指定实验。
项目仍是 **pre-alpha 开发者预览**，不是可以刷入普通商用 SSD 的生产固件。

当前发布版本为 **v0.1.0-spine-preview.1**：固定 **1 MiB namespace、文件 NAND 后端、有限运行预算**。
128 GB、512 GB、1 TB 等大容量尚未实现或验证。已完成能力和风险以[版本结果](docs/results/2026-09-05-vertical-spine-preview.md)为准。

**开发分支已超过上述旧标签。** 截至 `a6ee009`，已加入可扩展 FTL、物理 NAND v2／PAGE2，
以及显式选择的 **64 MiB 原生 namespace、最大 1 MiB I/O、两个 I/O 队列和三个 MSI-X 向量**。
两个队列仍共享一个 I/O buffer，FTL 串行执行，不是并行吞吐承诺。
容量构造现已共用 **64／256／65536 MiB** 配置。当前 PAGE2／v2 路径已通过 ARM64 **64 GiB 满写、覆盖、GC、恢复及全盘回读**；
原生 **256 MiB NVMe** 已通过 Linux reset／恢复和 ext4 挂载验证。详见[容量验证记录](docs/results/2026-09-10-current-capacity.md)。
这不等于原生 64 GiB、ARM PCI 或整盘性能已经验证；早期 C3／v1 大容量结果仍单独保留。
见[当前能力与平台边界](docs/current-status.md)、[开发证据](docs/results/2026-09-10-scaled-storage-mq2.md)
和[分层性能与全部成对样本](docs/results/2026-09-10-throughput.md)。本次公开开发成果，不改旧标签，也不宣称新的冻结版本。

## 先在 Linux 上运行软件检查

准备 Git、GNU Make 和带 Linux/POSIX 开发头文件的 C11 编译器，用普通用户执行：

```sh
git clone --branch v0.1.0-spine-preview.1 https://github.com/Evanshenf/ssd-firmware-lab.git
cd ssd-firmware-lab
make -C frontends/linux-m4 check-runtime
```

这条命令运行已有的软件集成检查，数据实际经过固件、FTL、NFC 和临时文件 NAND，覆盖读写、持续覆盖、GC、Flush、重启恢复及有限资源耗尽处理。
它不加载内核模块、不需要 KVM、不写入原始块设备，也**不会创建 `/dev/nvme*`**。
输出中的 `native_hif=not_connected` 是该软件检查的预期边界，不是原生 NVMe 绑定失败。

[入门说明](docs/getting-started.md)提供依赖、输出解释和完整软件检查入口。
实际创建可供 Linux `nvme` 驱动使用的设备，需要另外准备专用实验虚拟机，见[原生 PCI/HIF 指南](kernel/m4-native/README.md)。

## 实际数据路径

```text
Linux 原生 nvme 驱动 / 独占使用的 QEMU 客户机
  → 软件 PCI/HIF：BAR、队列、Host 数据传输、CQE 和中断
  → NVMe 命令策略与公共命令生命周期
  → Block 接口 → FTL 映射、GC、日志与恢复
  → NFC 控制器模型
  → 持久化 NAND 页、OOB、健康元数据和 WAL
  → 完成意图 → HIF 发布 CQE / IRQ
```

BAR 与控制器运行状态在内存中。`nand.bin` 保存的是模拟 NAND 的物理介质状态，不是固件可执行代码，也不是让 NVMe 按 LBA 直接读写的普通磁盘镜像。
namespace 的数据不能绕过 FTL、NFC 和 NAND 模型。

固件运行在 Linux 用户态进程；内核模块负责软件 PCI/HIF。
QEMU 所有权切换使用上游 `vfio-pci` 和 IOMMUFD，不另外建立一套 NVMe/FTL 执行器。
切换前需要停止写入、明确执行 Flush/FUA、关闭使用者，再完成撤销、排空和零引用检查；宿主和客户机不能同时占有同一 function。

## 当前能研究什么

- NVMe 初始化、Identify、最小 SMART、队列建立、Read/Write/Flush 和写 FUA。
- FTL 逻辑到物理映射、部分页读改写、异地写入和前台 GC。
- NAND 请求调度、阶段性读／编程／擦除、建模的 ECC、重试与故障。
- 映射日志、checkpoint、GC 中断和同一介质的重启恢复。
- 原生驱动 reset、解绑重绑，以及指定的宿主／客户机所有权切换。

这些是限定范围内的软件执行结果，不等于完整 NVMe 命令集、真实 NAND 时序或实体掉电验证。
查看[精确源码与测试结果](docs/results/2026-09-05-vertical-spine-preview.md)，不要将早期独立模块的通过记录当成额外产品能力。

## 从哪些源码开始看

| 主题 | 入口 |
| --- | --- |
| NVMe 命令策略 | [Linux-profile-v1](core/command-spine/profiles/linux_profile_v1_adapter.c) |
| 命令生命周期 | [共享生命周期](core/command-spine/spine_lifecycle.c) |
| FTL 地址映射与元数据 | [mapping](core/m3p/m3p_mapping.c)、[codec](core/m3p/m3p_codec.c) |
| 垃圾回收与恢复 | [GC](core/m3p/m3p_gc.c)、[recovery](core/m3p/m3p_recovery.c) |
| NAND 控制器 | [NFC 模型](nfc/README.md) |
| NAND 持久化介质 | [file-NAND](media/file-nand-v0/) |
| 原生设备连接 | [用户态固件](frontends/linux-m4/README.md)、[内核 PCI/HIF](kernel/m4-native/README.md) |

新路径的入口是 [scalable FTL](core/ftl-scale/README.md)、[PAGE2 NFC](core/nfc-page-v2/README.md)、
[物理 NAND v2](media/file-nand-v2/README.md) 和[原生 scaled／MQ2 启动说明](docs/native-scaled-usage.md)。
上表保留旧标签的参考实现入口。

## 阶段限制与后续方向

旧预览标签固定为一个 1 MiB namespace、512 字节 LBA、最大 8 KiB 传输和一个深度 32 的 I/O 队列对；开发分支的不同构建见上面的能力表。
命令、元数据序号及介质事务存在有限预算，重新打开介质不会清除持久化历史。
原生实验已验证的环境是专用 x86-64 Linux 虚拟机、Ubuntu `7.0.0-30-generic` 和明确预留的 16 KiB BAR 空间；不能推断任意内核均可直接加载。

更大的原生容量、进一步的 FTL／磨损均衡算法、独占原始块设备后端、RTOS 平台和实体 FPGA／SoC／NAND 适配属于后续工作。
实体 NAND 的擦除代数等信息由谁持久化、如何恢复，仍有契约需要落实，不能宣称只替换文件后端就完成硬件移植。
并发 FLR 与完成发布之间还有一项未确认风险，详见版本结果。

首次创建 NAND 镜像必须显式使用 `--format`；正常启动恢复已有 UUID 对应的介质。
不要用于保存真实业务数据，也不要把实验指向普通物理 SSD。原生实验前请阅读[安全说明](SECURITY.md)。

## 参与与反馈

欢迎[提交 issue](https://github.com/Evanshenf/ssd-firmware-lab/issues)，提供版本／commit、Linux 环境、复现命令、预期和实际输出；请先去除主机身份、私有路径等信息。
贡献方式和签署要求见 [CONTRIBUTING.md](CONTRIBUTING.md)。开发使用 AI 辅助，但不以模型结论代替源码审查和实际测试，见 [AI_ASSISTED.md](AI_ASSISTED.md)。

原创用户态源码使用 BSD-3-Clause，Linux 内核源码使用 GPL-2.0-only，Markdown 文档使用 CC-BY-4.0；具体以 [LICENSES.md](LICENSES.md) 和各文件 SPDX 为准。
项目独立开发，不隶属于 NVM Express，也不声称官方背书或认证。
