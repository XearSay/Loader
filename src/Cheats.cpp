#include "Cheats.h"
#include "Embedded.h"
#include "Log.h"
#include "Paths.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>

// Отключаем предупреждения
#pragma warning(disable : 4018 4244 4267 4996 4100 4127 4702)

using json = nlohmann::json;

namespace nl::data {
namespace {

time_t ReadLastLaunch(const json& obj) {
    if (obj.contains("last_launch")) return obj.value("last_launch", 0LL);
    return obj.value("lastlaunch", 0LL);
}

void WriteLastLaunch(json& obj, time_t when) {
    obj["last_launch"] = when;
    obj.erase("lastlaunch");
}

void EraseLastLaunch(json& obj) {
    obj.erase("last_launch");
    obj.erase("lastlaunch");
}

bool IsValidStoreShape(const json& data) {
    return data.is_array() && !data.empty();
}

void ParseChangelogs(const json& node, BranchInfo& out) {
    out.changelogs.clear();
    if (!node.contains("changelog")) return;

    const json& cl = node["changelog"];
    auto pushOne = [&](const json& e) {
        ChangelogEntry ent;
        ent.date = e.value("date", 0LL);
        ent.text = e.value("changelog", "");
        if (!ent.text.empty() || ent.date != 0) out.changelogs.push_back(std::move(ent));
    };

    if (cl.is_array()) {
        for (const auto& e : cl)
            if (e.is_object()) pushOne(e);
    } else if (cl.is_object()) {
        pushOne(cl);
    }
}

BranchInfo ParseBranch(const json& b) {
    BranchInfo out;
    out.id = b.value("id", "");
    out.name = b.value("name", out.id);
    out.version = b.value("version", "");
    out.buildDate = b.value("build_date", 0LL);
    out.lastLaunch = ReadLastLaunch(b);
    ParseChangelogs(b, out);
    return out;
}

void FillFromJson(CheatInfo& out, const json& cheat) {
    out = {};
    out.id = cheat.value("cheat", "");
    out.name = cheat.value("name", "");
    out.license = cheat.value("license", 0LL);
    out.lifetime = cheat.value("lifetime", false);
    out.selectedBranch = cheat.value("selected_branch", "");
    const time_t legacyLaunch = ReadLastLaunch(cheat);

    if (cheat.contains("branches") && cheat["branches"].is_array()) {
        for (const auto& b : cheat["branches"])
            out.branches.push_back(ParseBranch(b));
    } else {
        BranchInfo legacy;
        legacy.id = "default";
        legacy.name = cheat.value("type", "Release");
        legacy.version = cheat.value("version", "");
        legacy.buildDate = cheat.value("build_date", 0LL);
        legacy.lastLaunch = legacyLaunch;
        ParseChangelogs(cheat, legacy);
        out.branches.push_back(legacy);
        if (out.selectedBranch.empty()) out.selectedBranch = legacy.id;
    }

    if (out.selectedBranch.empty() && !out.branches.empty())
        out.selectedBranch = out.branches.front().id;

    if (legacyLaunch != 0) {
        if (auto* ab = out.ActiveBranch()) {
            if (ab->lastLaunch == 0) ab->lastLaunch = legacyLaunch;
        }
    }
}

bool ApplyStore(CheatStore& store, const json& data) {
    if (!data.is_array()) return false;
    store.items.clear();
    for (const auto& cheat : data) {
        if (!cheat.is_object()) continue;
        const std::string id = cheat.value("cheat", "");
        if (id.empty()) continue;
        CheatInfo info;
        FillFromJson(info, cheat);
        if (info.id.empty()) info.id = id;
        store.items.push_back(std::move(info));
    }
    return !store.items.empty();
}

constexpr std::uintmax_t kMaxStoreBytes = 8u * 1024u * 1024u;

json LoadJsonFile(const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (!ec && size > kMaxStoreBytes) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "cheats.json too large (%zu bytes > %zu cap); ignoring",
                 static_cast<size_t>(size), static_cast<size_t>(kMaxStoreBytes));
        Log(buffer);
        return nullptr;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return nullptr;
    try {
        return json::parse(in);
    } catch (const std::exception& e) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "JSON parse failed (%s): %s", path.string().c_str(),
                 e.what());
        Log(buffer);
        return nullptr;
    }
}

json LoadEmbeddedJson() {
    const embed::Blob blob = nl::embed::CheatsJson();
    try {
        return json::parse(blob.data, blob.data + blob.size);
    } catch (const std::exception& e) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "Embedded cheats JSON parse failed: %s", e.what());
        Log(buffer);
        return nullptr;
    }
}

std::filesystem::path WithSuffix(const std::filesystem::path& path, const char* suffix) {
    std::filesystem::path out = path;
    out += suffix;
    return out;
}

bool WriteJsonFile(const std::filesystem::path& path, const json& data) {
    namespace fs = std::filesystem;
    const fs::path tmp = WithSuffix(path, ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            char buffer[512];
            snprintf(buffer, sizeof(buffer), "Failed to open temp JSON for write: %s",
                     tmp.string().c_str());
            Log(buffer);
            return false;
        }
        out << data.dump(4);
        out.flush();
        if (!out) {
            Log("Failed while writing temp JSON");
            out.close();
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }

    std::error_code renameEc;
    fs::rename(tmp, path, renameEc);
    if (!renameEc) return true;

    const fs::path bak = WithSuffix(path, ".prev");
    std::error_code existsEc;
    const bool haveDest = fs::exists(path, existsEc);
    if (haveDest) {
        std::error_code bakEc;
        fs::rename(path, bak, bakEc);
        if (bakEc) {
            char buffer[512];
            snprintf(buffer, sizeof(buffer), "JSON write: failed to back up destination (%s): %s",
                     path.string().c_str(), bakEc.message().c_str());
            Log(buffer);
            std::error_code rmEc;
            fs::remove(tmp, rmEc);
            return false;
        }
    }
    std::error_code finalEc;
    fs::rename(tmp, path, finalEc);
    if (!finalEc) {
        std::error_code rmEc;
        fs::remove(bak, rmEc);
        return true;
    }
    if (haveDest) {
        std::error_code restoreEc;
        fs::rename(bak, path, restoreEc);
    }
    std::error_code rmEc;
    fs::remove(tmp, rmEc);
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "Atomic JSON rename failed (%s -> %s): %s",
             tmp.string().c_str(), path.string().c_str(), finalEc.message().c_str());
    Log(buffer);
    return false;
}

void QuarantineBadFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return;
    const std::filesystem::path bad = WithSuffix(path, ".bad");
    std::error_code ec;
    std::filesystem::rename(path, bad, ec);
    if (ec) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "Failed to quarantine bad cheats.json: %s",
                 ec.message().c_str());
        Log(buffer);
    } else {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "Quarantined unusable cheats.json -> %s",
                 bad.string().c_str());
        Log(buffer);
    }
}

json EnsureUserJson() {
    const std::filesystem::path path = nl::UserCheatsPath();
    std::error_code existsEc;
    const bool hadFile = std::filesystem::exists(path, existsEc);
    json data = LoadJsonFile(path);
    if (IsValidStoreShape(data)) {
        CheatStore probe;
        bool usable = false;
        try {
            usable = ApplyStore(probe, data);
        } catch (const std::exception& e) {
            char buffer[512];
            snprintf(buffer, sizeof(buffer), "cheats.json has invalid field types (%s); reseeding",
                     e.what());
            Log(buffer);
        }
        if (usable) return data;
        Log("cheats.json shape OK but unusable; reseeding");
        QuarantineBadFile(path);
    } else if (hadFile) {
        QuarantineBadFile(path);
    }

    data = LoadEmbeddedJson();
    if (IsValidStoreShape(data)) {
        if (!WriteJsonFile(path, data)) Log("Failed to seed user cheats.json");
    }
    return data;
}

} // namespace

const BranchInfo* CheatInfo::ActiveBranch() const {
    for (const auto& branch : branches) {
        if (branch.id == selectedBranch) {
            return &branch;
        }
    }
    return branches.empty() ? nullptr : &branches.front();
}

BranchInfo* CheatInfo::ActiveBranch() {
    return const_cast<BranchInfo*>(std::as_const(*this).ActiveBranch());
}

bool ParseStoreJson(std::string_view text, CheatStore& out, std::string* err) {
    try {
        json data = json::parse(text);
        if (!ApplyStore(out, data)) {
            if (err) *err = "invalid store shape or empty";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

bool UpdateLastLaunchJson(json& data, std::string_view cheatId, std::string_view branchId,
                          time_t when) {
    for (auto& cheat : data) {
        if (!cheat.is_object()) continue;
        if (cheat.value("cheat", "") != cheatId) continue;

        if (cheat.contains("branches") && cheat["branches"].is_array()) {
            for (auto& b : cheat["branches"]) {
                if (!b.is_object()) continue;
                if (b.value("id", "") != branchId) continue;
                WriteLastLaunch(b, when);
                EraseLastLaunch(cheat);
                return true;
            }
            return false;
        }
        WriteLastLaunch(cheat, when);
        return true;
    }
    return false;
}

bool UpdateSelectedBranchJson(json& data, std::string_view cheatId, std::string_view branchId) {
    for (auto& cheat : data) {
        if (!cheat.is_object()) continue;
        if (cheat.value("cheat", "") != cheatId) continue;

        if (cheat.contains("branches") && cheat["branches"].is_array()) {
            const auto& branches = cheat["branches"];
            bool found = false;
            for (const auto& b : branches) {
                if (b.is_object() && b.value("id", "") == branchId) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        cheat["selected_branch"] = branchId;
        return true;
    }
    return false;
}

bool CheatStore::Load() {
    try {
        json data = EnsureUserJson();
        if (!IsValidStoreShape(data)) data = LoadEmbeddedJson();
        if (!IsValidStoreShape(data)) {
            Log("CheatStore::Load: no valid store data");
            return false;
        }
        return ApplyStore(*this, data);
    } catch (const std::exception& e) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "CheatStore::Load failed: %s", e.what());
        Log(buffer);
        return false;
    }
}

bool CheatStore::SaveLastLaunch(const std::string& cheatId, const std::string& branchId,
                                time_t when) try {
    json data = EnsureUserJson();
    if (!IsValidStoreShape(data)) return false;

    if (!UpdateLastLaunchJson(data, cheatId, branchId, when)) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "SaveLastLaunch: cheat/branch not found (%s/%s)",
                 cheatId.c_str(), branchId.c_str());
        Log(buffer);
        return false;
    }
    if (!WriteJsonFile(nl::UserCheatsPath(), data)) {
        Log("SaveLastLaunch: write failed");
        return false;
    }
    return true;
} catch (const std::exception& e) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "SaveLastLaunch failed: %s", e.what());
    Log(buffer);
    return false;
}

bool CheatStore::SaveSelectedBranch(const std::string& cheatId, const std::string& branchId) try {
    json data = EnsureUserJson();
    if (!IsValidStoreShape(data)) return false;

    if (!UpdateSelectedBranchJson(data, cheatId, branchId)) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "SaveSelectedBranch: cheat/branch not found (%s/%s)",
                 cheatId.c_str(), branchId.c_str());
        Log(buffer);
        return false;
    }
    if (!WriteJsonFile(nl::UserCheatsPath(), data)) {
        Log("SaveSelectedBranch: write failed");
        return false;
    }
    return true;
} catch (const std::exception& e) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "SaveSelectedBranch failed: %s", e.what());
    Log(buffer);
    return false;
}

CheatInfo* CheatStore::Find(std::string_view id) {
    for (auto& item : items) {
        if (item.id == id) {
            return std::addressof(item);
        }
    }
    return nullptr;
}

const CheatInfo* CheatStore::Find(std::string_view id) const {
    for (const auto& item : items) {
        if (item.id == id) {
            return std::addressof(item);
        }
    }
    return nullptr;
}

} // namespace nl::data
