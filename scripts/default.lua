-- MOYU default personality + behaviour rules.
-- Sandbox: io/os/package/loadfile/require are unavailable.
-- Available API:
--   moyu.idle_seconds() -> number
--   moyu.mouse_distance() -> number
--   moyu.mood() -> valence, arousal
--   moyu.personality() -> mood_bias, sarcasm, curiosity, patience
--   moyu.emit(name, payload?)     -- queue a generic action
--   moyu.say(text, duration_ms?)  -- immediate speech bubble
--   moyu.anim(name)               -- "idle"|"blink"|"sleep"|"happy"|"sad"|"observe"
--   moyu.llm(prompt) -> string|nil
--   moyu.log(msg)
--
-- Hooks (optional, called by the runtime):
--   on_tick(idle_s, mouse_dist, valence, arousal)
--   on_click(button)

local poke_lines = {
  "you're slacking off again",
  "...",
  "zzz",
  "wake up",
  "?",
  "hmm..."
}

local rare_lines = {
  "long day?",
  "still here?",
  "go drink water",
  "stop staring",
  "again?"
}

-- Tiny deterministic-ish PRNG from math.random (sandboxed)
local function chance(p)
  return math.random() < p
end

function on_tick(idle_s, mouse_dist, valence, arousal)
  -- Mouse proximity -> observe
  if mouse_dist < 60 then
    moyu.anim("observe")
  elseif mouse_dist > 200 and idle_s < 60 then
    moyu.anim("idle")
  end

  -- Idle too long -> drift to sleep
  if idle_s > 120 then
    moyu.anim("sleep")
  end

  -- Emotional hint
  if valence > 0.4 and idle_s < 60 then
    moyu.anim("happy")
  elseif valence < -0.4 then
    moyu.anim("sad")
  end

  -- Idle poke threshold (rule-based, no LLM)
  if idle_s > 300 and chance(0.05) then
    local line = poke_lines[math.random(1, #poke_lines)]
    moyu.say(line, 3000)
  end

  -- Very rare spontaneous LLM-driven micro narrative.
  -- Only if mood is calm enough that an LLM call feels natural.
  if idle_s > 600 and chance(0.002) and moyu.llm then
    local mb, sarc, curi, _ = moyu.personality()
    if curi > 0.4 then
      moyu.llm("Say one short idle thought in <=30 ASCII chars.")
    end
  end
end

function on_click(button)
  -- Click = poke; react with happy + brief remark
  moyu.anim("happy")
  if chance(0.5) then
    moyu.say(rare_lines[math.random(1, #rare_lines)], 2500)
  end
end

moyu.log("default personality loaded")
