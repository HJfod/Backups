#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <matjson.hpp>
#include <Geode/utils/async.hpp>

using namespace geode::prelude;

class Backups;

using Clock = std::chrono::system_clock;
using Time = std::chrono::time_point<Clock>;

struct BackupMetadata final {
	std::optional<std::string> name;
	std::string user = GameManager::get()->m_playerName;
	Time time = Clock::now();

	inline BackupMetadata() = default;
	inline BackupMetadata(Time time) : time(time) {}
};

template <>
struct matjson::Serialize<BackupMetadata> {
    static matjson::Value toJson(BackupMetadata const& info);
    static Result<BackupMetadata> fromJson(matjson::Value const& value);
};

struct BackupInfo final {
	int playerIcon = 0;
	int playerColor1 = 0;
	int playerColor2 = 0;
	std::optional<int> playerGlow = 0;
	int starCount = 0;
	std::vector<std::string> levels;
};

class Backup final : public CCObject {
private:
	std::filesystem::path m_path;
	BackupMetadata m_meta;
	std::optional<size_t> m_autoRemoveOrder;

	Backup(std::filesystem::path const& path);

	friend class Backups;

public:
	static Result<> migrate(std::filesystem::path const& backupsDir, std::filesystem::path const& existingDir);

    static bool isBackup(std::filesystem::path const& dir);

	std::filesystem::path getPath() const;
	Time getTime() const;
	std::string getUser() const;
	std::chrono::hours getTimeSince() const;
	bool hasLocalLevels() const;
	bool hasGameManager() const;
	
	bool isAutoRemove() const;
	std::optional<size_t> getAutoRemoveOrder() const;
	void preserve();

	arc::Future<BackupInfo> loadInfo();

	Result<> restoreBackup() const;
	Result<> deleteBackup() const;
};

class Backups final {
private:
	std::filesystem::path m_dir;
	std::optional<std::vector<Ref<Backup>>> m_backupsCache;

	Backups();
    void fixNestedBackups(std::filesystem::path const& current);

public:
	static Backups* get();

	std::filesystem::path getDirectory() const;
	std::pair<size_t, size_t> migrateAllFrom(std::filesystem::path const& path);
	Result<> createBackup(bool autoRemove);
	Result<> updateBackupsDirectory(std::filesystem::path const& dir);
	Result<> cleanupAutomated();
	std::vector<Ref<Backup>> getAllBackups(bool invalidateCache = false);
	void invalidateCache();
    void fixNestedBackups();
};
