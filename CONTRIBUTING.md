# Contributing to d213-dashboard

## 开发环境搭建

```bash
# 1. 克隆仓库
git clone https://github.com/langoogg/d213-dashboard
cd d213-dashboard

# 2. 编译（交叉编译到板端）
make

# 3. 模拟编译（PC 端调试 UI）
make sim
```

**依赖**：`riscv64-unknown-linux-gnu-gcc` V2.10.1, LVGL v8.3.10, SDL2（仅模拟编译）

## 提交 Issue

### 报告 Bug

使用 Bug 报告模板（`.github/ISSUE_TEMPLATE/bug_report.md`），必须包含：
- 硬件型号（D213ECV-DEMO-V4-0 或其他）
- SDK/编译器版本
- 复现步骤
- 期望行为 vs 实际行为
- 最小代码片段（如有）

### 功能请求

使用功能请求模板（`.github/ISSUE_TEMPLATE/feature_request.md`），说明：
- 这个功能解决什么问题
- 建议的实现方案（如有）
- 是否愿意自己实现

## 提交 Pull Request

### 分支命名

```
feat/your-feature-name    # 新功能
fix/your-bug-fix          # Bug 修复
docs/your-doc-update      # 文档
refactor/your-refactor    # 重构
```

### Commit Message 规范

```
feat: add CAN bus integration to dashboard
fix: resolve FBIOPAN page flip deadlock on timeout
docs: update README with wiring diagram
refactor: split dashboard.c by display mode
```

### PR 提交前检查

- [ ] 代码通过编译（`make` 和 `make sim`）
- [ ] 在 D213ECV 板端实际测试通过
- [ ] 新功能有对应的文档更新
- [ ] 提交历史干净（rebase 到 main，无 merge commit）
- [ ] 通过 `clang-format` 格式化

## 代码审查

审查者会关注：
1. 内存安全（嵌入式 128MB RAM，不允许 malloc 泄漏）
2. 错误处理（所有系统调用和 ALSA/MPP API 都有错误检查）
3. LVGL 最佳实践（不阻塞主线程、合理使用 post_draw_cb）
4. 触摸坐标使用 `ui_config.h` 宏，不硬编码
