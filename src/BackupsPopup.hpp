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
    std::vector<std::string> m_loadedLevelNames;
	bool m_becameVisible = false;

	bool init(BackupsPopup* popup, Ref<Backup> backup, float width);

	void onLoadInfo(Task<BackupInfo>::Event* event);
    void onInfo(CCObject*);
    void onLevels(CCObject*);

	void onRestore(CCObject*);
	void onDelete(CCObject*);

public:
	static BackupNode* create(BackupsPopup* popup, Ref<Backup> backup, float width);

	void setVisible(bool visible) override;

	virtual ~BackupNode();
};

class BackupsPopup : public Popup<> {
protected:
	using ImportTask = Task<Result<std::filesystem::path>>;

	ScrollLayer* m_list;
	size_t m_page = 0;
	size_t m_lastPage = 0;
	EventListener<ImportTask> m_importPick;
	CCLabelBMFont* m_pageLabel;
	CCMenuItemSpriteExtra* m_prevPageBtn;
	CCMenuItemSpriteExtra* m_nextPageBtn;
	size_t m_backupsDirSizeCache = 0;

	bool setup() override;

	void onImportPicked(ImportTask::Event* ev);

	void onImport(CCObject*);
	void onNew(CCObject*);
	void onPage(CCObject* sender);
	void onDirectory(CCObject*);

public:
	static BackupsPopup* create();

	void gotoPage(size_t page);
	void reloadAll();
};

