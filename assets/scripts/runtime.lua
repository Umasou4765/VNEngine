local function hash_djb2(str)
    local hash = 5381
    for i = 1, #str do
        hash = (hash * 33 + string.byte(str, i)) % 4294967296
    end
    return math.floor(hash)
end

local function stable_callsite_id(prefix, stack_level, fallback_payload)
    local payload = fallback_payload and tostring(fallback_payload) or ""

    if payload ~= "" then
        -- Prefer payload-based hashes so adding comments/blank lines does not break save IDs.
        return tostring(prefix or "id") .. "_" .. tostring(hash_djb2(payload))
    end

    return tostring(prefix or "id") .. "_" .. tostring(hash_djb2(tostring(stack_level or 3)))
end

local function stable_dialogue_id(speaker, text, stack_level)
    return stable_callsite_id("line", stack_level or 3, (speaker or "") .. "\n" .. (text or ""))
end

local function stable_choice_id(options, stack_level)
    local joined = ""
    for i = 1, #(options or {}) do
        joined = joined .. (options[i] or "") .. "\n"
    end
    return stable_callsite_id("choice", stack_level or 3, joined)
end

local function stable_observe_id(config, stack_level)
    local joined = {}
    local hotspots = (config and config.hotspots) or {}
    local sprites = (config and config.sprites) or {}

    for i = 1, #hotspots do
        local hs = hotspots[i] or {}
        joined[#joined + 1] = table.concat({
            "hotspot",
            tostring(hs.id or hs.target or hs.target_branch or ""),
            tostring(hs.x or 0),
            tostring(hs.y or 0),
            tostring(hs.w or 0),
            tostring(hs.h or 0),
            tostring(hs.hover or hs.label or ""),
        }, "|")
    end

    for i = 1, #sprites do
        local spr = sprites[i] or {}
        joined[#joined + 1] = table.concat({
            "sprite",
            tostring(spr.slot or 1),
            tostring(spr.id or spr.target or spr.target_branch or ""),
            tostring(spr.x or ""),
            tostring(spr.y or ""),
            tostring(spr.w or ""),
            tostring(spr.h or ""),
            tostring(spr.hover or spr.label or ""),
        }, "|")
    end

    joined[#joined + 1] = "pan|" .. tostring((config and config.pan_scale) or 1.5)
    joined[#joined + 1] = "cancel|" .. tostring(config and config.allow_cancel == true)
    return stable_callsite_id("observe", stack_level or 3, table.concat(joined, "\n"))
end

local function stable_video_id(path, stack_level)
    return stable_callsite_id("video", stack_level or 3, tostring(path or ""))
end

function LoadBG(path, immediate)
    vn.load_bg(path, immediate == true)
end

function CutBG(path)
    vn.cut_bg(path)
end

function SetBGTransition(effect)
    vn.set_bg_transition(effect or "crossfade")
end

function Preload(paths)
    if type(paths) == "string" then
        vn.preload_image(paths)
        return
    end

    if type(paths) == "table" then
        for i = 1, #paths do
            local path = paths[i]
            if type(path) == "string" and path ~= "" then
                vn.preload_image(path)
            end
        end
    end
end

function ShowSprite(path, slot, immediate)
    vn.load_sprite(path, slot or 1, immediate == true)
end

function CutSprite(path, slot)
    vn.cut_sprite(path, slot or 1)
end

function SetSpriteTransform(slot, opts)
    local o = opts or {}
    vn.set_sprite_transform(
        slot or 1,
        o.x or 0.0,
        o.y or 0.0,
        o.scale_x or 1.0,
        o.scale_y or 1.0,
        o.rotation or 0.0,
        o.anchor_x or 0.5,
        o.anchor_y or 1.0,
        o.absolute == true,
        o.width or -1.0,
        o.height or -1.0
    )
end

function ClearSpriteTransform(slot)
    vn.clear_sprite_transform(slot or 1)
end

function SetSpriteClip(slot, x, y, w, h)
    vn.set_sprite_clip(slot or 1, x or 0.0, y or 0.0, w or 0.0, h or 0.0, true)
end

function ClearSpriteClip(slot)
    vn.clear_sprite_clip(slot or 1)
end

function SetLetterbox(ratio, duration, immediate)
    vn.set_letterbox(ratio or 0.0, duration or 0.0, immediate == true)
end

function SetSpriteInteraction(slot, character_tag, part_tag, callback_name)
    vn.set_sprite_interaction(slot or 1, character_tag or "", part_tag or "", callback_name or "")
end

function ClearSpriteInteraction(slot)
    vn.clear_sprite_interaction(slot or 1)
end

function ActWait(seconds)
    return {
        kind = "wait",
        duration = seconds or 0.0,
    }
end

function ActShake(intensity, duration)
    return {
        kind = "shake",
        intensity = intensity or 8.0,
        duration = duration or 0.18,
    }
end

function ActCamera(x, y, zoom, duration)
    return {
        kind = "camera",
        x = x or 0.0,
        y = y or 0.0,
        zoom = zoom or 1.0,
        duration = duration or 0.0,
    }
end

function ActLetterbox(ratio, duration)
    return {
        kind = "letterbox",
        ratio = ratio or 0.0,
        duration = duration or 0.0,
    }
end

function ActSpriteTransform(slot, opts)
    local o = opts or {}
    return {
        kind = "sprite_transform",
        slot = slot or 1,
        x = o.x or 0.0,
        y = o.y or 0.0,
        scale_x = o.scale_x or 1.0,
        scale_y = o.scale_y or 1.0,
        rotation = o.rotation or 0.0,
        anchor_x = o.anchor_x or 0.5,
        anchor_y = o.anchor_y or 1.0,
        absolute = o.absolute == true,
        width = o.width or -1.0,
        height = o.height or -1.0,
    }
end

function ActSpriteTween(slot, duration, opts)
    local o = opts or {}
    return {
        kind = "sprite_tween",
        slot = slot or 1,
        duration = duration or 0.5,
        x = o.x,
        y = o.y,
        scale_x = o.scale_x,
        scale_y = o.scale_y,
        rotation = o.rotation,
    }
end

function ActScreenFade(r, g, b, target_alpha, duration)
    return {
        kind = "screen_fade",
        r = r or 0,
        g = g or 0,
        b = b or 0,
        alpha = target_alpha or 1.0,
        duration = duration or 0.5,
    }
end

function ActSpriteTint(slot, duration, opts)
    local o = opts or {}
    return {
        kind = "sprite_tint",
        slot = slot or 1,
        duration = duration or 0.5,
        r = o.r,
        g = o.g,
        b = o.b,
        alpha = o.alpha,
    }
end

function Parallel(actions)
    local should_wait = vn.begin_parallel(actions or {})
    if should_wait then
        return coroutine.yield()
    end
end

function Observe(config, observe_id)
    local cfg = config or {}
    local id = observe_id or stable_observe_id(cfg, 3)
    local selected, should_wait = vn.begin_observe(cfg, id)
    if should_wait then
        return coroutine.yield()
    end
    return selected
end

local native_begin_video = vn.begin_video or vn.play_video

local function play_video_blocking(path, video_id, opts_or_skippable, audio_enabled)
    local opts = opts_or_skippable
    if type(video_id) == "table" and opts_or_skippable == nil then
        opts = video_id
        video_id = nil
    end

    local skippable = true
    local audio = true
    if type(opts) == "table" then
        skippable = opts.skippable ~= false
        audio = opts.audio ~= false
    else
        if opts ~= nil then
            skippable = opts ~= false
        end
        if audio_enabled ~= nil then
            audio = audio_enabled ~= false
        end
    end

    local id = video_id or stable_video_id(path, 5)
    local should_wait = native_begin_video(path or "", id, skippable, audio)
    if should_wait then
        return coroutine.yield()
    end
end

function vn.play_video(path, video_id, opts_or_skippable, audio_enabled)
    return play_video_blocking(path, video_id, opts_or_skippable, audio_enabled)
end

function PlayVideo(path, video_id, opts)
    return play_video_blocking(path, video_id, opts, nil)
end

function PlayBGM(path)
    vn.play_bgm(path)
end

function StopBGM()
    vn.stop_bgm()
end

function PlaySFX(path)
    vn.play_sfx(path)
end

function PlayVoice(path)
    vn.play_voice(path)
end

function StopVoice()
    vn.stop_voice()
end

function SetFlag(name, value)
    vn.set_flag(name, value == true)
end

function GetFlag(name)
    return vn.get_flag(name)
end

function SetVar(name, value)
    vn.set_num(name, value or 0)
end

function GetVar(name)
    return vn.get_num(name)
end

function SetChapter(name)
    vn.set_chapter(name)
end

function Say(speaker, text, voice, line_id)
    local id = line_id or stable_dialogue_id(speaker, text, 3)
    local should_wait = vn.begin_dialogue(speaker or "", text or "", voice or "", id)
    if should_wait then
        return coroutine.yield()
    end
end

function Character(name, voice_prefix)
    return function(text, voice_id, line_id)
        local voice_path = ""
        if voice_id and voice_prefix and voice_prefix ~= "" then
            voice_path = voice_prefix .. voice_id .. ".ogg"
        end
        local id = line_id or stable_dialogue_id(name, text, 3)
        local should_wait = vn.begin_dialogue(name or "", text or "", voice_path, id)
        if should_wait then
            return coroutine.yield()
        end
    end
end

function Wait(seconds)
    local should_wait = vn.begin_wait(seconds or 0.0)
    if should_wait then
        return coroutine.yield()
    end
end

function Shake(intensity, duration)
    local should_wait = vn.begin_shake(intensity or 8.0, duration or 0.18)
    if should_wait then
        return coroutine.yield()
    end
end

function Choice(options, choice_id)
    local id = choice_id or stable_choice_id(options, 3)
    local selected, should_wait = vn.begin_choice(options or {}, id)
    if should_wait then
        return coroutine.yield()
    end
    return selected
end
