#pragma once

#include "SaveManager.h"
#include "SceneManager.h"
#include "SaveSnapshot.h"

class SaveSystem {
public:
    static vn::SaveSnapshot Build(const SceneManager& scene);
    static bool Restore(SceneManager& scene,
                        const vn::SaveSnapshot& data,
                        SceneManager::RestoreMode mode = SceneManager::RestoreMode::Replay);
    static bool QuickSave(SaveManager& saveManager, const SceneManager& scene, int slot = 0);
    static bool QuickLoad(const SaveManager& saveManager, SceneManager& scene, int slot = 0);
};
