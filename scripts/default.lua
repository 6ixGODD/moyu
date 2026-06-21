-- MOYU's default policy. C owns budgets, permissions and state transitions;
-- Lua only expresses behavior and reacts to events.

moyu.use_skin_builtin()
moyu.set_anim("idle",    {0, 1, 2, 3},       2)
moyu.set_anim("blink",   {4, 5, 4},          5)
moyu.set_anim("sleep",   {6, 7, 8, 9},       2)
moyu.set_anim("happy",   {10, 11, 12, 13},   6)
moyu.set_anim("sad",     {14, 15, 16, 17},   2)
moyu.set_anim("observe", {18, 19, 18, 19},   3)
moyu.set_anim("walk",    {20, 21, 22, 23},   4)
moyu.set_anim("work",    {24, 25, 26, 27},   3)
moyu.set_anim("wait",    {28, 29, 30, 31},   2)
moyu.set_anim("found",   {32, 33, 34, 35},   6)
moyu.set_anim("confused",{36, 37, 38, 39},   2)
moyu.set_anim("giveup",  {40, 41, 42, 43},   2)

function on_tick(idle_s, mouse_dist, valence, arousal)
  if idle_s > 120 then
    moyu.anim("sleep")
  elseif mouse_dist < 60 then
    moyu.anim("observe")
  elseif valence < -0.55 then
    moyu.anim("sad")
  else
    moyu.anim("idle")
  end
end

function on_click(button)
  if button == 0 then
    moyu.anim("observe")
  end
end

function on_event(name, payload_json)
  if name == "tool_result" then
    moyu.anim("happy")
  elseif name == "permission_denied" then
    moyu.anim("sad")
  end
end

function propose_desires(state_json)
  -- The default C policy already proposes safe local scavenging. Personality
  -- packs may return candidates here in a future schema revision.
  return nil
end

function score_intention(candidate, state_json)
  return 1
end

function on_tool_result(result_json)
  moyu.anim("found")
end

function on_memory_candidate(summary)
  -- C applies sensitivity, novelty and probability gates.
end

moyu.log("default event-driven policy loaded")
