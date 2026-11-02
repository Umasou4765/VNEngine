#pragma once

// Replay binding audit for vn.* Lua API (ScriptDirector::InitializeLuaState).
//
// SafeDuringReplay — idempotent reads or state reconstruction required by replay:
//   t, get_flag, get_num, get_rng_seed
//   set_flag, set_num (writes go to replay snapshot stores while replay is active)
//   load_bg, cut_bg, load_sprite, cut_sprite (forced immediate during replay)
//   set_sprite_transform, clear_sprite_transform, set_sprite_clip, clear_sprite_clip
//   set_letterbox, set_bg_transition, clear_sprite_interaction
//   begin_dialogue, begin_choice, begin_observe, begin_wait, begin_parallel,
//   begin_shake, begin_video (yield points; replay handled in SceneManager)
//
// UnsafeDuringReplay — side effects suppressed at the binding layer:
//   set_rng_seed, preload_image, set_sprite_interaction
//   play_bgm, stop_bgm, play_sfx, play_voice, stop_voice
//   set_chapter, screen_shake (non-queued; begin_shake uses QueueLuaShake)
