#include "Backup.hpp"
#include "ParseCC.hpp"
#include <Geode/utils/JsonValidation.hpp>
#include <Geode/utils/file.hpp>
#include <matjson/std.hpp>
#include <pugixml.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/loader/Mod.hpp>

matjson::Value matjson::Serialize<BackupMetadata>::toJson(BackupMetadata const& info) {
    return matjson::makeObject({
        { "name", info.name },
        { "user", info.user },
        { "time", std::chrono::duration_cast<std::chrono::hours>(info.time.time_since_epoch()).count() },
    });
}
Result<BackupMetadata> matjson::Serialize<BackupMetadata>::fromJson(matjson::Value const& value) {
    auto info = BackupMetadata();
    auto json = checkJson(value, "BackupMetadata");
    json.has("name").into(info.name);
    json.needs("user").into(info.user);
    int time;
    json.needs("time").into(time);
    info.time = Time(std::chrono::hours(time));
    return json.ok(info);
}

Backup::Backup(std::filesystem::path const& path) : m_path(path) {
    if (auto meta = file::readFromJson<BackupMetadata>(path / "metadata.json")) {
        m_meta = *meta;
    }
    // Fix corrupt metadata
    else {
        std::error_code ec;
        auto time = std::chrono::time_point_cast<Time::duration>(
            std::filesystem::last_write_time(path, ec) - std::chrono::file_clock::now() + Clock::now()
        );
        (void)file::writeToJson(path / "metadata.json", BackupMetadata(time));
    }
    m_autoRemove = std::filesystem::exists(path / "auto-remove.txt");
    this->autorelease();
}

std::vector<Ref<Backup>> Backup::get(std::filesystem::path const& dir) {
    std::vector<Ref<Backup>> backups;
    for (auto b : file::readDirectory(dir, false).unwrapOrDefault()) {
        if (
            std::filesystem::exists(b / "CCGameManager.dat") || 
            std::filesystem::exists(b / "CCLocalLevels.dat")
        ) {
            backups.push_back(Ref(new Backup(b)));
        }
    }
    std::sort(backups.begin(), backups.end(), [](auto first, auto second) {
        return first->m_meta.time > second->m_meta.time;
    });
    return backups;
}
Result<> Backup::cleanupAutomated(std::filesystem::path const& dir) {
    auto backups = Backup::get(dir);
    int64_t limit = Mod::get()->getSettingValue<int64_t>("auto-backup-cleanup-limit");
    for (auto backup : backups) {
        if (backup->isAutoRemove()) {
            if (limit > 0) {
                limit -= 1;
            }
            else {
                GEODE_UNWRAP(backup->deleteBackup());
            }
        }
    }
    return Ok();
}
Result<> Backup::create(std::filesystem::path const& backupsDir, bool autoRemove) {
    auto time = std::chrono::system_clock::now();
    std::string dirname;
    try {
        // fmt::format uses exceptions :sob:
        dirname = fmt::format("{:%Y-%m-%d_%H-%M}", time);
    }
    catch(...) {
        dirname = "unktime";
    }

    std::string findname = dirname;
    size_t num = 0;
    while (std::filesystem::exists(backupsDir / findname)) {
        findname = dirname + "-" + std::to_string(num);
        num += 1;
    }

    auto dir = backupsDir / findname;
    GEODE_UNWRAP(file::createDirectoryAll(dir));

    // Copy CC files
    std::error_code ec;
    std::filesystem::copy_file(dirs::getSaveDir() / "CCGameManager.dat", dir / "CCGameManager.dat", ec);
    std::filesystem::copy_file(dirs::getSaveDir() / "CCLocalLevels.dat", dir / "CCLocalLevels.dat", ec);
    if (ec) {
        return Err("Unable to create backup: {} (code {})", ec.message(), ec.value());
    }

    // Save metadata
    GEODE_UNWRAP(file::writeToJson(dir / "metadata.json", BackupMetadata(time)));

    if (autoRemove) {
        // Not a big deal if this fails
        (void)file::writeString(dir / "auto-remove.txt", fmt::format(
            "This backup will be removed when your set auto backup limit of {} is reached.\n\nIf you'd like to preserve this backup, delete this text file.",
            Mod::get()->getSettingValue<int64_t>("auto-backup-cleanup-limit")
        ));
    }

    return Ok();
}
Result<> Backup::migrate(std::filesystem::path const& backupsDir, std::filesystem::path const& existingDir) {
    GEODE_UNWRAP(file::createDirectoryAll(backupsDir));

    // Try to infer backup creation date from folder write time
    std::error_code ec;
    auto time = std::chrono::time_point_cast<Time::duration>(
        std::filesystem::last_write_time(existingDir, ec) - std::chrono::file_clock::now() + Clock::now()
    );

    std::string dirname;
    try {
        // fmt::format uses exceptions :sob:
        dirname = fmt::format("{:%Y-%m-%d_%H-%M}", time);
    }
    catch(...) {
        dirname = "unktime";
    }

    std::string findname = dirname;
    size_t num = 0;
    while (std::filesystem::exists(backupsDir / findname)) {
        findname = dirname + "-" + std::to_string(num);
        num += 1;
    }

    auto dir = backupsDir / findname;

    std::filesystem::rename(existingDir, dir, ec);
    if (ec) {
        return Err("Unable to migrate backup: {} (code {})", ec.message(), ec.value());
    }
    // Save metadata
    GEODE_UNWRAP(file::writeToJson(dir / "metadata.json", BackupMetadata(time)));

    return Ok();
}
std::pair<size_t, size_t> Backup::migrateAll(std::filesystem::path const& backupsDir, std::filesystem::path const& path) {
    if (
        std::filesystem::exists(path / "CCGameManager.dat") || 
        std::filesystem::exists(path / "CCLocalLevels.dat") 
    ) {
        if (Backup::migrate(backupsDir, path)) {
            return std::make_pair(1, 0);
        }
        else {
            return std::make_pair(0, 1);
        }
    }

    size_t imported = 0;
    size_t failed = 0;
    for (auto folder : file::readDirectory(path).unwrapOrDefault()) {
        if (std::filesystem::is_directory(folder)) {
            auto [i, f] = Backup::migrateAll(backupsDir, folder);
            imported += i;
            failed += f;
        }
    }
    return std::make_pair(imported, failed);
}

std::filesystem::path Backup::getPath() const {
    return m_path;
}
Time Backup::getTime() const {
    return m_meta.time;
}
std::string Backup::getUser() const {
    return m_meta.user;
}
std::chrono::hours Backup::getTimeSince() const {
    return std::chrono::duration_cast<std::chrono::hours>(Clock::now() - m_meta.time);
}
bool Backup::hasLocalLevels() const {
    return std::filesystem::exists(m_path / "CCLocalLevels.dat");
}
bool Backup::hasGameManager() const {
    return std::filesystem::exists(m_path / "CCGameManager.dat");
}
bool Backup::isAutoRemove() const {
    return m_autoRemove;
}

Task<BackupInfo> Backup::loadInfo() {
    // Cache info because loading this takes forevah
    if (m_infoTask) {
        return *m_infoTask;
    }
    m_infoTask.emplace(Task<BackupInfo>::run([path = m_path](auto, auto cancelled) -> Task<BackupInfo>::Result {
        auto info = BackupInfo();

        auto ccgmData = cc::parseCompressedCCFile(path / "CCGameManager.dat", cancelled).unwrapOrDefault();

        if (cancelled()) {
            return Task<BackupInfo>::Cancel();
        }

        pugi::xml_document ccgm;
        if (ccgm.load_buffer(ccgmData.data(), ccgmData.size())) {
            if (auto find = ccgm.select_single_node(
                "//k[normalize-space()=\"GS_value\"]/following-sibling::d/k[normalize-space()=\"6\"]/following-sibling::s"
            )) {
                info.starCount = find.node().text().as_int();
            }
            if (auto find = ccgm.select_single_node(
                "//k[normalize-space()=\"playerFrame\"]/following-sibling::i"
            )) {
                info.playerIcon = find.node().text().as_int();
            }
            if (auto find = ccgm.select_single_node(
                "//k[normalize-space()=\"playerColor\"]/following-sibling::i"
            )) {
                info.playerColor1 = find.node().text().as_int();
            }
            if (auto find = ccgm.select_single_node(
                "//k[normalize-space()=\"playerColor2\"]/following-sibling::i"
            )) {
                info.playerColor2 = find.node().text().as_int();
            }
            if (ccgm.select_single_node(
                "//k[normalize-space()=\"playerGlow\"]/following-sibling::t"
            )) {
                info.playerGlow = true;
            }
        }

        auto ccllData = cc::parseCompressedCCFile(path / "CCLocalLevels.dat", cancelled).unwrapOrDefault();

        if (cancelled()) {
            return Task<BackupInfo>::Cancel();
        }

        pugi::xml_document ccll;
        if (ccll.load_buffer(ccllData.data(), ccllData.size())) {
            info.levelCount = ccll.select_nodes("//k[text()=\"LLM_01\"]/following-sibling::d/k").size();
        }

        return info;
    }));
    return *m_infoTask;
}
void Backup::cancelLoadInfoIfNotComplete() {
    if (m_infoTask && !m_infoTask->isFinished()) {
        m_infoTask->cancel();
        m_infoTask = std::nullopt;
    }
}

Result<> Backup::restoreBackup() const {
    std::error_code ec;
    std::filesystem::copy_file(
        m_path / "CCGameManager.dat", dirs::getSaveDir() / "CCGameManager.dat",
        std::filesystem::copy_options::overwrite_existing, ec
    );
    std::filesystem::copy_file(
        m_path / "CCLocalLevels.dat", dirs::getSaveDir() / "CCLocalLevels.dat",
        std::filesystem::copy_options::overwrite_existing, ec
    );
    if (ec) {
        return Err("Unable to restore backup: {} (code {})", ec.message(), ec.value());
    }
    return Ok();
}
Result<> Backup::deleteBackup() const {
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
    if (ec) {
        return Err("Unable to delete backup: {} (code {})", ec.message(), ec.value());
    }
    return Ok();
}
