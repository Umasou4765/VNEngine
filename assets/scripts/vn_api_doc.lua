---@meta
---@diagnostic disable: lowercase-global

-- This file is for editor IntelliSense/Copilot guidance only.
-- Do not require it in runtime scripts.

---@class VNAPI
vn = {}

---Set deterministic RNG seed for Lua-side random.
---@param seed integer
function vn.set_rng_seed(seed) end

---Get current deterministic RNG seed.
---@return integer
function vn.get_rng_seed() end

---Load background with optional transition.
---@param path string
---@param immediate boolean
function vn.load_bg(path, immediate) end

---Instantly switch background.
---@param path string
function vn.cut_bg(path) end

---Set background transition effect.
---Supported: "crossfade", "dissolve", "wipe_left", "wipe_right".
---@param effect string
function vn.set_bg_transition(effect) end

---Preload image asynchronously.
---@param path string
function vn.preload_image(path) end

---Load sprite into slot (0=left, 1=center, 2=right).
---@param path string
---@param slot integer
---@param immediate boolean
function vn.load_sprite(path, slot, immediate) end

---Instantly switch sprite in slot.
---@param path string
---@param slot integer
function vn.cut_sprite(path, slot) end

---Set sprite transform.
---@param slot integer
---@param x number
---@param y number
---@param scaleX number
---@param scaleY number
---@param rotationDeg number
---@param anchorX number
---@param anchorY number
---@param useAbsolute boolean
---@param width number|nil
---@param height number|nil
function vn.set_sprite_transform(slot, x, y, scaleX, scaleY, rotationDeg, anchorX, anchorY, useAbsolute, width, height) end

---Reset sprite transform to defaults.
---@param slot integer
function vn.clear_sprite_transform(slot) end

---Set sprite clip rectangle.
---@param slot integer
---@param x number
---@param y number
---@param w number
---@param h number
---@param enabled boolean
function vn.set_sprite_clip(slot, x, y, w, h, enabled) end

---Clear sprite clip rectangle.
---@param slot integer
function vn.clear_sprite_clip(slot) end

---Set letterbox ratio tween.
---@param ratio number
---@param duration number
---@param immediate boolean
function vn.set_letterbox(ratio, duration, immediate) end

---Bind sprite interaction callback.
---@param slot integer
---@param characterTag string
---@param partTag string
---@param callbackName string
function vn.set_sprite_interaction(slot, characterTag, partTag, callbackName) end

---Clear sprite interaction callback.
---@param slot integer
function vn.clear_sprite_interaction(slot) end

---@param path string
function vn.play_bgm(path) end

function vn.stop_bgm() end

---@param path string
function vn.play_sfx(path) end

---@param path string
function vn.play_voice(path) end

function vn.stop_voice() end

---@param name string
---@param value boolean
function vn.set_flag(name, value) end

---@param name string
---@return boolean
function vn.get_flag(name) end

---@param name string
---@param value number
function vn.set_num(name, value) end

---@param name string
---@return number
function vn.get_num(name) end

---@param name string
function vn.set_chapter(name) end

---@param intensity number
---@param duration number
function vn.screen_shake(intensity, duration) end

---Begin a dialogue and usually yield in script afterwards.
---@param speaker string
---@param text string
---@param voicePath string
---@param lineId string
---@return boolean
function vn.begin_dialogue(speaker, text, voicePath, lineId) end

---Begin wait action and usually yield in script afterwards.
---@param seconds number
---@return boolean
function vn.begin_wait(seconds) end

---Begin parallel action group and usually yield in script afterwards.
---@param actions table
---@return boolean
function vn.begin_parallel(actions) end

---@param intensity number
---@param duration number
---@return boolean
function vn.begin_shake(intensity, duration) end

---Show choice and return selected value or waiting state tuple.
---@param options table
---@param choiceId string
---@return integer, boolean
function vn.begin_choice(options, choiceId) end

---Start observe mode and return target or waiting state tuple.
---@param config table
---@param observeId string
---@return string, boolean
function vn.begin_observe(config, observeId) end

---Low-level fullscreen video playback entry; returns whether script should yield.
---@param path string
---@param videoId string|nil
---@param skippable boolean|nil
---@param audioEnabled boolean|nil
---@return boolean
function vn.begin_video(path, videoId, skippable, audioEnabled) end

---Play fullscreen video and block the current script coroutine until finished or skipped.
---@param path string
---@param videoId string|table|nil
---@param opts table|boolean|nil
---@param audioEnabled boolean|nil
function vn.play_video(path, videoId, opts, audioEnabled) end
