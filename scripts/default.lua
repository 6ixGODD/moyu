-- MOYU default personality + behaviour + appearance.
-- Sandbox: io/os/package/loadfile/require/load are unavailable.
--
-- Behaviour API:
--   moyu.idle_seconds() -> number
--   moyu.mouse_distance() -> number
--   moyu.mood() -> valence, arousal
--   moyu.personality() -> mood_bias, sarcasm, curiosity, patience
--   moyu.emit(name, payload?)
--   moyu.say(text, duration_ms?)
--   moyu.anim(name)               -- "idle"|"blink"|"sleep"|"happy"|"sad"|"observe"
--   moyu.llm(prompt) -> string|nil
--   moyu.log(msg)
--
-- Appearance API (this is what makes the look plugin-defined):
--   moyu.use_skin_builtin()                 -- reset to the procedural blob
--   moyu.use_skin_bmp(path, fw, fh)         -- load a BMP spritesheet (true on ok)
--   moyu.set_anim(name, frames, fps)        -- map an anim to sheet frame indices
--
-- Hooks (optional):
--   on_tick(idle_s, mouse_dist, valence, arousal)
--   on_click(button)

-- ---------------------------------------------------------------------------
-- APPEARANCE
-- ---------------------------------------------------------------------------
-- Default look: the builtin procedural blob. The builtin sheet has 20 frames
-- laid out as:
--   idle 0..3   blink 4..5   sleep 6..9   happy 10..13   sad 14..17   observe 18..19
-- We declare the anims explicitly here so a custom skin can be a drop-in:
-- just call moyu.use_skin_bmp(...) then redefine the same six anims with your
-- own frame indices.
moyu.use_skin_builtin()

moyu.set_anim("idle",    {0, 1, 2, 3},       3)
moyu.set_anim("blink",   {4, 5, 4, 5},       5)
moyu.set_anim("sleep",   {6, 7, 8, 9},       3)
moyu.set_anim("happy",   {10, 11, 12, 13},   8)
moyu.set_anim("sad",     {14, 15, 16, 17},   3)
moyu.set_anim("observe", {18, 19, 18, 19},   4)

-- To use your own pixel art instead, drop a BMP next to the exe and do e.g.:
--   moyu.use_skin_bmp("skins/cat.bmp", 32, 32)
--   moyu.set_anim("idle",    {0, 1, 2, 3}, 4)
--   moyu.set_anim("happy",   {4, 5, 6, 7}, 8)
--   ... etc. The BMP must be uncompressed 24 or 32-bit, width/height a
--   multiple of the frame size, frames laid out row-major.

-- ---------------------------------------------------------------------------
-- BEHAVIOUR
-- ---------------------------------------------------------------------------
local poke_lines = {
  "又摸鱼？", "...", "Zzz", "醒醒", "?", "嗯...",
  "still here?", "go drink water", "again?"
}

local rare_lines = {
  "long day?", "still here?", "go drink water", "stop staring", "again?"
}

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
    moyu.say(poke_lines[math.random(1, #poke_lines)], 3000)
  end

  -- Very rare spontaneous LLM-driven micro narrative.
  if idle_s > 600 and chance(0.002) and moyu.llm then
    local _, _, curi, _ = moyu.personality()
    if curi > 0.4 then
      moyu.llm("Say one short idle thought in <=30 chars.")
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

moyu.log("default personality + builtin skin loaded")
