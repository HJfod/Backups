#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include "Backup.hpp"

using namespace geode::prelude;

class BackupsPopup;

class BackupNode : public CCNode {
protected:
	BackupsPopup* m_popup;
	Ref<Backup> m_backup;
	LoadingSpinner* m_loadingCircle;
	EventListener<Task<BackupInfo>> m_infoListener;

	bool init(BackupsPopup* popup, Ref<Backup> backup, float width);

	void onInfo(Task<BackupInfo>::Event* event);

	void onRestore(CCObject*);
	void onDelete(CCObject*);

public:
	static BackupNode* create(BackupsPopup* popup, Ref<Backup> backup, float width);

	virtual ~BackupNode();
};

class BackupsPopup : public Popup<std::filesystem::path const&> {
protected:
	using ImportTask = Task<Result<std::filesystem::path>>;

	std::filesystem::path m_dir;
	ScrollLayer* m_list;
	size_t m_page = 0;
	size_t m_lastPage = 0;
	std::vector<Ref<Backup>> m_backups;
	EventListener<ImportTask> m_importPick;
	CCLabelBMFont* m_pageLabel;
	CCMenuItemSpriteExtra* m_prevPageBtn;
	CCMenuItemSpriteExtra* m_nextPageBtn;

	bool setup(std::filesystem::path const& dir) override;

	void onImportPicked(ImportTask::Event* ev);

	void onImport(CCObject*);
	void onNew(CCObject*);
	void onPage(CCObject* sender);

public:
	static BackupsPopup* create(std::filesystem::path const& dir);

	std::filesystem::path getDir() const;

	void gotoPage(size_t page);
	void reloadAll();
};

