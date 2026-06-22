# MOYU

MOYU 是一只寄居在桌面的轻量自治生物，也是一套小型 C/Lua Agent Runtime。

它不是随机播放动画的挂件，也不是通用办公助手。它会在用户授权的范围内形成信念、习惯和一个正在进行的小计划，通过内置工具或 MCP 观察外界，把有意义的经历保存成用户可读的长期记忆，并允许人类纠正、塑造和让它忘记。

英文 README: [README.md](README.md)

## 核心能力

- 连续生命：`~/.moyu/SOUL.md`、`MEMORY.md` 与 SQLite 状态跨重启保留。
- 受限自主性：单 intention、最多 3 步、持久预算、权限和超时。
- 可塑性：允许、拒绝、纠正、记住和忘记会改变后续策略。
- 真实 MCP：支持 stdio 与 Streamable HTTP；动态发现并注册工具。
- 终端聊天：双击宠物或打开 `moyu-chat.exe`，与同一个 Runtime 对话，不引入 WebView 或 Electron。
- 桌面交互：托盘图标、Windows 原生右键菜单、拖放投喂、悬停反馈和拖拽重定位。
- 多模态投喂：文本文件本地预览后交给主模型摘要；图片可以交给独立 vision 模型分析。
- 低资源：事件驱动渲染，模型调用运行在后台 worker。

## 构建

Windows 需要 Visual Studio 2022 C++ Build Tools 与 `clang-cl`：

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

## 运行

```bat
build\moyu.exe
```

首次启动会创建 `~/.moyu/`、默认 SOUL 与 MEMORY 文件、SQLite 状态库，并弹出只读观察目录和低频自治的引导。

交互方式：

- 双击桌宠打开终端聊天。
- 右键桌宠打开 Windows 原生菜单。
- 右键托盘图标打开同一套 Windows 原生菜单。
- 把文件或目录拖到桌宠身上，相当于给它“投喂”一个要观察或收藏的对象。
- 直接拖动桌宠，可以给它换一个位置。

## 配置

长期配置位于 `~/.moyu/config.json`。

- 运行时配置从磁盘加载，不会编译进二进制。
- API key 存在 `~/.moyu/secrets.dat`，通过 Windows DPAPI 加密，而不是明文配置。
- 配置中可以包含 `llm`、`vision`、`owner`、`appearance`、`privacy`、`autonomy` 和 `mcp_servers`。

完整示例见 [assets/config.example.json](assets/config.example.json)。

stdio MCP 示例：

```json
{
  "name": "git-local",
  "transport": "stdio",
  "command": "D:\\path\\moyu-mcp-git.exe",
  "args": []
}
```

Streamable HTTP MCP 示例：

```json
{
  "name": "weather-local",
  "transport": "streamable_http",
  "url": "http://127.0.0.1:7788/mcp"
}
```

## 说明

- 当前完整支持的平台是 Windows。Linux 和 macOS 后端仍是可编译桩。
- `observe` 权限可以持久化，`draft` 只写私人输出，`mutate` 必须由人类显式触发。
- 用户文件不会整份复制进 SQLite，大型工具响应也不会长期保留。

## 文档

- [架构](docs/architecture.md)
- [设计](docs/design.md)
- [上下文与记忆](docs/context.md)
- [MCP 与工具](docs/mcp.md)
- [Lua Policy API](docs/lua_api.md)
- [构建与测试](docs/build.md)
- [用户指南](docs/user-guide.zh-CN.md)
- [英文用户指南](docs/user-guide.md)
- [v0.2 需求与验收](TODO/README.md)

## License

MIT。Lua 与 cJSON 使用 MIT；SQLite amalgamation 为 public domain。详见 `third_party/`。
