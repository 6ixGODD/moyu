# MCP 与工具需求

## 1. 支持范围

MOYU 只支持 MCP 当前两种标准 transport：

1. **stdio**：MOYU 作为 client 启动本地 server 子进程，通过 stdin/stdout 交换 JSON-RPC；stderr 仅作日志。
2. **Streamable HTTP**：单一 MCP endpoint，支持 HTTP POST，按协议处理 JSON 或 SSE 响应、session id 和协议版本协商。

删除当前自定义“向 URL POST 两个 JSON-RPC 方法就算 HTTP MCP”的假设。不得继续把旧实现称为完整 MCP 支持。

首版 MCP client 必须覆盖：

- initialize / initialized 生命周期。
- capability negotiation 与协议版本。
- tools/list 和分页。
- tools/call、错误与超时。
- server 断开、进程退出和受控重连。
- Streamable HTTP session 管理。
- request id、取消和日志脱敏。

首版不要求 resources、prompts、sampling、elicitation 和 OAuth；server 宣告但客户端不支持时必须明确忽略或报 capability mismatch。

## 2. 第三方 C 实现策略

可以在 `third_party/` 引入已有 C/C++ MCP 实现，但必须先通过技术评估：

- 许可证可兼容 MIT 且保留声明。
- 支持 stdio 与 Streamable HTTP，不是旧 HTTP+SSE transport。
- 不引入 Node/Python runtime、Boost、Qt、WebView 或显著体积依赖。
- 可在 Windows clang-cl/MSVC 静态构建。
- JSON 层能复用 cJSON 或依赖成本可接受。
- transport、session、取消和生命周期有可测试接口。

若没有同时满足条件的成熟 C 实现，首版自行实现最小 MCP client。禁止为了“third_party 一个实现”引入比 MOYU 本体更重的 runtime。选型决定必须写入 `docs/adr/`，包含版本、commit、许可证和体积变化。

## 3. 工具统一模型

```text
ToolDef {
  id
  name
  description
  input_schema
  source: builtin | mcp
  risk: observe | draft | mutate
  scope
  timeout_ms
}

ToolCall {
  request_id
  intention_id
  tool_id
  input_json
  status
  output_summary
  started_at / finished_at
  idempotency_key
}
```

工具结果先做大小限制和敏感信息过滤，再进入 episode。原始大响应只在调用期存在，默认不持久化。

## 4. 应直接内置的工具

内置工具用于 MOYU 自身生命活动，避免为了基本能力启动 MCP server：

### 系统与桌宠

- `system.now`：本地时间、时区、日期。
- `system.idle_time`：用户空闲时长。
- `system.foreground_app`：仅返回进程/应用名；默认关闭，需授权。
- `pet.say`、`pet.move`、`pet.animate`：表达能力。
- `pet.notify_request`：产生不抢焦点的待处理请求。

### 自身工作目录

- `memory.read_sections`、`memory.propose`、`memory.forget`。
- `collection.add/list/remove`。
- `draft.create/list/remove`。
- `runtime.explain/cancel_intention`。

这些工具只能访问 `~/.moyu/`，不等于通用 filesystem 工具。

### 最小只读文件观察

首版可内置一个严格受限的 `filesystem.observe`，仅对用户显式添加的 roots 生效，支持：

- 列目录元数据。
- 读取文件名、大小、mtime。
- 按 glob 查找。
- 读取小型文本文件的受限片段，必须独立授权。

不得内置删除、移动、覆盖用户文件。复杂文件操作交给 MCP。

## 5. 不应内置、应通过 MCP 提供的能力

- GitHub/GitLab 等远程代码平台。
- 日历、邮件、聊天软件。
- 天气、网页搜索、浏览器自动化。
- 音乐播放器、智能家居。
- 数据库、完整文件系统写操作。

原因是这些能力有独立认证、权限和更新周期，不应污染 600 KB 级核心。

## 6. 仓库内参考 MCP servers

为验证 client 和展示玩法，后续在 `examples/mcp/` 提供三个极小参考 server；它们是开发/演示工具，不随 MOYU 默认常驻：

### `moyu-mcp-weather`

- `weather.current(location)`，`observe`。
- 可使用固定 fixture 离线测试；联网 provider 通过配置替换。
- 用于验证 Streamable HTTP、缓存 TTL 和“天气感官”。

### `moyu-mcp-git`

- `git.recent_commits(root, limit)`，`observe`。
- `git.stale_branches(root)`，`observe`。
- `git.todo_candidates(root)`，`observe`。
- 仅调用本地 git CLI，不执行 commit/push/reset/checkout。
- 用于验证 stdio 与“代码嗅觉”。

### `moyu-mcp-notes`

- `notes.create_draft(title, body)`，`draft`。
- `notes.publish(draft_id)`，`mutate`，每次确认。
- 用于验证权限升级、幂等和恢复。

若坚持纯 C 示例，优先实现 git stdio server；weather HTTP server 可先使用测试 fixture。不要把三个 server 的网络 SDK 全部静态塞进主程序。

## 7. 工具如何改变行为

每个工具或 MCP server 注册时除 schema 外，还应配置一个 `affordance`：

```text
domain: code | weather | calendar | files | music | unknown
senses: [history, anomaly, recency]
actions: [observe, draft]
default_drive_effect: curiosity +0.1
```

Lua policy 根据 affordance 生成候选行为。未配置 affordance 的未知 MCP 工具只能在用户聊天中显式调用，不能自主使用。这样接入 MCP 才会产生新行为类别，而不是只增加工具列表。

## 8. 权限与安全

- `observe`：首次按 server + tool + scope 授权，可持久化。
- `draft`：可持久授权，但产物只能进入 `~/.moyu/drafts/` 或外部系统草稿态。
- `mutate`：默认每次确认；不可撤销动作禁止自主执行。
- stdio server 的 command、args、cwd、env 必须来自配置，不允许 LLM 构造。
- 子进程环境默认剥离无关 secrets；单独白名单注入。
- 路径 scope 必须 canonicalize，防止 `..`、symlink/junction 越界。
- HTTP 只默认允许 HTTPS；localhost 可允许 HTTP。

