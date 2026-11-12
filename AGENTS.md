# AGENTS.md — `cJSON`（ct-pkg 薄指针）

组织契据在 `../index/AGENTS.md`（`index` 仓库同名文件）。本文只记本包实例化；
必守核心、角色权限、门禁含义一律以契据为准，本文件只能加严。

spec-version: 1.0.0
spec-sha256: 93a8e968f76cefd5151e60b3d3c476a023910fa92dc768731467d1fa66aa2b67
spec-commit: 07e8cad

## 本包实例化

- 测试入口：`bash tests/run.sh`（默认 `CT=ct`，即 PATH 上的 0.24.0；门禁实际版本由首行
  `ct: ...` 打印）。四个 suite（`unit` / `chain` / `serialize` / `sugar`）要求
  `SUMMARY passed=N failed=0`；`tests/compile_fail/temp_receiver.ct` 用动态派发形状钉住仍然生效的
  临时值规则，必须按记录的诊断失败；`demo/main.ct` 输出与 `demo/expected.out` 逐字节比对。
- 编译器要求：`ct.pkg` 写 `requires = ct >= 0.20.3`（`pub type` 别名）；`cflags` 带
  `-I vendor/cJSON/third_party -DCJSON_NESTING_LIMIT=64`，`ldflags` 编译 cJSON；门禁在
  ct v0.24.0 上验证。0.24.0 起 type-host 链可出现在任意表达式位置，动态派发仍受临时值限制；
  0.20.3 上两者都受限。
- 模块类型：复杂模块 —— 「严格校验 → 树 → 渲染」三阶段，需先理解文档生命周期与 `drop()` 才能用对。
- 加严：`tools/check.sh` 是包内总门禁（构建 + 测试 + 静态检查）；cJSON 作为第三方源码固定版本
  放在 `third_party/`（不用 `vendor/`，否则 `ct get` 会跳过它）。
- 偏离：README 405 行 > 300（DOC-S1）；无 `docs/tutorial/`，教程在 README 内。
