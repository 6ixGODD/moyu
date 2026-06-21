# 现有系统迁移、开发阶段与验收

> 状态：本文件描述的 v0.2 迁移已经实现。P0 用例由 `moyu-tests.exe`、`--smoke-llm` 和最终 GUI 启动验收覆盖。

## 1. 保留

以下现有能力保留并演进：

- C11 单进程核心、Lua 5.4 sandbox、cJSON。
- 原生透明窗口、软件 RGBA 渲染、像素 sprite/skin 系统。
- platform 边界与 Windows 首发策略。
- OpenAI-compatible completion adapter 和调用预算概念。
- tool registry 的统一入口。
- 事件队列、短期 context ring、基本日志和内存统计。
- 无网络、无 API key 时可运行。

## 2. 删除

- 删除“随机 idle thought 就是微型叙事”的产品逻辑。
- 删除 C 和 Lua 各自重复的 idle poke/随机吐槽规则，只保留一个 policy 来源。
- 删除点击必定 happy 的硬编码。
- 删除无事件因果的每秒情绪随机游走，改为事件驱动 + 低频衰减。
- 删除重启后人格和上下文全部清零的行为。
- 删除旧 HTTP MCP 作为标准 MCP 的文档和实现假设。
- 删除“LLM 只是旁白且不能工具调用”作为架构原则；替换为“runtime 调度，LLM 可提出候选但不能越权执行”。
- 删除 README 中当前已不真实的“完整 agent runtime”“MCP 可从 Lua 调用”等声明，直到对应验收通过。

## 3. 修改

### Context

当前 64 节点 ring 改为 session working memory；长期状态迁移到 SQLite 与 `MEMORY.md`。

### Persona/Emotion

四个 float 保留为初始 bias，不再是人格全部。新增 habits、relationships、beliefs；情绪由真实事件更新并随时间衰减。

### Lua

Lua 从动画脚本升级为 policy 层，至少新增：

```text
on_event(event)
propose_desires(state)
score_intention(candidate, state)
on_tool_result(result)
on_memory_candidate(episode)
```

新增受控 API：`moyu.tool`、belief/drive/intention 查询、memory proposal、permission request。Lua 不直接访问 OS、SQLite、secret 或任意文件。

### LLM

支持统一 context compose、取消、超时、JSON structured response。规划输出只能成为候选 intention，必须经 C runtime 校验工具、参数、预算和权限。

### Rendering loop

平台事件、runtime timer、MCP IO 和聊天输入统一唤醒事件循环。聊天关闭且无动画时不得固定重绘。

## 4. 新增

- `~/.moyu/` 工作目录、默认 `SOUL.md`、`MEMORY.md`。
- SQLite 状态仓库和 schema migration。
- BDI runtime、单 intention 状态机、崩溃恢复和幂等。
- memory promotion、遗忘、纠正、去重和敏感信息过滤。
- 终端聊天、收件箱、解释与权限交互。
- MCP stdio 与 Streamable HTTP transports。
- 工具风险分级、scope、affordance 与调用审计。
- 收藏、草稿、active project 和桌面拾荒者规则包。
- 自动化测试：状态机、数据库迁移、路径越界、MCP fixture、权限和崩溃恢复。

## 5. 推荐模块变化

建议新增或拆分，最终命名可遵循现有扁平 `src/` 风格：

```text
agent.{h,c}        BDI 状态机和 intention 执行
state.{h,c}        SQLite repository 与 migration
memory.{h,c}       SOUL/MEMORY 加载、promotion、原子写
permission.{h,c}   scope 与风险决策
mcp_stdio.{h,c}    子进程 transport
mcp_http.{h,c}     Streamable HTTP transport
chat.{h,c}         对话状态与 compose
tools/openchat.c   独立原生终端聊天客户端
collection.{h,c}   收藏和草稿
```

`loop.c` 只负责事件泵和模块协调，不继续堆业务规则。`main.c` 只负责装配、配置和生命周期。

## 6. 开发阶段

### M0：修正文档与基线

- 更新 README，明确当前产品能力与 v0.2 目标。
- 给现有行为补最小回归测试或可运行 harness。
- 测量二进制、工作集、idle CPU 和启动时间，作为后续对比基线。

### M1：连续生命

- 创建工作目录、SOUL、MEMORY、SQLite。
- 实现 episode、belief、habit、relationship 与 active project 持久化。
- 实现 memory promotion、记住、忘记和人工编辑重载。
- 不接 MCP 也能运行桌面拾荒者的本地规则。

### M2：真正的 Agent Runtime

- 实现 drive -> desire -> intention -> step -> result 状态机。
- 实现最多 3 步、预算、超时、取消、崩溃恢复和解释摘要。
- Lua policy 接入，LLM 只生成受约束候选。

### M3：人与它的关系

- 原生 TUI 聊天入口、收件箱、记住/忘记、暂停和权限命令。
- 用户反馈能实质改变 belief 与 habit。

### M4：MCP 感官

- stdio、Streamable HTTP、生命周期和工具 schema。
- affordance、权限分级和调用审计。
- 至少跑通 git stdio、weather HTTP、notes 权限升级三个 fixture。

### M5：打磨与发布

- 7 天 soak test、断网/崩溃/坏数据库/坏 MCP 测试。
- 更新文档、示例 SOUL/行为包、迁移说明。
- 重新测量资源，未满足预算不得发布。

## 7. P0 验收用例

1. 首次启动创建 `~/.moyu/SOUL.md`、`MEMORY.md`、`state.db`，再次启动不覆盖人工修改。
2. 用户说“记住我不喜欢工作时弹窗”，重启后仍遵守；“忘掉它”后 Markdown 和索引均删除。
3. 自主发现满足 promotion 条件时可能写入 MEMORY；普通 tick 和重复事件绝不写入。
4. 用户纠正一个 belief 后，其置信度下降或删除，后续行为改变且可解释。
5. active project 在进程被强制终止后恢复；未确认的 mutate 不执行，已提交步骤不重复。
6. 没有 LLM/API key 时，仍可形成发现、收藏、belief、habit 和跨重启 project。
7. stdio MCP 完成 initialize、tools/list、tools/call；server 退出后无死循环和僵尸进程。
8. Streamable HTTP 正确处理 session 与 JSON/SSE 响应；断网时退避。
9. 未授权路径、`..`、symlink/junction 越界均被拒绝并记录。
10. 未知 MCP 工具没有 affordance 时不能被自主调用，但用户可在聊天中显式请求并授权。
11. TUI 关闭后进程完全退出；桌宠和任何提示不抢焦点。
12. 连续运行 24 小时后无明显内存增长，空闲 CPU 与 v0.1 基线同级。

## 8. 资源预算

在 v0.1 实测基线之上设回归门槛，而不是只看绝对值：

- 主程序二进制仍以 <=10 MB 为目标，绝对上限 20 MB。
- MOYU 自身 idle 工作集目标 <=50 MB，绝对上限 100 MB；外部 MCP server 单独统计。
- 无聊天、无动画、无 active intention 时 idle CPU 接近 0。
- `state.db` 和 MEMORY 默认总量保持在 MB 级以下；大工具响应不入库。
- MCP/LLM 调用次数必须能从诊断页和数据库审计核对。

## 9. Definition of Done

v0.2 只有在以下陈述都真实时才能发布：

- 它有跨重启的记忆、信念、习惯和一个可恢复的小计划。
- 人类能通过聊天、纠正、允许、拒绝、记住和忘记改变它。
- MCP 是它新增感官与行动方式的协议，而不是闲置 registry。
- 它可以解释行为依据，但不泄露内部 prompt、秘密或伪造思维链。
- 它在无网络时仍是一只完整、有连续性的桌宠。
- 新能力没有以高频轮询和常驻重型 runtime 换取。
