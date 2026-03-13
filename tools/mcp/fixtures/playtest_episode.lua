-- title:   MCP Playtest Episode Fixture
-- author:  OpenAI
-- desc:    Deterministic Lua cart for run_playtest_episode smoke coverage
-- version: 0.1
-- script:  lua

frame = 0
x = 16

function TIC()
  frame = frame + 1

  if btn(3) then
    x = x + 2
  end

  cls(12)
  rect(12, 20, 216, 96, 6)
  rect(x, 54, 18, 18, 14)
  if DEBUG_MODE then
    rectb(x-2, 52, 22, 22, 2)
    rect(180, 32, 20, 20, 2)
    print("DEBUG", 176, 56, 2, false, 1, true)
    trace("debug on")
  end
  print("PLAYTEST", 86, 26, 1, false, 1, true)
  print("FRAME "..frame, 84, 94, 15, false, 1, true)
  trace("tick "..frame)
end

-- <PALETTE>
-- 000:1a1c2c5d275db13e53ef7d57ffcd75a7f07038b76425717929366f3b5dc941a6f673eff7f4f4f494b0c2566c86333c57
-- </PALETTE>
