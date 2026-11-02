#include "SaveManager.h"

#include "LuaRuntimeSafety.h"
#include "ScriptIdentity.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
#include <sol/sol.hpp>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr int kUiSlotsPerPage = 6;
constexpr int kMaxManualSlot = SaveManager::kMaxSavePages * kUiSlotsPerPage;
constexpr int kCurrentSaveFormatVersion = 4;
constexpr int kCurrentGlobalSaveFormatVersion = 2;
constexpr std::uintmax_t kMaxSaveFileBytes = 4ull * 1024ull * 1024ull;
constexpr std::uintmax_t kMaxGlobalSaveFileBytes = 2ull * 1024ull * 1024ull;
constexpr std::size_t kMaxSaveStringBytes = 64ull * 1024ull;
constexpr std::size_t kMaxSavePathBytes = 2048;
constexpr std::size_t kMaxSpriteStates = 128;
constexpr std::size_t kMaxSaveMapEntries = 8192;
constexpr std::size_t kMaxReplayHistoryEntries = 4096;
constexpr std::size_t kMaxDialogueLogEntries = 4096;

bool ValidateSlotIndex(int slot, std::string& outError) {
    if (slot < 0) {
        outError = "slot must be >= 0";
        return false;
    }
    if (slot > kMaxManualSlot) {
        outError = "slot exceeds configured slot range";
        return false;
    }
    return true;
}

std::filesystem::path TempPathFor(const std::filesystem::path& targetPath) {
    return targetPath.string() + ".tmp";
}

std::filesystem::path BackupPathFor(const std::filesystem::path& targetPath) {
    return targetPath.string() + ".bak";
}

#if defined(_WIN32)
std::string WindowsErrorMessage(const char* operation, DWORD code) {
    return std::string(operation) + " failed with Windows error " + std::to_string(code);
}
#endif

bool ReplaceTextFile(const std::filesystem::path& tmpPath,
                     const std::filesystem::path& targetPath,
                     const std::filesystem::path& backupPath,
                     std::string* outError) {
    std::error_code ec;
    const bool targetExists = std::filesystem::exists(targetPath, ec);
    if (ec) {
        if (outError) *outError = "failed to inspect existing save file";
        return false;
    }

    std::filesystem::remove(backupPath, ec);

#if defined(_WIN32)
    const std::wstring tmpWide = tmpPath.wstring();
    const std::wstring targetWide = targetPath.wstring();
    const std::wstring backupWide = backupPath.wstring();

    if (targetExists) {
        if (!ReplaceFileW(targetWide.c_str(),
                          tmpWide.c_str(),
                          backupWide.c_str(),
                          REPLACEFILE_WRITE_THROUGH,
                          nullptr,
                          nullptr)) {
            if (outError) *outError = WindowsErrorMessage("ReplaceFileW", GetLastError());
            return false;
        }
        return true;
    }

    if (!MoveFileExW(tmpWide.c_str(), targetWide.c_str(), MOVEFILE_WRITE_THROUGH)) {
        if (outError) *outError = WindowsErrorMessage("MoveFileExW", GetLastError());
        return false;
    }
    return true;
#else
    if (targetExists) {
        ec.clear();
        std::filesystem::copy_file(targetPath,
                                   backupPath,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) {
            if (outError) *outError = "failed to create save backup";
            return false;
        }
    }

    ec.clear();
    std::filesystem::rename(tmpPath, targetPath, ec);
    if (ec) {
        if (outError) *outError = "failed to replace save file";
        return false;
    }
    return true;
#endif
}

bool HasRecoverableSaveFile(const std::filesystem::path& targetPath) {
    std::error_code ec;
    return std::filesystem::exists(targetPath, ec) ||
           std::filesystem::exists(BackupPathFor(targetPath), ec) ||
           std::filesystem::exists(TempPathFor(targetPath), ec);
}

bool RecoverMissingSaveFile(const std::filesystem::path& targetPath, std::string* outError) {
    std::error_code ec;
    if (std::filesystem::exists(targetPath, ec)) {
        return true;
    }

    const std::filesystem::path backupPath = BackupPathFor(targetPath);
    const std::filesystem::path tmpPath = TempPathFor(targetPath);
    const std::filesystem::path* sourcePath = nullptr;

    ec.clear();
    if (std::filesystem::exists(backupPath, ec)) {
        sourcePath = &backupPath;
    } else {
        ec.clear();
        if (std::filesystem::exists(tmpPath, ec)) {
            sourcePath = &tmpPath;
        }
    }

    if (!sourcePath) {
        return false;
    }

    ec.clear();
    std::filesystem::copy_file(*sourcePath,
                               targetPath,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
        if (outError) *outError = "failed to recover missing save file";
        return false;
    }

    return true;
}

bool ValidateFileSize(const std::filesystem::path& path,
                      std::uintmax_t maxBytes,
                      const char* label,
                      std::string& outError) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        outError = std::string("failed to read ") + label + " size";
        return false;
    }

    if (size > maxBytes) {
        outError = std::string(label) + " exceeds maximum supported size";
        return false;
    }

    return true;
}

bool ValidateStringBytes(const std::string& value,
                         const char* fieldName,
                         std::size_t maxBytes,
                         std::string& outError) {
    if (value.size() <= maxBytes) {
        return true;
    }

    outError = std::string("save field exceeds maximum supported size: ") + fieldName;
    return false;
}

bool ValidateSaveSnapshotLimits(const vn::SaveSnapshot& data, std::string& outError) {
    if (data.spriteStates.size() > kMaxSpriteStates) {
        outError = "save data has too many sprite states";
        return false;
    }
    if (data.flags.size() > kMaxSaveMapEntries) {
        outError = "save data has too many flag entries";
        return false;
    }
    if (data.numVars.size() > kMaxSaveMapEntries) {
        outError = "save data has too many numeric variable entries";
        return false;
    }
    if (data.luaChoiceHistory.size() > kMaxReplayHistoryEntries) {
        outError = "save data has too many choice history entries";
        return false;
    }
    if (data.luaObserveHistory.size() > kMaxReplayHistoryEntries) {
        outError = "save data has too many observe history entries";
        return false;
    }
    if (data.dialogueLog.size() > kMaxDialogueLogEntries) {
        outError = "save data has too many dialogue log entries";
        return false;
    }
    if (data.luaChoiceCursor > data.luaChoiceHistory.size()) {
        outError = "save data has invalid choice replay cursor";
        return false;
    }
    if (data.luaObserveCursor > data.luaObserveHistory.size()) {
        outError = "save data has invalid observe replay cursor";
        return false;
    }
    if (data.luaYieldSequence != 0 && data.luaYieldOccurrence == 0) {
        outError = "save data has invalid yield occurrence";
        return false;
    }

    if (!ValidateStringBytes(data.saveTime, "time", kMaxSaveStringBytes, outError) ||
        !ValidateStringBytes(data.chapterName, "chapter", kMaxSaveStringBytes, outError) ||
        !ValidateStringBytes(data.scriptPath, "script", kMaxSavePathBytes, outError) ||
        !ValidateStringBytes(data.luaEntryPoint, "lua_entry", kMaxSaveStringBytes, outError) ||
        !ValidateStringBytes(data.luaResumeId, "lua_resume", kMaxSaveStringBytes, outError) ||
        !ValidateStringBytes(data.luaWaitReason, "lua_wait", kMaxSaveStringBytes, outError) ||
        !ValidateStringBytes(data.bgPath, "bg", kMaxSavePathBytes, outError) ||
        !ValidateStringBytes(data.currentBgmPath, "bgm", kMaxSavePathBytes, outError)) {
        return false;
    }

    for (const auto& sprite : data.spriteStates) {
        if (!ValidateStringBytes(sprite.path, "sprite.path", kMaxSavePathBytes, outError)) {
            return false;
        }
    }

    for (const auto& kv : data.flags) {
        if (!ValidateStringBytes(kv.first, "flags.key", kMaxSaveStringBytes, outError)) {
            return false;
        }
    }

    for (const auto& kv : data.numVars) {
        if (!ValidateStringBytes(kv.first, "nums.key", kMaxSaveStringBytes, outError)) {
            return false;
        }
    }

    for (const auto& choice : data.luaChoiceHistory) {
        if (choice.selectedIndex < 0) {
            outError = "save data has invalid choice index";
            return false;
        }
        if (!ValidateStringBytes(choice.id, "lua_choice.id", kMaxSaveStringBytes, outError)) {
            return false;
        }
    }

    for (const auto& observe : data.luaObserveHistory) {
        if (!ValidateStringBytes(observe.id, "lua_observe.id", kMaxSaveStringBytes, outError) ||
            !ValidateStringBytes(observe.selectedTarget, "lua_observe.target", kMaxSaveStringBytes, outError)) {
            return false;
        }
    }

    for (const auto& line : data.dialogueLog) {
        if (!ValidateStringBytes(line.speaker, "log.speaker", kMaxSaveStringBytes, outError) ||
            !ValidateStringBytes(line.text, "log.text", kMaxSaveStringBytes, outError)) {
            return false;
        }
    }

    return true;
}

bool ValidateGlobalSaveLimits(const SaveManager::GlobalSaveData& data, std::string& outError) {
    if (data.seenLines.size() > kMaxSaveMapEntries) {
        outError = "global save has too many seen-line entries";
        return false;
    }
    if (data.unlockFlags.size() > kMaxSaveMapEntries) {
        outError = "global save has too many unlock entries";
        return false;
    }

    if (!ValidateStringBytes(data.locale, "locale", kMaxSaveStringBytes, outError)) {
        return false;
    }

    for (const auto& key : data.seenLines) {
        if (!ValidateStringBytes(key, "seen.key", kMaxSaveStringBytes, outError)) {
            return false;
        }
    }

    for (const auto& kv : data.unlockFlags) {
        if (!ValidateStringBytes(kv.first, "unlocks.key", kMaxSaveStringBytes, outError)) {
            return false;
        }
    }

    return true;
}

// Escape C++ strings into Lua string literals.
void AppendLuaDecimalEscape(std::string& out, unsigned char value) {
    out.push_back('\\');
    out.push_back(static_cast<char>('0' + (value / 100)));
    out.push_back(static_cast<char>('0' + ((value / 10) % 10)));
    out.push_back(static_cast<char>('0' + (value % 10)));
}

std::string LuaQuote(const std::string& str) {
    std::string out;
    out.reserve(str.size() + 2);
    out.push_back('"');
    for (unsigned char c : str) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20 || c == 0x7f) {
                AppendLuaDecimalEscape(out, c);
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

bool AtomicWriteTextFile(const std::filesystem::path& targetPath,
                         const std::function<bool(std::ostream&)>& writer,
                         std::string* outError) {
    std::error_code ec;
    const std::filesystem::path tmpPath = TempPathFor(targetPath);
    const std::filesystem::path backupPath = BackupPathFor(targetPath);
    std::string writeError;

    std::filesystem::remove(tmpPath, ec);

    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out) {
            if (outError) *outError = "failed to open temp save file for writing";
            return false;
        }

        if (!writer(out)) {
            writeError = "failed while writing temp save file";
        } else {
            out.flush();
            if (!out) {
                writeError = "failed to flush temp save file";
            }
        }
    }

    if (!writeError.empty()) {
        std::filesystem::remove(tmpPath, ec);
        if (outError) *outError = writeError;
        return false;
    }

    if (!ReplaceTextFile(tmpPath, targetPath, backupPath, outError)) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    return true;
}

void WriteLuaStringField(std::ostream& out, const char* key, const std::string& value) {
    out << "  " << key << " = " << LuaQuote(value) << ",\n";
}

std::string Uint64ToHex(std::uint64_t value) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string out = "0x";
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned digit = static_cast<unsigned>((value >> shift) & 0x0full);
        if (digit != 0 || started || shift == 0) {
            out.push_back(kHexDigits[digit]);
            started = true;
        }
    }
    return out;
}

std::uint64_t ParseUint64HexString(const std::string& raw) {
    std::size_t index = 0;
    if (raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X')) {
        index = 2;
    }

    std::uint64_t value = 0;
    bool any = false;
    for (; index < raw.size(); ++index) {
        const unsigned char c = static_cast<unsigned char>(raw[index]);
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(10 + c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(10 + c - 'A');
        } else {
            return 0;
        }

        if (value > (std::numeric_limits<std::uint64_t>::max() >> 4)) {
            return 0;
        }
        value = (value << 4) | digit;
        any = true;
    }

    return any ? value : 0;
}

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
std::uint64_t ReadLuaUint64(sol::table table, const char* key) {
    sol::object value = table[key];
    if (!value.valid() || value.get_type() == sol::type::nil) {
        return 0;
    }
    if (value.is<std::string>()) {
        return ParseUint64HexString(value.as<std::string>());
    }
    if (value.is<std::uint64_t>()) {
        return value.as<std::uint64_t>();
    }
    return 0;
}
#endif

template <typename T>
void WriteLuaScalarField(std::ostream& out, const char* key, const T& value) {
    out << "  " << key << " = " << value << ",\n";
}

} // namespace

SaveManager::SaveManager(std::string saveDir)
    : m_saveDir(std::move(saveDir)) {}

bool SaveManager::SaveSlot(int slot, const vn::SaveSnapshot& data) {
    m_lastError.clear();
    if (!ValidateSlotIndex(slot, m_lastError)) {
        return false;
    }
    if (!ValidateSaveSnapshotLimits(data, m_lastError)) {
        return false;
    }

    if (!EnsureSaveDir()) return false;

    const std::filesystem::path slotPath = SlotPath(slot);
    if (!AtomicWriteTextFile(slotPath,
                             [&data](std::ostream& out) {
                                 out << "return {\n";
                                 WriteLuaScalarField(out, "version", kCurrentSaveFormatVersion);
                                 WriteLuaStringField(out, "time", data.saveTime);
                                 WriteLuaStringField(out, "chapter", data.chapterName);
                                 WriteLuaStringField(out, "script", data.scriptPath);
                                 WriteLuaStringField(out, "script_hash", Uint64ToHex(data.scriptContentHash));
                                 WriteLuaStringField(out, "lua_entry", data.luaEntryPoint);
                                 WriteLuaStringField(out, "lua_resume", data.luaResumeId);
                                 WriteLuaStringField(out, "lua_wait", data.luaWaitReason);
                                 WriteLuaScalarField(out, "lua_yield_sequence", data.luaYieldSequence);
                                 WriteLuaScalarField(out, "lua_yield_occurrence", data.luaYieldOccurrence);
                                 WriteLuaScalarField(out, "lua_choice_cursor", data.luaChoiceCursor);
                                 WriteLuaScalarField(out, "lua_observe_cursor", data.luaObserveCursor);
                                 WriteLuaScalarField(out, "lua_rng_seed", data.luaDeterministicSeed);
                                 WriteLuaScalarField(out, "lua_rng_counter", data.luaDeterministicCounter);
                                 WriteLuaStringField(out, "bg", data.bgPath);
                                 WriteLuaStringField(out, "bgm", data.currentBgmPath);
                                 WriteLuaScalarField(out, "camx", data.cameraX);
                                 WriteLuaScalarField(out, "camy", data.cameraY);
                                 WriteLuaScalarField(out, "camz", data.cameraZoom);

                                 out << "  sprites = {\n";
                                 for (const auto& spr : data.spriteStates) {
                                     out << "    { slot = " << spr.slot
                                         << ", path = " << LuaQuote(spr.path)
                                         << ", x = " << spr.x
                                         << ", y = " << spr.y
                                         << ", sx = " << spr.scaleX
                                         << ", sy = " << spr.scaleY
                                         << ", r = " << spr.rotation
                                         << ", ax = " << spr.anchorX
                                         << ", ay = " << spr.anchorY
                                         << ", abs = " << (spr.useAbsolute ? "true" : "false")
                                         << ", tr = " << static_cast<int>(spr.r)
                                         << ", tg = " << static_cast<int>(spr.g)
                                         << ", tb = " << static_cast<int>(spr.b)
                                         << ", ta = " << spr.alpha
                                         << " },\n";
                                 }
                                 out << "  },\n";

                                 out << "  flags = {\n";
                                 for (const auto& kv : data.flags) {
                                     out << "    [" << LuaQuote(kv.first) << "] = " << (kv.second ? "true" : "false") << ",\n";
                                 }
                                 out << "  },\n";

                                 out << "  nums = {\n";
                                 for (const auto& kv : data.numVars) {
                                     out << "    [" << LuaQuote(kv.first) << "] = " << kv.second << ",\n";
                                 }
                                 out << "  },\n";

                                 out << "  lua_choice = {\n";
                                 for (const auto& choice : data.luaChoiceHistory) {
                                     out << "    { id = " << LuaQuote(choice.id) << ", index = " << choice.selectedIndex << " },\n";
                                 }
                                 out << "  },\n";

                                 out << "  lua_observe = {\n";
                                 for (const auto& obs : data.luaObserveHistory) {
                                     out << "    { id = " << LuaQuote(obs.id) << ", target = " << LuaQuote(obs.selectedTarget) << " },\n";
                                 }
                                 out << "  },\n";

                                 out << "  log = {\n";
                                 for (const auto& line : data.dialogueLog) {
                                     out << "    { speaker = " << LuaQuote(line.speaker) << ", text = " << LuaQuote(line.text) << " },\n";
                                 }
                                 out << "  }\n";
                                 out << "}\n";
                                 return out.good();
                             },
                             &m_lastError)) {
        return false;
    }

    return true;
}

bool SaveManager::SaveSlotThumbnailFromSurface(int slot, SDL_Surface* surface) {
    m_lastError.clear();
    if (!ValidateSlotIndex(slot, m_lastError)) {
        return false;
    }
    if (!surface) {
        m_lastError = "thumbnail surface is null";
        return false;
    }
    if (!EnsureSaveDir()) return false;

    if (!SDL_SaveBMP(surface, SlotThumbnailPath(slot).c_str())) {
        m_lastError = "failed to save slot thumbnail";
        return false;
    }
    return true;
}

bool SaveManager::LoadSlot(int slot, vn::SaveSnapshot& outData) const {
    m_lastError.clear();
    if (!ValidateSlotIndex(slot, m_lastError)) {
        return false;
    }

    const std::filesystem::path saveFile = SlotPath(slot);
    if (!RecoverMissingSaveFile(saveFile, &m_lastError)) {
        if (m_lastError.empty()) {
            m_lastError = "save file not found";
        }
        return false;
    }

    if (!std::filesystem::exists(saveFile)) {
        m_lastError = "save file not found";
        return false;
    }

    if (!ValidateFileSize(saveFile, kMaxSaveFileBytes, "save file", m_lastError)) {
        return false;
    }

    outData = {};

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
    sol::state lua;
    // Intentionally do not open any standard libraries.
    // Save files must be data-only Lua tables.

    try {
        sol::load_result loaded = lua.load_file(saveFile.string());
        if (!loaded.valid()) {
            m_lastError = "invalid save file format or syntax error";
            return false;
        }

        sol::environment sandbox(lua, sol::create);
        sol::protected_function script = loaded;
        sol::set_environment(sandbox, script);

        sol::protected_function_result result = vn::lua_safety::RunLuaWithInstructionBudget(
            lua.lua_state(),
            "save slot chunk",
            [&]() { return script(); },
            vn::lua_safety::kSaveFileLuaInstructionBudgetHookEvents);
        if (!result.valid() || result.get_type() != sol::type::table) {
            m_lastError = "save file is safe but did not return a valid data table";
            return false;
        }
        sol::table t = result;

        const int version = t.get_or("version", 1);
        if (version < 1 || version > kCurrentSaveFormatVersion) {
            m_lastError = "unsupported save file version";
            return false;
        }

        outData.saveTime = t.get_or("time", std::string());
        outData.chapterName = t.get_or("chapter", std::string("Prologue"));
        outData.scriptPath = t.get_or("script", std::string());
        outData.scriptContentHash = ReadLuaUint64(t, "script_hash");
        outData.luaEntryPoint = t.get_or("lua_entry", std::string("main"));
        outData.luaResumeId = t.get_or("lua_resume", std::string());
        outData.luaWaitReason = t.get_or("lua_wait", std::string());
        outData.luaYieldSequence = ReadLuaUint64(t, "lua_yield_sequence");
        outData.luaYieldOccurrence = static_cast<std::uint32_t>(
            std::clamp(t.get_or("lua_yield_occurrence", 0), 0, std::numeric_limits<int>::max()));
        outData.luaChoiceCursor = static_cast<std::size_t>(
            std::max(0, t.get_or("lua_choice_cursor", 0)));
        outData.luaObserveCursor = static_cast<std::size_t>(
            std::max(0, t.get_or("lua_observe_cursor", 0)));
        outData.luaDeterministicSeed = t.get_or<std::uint64_t>("lua_rng_seed", 0);
        outData.luaDeterministicCounter = t.get_or<std::uint64_t>("lua_rng_counter", 0);
        outData.bgPath = t.get_or("bg", std::string());
        outData.currentBgmPath = t.get_or("bgm", std::string());
        outData.cameraX = t.get_or("camx", 0.0f);
        outData.cameraY = t.get_or("camy", 0.0f);
        outData.cameraZoom = t.get_or("camz", 1.0f);

        if (!ValidateStringBytes(outData.saveTime, "time", kMaxSaveStringBytes, m_lastError) ||
            !ValidateStringBytes(outData.chapterName, "chapter", kMaxSaveStringBytes, m_lastError) ||
            !ValidateStringBytes(outData.scriptPath, "script", kMaxSavePathBytes, m_lastError) ||
            !ValidateStringBytes(outData.luaEntryPoint, "lua_entry", kMaxSaveStringBytes, m_lastError) ||
            !ValidateStringBytes(outData.luaResumeId, "lua_resume", kMaxSaveStringBytes, m_lastError) ||
            !ValidateStringBytes(outData.luaWaitReason, "lua_wait", kMaxSaveStringBytes, m_lastError) ||
            !ValidateStringBytes(outData.bgPath, "bg", kMaxSavePathBytes, m_lastError) ||
            !ValidateStringBytes(outData.currentBgmPath, "bgm", kMaxSavePathBytes, m_lastError)) {
            return false;
        }

        sol::table sprites = t["sprites"];
        if (sprites.valid()) {
            for (auto& kv : sprites) {
                if (kv.second.is<sol::table>()) {
                    if (outData.spriteStates.size() >= kMaxSpriteStates) {
                        m_lastError = "save file has too many sprite states";
                        return false;
                    }

                    sol::table s = kv.second.as<sol::table>();
                    vn::SaveSnapshot::SaveSpriteState st;
                    st.slot = s.get_or("slot", 0);
                    st.path = s.get_or("path", std::string());
                    if (!ValidateStringBytes(st.path, "sprite.path", kMaxSavePathBytes, m_lastError)) {
                        return false;
                    }
                    st.x = s.get_or("x", 0.0f);
                    st.y = s.get_or("y", 0.0f);
                    st.scaleX = s.get_or("sx", 1.0f);
                    st.scaleY = s.get_or("sy", 1.0f);
                    st.rotation = s.get_or("r", 0.0f);
                    st.anchorX = s.get_or("ax", 0.5f);
                    st.anchorY = s.get_or("ay", 1.0f);
                    st.useAbsolute = s.get_or("abs", false);
                    st.r = static_cast<Uint8>(std::clamp(s.get_or("tr", 255), 0, 255));
                    st.g = static_cast<Uint8>(std::clamp(s.get_or("tg", 255), 0, 255));
                    st.b = static_cast<Uint8>(std::clamp(s.get_or("tb", 255), 0, 255));
                    st.alpha = s.get_or("ta", 1.0f);
                    outData.spriteStates.push_back(st);
                }
            }
        }

        sol::table flags = t["flags"];
        if (flags.valid()) {
            for (auto& kv : flags) {
                if (kv.first.is<std::string>() && kv.second.is<bool>()) {
                    if (outData.flags.size() >= kMaxSaveMapEntries) {
                        m_lastError = "save file has too many flag entries";
                        return false;
                    }

                    std::string key = kv.first.as<std::string>();
                    if (!ValidateStringBytes(key, "flags.key", kMaxSaveStringBytes, m_lastError)) {
                        return false;
                    }
                    outData.flags[std::move(key)] = kv.second.as<bool>();
                }
            }
        }

        sol::table nums = t["nums"];
        if (nums.valid()) {
            for (auto& kv : nums) {
                if (kv.first.is<std::string>() && kv.second.is<double>()) {
                    if (outData.numVars.size() >= kMaxSaveMapEntries) {
                        m_lastError = "save file has too many numeric variable entries";
                        return false;
                    }

                    std::string key = kv.first.as<std::string>();
                    if (!ValidateStringBytes(key, "nums.key", kMaxSaveStringBytes, m_lastError)) {
                        return false;
                    }
                    outData.numVars[std::move(key)] = kv.second.as<double>();
                }
            }
        }

        sol::table choiceHist = t["lua_choice"];
        if (choiceHist.valid()) {
            for (auto& kv : choiceHist) {
                if (!kv.second.is<sol::table>()) {
                    m_lastError = "save file has invalid choice history entry";
                    return false;
                }
                if (outData.luaChoiceHistory.size() >= kMaxReplayHistoryEntries) {
                    m_lastError = "save file has too many choice history entries";
                    return false;
                }

                sol::table c = kv.second.as<sol::table>();
                vn::SaveSnapshot::ChoiceRecord rec;
                rec.id = c.get_or("id", std::string());
                rec.selectedIndex = c.get_or("index", 0);
                if (rec.selectedIndex < 0) {
                    m_lastError = "save file has invalid choice index";
                    return false;
                }
                if (!ValidateStringBytes(rec.id, "lua_choice.id", kMaxSaveStringBytes, m_lastError)) {
                    return false;
                }
                outData.luaChoiceHistory.push_back(std::move(rec));
            }
        }

        sol::table obsHist = t["lua_observe"];
        if (obsHist.valid()) {
            for (auto& kv : obsHist) {
                if (!kv.second.is<sol::table>()) {
                    m_lastError = "save file has invalid observe history entry";
                    return false;
                }
                if (outData.luaObserveHistory.size() >= kMaxReplayHistoryEntries) {
                    m_lastError = "save file has too many observe history entries";
                    return false;
                }

                sol::table o = kv.second.as<sol::table>();
                vn::SaveSnapshot::ObserveRecord rec;
                rec.id = o.get_or("id", std::string());
                rec.selectedTarget = o.get_or("target", std::string());
                if (!ValidateStringBytes(rec.id, "lua_observe.id", kMaxSaveStringBytes, m_lastError) ||
                    !ValidateStringBytes(rec.selectedTarget, "lua_observe.target", kMaxSaveStringBytes, m_lastError)) {
                    return false;
                }
                outData.luaObserveHistory.push_back(std::move(rec));
            }
        }

        sol::table log = t["log"];
        if (log.valid()) {
            for (auto& kv : log) {
                if (!kv.second.is<sol::table>()) {
                    m_lastError = "save file has invalid dialogue log entry";
                    return false;
                }
                if (outData.dialogueLog.size() >= kMaxDialogueLogEntries) {
                    m_lastError = "save file has too many dialogue log entries";
                    return false;
                }

                sol::table l = kv.second.as<sol::table>();
                std::string speaker = l.get_or("speaker", std::string());
                std::string text = l.get_or("text", std::string());
                if (!ValidateStringBytes(speaker, "log.speaker", kMaxSaveStringBytes, m_lastError) ||
                    !ValidateStringBytes(text, "log.text", kMaxSaveStringBytes, m_lastError)) {
                    return false;
                }
                outData.dialogueLog.push_back({ std::move(speaker), std::move(text) });
            }
        }

        if (outData.luaChoiceCursor > outData.luaChoiceHistory.size()) {
            m_lastError = "save file has invalid choice replay cursor";
            return false;
        }
        if (outData.luaObserveCursor > outData.luaObserveHistory.size()) {
            m_lastError = "save file has invalid observe replay cursor";
            return false;
        }
        if (outData.luaYieldSequence != 0 && outData.luaYieldOccurrence == 0) {
            m_lastError = "save file has invalid yield occurrence";
            return false;
        }
    } catch (const sol::error& e) {
        m_lastError = std::string("Lua parse error: ") + e.what();
        return false;
    }
#else
    m_lastError = "Lua support not available to parse save file.";
    return false;
#endif

    if (outData.scriptPath.empty()) {
        m_lastError = "invalid save data: missing script path";
        return false;
    }

    if (outData.scriptPath.find("..") != std::string::npos) {
        m_lastError = "invalid save data: script path contains directory traversal (..)";
        return false;
    }

    std::filesystem::path scriptPath = std::filesystem::absolute(std::filesystem::path(outData.scriptPath));
    std::filesystem::path normalizedScriptPath;
    if (!vn::lua_safety::TryNormalizeTrustedScriptPath(scriptPath, normalizedScriptPath)) {
        m_lastError = "invalid save data: script path is outside safe directory";
        return false;
    }
    scriptPath = normalizedScriptPath;

    if (scriptPath.extension() != ".lua") {
        m_lastError = "invalid save data: script must be a .lua file";
        return false;
    }

    if (!std::filesystem::exists(scriptPath)) {
        m_lastError = "invalid save data: script file not found";
        return false;
    }

    if (outData.scriptContentHash != 0) {
        const std::uint64_t currentHash = vn::ComputeFileContentHash(scriptPath);
        if (currentHash == 0 || currentHash != outData.scriptContentHash) {
            m_lastError = "invalid save data: script content changed since save was created";
            return false;
        }
    }

    return true;
}

bool SaveManager::SaveGlobal(const GlobalSaveData& data) {
    m_lastError.clear();
    if (!ValidateGlobalSaveLimits(data, m_lastError)) {
        return false;
    }
    if (!EnsureSaveDir()) return false;

    const std::filesystem::path globalPath = GlobalPath();
    if (!AtomicWriteTextFile(globalPath,
                             [&data](std::ostream& out) {
                                 out << "return {\n";
                                 out << "  version = " << kCurrentGlobalSaveFormatVersion << ",\n";
                                 out << "  text_speed = " << data.textSpeed << ",\n";
                                 out << "  locale = " << LuaQuote(data.locale) << ",\n";
                                 out << "  bgm_volume = " << data.bgmVolume << ",\n";
                                 out << "  sfx_volume = " << data.sfxVolume << ",\n";
                                 out << "  fullscreen = " << (data.fullscreen ? "true" : "false") << ",\n";
                                 out << "  skip_unseen = " << (data.skipUnseen ? "true" : "false") << ",\n";
                                 out << "  wheel_advance = " << (data.wheelAdvanceEnabled ? "true" : "false") << ",\n";
                                 out << "  voice_bleed = " << (data.voiceBleedEnabled ? "true" : "false") << ",\n";
                                 out << "  instant_text = " << (data.instantText ? "true" : "false") << ",\n";
                                 out << "  animated_sprites = " << (data.animatedSprites ? "true" : "false") << ",\n";

                                 out << "  seen = {\n";
                                 for (const auto& key : data.seenLines) {
                                     out << "    [" << LuaQuote(key) << "] = true,\n";
                                 }
                                 out << "  },\n";

                                 out << "  unlocks = {\n";
                                 for (const auto& kv : data.unlockFlags) {
                                     out << "    [" << LuaQuote(kv.first) << "] = " << (kv.second ? "true" : "false") << ",\n";
                                 }
                                 out << "  }\n";
                                 out << "}\n";
                                 return out.good();
                             },
                             &m_lastError)) {
        return false;
    }

    return true;
}

bool SaveManager::LoadGlobal(GlobalSaveData& outData) const {
    m_lastError.clear();
    outData = {};

    const std::filesystem::path globalPath = GlobalPath();
    if (!RecoverMissingSaveFile(globalPath, &m_lastError)) {
        if (m_lastError.empty()) {
            return true;
        }
        return false;
    }

    if (!std::filesystem::exists(globalPath)) {
        return true;
    }

    if (!ValidateFileSize(globalPath, kMaxGlobalSaveFileBytes, "global save file", m_lastError)) {
        return false;
    }

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
    sol::state lua;
    // Intentionally do not open any standard libraries.
    // Global save must be a pure data table.

    try {
        const std::string globalFile = globalPath.string();
        sol::load_result loaded = lua.load_file(globalFile);
        if (!loaded.valid()) {
            m_lastError = "invalid global save format or syntax error";
            return false;
        }

        sol::environment sandbox(lua, sol::create);
        sol::protected_function script = loaded;
        sol::set_environment(sandbox, script);

        sol::protected_function_result result = vn::lua_safety::RunLuaWithInstructionBudget(
            lua.lua_state(),
            "global save chunk",
            [&]() { return script(); },
            vn::lua_safety::kSaveFileLuaInstructionBudgetHookEvents);
        if (!result.valid() || result.get_type() != sol::type::table) {
            m_lastError = "global save is safe but did not return a valid data table";
            return false;
        }
        sol::table t = result;

        const int version = t.get_or("version", 1);
        if (version < 1 || version > kCurrentGlobalSaveFormatVersion) {
            m_lastError = "unsupported global save version";
            return false;
        }

        outData.textSpeed = t.get_or("text_speed", 30.0f);
        outData.locale = t.get_or("locale", std::string("zh"));
        outData.bgmVolume = t.get_or("bgm_volume", 0.8f);
        outData.sfxVolume = t.get_or("sfx_volume", 0.9f);
        outData.fullscreen = t.get_or("fullscreen", false);
        outData.skipUnseen = t.get_or("skip_unseen", false);
        outData.wheelAdvanceEnabled = t.get_or("wheel_advance", false);
        outData.voiceBleedEnabled = t.get_or("voice_bleed", false);
        outData.instantText = t.get_or("instant_text", false);
        outData.animatedSprites = t.get_or("animated_sprites", true);

        if (!ValidateStringBytes(outData.locale, "locale", kMaxSaveStringBytes, m_lastError)) {
            return false;
        }

        sol::table seen = t["seen"];
        if (seen.valid()) {
            for (auto& kv : seen) {
                if (kv.first.is<std::string>() && kv.second.is<bool>() && kv.second.as<bool>()) {
                    if (outData.seenLines.size() >= kMaxSaveMapEntries) {
                        m_lastError = "global save has too many seen-line entries";
                        return false;
                    }

                    std::string key = kv.first.as<std::string>();
                    if (!ValidateStringBytes(key, "seen.key", kMaxSaveStringBytes, m_lastError)) {
                        return false;
                    }
                    outData.seenLines.insert(std::move(key));
                }
            }
        }

        sol::table unlocks = t["unlocks"];
        if (unlocks.valid()) {
            for (auto& kv : unlocks) {
                if (kv.first.is<std::string>() && kv.second.is<bool>()) {
                    if (outData.unlockFlags.size() >= kMaxSaveMapEntries) {
                        m_lastError = "global save has too many unlock entries";
                        return false;
                    }

                    std::string key = kv.first.as<std::string>();
                    if (!ValidateStringBytes(key, "unlocks.key", kMaxSaveStringBytes, m_lastError)) {
                        return false;
                    }
                    outData.unlockFlags[std::move(key)] = kv.second.as<bool>();
                }
            }
        }
    } catch (const sol::error& e) {
        m_lastError = std::string("Lua parse error: ") + e.what();
        return false;
    }
#else
    m_lastError = "Lua support not available to parse global save file.";
    return false;
#endif

    return true;
}

bool SaveManager::DeleteSlot(int slot) {
    m_lastError.clear();
    if (!ValidateSlotIndex(slot, m_lastError)) {
        return false;
    }

    bool success = true;
    std::error_code ec;

    const std::filesystem::path savePath = SlotPath(slot);
    if (std::filesystem::exists(savePath)) {
        ec.clear();
        if (!std::filesystem::remove(savePath, ec)) {
            m_lastError = "failed to delete save file";
            success = false;
        }
    }

    for (const auto& sidecarPath : { TempPathFor(savePath), BackupPathFor(savePath) }) {
        ec.clear();
        if (std::filesystem::exists(sidecarPath, ec)) {
            ec.clear();
            if (!std::filesystem::remove(sidecarPath, ec)) {
                m_lastError += (m_lastError.empty() ? "" : " | ");
                m_lastError += "failed to delete save sidecar file";
                success = false;
            }
        }
    }

    const std::filesystem::path thumbPath = SlotThumbnailPath(slot);
    if (std::filesystem::exists(thumbPath)) {
        ec.clear();
        if (!std::filesystem::remove(thumbPath, ec)) {
            m_lastError += (m_lastError.empty() ? "" : " | ");
            m_lastError += "failed to delete thumbnail file";
            success = false;
        }
    }

    return success;
}

std::vector<SaveManager::SlotInfo> SaveManager::ListSlots(int maxSlots, int page) const {
    if (maxSlots <= 0 || page < 0 || page >= kMaxSavePages) {
        return {};
    }

    std::vector<SlotInfo> out;
    out.reserve(static_cast<std::size_t>(maxSlots));

    const long long startSlot64 = static_cast<long long>(page) * static_cast<long long>(maxSlots) + 1;
    if (startSlot64 > static_cast<long long>(kMaxManualSlot)) {
        return {};
    }
    const long long endSlot64 = std::min(startSlot64 + static_cast<long long>(maxSlots) - 1,
                                         static_cast<long long>(kMaxManualSlot));

    for (long long slot64 = startSlot64; slot64 <= endSlot64; ++slot64) {
        const int slot = static_cast<int>(slot64);
        SlotInfo info;
        info.slot = slot;
        info.path = SlotPath(slot);
        info.exists = HasRecoverableSaveFile(info.path);

        if (info.exists) {
            vn::SaveSnapshot metadata;
            if (LoadSlot(slot, metadata)) {
                info.chapterName = metadata.chapterName;
                info.saveTime = metadata.saveTime;
                info.bgPath = metadata.bgPath;
            } else {
                info.exists = false;
            }
        }
        info.thumbnailPath = SlotThumbnailPath(slot);
        info.hasThumbnail = std::filesystem::exists(info.thumbnailPath);
        out.push_back(std::move(info));
    }

    return out;
}

int SaveManager::GetSavePageCount() const {
    return kMaxSavePages;
}

std::string SaveManager::SlotPath(int slot) const {
    return m_saveDir + "/slot" + std::to_string(slot) + ".sav";
}

std::string SaveManager::SlotThumbnailPath(int slot) const {
    return m_saveDir + "/slot" + std::to_string(slot) + ".bmp";
}

std::string SaveManager::GlobalPath() const {
    return m_saveDir + "/global.sav";
}

bool SaveManager::EnsureSaveDir() const {
    std::error_code ec;
    std::filesystem::create_directories(m_saveDir, ec);
    if (ec) {
        m_lastError = "failed to create save directory";
        return false;
    }
    return true;
}

bool SaveManager::QuickSave(const vn::SaveSnapshot& data) {
    return SaveSlot(0, data);
}

bool SaveManager::QuickLoad(vn::SaveSnapshot& outData) const {
    return LoadSlot(0, outData);
}

bool SaveManager::QuickSaveExists() const {
    return HasRecoverableSaveFile(SlotPath(0));
}
