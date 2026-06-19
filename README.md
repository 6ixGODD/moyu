# MOYU 桌宠 Agent Runtime v0.1

一个用 C + Lua 实现的 <10MB 像素桌宠 agent runtime。详见 `TODO.md`。

## 构建

仓库内置 Lua 5.4 与 cJSON 源码，无需任何外部依赖。

### Windows（首选，使用 clang 直接编译）

```bash
./build.sh
```

产物：`build/moyu.exe`。需要 `clang` 在 PATH 中（已通过测试：clang 21 + Win10/11）。
也可以用 CMake + MSVC：

```bat
cmake -B build -S .
cmake --build build --config Release
```

### Linux / macOS

平台抽象层提供了 stub，编译可过，运行时会报 "not implemented"。
完整实现 X11/libcurl 或 Cocoa/NSURLSession 留作后续工作。

## 配置

可执行文件同目录下需要 `assets/config.json` 与 `scripts/default.lua`。
`build.sh` 与 CMake 都会自动把仓库内的 `assets/` 与 `scripts/` 复制到产物旁。

```json
{
  "llm": {
    "base_url": "https://api.deepseek.com/v1",
    "api_key": "",                   // 留空 → 退化纯 rule-based
    "model": "deepseek-chat",
    "max_tokens": 256,
    "temperature": 0.8,
    "daily_limit": 50
  },
  "personality": { "mood_bias": 0.0, "sarcasm": 0.3, "curiosity": 0.6, "patience": 0.7 },
  "rules": { "idle_poke_seconds": 300, "rare_event_chance": 0.02 },
  "mcp_servers": [                   // 可选，动态加载 MCP 工具
    { "url": "http://127.0.0.1:8080/mcp", "api_key": "" }
  ]
}
```

## 运行

```bash
./build/moyu.exe
```

桌面右下角出现一只蓝色像素小生物。行为：

- 鼠标靠近 → observe（眼睛跟随）
- 长时间无操作 → sleep + "Z"
- 空闲超过 5 分钟 → 5%/秒概率弹吐槽
- 点击 → happy 表情
- 配置了 LLM api_key → 极低概率自发生成一句话（点缀层）
- 未配置 LLM → 完全 rule-based，不崩溃

## 架构

详见 `TODO.md`。简而言之：

- **唯一的抽象层**：`src/platform/platform.h` — 窗口、事件、HTTP、时间、文件系统
  - Win32 用 `CreateWindowExW(WS_EX_LAYERED)` + `UpdateLayeredWindow` + `WinHTTP`
  - Linux/macOS stub
- **其余模块直接实现**，无多余抽象：
  - `core/` — context ring buffer、event queue、personality、emotion、loop
  - `render/` — 软件光栅化 + 程序化生成的 32×32 像素 sprite
  - `llm/` — OpenAI-compatible completions + LRU cache
  - `tools/` — 轻量调度器 + MCP HTTP 客户端
  - `script/` — Lua 5.4 沙箱（禁用 io/os/package/loadfile/require）

## Lua API（沙箱内可用）

```lua
moyu.idle_seconds()   -> number
moyu.mouse_distance() -> number
moyu.mood()           -> valence, arousal
moyu.personality()    -> mood_bias, sarcasm, curiosity, patience
moyu.emit(name, payload?)
moyu.say(text, duration_ms?)
moyu.anim(name)       -- idle|blink|sleep|happy|sad|observe
moyu.llm(prompt)      -> string|nil
moyu.log(msg)

-- Hooks (optional):
function on_tick(idle_s, mouse_dist, valence, arousal) end
function on_click(button) end
```

## 体积

`moyu.exe` 约 1-2 MB（含 Lua + cJSON），远低于 10MB 上限。Sprite 程序化生成，无外部资源文件。
