# MOYU

MOYU 是一只寄居在桌面的轻量自治生物，也是一套约 1.8 MB 的 C/Lua Agent Runtime。

它不是随机播放动画的挂件，也不是通用办公助手。它会在用户授权的范围内形成信念、需求、习惯和一个正在进行的小计划，通过内置工具或 MCP 观察外界，把有意义的经历保存成用户可读的长期记忆，并允许人类纠正、塑造和让它忘记。

## 核心能力

- **连续生命**：`~/.moyu/SOUL.md`、`MEMORY.md` 与 SQLite 状态跨重启保留。
- **受限自主性**：单 intention、最多 3 步、持久预算、权限和超时。
- **可塑性**：允许、拒绝、纠正、记住和忘记会改变后续策略。
- **真实 MCP**：支持 stdio 与 Streamable HTTP；动态发现并注册工具。
- **终端聊天**：双击宠物或右键打开独立 TUI，共享完整记忆与 Runtime，不引入 WebView/Electron。
- **桌面交互层**：自绘控制面板、收藏反馈卡片、拖放收藏和可拖拽重定位，让 GUI 本身承载互动。
- **离线完整**：没有 LLM/MCP 时仍可记忆、收藏、形成习惯和本地项目。
- **低资源**：事件驱动，LLM 在阻塞 worker 中运行，dirty frame 才重绘。

## 默认形象

默认角色是程序化生成的 48×48 “奶油团子精灵”：暖奶油主体、炭黑简笔轮廓、短耳芽、短尾和鼠尾草绿标记。它不依赖外部图片，支持 12 种与 Runtime 状态对应的动作：idle、blink、sleep、happy、sad、observe、walk、work、wait、found、confused、giveup。

## 构建

Windows 需要 Visual Studio 2022 C++ Build Tools 与 clang-cl：

```bat
build.bat
```

输出：

```text
build/moyu.exe
build/moyu-chat.exe
build/moyu-tests.exe
build/moyu-mcp-git.exe
build/moyu-mcp-notes.exe
build/moyu-mcp-weather.exe
```

运行测试：

```bat
build\moyu-tests.exe
```

测试当前 LLM 配置：

```bat
build\moyu.exe --smoke-llm
```

## 首次运行

```bat
build\moyu.exe
```

首次启动会：

1. 创建 `~/.moyu/`、默认 SOUL、MEMORY 和状态数据库。
2. 将旧 `assets/config.json` 的 API key 用 Windows DPAPI 迁移到 `secrets.dat`。
3. 询问是否授权一个只读观察目录。
4. 询问是否允许低频自主行动。

双击宠物打开 `moyu-chat.exe` 交互式终端；桌宠右键和系统托盘小图标会唤起自绘控制面板，可暂停自主行为、查看状态、打开收藏或工作目录并退出。也可以直接运行 `build\moyu-chat.exe`。

额外的桌面交互：

- 把文件或目录拖到桌宠身上，它会把这次投喂记成一条收藏，并给出即时 GUI 反馈。
- 长按并拖动桌宠可以给它换停靠位置。
- 连续摸它几下会触发不同的情绪反馈，而不只是随机动画。

聊天支持：

```text
/remember 我喜欢安静的提醒
/forget 安静的提醒
/status
/config
/model deepseek-v4-flash
/provider deepseek
/pause
/allow git.recent_commits *
/tool git.recent_commits {"root":"D:\\WorkSpace\\project"}
```

## 配置

长期配置位于 `~/.moyu/config.json`，运行时可在 TUI 中通过 `/config`、`/model`、`/baseurl`、`/provider` 查看和修改。API key 不写在配置文件里，而是保存在 `~/.moyu/secrets.dat`，并由 Windows DPAPI 加密。完整示例见 `assets/config.example.json`。

stdio MCP：

```json
{
  "name": "git-local",
  "transport": "stdio",
  "command": "D:\\path\\moyu-mcp-git.exe",
  "args": []
}
```

Streamable HTTP MCP：

```json
{
  "name": "weather-local",
  "transport": "streamable_http",
  "url": "http://127.0.0.1:7788/mcp"
}
```

## 设计边界

- Windows 是 v0.2 完整支持平台；Linux/macOS 后端仍为可编译 stub。
- MCP v2025-11-25，兼容服务端协商其他版本；当前实现 tools 能力，不实现 resources/prompts/sampling/OAuth。
- `observe` 可持久授权，`draft` 只产生草稿，`mutate` 必须由人类明确触发。
- 默认每天最多 6 次自主观察和 5 次自主 LLM；无有效意图时休眠。
- 用户文件不会被复制进数据库，大工具响应不会长期保存。

## 文档

- [架构](docs/architecture.md)
- [设计决策](docs/design.md)
- [记忆系统](docs/context.md)
- [MCP 与工具](docs/mcp.md)
- [Lua Policy API](docs/lua_api.md)
- [构建与测试](docs/build.md)
- [使用、隐私与恢复](docs/user-guide.md)
- [中文完整使用指南](docs/user-guide.zh-CN.md)
- [v0.2 需求与验收](TODO/README.md)

## License

MIT。Lua 与 cJSON 使用 MIT；SQLite 官方 amalgamation 为 public domain。详见 `third_party/`。
