#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "ActionQueue.h"
#include "AssetManager.h"
#include "AudioManager.h"
#include "EngineScriptHost.h"
#include "LuaScriptContext.h"
#include "SceneManager.h"
#include "SaveManager.h"
#include "SaveSystem.h"
#include "ScriptIdentity.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", message);
        ++g_failures;
    }
}

void PumpScene(SceneManager& scene, ActionQueue& actionQueue, float dt = 0.016f) {
    actionQueue.Update(dt, true);
    scene.Update(dt, true);
}

bool PumpScriptProgress(SceneManager& scene, ActionQueue& actionQueue) {
    PumpScene(scene, actionQueue);

    if (scene.state == SceneMode::DIALOGUE &&
        scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::Dialogue) {
        return scene.AdvanceDialogue();
    }

    if (scene.state == SceneMode::OBSERVE) {
        const auto& hotspots = scene.GetObserveHotspots();
        if (!hotspots.empty()) {
            scene.OnObserveTargetSelected(hotspots.front().targetId);
            return true;
        }
    }

    return false;
}

bool AdvanceUntilState(SceneManager& scene,
                       ActionQueue& actionQueue,
                       SceneMode target,
                       int maxSteps) {
    for (int step = 0; step < maxSteps; ++step) {
        if (scene.state == target) {
            return true;
        }
        (void)PumpScriptProgress(scene, actionQueue);
    }
    return scene.state == target;
}

void PumpSceneRealtime(SceneManager& scene, ActionQueue& actionQueue, float dt = 0.016f) {
    actionQueue.Update(dt, false);
    scene.Update(dt, false);
}

std::string WriteTransientReplayScript() {
    const std::filesystem::path scriptPath =
        std::filesystem::current_path() / "assets" / "scripts" / "phase2_transient_replay.lua";
    std::filesystem::create_directories(scriptPath.parent_path());
    std::ofstream out(scriptPath, std::ios::trunc);
    out << "function main()\n"
           "    Choice({\"Path A\", \"Path B\"}, \"BRANCH_TRANSIENT\")\n"
           "    Wait(5.0)\n"
           "    Say(\"Sys\", \"Post-wait\", \"\", \"D_POST\")\n"
           "end\n";
    return scriptPath.string();
}

std::string WriteRepeatedYieldReplayScript() {
    const std::filesystem::path scriptPath =
        std::filesystem::current_path() / "assets" / "scripts" / "phase2_repeated_yield_replay.lua";
    std::filesystem::create_directories(scriptPath.parent_path());
    std::ofstream out(scriptPath, std::ios::trunc);
    out << "function main()\n"
           "    for round = 1, 3 do\n"
           "        Choice({\"Continue\"}, \"INV_MENU\")\n"
           "        SetVar(\"round\", round)\n"
           "        Wait(0.5)\n"
           "    end\n"
           "    Say(\"Sys\", \"Done\", \"\", \"D_DONE\")\n"
           "end\n";
    return scriptPath.string();
}

std::string WriteHashValidationScript(const std::string& body) {
    const std::filesystem::path scriptPath =
        std::filesystem::current_path() / "assets" / "scripts" / "phase2_hash_validation.lua";
    std::filesystem::create_directories(scriptPath.parent_path());
    std::ofstream out(scriptPath, std::ios::trunc);
    out << body;
    return scriptPath.string();
}

std::string BuildEscapedSaveProbeString() {
    std::string out = "speaker \"quote\" slash \\ newline\n tab\t ctrl ";
    out.push_back('\x01');
    out += " end";
    return out;
}

void WriteOversizedLuaSaveFile(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream out(path, std::ios::trunc);
    out << "return { version = 3, script = \"assets/scripts/demo.lua\", payload = \"";

    const std::string chunk(1024, 'x');
    for (int i = 0; i < 5 * 1024; ++i) {
        out << chunk;
    }

    out << "\" }\n";
}

} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed\n");
        return 1;
    }
    if (!TTF_Init()) {
        SDL_Quit();
        std::fprintf(stderr, "TTF_Init failed\n");
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Phase2Validation", 1280, 720, SDL_WINDOW_HIDDEN);
    if (!window) {
        TTF_Quit();
        SDL_Quit();
        std::fprintf(stderr, "SDL_CreateWindow failed\n");
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        std::fprintf(stderr, "SDL_CreateRenderer failed\n");
        return 1;
    }

    vn::AssetManager assets;
    assets.SetRenderer(renderer);

    Check(assets.PreloadAsync("assets/bg/classroom.png"), "AssetManager queues async preload");
    Check(assets.IsPendingAsync("assets/bg/classroom.png"), "AssetManager reports pending preload");
    Check(assets.WaitForAsync("assets/bg/classroom.png", 5000), "AssetManager completes async preload");
    Check(!assets.IsPendingAsync("assets/bg/classroom.png"), "AssetManager clears completed preload");
    Check(static_cast<bool>(assets.GetTexture("assets/bg/classroom.png")), "AssetManager returns preloaded texture");

    vn::AudioManager audio;
    audio.SetAssetManager(&assets);
    audio.Init();
    ActionQueue actionQueue;
    vn::GameState gameState;
    vn::LuaScriptContext scriptContext;
    SceneManager scene;
    scriptContext.SetServices(EngineScriptHost::Build(scene, gameState, audio));
    scene.Init({ renderer, 1280, 720, &gameState, &scriptContext, {}, &audio, &assets, &actionQueue });
    scene.SetTranslationResolver([](const std::string& key) { return key; });

    // New Game → Dialogue
    Check(scene.LoadLuaScript("assets/scripts/demo.lua"), "Load demo.lua");
    PumpScene(scene, actionQueue);
    Check(scene.state == SceneMode::DIALOGUE, "Reached dialogue after new game");

    // Advance through dialogue and observe until branch choice
    Check(AdvanceUntilState(scene, actionQueue, SceneMode::CHOICE, 12000),
          "Reached branch choice");
    Check(scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::Choice,
          "Script waiting on choice");

    // Make choice before save so replay has choice history (avoids empty-history divergence)
    scene.OnChoiceSelected(1);
    PumpScene(scene, actionQueue);
    Check(AdvanceUntilState(scene, actionQueue, SceneMode::DIALOGUE, 4000),
          "Advanced into branch after choice");

    // Save after branch selection
    vn::SaveSnapshot saveAtChoice = scene.BuildSaveData();
    Check(!saveAtChoice.luaResumeId.empty(), "Save has lua resume id");
    Check(!saveAtChoice.luaChoiceHistory.empty(), "Save records choice history");
    Check(saveAtChoice.scriptContentHash != 0, "Save records script content hash");

    // Quit / restart simulation
    scene.ResetScriptRuntime();
    Check(scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::None,
          "Runtime reset clears wait reason");

    // Load save with replay
    Check(scene.LoadLuaScript(saveAtChoice.scriptPath, saveAtChoice.luaEntryPoint, false),
          "Reload script for restore");
    Check(SaveSystem::Restore(scene, saveAtChoice, SceneManager::RestoreMode::Replay),
          "Replay restore succeeds");
    PumpScene(scene, actionQueue);

    const auto& replayState = scene.GetScriptState();
    Check(!replayState.replayDiverged, "Replay did not diverge on load");
    Check(replayState.choiceHistory.size() >= saveAtChoice.luaChoiceHistory.size(),
          "Choice history preserved across replay");

    // Transient save regression test: a choice followed by an ActionQueue wait must not carry the previous choice resume ID.
    {
        const std::string transientScriptPath = WriteTransientReplayScript();
        scene.ResetScriptRuntime();
        Check(scene.LoadLuaScript(transientScriptPath), "Load transient replay script");

        Check(AdvanceUntilState(scene, actionQueue, SceneMode::CHOICE, 1000),
              "Reached transient branch choice");
        Check(scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::Choice,
              "Transient script waiting on choice");

        scene.OnChoiceSelected(1);
        PumpSceneRealtime(scene, actionQueue);
        Check(scene.state == SceneMode::DIALOGUE &&
              scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::ActionQueue,
              "Entered ActionQueue wait after choice");

        vn::SaveSnapshot transientSave = scene.BuildSaveData();
        Check(transientSave.luaResumeId == "wait",
              "Transient save uses ActionQueue wait anchor");
        Check(transientSave.luaWaitReason == "action_queue",
              "Transient save records ActionQueue wait reason");
        Check(transientSave.luaYieldSequence != 0 && transientSave.luaYieldOccurrence != 0,
              "Transient save records structured suspension point");

        scene.ResetScriptRuntime();
        Check(scene.LoadLuaScript(transientSave.scriptPath, transientSave.luaEntryPoint, false),
              "Reload transient script for restore");
        Check(SaveSystem::Restore(scene, transientSave, SceneManager::RestoreMode::Replay),
              "Transient replay restore succeeds");

        PumpSceneRealtime(scene, actionQueue);
        Check(!scene.GetScriptState().replayDiverged,
              "Transient replay did not diverge");
        Check(scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::ActionQueue,
              "Replay successfully yielded at wait");
    }

    // Reused yield IDs must restore by structured suspension identity, not by the first matching anchor.
    {
        const std::string repeatedScriptPath = WriteRepeatedYieldReplayScript();
        scene.ResetScriptRuntime();
        Check(scene.LoadLuaScript(repeatedScriptPath), "Load repeated yield replay script");

        Check(AdvanceUntilState(scene, actionQueue, SceneMode::CHOICE, 1000),
              "Reached first repeated choice");
        scene.OnChoiceSelected(1);
        Check(AdvanceUntilState(scene, actionQueue, SceneMode::CHOICE, 1000),
              "Reached second repeated choice");
        scene.OnChoiceSelected(1);
        PumpSceneRealtime(scene, actionQueue);

        Check(scene.state == SceneMode::DIALOGUE &&
              scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::ActionQueue,
              "Entered second repeated ActionQueue wait");

        vn::SaveSnapshot repeatedSave = scene.BuildSaveData();
        Check(repeatedSave.luaResumeId == "wait", "Repeated save targets wait anchor");
        Check(repeatedSave.luaYieldSequence != 0, "Repeated save records yield sequence");
        Check(repeatedSave.luaYieldOccurrence == 2, "Repeated save records second wait occurrence");
        Check(repeatedSave.luaChoiceHistory.size() == 2, "Repeated save records both consumed choices");
        Check(repeatedSave.numVars["round"] == 2.0, "Repeated save captured second round state");

        scene.ResetScriptRuntime();
        Check(scene.LoadLuaScript(repeatedSave.scriptPath, repeatedSave.luaEntryPoint, false),
              "Reload repeated script for restore");
        Check(SaveSystem::Restore(scene, repeatedSave, SceneManager::RestoreMode::Replay),
              "Repeated replay restore succeeds");

        const auto& repeatedState = scene.GetScriptState();
        Check(!repeatedState.replayDiverged, "Repeated replay did not diverge");
        Check(repeatedState.currentSuspension.sequence == repeatedSave.luaYieldSequence,
              "Repeated replay restored target sequence");
        Check(repeatedState.currentSuspension.occurrence == repeatedSave.luaYieldOccurrence,
              "Repeated replay restored target occurrence");
        Check(scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::ActionQueue,
              "Repeated replay yielded at saved wait");
        const vn::SaveSnapshot restoredRepeatedSave = scene.BuildSaveData();
        const auto restoredRound = restoredRepeatedSave.numVars.find("round");
        Check(restoredRound != restoredRepeatedSave.numVars.end() && restoredRound->second == 2.0,
              "Repeated replay restored second round number");
    }

    // Skip mode simulation: fast-forward through replayed dialogue
    for (int i = 0; i < 200 && scene.GetScriptState().replayActive; ++i) {
        (void)PumpScriptProgress(scene, actionQueue);
    }
    Check(!scene.GetScriptState().replayActive, "Skip replay completed");

    // Continue story after skip
    for (int i = 0; i < 200; ++i) {
        (void)PumpScriptProgress(scene, actionQueue);
    }

    // Save again → load again
    vn::SaveSnapshot saveAfterBranch = scene.BuildSaveData();
    scene.ResetScriptRuntime();
    Check(scene.LoadLuaScript(saveAfterBranch.scriptPath, saveAfterBranch.luaEntryPoint, false),
          "Reload script for second restore");
    Check(SaveSystem::Restore(scene, saveAfterBranch, SceneManager::RestoreMode::Replay),
          "Second replay restore succeeds");
    PumpScene(scene, actionQueue);
    Check(!scene.GetScriptState().replayDiverged, "Second replay did not diverge");

    // Auto mode simulation: timed advances
    for (int i = 0; i < 120; ++i) {
        (void)PumpScriptProgress(scene, actionQueue);
    }

    // Return to title
    scene.ResetScriptRuntime();
    Check(scene.GetScriptWaitReason() == vn::ScriptDirector::WaitReason::None,
          "Return to title resets script runtime");

    // Load save and continue
    Check(scene.LoadLuaScript(saveAfterBranch.scriptPath, saveAfterBranch.luaEntryPoint, false),
          "Reload script after return to title");
    Check(SaveSystem::Restore(scene, saveAfterBranch, SceneManager::RestoreMode::Replay),
          "Load after return to title succeeds");
    PumpScene(scene, actionQueue);
    Check(!scene.HasLuaError(), "No Lua error after full validation cycle");

    // Coroutine lifetime: resume after reset must be inert
    scene.ResetScriptRuntime();
    Check(!scene.ResumeLuaCoroutine(), "Resume after reset does not touch destroyed runtime");

    // RenderState allocation profile sanity
    (void)scene.GetRenderState(0.0f, 0.0f);
    const auto& profile = scene.GetRenderStateAllocationProfile();
    Check(profile.callCount >= 1, "RenderState profile recorded at least one call");

    // Save serialization must round-trip Lua-sensitive characters.
    {
        std::error_code ec;
        const std::filesystem::path saveDir =
            std::filesystem::temp_directory_path() / "vn_engine_phase2_save_probe";
        std::filesystem::remove_all(saveDir, ec);

        SaveManager saveManager(saveDir.string());
        const std::string escapedProbe = BuildEscapedSaveProbeString();

        vn::SaveSnapshot saveProbe;
        saveProbe.scriptPath = "assets/scripts/demo.lua";
        saveProbe.scriptContentHash = vn::ComputeFileContentHash(saveProbe.scriptPath);
        saveProbe.luaEntryPoint = "main";
        saveProbe.luaResumeId = "D_SAVE_PROBE";
        saveProbe.luaWaitReason = vn::ScriptDirector::WaitReasonToString(
            vn::ScriptDirector::WaitReason::Dialogue);
        saveProbe.chapterName = escapedProbe;
        saveProbe.flags[escapedProbe] = true;
        saveProbe.numVars["probe"] = 3.5;
        saveProbe.dialogueLog.push_back({ escapedProbe, escapedProbe, "", "D_SAVE_PROBE" });

        const std::string backupProbeChapter = "backup probe";
        saveProbe.chapterName = backupProbeChapter;
        Check(saveManager.SaveSlot(2, saveProbe), "SaveManager writes initial backup probe");

        saveProbe.chapterName = escapedProbe;
        Check(saveManager.SaveSlot(2, saveProbe), "SaveManager writes escaped Lua strings");

        vn::SaveSnapshot loadedProbe;
        Check(saveManager.LoadSlot(2, loadedProbe), "SaveManager loads escaped Lua strings");
        Check(loadedProbe.chapterName == escapedProbe, "Save chapter string round-trips");
        Check(!loadedProbe.dialogueLog.empty() &&
                  loadedProbe.dialogueLog.front().speaker == escapedProbe &&
                  loadedProbe.dialogueLog.front().text == escapedProbe,
              "Save dialogue log strings round-trip");
        Check(loadedProbe.flags.count(escapedProbe) == 1 && loadedProbe.flags[escapedProbe],
              "Save map key string round-trips");

        std::filesystem::remove(saveDir / "slot2.sav", ec);
        vn::SaveSnapshot recoveredProbe;
        Check(saveManager.LoadSlot(2, recoveredProbe), "SaveManager recovers missing slot from backup");
        Check(recoveredProbe.chapterName == backupProbeChapter,
              "SaveManager recovered backup slot contents");

        std::filesystem::remove_all(saveDir, ec);
    }

    // Oversized save files must be rejected before the Lua parser runs.
    {
        std::error_code ec;
        const std::filesystem::path saveDir =
            std::filesystem::temp_directory_path() / "vn_engine_phase2_oversize_probe";
        std::filesystem::remove_all(saveDir, ec);

        SaveManager saveManager(saveDir.string());

        WriteOversizedLuaSaveFile(saveDir / "slot4.sav");
        vn::SaveSnapshot oversizedSlot;
        Check(!saveManager.LoadSlot(4, oversizedSlot), "SaveManager rejects oversized slot save");

        WriteOversizedLuaSaveFile(saveDir / "global.sav");
        SaveManager::GlobalSaveData oversizedGlobal;
        Check(!saveManager.LoadGlobal(oversizedGlobal), "SaveManager rejects oversized global save");

        std::filesystem::remove_all(saveDir, ec);
    }

    // Script hash must reject saves after script content changes.
    {
        std::error_code ec;
        const std::filesystem::path saveDir =
            std::filesystem::temp_directory_path() / "vn_engine_phase2_hash_probe";
        std::filesystem::remove_all(saveDir, ec);

        const std::string hashScriptPath = WriteHashValidationScript(
            "function main()\n"
            "    Say(\"Sys\", \"Hash A\", \"\", \"D_HASH\")\n"
            "end\n");

        SaveManager saveManager(saveDir.string());
        vn::SaveSnapshot hashProbe;
        hashProbe.scriptPath = hashScriptPath;
        hashProbe.scriptContentHash = vn::ComputeFileContentHash(hashScriptPath);
        hashProbe.luaEntryPoint = "main";
        hashProbe.luaResumeId = "D_HASH";
        hashProbe.luaWaitReason = vn::ScriptDirector::WaitReasonToString(
            vn::ScriptDirector::WaitReason::Dialogue);

        Check(hashProbe.scriptContentHash != 0, "Hash probe script hash is nonzero");
        Check(saveManager.SaveSlot(3, hashProbe), "SaveManager writes script hash");

        vn::SaveSnapshot loadedHashProbe;
        Check(saveManager.LoadSlot(3, loadedHashProbe), "SaveManager accepts unchanged script hash");
        Check(loadedHashProbe.scriptContentHash == hashProbe.scriptContentHash,
              "SaveManager preserves script hash");

        (void)WriteHashValidationScript(
            "function main()\n"
            "    Say(\"Sys\", \"Hash B\", \"\", \"D_HASH\")\n"
            "end\n");
        vn::SaveSnapshot rejectedHashProbe;
        Check(!saveManager.LoadSlot(3, rejectedHashProbe), "SaveManager rejects changed script hash");

        std::filesystem::remove(hashScriptPath, ec);
        std::filesystem::remove_all(saveDir, ec);
    }

    audio.Shutdown();
    assets.SetRenderer(nullptr);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    if (g_failures == 0) {
        std::printf("Phase2ValidationTest: PASS\n");
        return 0;
    }

    std::fprintf(stderr, "Phase2ValidationTest: FAIL (%d checks)\n", g_failures);
    return 1;
}
