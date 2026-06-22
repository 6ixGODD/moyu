# Lua Policy API

Lua defines behavior policy and appearance. It does not own persistence, permissions or scheduling.

## Sandbox

Available libraries: base, math, string, table, utf8. File IO, OS, package loading, debug, FFI, dynamic load and `collectgarbage` are unavailable.

## Queries

```lua
moyu.idle_seconds()
moyu.mouse_distance()
moyu.mood()          -- valence, arousal
moyu.personality()   -- mood_bias, sarcasm, curiosity, patience
moyu.beliefs()       -- bounded JSON context
moyu.drives()        -- bounded JSON state
moyu.intention()     -- JSON explanation
```

## Actions

```lua
moyu.say(text, duration_ms?)
moyu.anim(name)
moyu.emit(name, payload?)
moyu.llm(prompt)                 -- queues async reflection; result arrives later
moyu.tool(name, args_json)       -- observe/draft only, permission checked
moyu.propose_memory(text)
moyu.request_permission(tool, scope?)
moyu.log(text)
```

`moyu.tool` returns result JSON or `nil,error`. Mutating tools cannot be called from autonomous Lua. `moyu.llm` no longer blocks the UI and therefore does not synchronously return generated text.

## Hooks

```lua
function on_tick(idle_s, mouse_dist, valence, arousal) end
function on_click(button) end
function on_event(name, payload_json) end
function propose_desires(state_json) return nil end
function score_intention(candidate, state) return 0 end
function on_tool_result(result) end
function on_memory_candidate(episode) end
```

The runtime calls all hooks when their corresponding event occurs. A desire must return a table containing `goal`, `tool` and JSON-string `args`; `score_intention` must return a positive number to admit it. C still validates risk, permission, budget and deadline before execution.

## Appearance

```lua
moyu.use_skin_builtin()
moyu.use_skin_bmp(path, frame_w, frame_h)
moyu.set_anim(name, {frame_indices...}, fps)
```

Animation names: idle, blink, sleep, happy, sad, observe, walk, work, wait, found, confused, giveup, bored, sneak, scared, playful, peek, dodge, stretch, yawn.
