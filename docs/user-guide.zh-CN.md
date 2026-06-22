# MOYU 中文使用指南

## 这东西到底能干什么

MOYU 现在由两个协作进程组成：

- `moyu.exe`：桌面上的小家伙，同时承载自治 Agent Runtime。
- `moyu-chat.exe`：类似 Claude Code/Codex 的交互式终端，用来聊天、查看状态、管理记忆和调用工具。

两者共用 `~/.moyu/`。关掉终端不会杀掉桌宠；重启也不会丢掉人格、记忆、权限、未完成意图、收藏和草稿。

已经实现的能力如下，不是规划清单：

| 能力 | 实际行为 | 在哪里查看或操作 |
| --- | --- | --- |
| 灵魂设定 | 每次启动固定加载身份、原则和行为边界 | `~/.moyu/SOUL.md` |
| 长期记忆 | 保存关于人类、习惯、信念和重要经历的可读文本 | `MEMORY.md`、`/memory`、`/remember`、`/forget` |
| 情景状态 | 保存对话、发现、信念、反馈、意图、权限和预算 | `state.db`、`/status` |
| Agent Runtime | 同时只运行一个 intention，最多三步，有截止时间和调用预算 | `/status`，或直接问“你在做什么” |
| 可被人类塑造 | 纠正会降低错误信念置信度；记住、忘记、授权会影响以后行为 | 自然对话和终端命令 |
| 授权观察 | 只在授权目录内读取文件名、大小等元数据，并阻止软链接/目录联接越界 | 首次启动授权、`filesystem.observe` |
| 收藏 | 把值得保留的发现写成独立 Markdown 卡片，并回流到桌面 GUI | `/collect`、拖放投喂、`collections/`、悬停反馈卡片 |
| 草稿 | 生成只留在本地的 Markdown 草稿，不会自己发布 | `/draft`、`/drafts`、`drafts/` |
| MCP | 通过 stdio 或 Streamable HTTP 发现和调用外部工具 | `config.json`、`/tools`、`/tool` |
| LLM 对话 | 把 SOUL、MEMORY 和相关 SQLite 状态组合为上下文，调用当前模型 | 在 `moyu-chat.exe` 里直接输入 |
| Vision 分析 | 用独立多模态模型查看图片，并把结果回灌给桌宠 | `vision` 配置、拖放图片 |
| 离线模式 | 没有模型时仍能使用记忆、收藏、草稿、权限和本地状态 | 所有本地斜杠命令 |

这里的“收藏”不是浏览器书签。它是 MOYU 认为值得留下的发现，例如一个奇怪的小项目、一种反复出现的工作习惯，或者你明确让它保存的东西。每条收藏都是普通 Markdown 文件。现在一旦收藏发生变化，MOYU 会把这件事回显到桌面 GUI：弹出反馈卡片，并记住“最近收好的一样东西”。草稿则是尚未发布的候选文本，只能停留在自己的工作目录，必须由人类决定后续用途。

## 启动和交互

启动桌宠：

```bat
build\moyu.exe
```

双击桌宠会在它旁边打开一个像素风快捷输入框。输入框会限制在当前显示器工作区内，默认只有一行，随内容自动长到五行，更多内容改为文本区内部滚动。按 Enter 或圆形勾选按钮发送，Shift+Enter 换行。

需要完整会话时，右键系统托盘图标并选择 **Open Terminal Chat**，会打开新的终端聊天窗口。也可以直接启动：

```bat
build\moyu-chat.exe
```

Windows 系统托盘小图标使用跟随系统明暗主题的原生右键菜单，只保留系统级操作：打开终端聊天、打开收藏和退出。右键桌宠本体则会打开 MOYU 自己的 companion panel。普通气泡和反馈卡片不会抢键盘焦点。

额外的桌面交互：

- 把文件或目录拖到桌宠身上，它会把这次投喂写成收藏，并立即给出可视反馈。
- 拖入文本/代码文件时，它会本地读取片段，再让主模型做摘要，不依赖服务端“文件解析”能力。
- 拖入图片时，如果 `vision` 已配置，它会调用独立多模态模型分析图片内容。
- 长按并拖动桌宠，可以给它换一个新的停靠位置。
- 连续摸它几下会触发不同的情绪反应，而不是永远同一种点按反馈。
- 鼠标靠近、拖动状态和 companion panel 的反馈都已经接进桌面 GUI，不再只是一个静态托盘程序。

## 终端怎么用

普通文字会连同当前 SOUL、MEMORY 和相关运行状态一起发给当前配置的 LLM。斜杠命令在本地执行，即使 LLM 离线也能用：

```text
/help                         查看命令
/status                       查看模型、工作目录、MCP 和最近状态
/config                       查看当前配置路径、密钥路径、base_url 和 model
/model qwen-plus              立即切换模型并写回 config.json
/baseurl https://dashscope.aliyuncs.com/compatible-mode/v1
/apikey sk-...                把密钥写入 Windows DPAPI
/pause                        暂停自治行为
/resume                       恢复自治行为
/cancel                       取消当前 intention
/memory                       查看长期记忆
/remember 我不喜欢频繁提醒     写入长期记忆
/forget 频繁提醒               查找并删除相关记忆，需要输入 YES 确认
/collections                  列出收藏
/collect 有趣的项目 | 一个值得以后再看的 C 项目。
/drafts                       列出草稿
/draft 更新说明 | 介绍新的终端聊天入口。
/tools                        列出 MCP 工具、来源和风险等级
/allow git.recent_commits *   持久授权一个外部只读工具
/tool git.recent_commits {"root":"D:\\WorkSpace\\project"}
/clear                        重绘终端
/exit                         只关闭聊天终端
```

删除记忆必须输入大写 `YES`。调用 `mutate` 类型的 MCP 工具必须输入大写 `RUN`。这样即使粘贴了一段命令，或者模型生成了危险文本，也不会直接改变外部系统。

## 工作目录里有什么

```text
~/.moyu/
  SOUL.md             基本人格和边界；程序不会自行修改
  MEMORY.md           人类可以直接读写的长期记忆
  MEMORY.md.bak       上一次原子写入前的备份
  config.json         LLM、MCP、隐私与自治配置
  secrets.dat         Windows DPAPI 加密的模型密钥
  state.db            结构化运行状态和审计记录
  collections/*.md    收藏的发现
  drafts/*.md         尚未发布的私人草稿
  logs/               运行日志
```

直接编辑 `SOUL.md` 或 `MEMORY.md` 会影响之后的行为。重启会重新加载两者，`/memory` 会立即重新加载 MEMORY。`config.json` 也是从这里加载的，不会编译进二进制。记忆写入器会拒绝常见密钥、Token 和密码模式，`/apikey` 会把模型密钥写进 `secrets.dat`，由 Windows DPAPI 加密保存。`config.json` 现在还支持 `vision`、`owner` 和 `appearance.skin`。

## Agent Runtime 到底在做什么

桌宠不是靠随机动画假装活着。Runtime 会维护信念、驱动力、习惯、关系和 intention。每次只允许一个 intention，最多三步；每一步执行前都会检查工具风险、授权范围和预算。桌宠空闲且人类长时间无操作时，才可能在授权目录内做低频只读观察。发现会先进入情景记忆；只有重要度、新颖度和人格偏好达到阈值时，才可能提升进 `MEMORY.md`。

人类可以随时纠正它、撤销观察授权、暂停自治、取消 intention、删除记忆，或者直接修改两个 Markdown 文件。重启后，未完成 intention 会从 SQLite 恢复；超时任务会失败，而不会重放外部修改。

## MCP 与内置工具

MCP server 配置在 `~/.moyu/config.json`。当前支持 initialize/initialized、tools/list、tools/call 和 ping，传输支持本地 stdio 与 Streamable HTTP。`/tools` 会展示动态发现的工具、服务来源和 `observe/draft/mutate` 风险分类。

构建产物中自带三个真实参考实现：

- `moyu-mcp-git.exe`：stdio；查看仓库状态、近期提交和分支。
- `moyu-mcp-notes.exe`：stdio；创建草稿，或者在明确确认后发布。
- `moyu-mcp-weather.exe`：本地 Streamable HTTP；通过 Open-Meteo 查询天气。

无需 MCP 的本地能力已经直接内置：当前时间、用户空闲时间、前台应用（需要权限）、长期记忆、收藏、草稿、Runtime 解释/取消，以及限定目录的文件元数据观察。自治 Runtime 可以使用这些内置工具；常见的人类操作在 TUI 中有更直观的专用命令。

具体 MCP 配置见 [mcp.md](mcp.md)。

## 权限与隐私边界

- 初始状态只能看到自己的 `~/.moyu`，观察其他目录必须先授权根目录。
- 文件观察只读取名称、大小、目录标记等元数据，不会扫描任意文件正文。
- `observe` 可以持久授权；`draft` 只能产生私人草稿；`mutate` 必须由人类显式发起并二次确认。
- 密钥使用 Windows DPAPI 保存在 `secrets.dat`，不会明文写入用户配置。
- 常见凭据会被记忆过滤器拒绝；日志和工具审计不会故意保留完整敏感载荷。
- 数据库损坏时，原文件会保留为 `state.db.corrupt-<时间戳>`，然后新建干净数据库。
- Markdown 使用原子写入并保留 `.bak`，中断不会覆盖最后一个完整版本。

## 当前边界

Windows 是 v0.2 完整支持平台，Linux/macOS 后端仍是可编译桩。MCP 暂未实现 resources、prompts、sampling、OAuth 和独立 HTTP GET 事件流。TUI 刻意保持原生、轻量、零新增运行时依赖：它支持 UTF-8、ANSI 色彩、持久上下文、本地命令和显式工具调用，但不是 Shell，也不是代码编辑器。
