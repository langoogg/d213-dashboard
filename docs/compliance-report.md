# D213 Dashboard — 开源合规审查报告

> 审查日期：2026-07-16 | 版本：v1.0

---

## 模块一：SDK 许可证兼容性审查

### 1.1 许可证清单

| 组件 | 路径 | 许可证 | 传染性 | 允许再分发 | 备注 |
|------|------|--------|--------|-----------|------|
| **Luban-Lite SDK** | `~/luban-lite/` | Apache-2.0 | 无 | ✅ | Gitee README 声明，无 root LICENSE 文件 |
| **RT-Thread** | `kernel/rt-thread/` | Apache-2.0 | 无 | ✅ | 独立项目，root 有 LICENSE 文件 |
| **LVGL v8** | `~/lv_sim/lvgl/` | MIT | 无 | ✅ | LICENCE.txt 在源码根目录 |
| **Zalink 蓝牙库** | `zalink/lib/libzablue.a` | **无声明** | 未知 | ❓ | 二进制 blob，zalink_v1.4.zip 中发布，无 LICENSE |
| **Zalink 互联库** | `zalink/lib/libzalink.a` | **无声明** | 未知 | ❓ | 同上 |
| **MPP 解码库** | `aicp-dec/libmpp_aicp_dec_*.a` | **无声明** | 未知 | ❓ | ArtInChip SDK 中的二进制库，无 LICENSE |
| **AIC8800 WiFi 固件** | `aic8800/host/libwlan_*.a` | **无声明** | 未知 | ❓ | 二进制 blobs |
| **Montserrat 字体** | LVGL 内置 | SIL OFL 1.1 | 无 | ✅ | 可商用，需保留版权声明 |
| **Linux Kernel** | 板端运行，不在仓库 | GPL-2.0 | 强 Copyleft | ✅ | 不影响应用层代码 |
| **Buildroot** | 板端运行，不在仓库 | GPL-2.0 | 强 Copyleft | ✅ | 不影响应用层代码 |
| **BusyBox** | 板端运行，不在仓库 | GPL-2.0 | 强 Copyleft | ✅ | 不影响应用层代码 |

### 1.2 兼容性结论

**你的应用层代码（main_fb.c、dashboard/ 下所有 .c/.h）可以使用 MIT 开源。**

理由：
1. LVGL (MIT) — 无传染性
2. RT-Thread (Apache-2.0) — 无传染性，你只是 link
3. Luban-Lite SDK (Apache-2.0) — 同上
4. GPL-2.0 的 Linux/Buildroot/BusyBox 运行在板端，你的应用通过系统调用和动态链接使用，GPL 的"传染性"不跨越系统调用边界

**风险点**：Zalink 和 MPP 解码库缺少 LICENSE 声明。这些是二进制 blob，理论上 ArtInChip 分发给开发者时应该附带许可说明。建议：
- 方案 A：README 中声明"本项目依赖 ArtInChip SDK 中的二进制库，请遵守 ArtInChip 的许可条款"
- 方案 B：向黄工/ArtInChip 确认这些 .a 文件的许可条款

### 1.3 许可证建议

**推荐：Apache-2.0**

- 与 SDK/RT-Thread 保持一致
- 比 MIT 多一层专利保护
- LVGL (MIT) 兼容 Apache-2.0

**备选：MIT**

- 更简单，最低限制
- 但缺少专利授权条款

---

## 模块二：代码敏感信息扫描

### 2.1 发现的问题

| 文件 | 行号 | 敏感内容类型 | 风险等级 | 修复建议 |
|------|------|-------------|---------|---------|
| `Makefile.d213` | 1 | 绝对路径 `/home/lh/d211/toolchain/...` | 🔴 高 | 用变量 `$(TOOLCHAIN)` 替代，从环境变量读取 |
| `Makefile.d213` | 3 | 绝对路径 `/home/lh/d211/output/...` | 🔴 高 | 用变量 `$(SYSROOT)` 替代 |
| `Makefile.d213` | 4 | 绝对路径 `/home/lh/d211/output/...` | 🔴 高 | 用变量 `$(SYSROOT)` 替代 |

### 2.2 无问题项

| 检查项 | 结果 |
|--------|------|
| 硬编码 IP 地址 | ✅ 未发现 |
| 域名/URL | ✅ 未发现 |
| 密码/Token/API Key | ✅ 未发现 |
| 邮箱地址 | ✅ 未发现 |
| 作者真实姓名 | ✅ 未发现（代码中未标注作者） |
| Wi-Fi SSID/密码 | ✅ 未发现硬编码 |
| 串口参数 | ✅ 未发现 |
| 车辆 VIN/车牌 | ✅ 未发现 |
| 内部 Bug 编号 | ✅ 未发现 |

### 2.3 修复方案

将 `Makefile.d213` 中的绝对路径替换为变量：

```makefile
# 修改前
CC = /home/lh/d211/toolchain/riscv64-linux-glibc-x86_64-V2.10.1/bin/riscv64-unknown-linux-gnu-gcc
SYSROOT = /home/lh/d211/output/d211_demo128_nand/host/riscv64-linux-gnu/sysroot

# 修改后
TOOLCHAIN_DIR ?= /opt/riscv64-toolchain
SYSROOT      ?= /opt/artinchip-sdk/sysroot
CC            = $(TOOLCHAIN_DIR)/bin/riscv64-unknown-linux-gnu-gcc
```

---

## 模块三：第三方资源版权审查

### 3.1 资源清单

| 资源名 | 用途 | 来源 | 许可证 | 允许再分发？ | 需要署名？ | 建议 |
|--------|------|------|--------|------------|-----------|------|
| Montserrat 字体 | 仪表盘 UI 文字 | LVGL 内置 / Google Fonts | SIL OFL 1.1 | ✅ | 名字保留 | ✅ 保留 |
| danking.mp4 | 视频播放测试 | 不详 | 未知 | ❓ | ❓ | ⚠️ 替换为自制测试视频 |
| 测试 WAV 文件 | 音频播放测试 | 不详 | 未知 | ❓ | ❓ | ⚠️ 替换为自制音频 |
| 测试 MP4 文件 | 视频解码测试 | 不详 | 未知 | ❓ | ❓ | ⚠️ 替换或删除 |

### 3.2 替代方案

| 需要替换的资源 | 免费替代方案 |
|--------------|------------|
| danking.mp4 | 用 ffmpeg 自制测试视频：`ffmpeg -f lavfi -i testsrc=duration=10:size=1280x720:rate=30 test.mp4` |
| 测试 WAV | 用 Audacity 生成 440Hz 正弦波测试音 |

### 3.3 README 第三方资源致谢

```markdown
### 第三方资源

- **LVGL** — MIT License. Copyright (c) 2021 LVGL Kft. https://lvgl.io/
- **Montserrat Font** — SIL Open Font License 1.1. Copyright (c) The Montserrat Project Authors.
- **ArtInChip D21x SDK** — Apache License 2.0. Copyright (c) ArtInChip Technology Co., Ltd.
- **RT-Thread** — Apache License 2.0. Copyright (c) 2006-2023, RT-Thread Development Team.
```

---

## 模块四：生成 LICENSE 和 DISCLAIMER.md

（见下方独立输出文件）

---

## 开源合规检查清单

| 检查项 | 状态 | 说明 |
|--------|------|------|
| SDK 许可证兼容性 | ⚠️ 需确认 | 应用层可 MIT，但 zalink/MPP .a 文件缺 LICENSE，需向 ArtInChip 确认 |
| 代码敏感信息 | ⚠️ 发现 3 处 | Makefile.d213 含 `/home/lh/` 绝对路径，需用变量替代 |
| 第三方资源版权 | ⚠️ 需替换 3 项 | danking.mp4 + 测试 WAV/MP4 来源不明，建议自制测试文件 |
| 项目 LICENSE | Apache-2.0 | 与 SDK/RT-Thread 一致，专利保护更完善 |
| 免责声明 | 已生成 | DISCLAIMER.md |
