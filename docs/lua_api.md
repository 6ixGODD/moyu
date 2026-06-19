# Lua API

The pet's behaviour lives in `scripts/default.lua`. The runtime loads this
file at startup into a sandboxed Lua 5.4 state and calls back into it on
every tick and click.

## Sandbox

Opened standard libraries: `base`, `math`, `string`, `table`, `utf8`.

Stripped (unavailable): `io`, `os`, `package`, `debug`, `ffi`, `loadfile`,
`dofile`, `require`, `load`, `collectgarbage`, `getfenv`, `setfenv`,
`rawequal`, `rawget`, `rawset`, `rawlen`.

There is no filesystem access, no subprocess, no `require`. The only way
for Lua to affect the outside world is the `moyu.*` API.

## `moyu.*` API

### Information queries

```lua
moyu.idle_seconds()      -- number: seconds since last user interaction
moyu.mouse_distance()    -- number: pixels from cursor to pet center
moyu.mood()              -- valence:number, arousal:number  (both in [-1, 1])
moyu.personality()       -- mood_bias, sarcasm, curiosity, patience (each [0, 1])
```

### Actions

```lua
moyu.say(text, duration_ms?)     -- show a speech bubble; default 3000 ms
moyu.anim(name)                  -- switch animation
                                 --   "idle" | "blink" | "sleep"
                                 --   | "happy" | "sad" | "observe"
moyu.emit(name, payload?)        -- queue an arbitrary internal event
moyu.llm(prompt) -> string|nil   -- synchronous LLM call (subject to throttle)
moyu.log(msg)                    -- write to moyu.log
```

`moyu.llm` is synchronous from Lua's point of view — it blocks the loop
until the HTTP round-trip completes. This is intentional: the pet visibly
"pauses to think." Returns `nil` if the LLM is disabled, the daily limit
is exceeded, or the request fails. The result is also pushed into the
context store as a `CTX_REFLECTION` node.

## Hooks

The runtime calls these if defined. Both are optional.

```lua
function on_tick(idle_s, mouse_dist, valence, arousal)
  -- called at ~1 Hz
end

function on_click(button)
  -- button: 0=left, 1=right, 2=middle
end
```

## Customising behaviour

To change the pet's personality, edit `scripts/default.lua` directly —
or replace it wholesale. The runtime loads whatever file sits at
`<exe_dir>/scripts/default.lua`.

Common customisations:

### Change speech lines

```lua
local poke_lines = {
  "又摸鱼？",
  "...",
  "困了",
}
```

CJK characters render via `platform_get_glyph` — they work out of the box.

### React differently to clicks

```lua
function on_click(button)
  if button == 0 then
    moyu.anim("happy")
    moyu.say("嘿", 2000)
  elseif button == 1 then
    moyu.anim("sad")
  end
end
```

### Gate LLM calls behind personality

```lua
function on_tick(idle_s, mouse_dist, valence, arousal)
  if idle_s > 600 and math.random() < 0.002 then
    local _, _, curiosity, _ = moyu.personality()
    if curiosity > 0.5 then
      local reply = moyu.llm("One short idle thought.")
      if reply then moyu.say(reply, 4000) end
    end
  end
end
```

### Driving animations from mood

```lua
function on_tick(idle_s, mouse_dist, valence, arousal)
  if valence > 0.4 then
    moyu.anim("happy")
  elseif valence < -0.4 then
    moyu.anim("sad")
  elseif idle_s > 120 then
    moyu.anim("sleep")
  else
    moyu.anim("idle")
  end
end
```

## Limits

- The Lua state is single-threaded with the rest of the runtime. Long
  loops in `on_tick` will stall the pet's rendering.
- `moyu.llm` is throttled by a daily-limit ring (see `llm_daily_limit` in
  `config.json`). Calls past the limit silently return `nil`.
- There is no persistence of Lua-side state across restarts. If you need
  memory, push it into the context store via `moyu.emit` and read it back
  from C — or wait for a future `moyu.context_get` binding.
