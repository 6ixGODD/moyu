# 技术需求：Agent Runtime 与记忆

## 1. Runtime 模型

实现一个受限 BDI（Belief-Desire-Intention）runtime，不实现通用多 Agent 框架。

核心状态：

```text
belief_store   16~32 条事实：subject/predicate/object/confidence/source/updated_at
drive_state    4~6 个需求：value/decay/last_trigger
intention      当前唯一计划：goal/step/status/budget/permission/deadline
episode_store  有意义经历的结构化记录
habit_state    从重复反馈形成的策略权重
relationship   用户对领域/工具的允许、拒绝、纠正统计
```

调度器必须是确定性 C 状态机；Lua 负责候选 desire、规则评分和事件反应；LLM 只负责低频解释、归纳或生成候选，不拥有调度权。

每个 intention 必须可恢复、可取消、可超时。工具调用前后均写入状态，崩溃恢复后不得重复执行 `mutate` 步骤。

## 2. 工作目录

MOYU 的唯一持久工作目录为：

```text
~/.moyu/
  SOUL.md
  MEMORY.md
  state.db
  config.json
  logs/
  collections/
  drafts/
```

Windows 上 `~` 展开为当前用户 profile，例如 `%USERPROFILE%\.moyu`；不得写入程序安装目录。首次运行时创建目录和缺失的默认文件，不覆盖已有用户文件。

## 3. 两个固定上下文文件

每次启动固定加载且每次 LLM completion 固定注入以下上下文，顺序不可变：

1. `~/.moyu/SOUL.md`
2. `~/.moyu/MEMORY.md`

聊天和自主意图使用相同的基础上下文，之后才拼接当前 belief、active intention、最近 episode 和用户消息。

读取失败时使用内置默认值并记录一次诊断；不得循环弹错。

### 3.1 SOUL.md

`SOUL.md` 是基本设定和不可自行改变的行为宪法，包括：

- 名字、物种感、说话倾向和边界。
- 它是桌面生物而非生产力助手的定位。
- 对隐私、权限、诚实和危险操作的约束。
- 面对不确定信息时允许形成误解，但必须标记为 belief 而非事实。
- 哪些特征可以被用户长期改变。

用户可以直接编辑；MOYU 只读。文件变化应在下一次安全唤醒或聊天开始时重新加载，不要求文件系统高频 watcher。

### 3.2 MEMORY.md

`MEMORY.md` 是用户可读、可编辑的长期叙事记忆，参考 coding agent 的持久指令文件思想，但内容属于 MOYU 自己：

```markdown
# About the human
- ...

# Habits I have formed
- ...

# Things I believe
- [confidence: 0.65] ...

# Important episodes
- 2026-06-20: ...

# Ongoing obsessions
- ...
```

要求：

- `MEMORY.md` 是长期自然语言记忆的事实来源，必须易于人工删除和纠正。
- MOYU 可以自主写入，但不能“每次事件都写”或纯随机改写。
- 所谓随机写入是 **符合提升条件的 episode 经过带概率的 memory promotion**，概率受重要性、新颖性、重复次数、用户反馈和人格影响。
- 用户明确说“记住这个”时跳过随机门，但仍进行去重和安全过滤。
- 用户明确说“忘掉这个”时必须从 Markdown 和相关结构化索引删除。
- 自主写入只允许追加或更新受控 section，不得整体重写用户手工内容。
- 写入采用临时文件 + flush + 原子替换；保留一个 `.bak`。
- 默认最大 64 KiB；接近上限时合并重复项，不得无限增长。
- 任何密码、token、私钥、完整消息正文和未经允许的敏感文件内容禁止进入长期记忆。

## 4. SQLite 使用边界

只有结构化、需要事务或精确恢复的数据进入 `~/.moyu/state.db`：

- episodes 与工具调用审计。
- beliefs、drives、habits、relationships。
- active intention、步骤状态和幂等键。
- MCP server/tool 元数据和缓存 TTL。
- permission decisions、LLM/MCP 日预算。
- 聊天会话索引与未读状态。

以下内容不进入 SQLite：

- `SOUL.md` 和 `MEMORY.md` 正文。
- 大体积 MCP 原始响应。
- 用户文件副本。

SQLite 必须静态链接或作为已审计的单文件 C 依赖 vendored；启用 WAL 不是默认要求。数据库 schema 必须有 `schema_version` 和顺序迁移。数据库损坏时保留原文件、创建新库并向用户报告，不能静默清空记忆文件。

## 5. Context compose

现有 64 节点内存 ring 降级为“本次运行的短期工作记忆”，不再冒充唯一状态来源。

一次 completion 的上下文预算按以下顺序组装：

```text
SOUL.md
-> MEMORY.md 中相关 section
-> active intention
-> top beliefs/habits
-> 最近 8~16 个 episodes
-> 当前对话或事件
-> 当前可用工具 schema（仅需要时）
```

必须按 token/字符预算截断；优先保留 SOUL、安全规则、当前意图和用户当前消息。禁止把整个数据库、全部工具响应或完整历史直接塞给模型。

## 6. 资源与可靠性

- 继续单进程、事件驱动；允许 stdio MCP 子进程，但必须按需启动或明确配置常驻。
- 无 active intention 时不运行规划器。
- 不为记忆增加后台 embedding、向量库或索引服务。
- Markdown 相关性首版使用 section、标签和关键词匹配。
- SQLite 操作批量提交；普通 idle tick 不落盘。
- 所有外部动作拥有 request id、幂等键、deadline 和取消路径。
- 日志不得记录 API key、Authorization header 或敏感工具参数。

