# MOYU v0.2 需求索引

> 实现状态：v0.2 已于 2026-06-20 落地。本文档保留为需求与验收基线；实际使用说明以根目录 README 和 `docs/` 为准。

本目录是 MOYU 下一阶段开发的需求基线，供实现者直接拆分任务与验收。
仓库根目录的 `TODO.md` 是 v0.1 原始需求背景；若与本目录冲突，以本目录为准。

## 产品定义

MOYU 不再是“偶尔播放动画和吐槽的桌面挂件”，也不是通用聊天机器人。

MOYU 是一个寄居在用户电脑里的轻量自治生物：它有记忆、信念、需求、习惯和正在进行的小计划；它能被人类长期改变，并通过内置工具与 MCP 获得感官和行动能力。

首个明确玩法是 **桌面拾荒者**：MOYU 在用户授权的数字环境中发现被遗忘、异常或有趣的事物，将其形成收藏、误解、习惯和持续数日的小项目。

## 文档

- [01-product.md](01-product.md)：产品机制、核心循环、边界与成功标准
- [02-runtime-memory.md](02-runtime-memory.md)：Agent Runtime、`SOUL.md`、`MEMORY.md`、SQLite 与资源约束
- [03-interaction-ui.md](03-interaction-ui.md)：人与 MOYU 的交互、聊天入口与 UI 要求
- [04-mcp-tools.md](04-mcp-tools.md)：MCP transports、内置工具、参考 MCP 与权限模型
- [05-migration-acceptance.md](05-migration-acceptance.md)：现有能力的保留/删除/修改/新增、里程碑和验收

## 不可妥协约束

1. 空闲时 CPU 接近 0，不增加高频轮询，常规调度频率不高于 1 Hz。
2. 不引入 Electron、Node.js、Python runtime、WebView 或常驻云端编排服务。
3. 无网络、无 LLM、无 MCP 时仍然是一只有记忆和习惯的完整桌宠。
4. 一次只执行一个 intention；每个自主计划最多 3 个工具步骤，禁止无限 ReAct。
5. 默认不修改用户数据；任何外部写操作都必须通过权限门和可审计记录。
6. 有趣性必须来自持续状态、因果和真实后果，而不是随机台词或动画数量。

## 完成摘要

- [x] SOUL/MEMORY/SQLite 持久化、DPAPI secret 与损坏恢复
- [x] 单 intention BDI、Lua policy、预算、权限与跨重启恢复
- [x] 独立原生 TUI、首次引导、收件箱、解释、记住/忘记与权限命令
- [x] stdio 与 Streamable HTTP MCP、内置工具与三个参考服务
- [x] 奶油团子精灵、12 种状态动画与 dirty rendering
- [x] clean build、自动化测试、真实 MCP 互操作与 LLM smoke 入口

## 术语

- `belief`：MOYU 当前相信的事实，允许错误并带置信度。
- `drive`：好奇、无聊、依恋、收藏欲等缓慢变化的需求。
- `intention`：当前已经承诺执行的唯一小计划。
- `episode`：一次有意义的经历。
- `memory promotion`：把结构化经历提升为长期自然语言记忆。
- `observe/draft/mutate`：只读观察、生成草稿、改变外界三类工具权限。
