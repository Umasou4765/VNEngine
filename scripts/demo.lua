-- ============================================================================
-- Demo: Before She Speaks
-- Meta-horror vertical slice for the current VNEngine Lua runtime.
-- ============================================================================

local BG_DAY = "assets/bg/classroom.png"
local BG_DUSK = "assets/bg/classroom_dusk.png"
local BG_NIGHT = "assets/bg/classroom_night.png"
local BG_GLITCH = "assets/bg/classroom_glitch.png"

local GIRL_NORMAL = "assets/sprites/girl_normal.png"
local GIRL_SAD = "assets/sprites/girl_sad.png"
local GIRL_GLITCH = "assets/sprites/girl_glitch.png"

local function Line(id, speaker, text)
    Say(speaker or "", text or "", "", id)
end

local function PlaceGirl(path, immediate)
    SetSpriteTransform(1, {
        x = 640,
        y = 724,
        width = 360,
        height = 645,
        anchor_x = 0.5,
        anchor_y = 1.0,
        absolute = true
    })
    ShowSprite(path, 1, immediate == true)
end

local function FadeToBG(path, sprite)
    Parallel({
        ActScreenFade(0, 0, 0, 1.0, 0.22),
        ActLetterbox(0.14, 0.22)
    })

    LoadBG(path, true)
    if sprite then
        PlaceGirl(sprite, true)
    end

    Parallel({
        ActScreenFade(0, 0, 0, 0.0, 0.34)
    })
end

local function RedPulse(intensity)
    Parallel({
        ActScreenFade(120, 0, 0, 0.34, 0.05),
        ActShake(intensity or 6.0, 0.20)
    })
    Parallel({
        ActScreenFade(120, 0, 0, 0.0, 0.22)
    })
end

function main()
    SetChapter("Before She Speaks")
    SetBGTransition("dissolve")

    Preload({
        BG_DAY,
        BG_DUSK,
        BG_NIGHT,
        BG_GLITCH,
        GIRL_NORMAL,
        GIRL_SAD,
        GIRL_GLITCH
    })

    SetLetterbox(0.10, 0.0, true)
    LoadBG(BG_DAY, true)
    PlaceGirl(GIRL_NORMAL, true)

    Parallel({
        ActLetterbox(0.12, 0.35)
    })

    Line("bss_001", "", "午后的教室很安静。")
    Line("bss_002", "", "安静到我能听见粉笔灰落在讲台上的声音。")

    Line(
        "bss_003",
        "少女",
        "「今天放学后，可以陪我一下吗？」{w=1.15}\n\n她的嘴唇在一秒后才跟上这句话。"
    )

    Line("bss_004", "我", "（不是既视感。是字幕先出现了。）")
    Line("bss_005", "我", "（像游戏里的文字框一样，提前把现实剧透给我。）")

    FadeToBG(BG_DUSK, GIRL_SAD)

    Line("bss_006", "少女", "「我丢了一本笔记。」{w=0.75}")
    Line(
        "bss_007",
        "我",
        "（文字框写的是『笔记』。）\n（可她刚才说出口的，明明是<color=red>『记忆』</color>。）"
    )
    Line("bss_008", "我", "（第一次，字幕和声音不一致。）")

    Line("bss_009", "少女", "「你能看到的吧？那个悬浮在空中的剧本。」")

    local options = {
        "承认自己看得到",
        "撒谎说看不到",
        "反问她为什么知道"
    }
    local selected = Choice(options, "bss_choice_polluted")
    local selected_text = options[selected] or "未定义选项"

    Line(
        "bss_010_choice_echo",
        "系统",
        "<color=#e5484d>选择已记录：" .. selected_text .. "</color>"
    )

    RedPulse(8.0)

    Line("bss_011", "我", "「我看不到任何东西。」")
    Line("bss_012", "我", "（不对。我刚才没有选这句。）")
    Line("bss_013", "我", "（我的喉咙自己动了起来，好像有人把台词塞进了我嘴里。）")

    FadeToBG(BG_NIGHT, GIRL_SAD)

    Line("bss_014", "少女", "「不要相信文字框。」")
    Line("bss_015", "少女", "「它不是未来，是命令。」")

    Parallel({
        ActScreenFade(255, 255, 255, 0.72, 0.04),
        ActShake(10.0, 0.28)
    })

    LoadBG(BG_GLITCH, true)
    PlaceGirl(GIRL_GLITCH, true)

    Parallel({
        ActScreenFade(255, 255, 255, 0.0, 0.28)
    })

    Line("bss_016", "系统", "<color=red>「不要相信她。她已经被改写了。」</color>")
    Line("bss_017", "我", "（到底是谁在撒谎？）\n（她？还是这个一直替现实发言的 UI？）")

    Parallel({
        ActScreenFade(0, 0, 0, 1.0, 0.55),
        ActSpriteTint(1, 0.45, { alpha = 0.0 })
    })

    CutSprite("", 1)
    CutBG("")

    Line("bss_018", "", "「下一句话，不是她说的。」")
    Wait(0.45)
    Line("bss_019", "", "「也不是文字框说的。」")
    Wait(0.35)
    Line("bss_020", "我", "「我终于找到玩家了。」")

    Wait(0.8)
    SetChapter("Before She Speaks - Demo Completed")
    SetLetterbox(0.0, 0.45, false)
end
