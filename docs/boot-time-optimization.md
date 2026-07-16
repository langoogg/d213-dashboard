# 启动时间优化方案

> 项目：d213-dashboard | 平台：ArtInChip D211 RISC-V | 版本：v1.0 | 日期：2026-07

## 1. 启动时间测量体系

### 1.1 各阶段插入时间戳

```c
// U-Boot 阶段 (arch/riscv/cpu/aic/spl.c)
uint64_t t0 = timer_get_us();
// ... init DDR ...
printf("[BOOT] SPL init: %llu us\n", timer_get_us() - t0);
```

```c
// Kernel 阶段 — 使用 printk_time 或 GPIO 翻转
// 内核启动参数添加：printk.time=1 quiet
```

```c
// 应用层 (core/main.c)
#include <time.h>
struct timespec ts;
clock_gettime(CLOCK_MONOTONIC, &ts);
uint64_t app_start_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

// ... LVGL init, framebuffer init ...
clock_gettime(CLOCK_MONOTONIC, &ts);
uint64_t ui_ready_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
printf("[STARTUP] App init: %llu ms, UI ready: %llu ms\n",
       app_start_ms, ui_ready_ms - app_start_ms);
```

### 1.2 日志解析脚本

```python
#!/usr/bin/env python3
"""Parse boot time from UART log"""
import re, sys

stages = {'SPL': 0, 'DDR': 0, 'KERNEL': 0, 'ROOTFS': 0, 'APP': 0}
for line in sys.stdin:
    for stage in stages:
        m = re.search(rf'\[{stage}\]\s+(\d+)', line)
        if m: stages[stage] = int(m.group(1))

total = sum(stages.values())
for k, v in stages.items():
    print(f"{k:10s}: {v:6d} ms  ({v/total*100:5.1f}%)")
print(f"{'TOTAL':10s}: {total:6d} ms")
```

## 2. 分阶段优化策略

### 2.1 Bootloader (U-Boot)

```bash
# U-Boot config 裁剪
# defconfig 中去掉不用的驱动
# CONFIG_USB=n
# CONFIG_NET=n
# CONFIG_CMD_NET=n
# CONFIG_CMD_USB=n
# CONFIG_DM_GPIO=n（如果不用 GPIO）
# CONFIG_LED=n
```

**Falcon mode**：跳过 U-Boot 命令行，直接加载 kernel：

```bash
# 设置环境变量
fw_setenv bootdelay -2           # 禁止等待按键
fw_setenv silent 1                # 静默输出
fw_setenv bootcmd "mmc read ${loadaddr} ${kernel_offset} ${kernel_size}; bootm ${loadaddr}"
fw_setenv boot_os 1               # Falcon mode
```

**预期效果**：U-Boot 从 800ms → 300ms

### 2.2 Linux Kernel

```bash
# 内核配置裁剪 (.config)
# 去掉不用的驱动
# CONFIG_USB_SUPPORT=n（如果不插 USB）
# CONFIG_WIRELESS=n（如果不用 WiFi 启动阶段）
# CONFIG_SOUND=n（如果不用音频启动阶段）
# CONFIG_PRINTK=n
# CONFIG_EARLY_PRINTK=n

# 内核启动参数
# 在 device tree 或 bootargs 中添加：
quiet loglevel=0 initcall_debug=0
```

**使用 initramfs**：把最小 rootfs 打包进内核镜像，避免挂载文件系统延迟。

```bash
# Buildroot: BR2_TARGET_ROOTFS_INITRAMFS=y
```

**预期效果**：Kernel 从 1200ms → 600ms

### 2.3 Rootfs

```bash
# Buildroot 关键选项
BR2_TARGET_GENERIC_REMOUNT_ROOTFS_RW=n        # 只读挂载
BR2_PACKAGE_BUSYBOX_CONFIG="busybox-minimal.config"  # 最小 BusyBox
BR2_TOOLCHAIN_BUILDROOT_LIBC="musl"           # musl libc 比 glibc 快约 30%
BR2_STATIC_LIBS=y                             # 应用静态链接，加快加载
```

**精简启动脚本**：

```bash
# /etc/init.d/rcS — 只保留必要服务
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
ifconfig lo up
/d213-dashboard &
```

### 2.4 应用层

**LVGL 资源预编译**：

```c
// 将字体、图片提前编成 C 数组，链接进 ELF
// tools/font_to_c.py ← 把 .ttf 转成 LVGL 字体 C 文件
// 避免运行时读取文件系统

extern const lv_font_t my_font_16;
extern const lv_font_t my_font_32;

// 预初始化 Framebuffer
static uint32_t fb_cache[1024*600];  // 4096KB, 可在 BSS 段
memset(fb_cache, 0, sizeof(fb_cache));  // 清屏一次
```

**延迟加载非关键模块**：

```c
// 只加载 Mode 0 UI，音频/视频模块按需加载
// 使用 lv_timer 延迟初始化
lv_timer_create(deferred_init_audio, 500, NULL);   // 0.5s 后初始化音频
lv_timer_create(deferred_init_wifi, 1000, NULL);   // 1s 后初始化 WiFi
lv_timer_create(deferred_init_video, 2000, NULL);  // 2s 后初始化视频
```

## 3. 时间分配目标

| 阶段 | 优化前 (ms) | 优化后 (ms) | 优化手段 |
|------|-----------|-----------|---------|
| U-Boot SPL | 200 | 100 | 裁剪驱动、禁用输出 |
| U-Boot U-Boot | 600 | 200 | Falcon mode、禁用 USB/NET |
| Kernel init | 800 | 400 | 裁剪驱动、initramfs、quiet |
| Rootfs mount | 400 | 200 | 只读 squashfs、musl libc |
| App init + UI | 500 | 300 | 静态链接、资源预编译 |
| **总计** | **2500** | **1200** | — |

## 4. 启动脚本示例

```bash
#!/bin/sh
# /etc/init.d/S99dashboard — Auto-start script

START_TS=$(awk '{print $1*1000}' /proc/uptime)

# 初始化硬件
echo 1 > /sys/class/graphics/fb0/blank
echo 0 > /sys/class/graphics/fb0/blank

# 启动应用（后台）
/d213-dashboard < /dev/zero &
PID=$!

# 等待 UI 就绪
sleep 2
UI_TS=$(awk '{print $1*1000}' /proc/uptime)
ELAPSED=$((UI_TS - START_TS))
echo "[STARTUP] UI ready in ${ELAPSED} ms"

wait $PID
```

## 5. 验证与测试

### 5.1 重复测量

```bash
# 自动化冷启动时间测试（10 次）
for i in $(seq 1 10); do
    echo "=== Test $i ==="
    # 继电器断电 → 上电 → 等待串口输出 → 记录第一次 UI 刷新
    sleep 2
done
# 取平均值，去掉最高最低
```

### 5.2 排除测量误差

| 方法 | 精度 | 适用阶段 |
|------|------|---------|
| 串口日志时间戳 | ±10ms | U-Boot / Kernel |
| GPIO 翻转 + 逻辑分析仪 | ±1µs | 全阶段 |
| JTAG 断点 | ±1µs | 全阶段 |
| 应用内 `clock_gettime` | ±1ms | 应用层 |

> **建议**：正式测试用 GPIO 翻转法——在关键阶段拉高 GPIO，用逻辑分析仪抓波形，误差 < 1µs。
