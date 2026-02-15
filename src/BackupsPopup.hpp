#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/file.hpp>
#include "Backup.hpp"

using namespace geode::prelude;

class BackupsPopup;

class BackupNode : public CCNode {
protected:
	BackupsPopup* m_popup;
	Ref<Backup> m_backup;
	LoadingSpinner* m_loadingCircle;
	async::TaskHolder<BackupInfo> m_infoListener;
    std::vector<std::string> m_loadedLevelNames;
	bool m_becameVisible = false;

	bool init(BackupsPopup* popup, Ref<Backup> backup, float width);

	void onLoadInfo(BackupInfo event);
    void onInfo(CCObject*);
    void onLevels(CCObject*);

	void onRestore(CCObject*);
	void onDelete(CCObject*);

public:
	static BackupNode* create(BackupsPopup* popup, Ref<Backup> backup, float width);

	void setVisible(bool visible) override;
};

class BackupsPopup : public Popup {
protected:
	ScrollLayer* m_list;
	size_t m_page = 0;
	size_t m_lastPage = 0;
	async::TaskHolder<file::PickResult> m_importPick;
	CCLabelBMFont* m_pageLabel;
	CCMenuItemSpriteExtra* m_prevPageBtn;
	CCMenuItemSpriteExtra* m_nextPageBtn;
	size_t m_backupsDirSizeCache = 0;

	bool init();

	void onImportPicked(file::PickResult result);

	void onImport(CCObject*);
	void onNew(CCObject*);
	void onPage(CCObject* sender);
	void onDirectory(CCObject*);

public:
	static BackupsPopup* create();

	void gotoPage(size_t page);
	void reloadAll();
};

