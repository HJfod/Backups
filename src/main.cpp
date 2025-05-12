#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/AccountLayer.hpp>
#include <Geode/modify/OptionsLayer.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <chrono>
#include <filesystem>
#include <fmt/chrono.h>
#include "ParseCC.hpp"
#include <pugixml.hpp>

using namespace geode::prelude;

using Clock = std::chrono::system_clock;
using Time = std::chrono::time_point<Clock>;

constexpr size_t BACKUPS_PER_PAGE = 10;

static void enableButton(CCMenuItemSpriteExtra* btn, bool enabled, bool visualOnly = false) {
    btn->setEnabled(enabled || visualOnly);
    if (auto spr = typeinfo_cast<CCRGBAProtocol*>(btn->getNormalImage())) {
        spr->setCascadeColorEnabled(true);
        spr->setCascadeOpacityEnabled(true);
        spr->setColor(enabled ? ccWHITE : ccc3(100, 100, 100));
        // spr->setOpacity(enabled ? 255 : 200);
    }
}

static std::string toAgoString(Time const& time) {
    auto const fmtPlural = [](auto count, auto unit) {
        if (count == 1) {
            return fmt::format("{} {} ago", count, unit);
        }
        return fmt::format("{} {}s ago", count, unit);
    };
    auto now = Clock::now();
    auto len = std::chrono::duration_cast<std::chrono::minutes>(now - time).count();
    if (len < 60) {
        return "Just now";
    }
    len = std::chrono::duration_cast<std::chrono::hours>(now - time).count();
    if (len < 24) {
        return fmtPlural(len, "hour");
    }
    len = std::chrono::duration_cast<std::chrono::days>(now - time).count();
    if (len < 31) {
        return fmtPlural(len, "day");
    }
    return fmt::format("{:%b %d %Y}", time);
}

struct BackupMetadata final {
	std::optional<std::string> name;
	std::string user = GameManager::get()->m_playerName;
	Time time = Clock::now();

	BackupMetadata() = default;
	BackupMetadata(Time time) : time(time) {}
};

template <>
struct matjson::Serialize<BackupMetadata> {
    static matjson::Value toJson(BackupMetadata const& info) {
		return matjson::makeObject({
			{ "name", info.name },
			{ "user", info.user },
			{ "time", std::chrono::duration_cast<std::chrono::hours>(info.time.time_since_epoch()).count() },
		});
	}
    static Result<BackupMetadata> fromJson(matjson::Value const& value) {
		auto info = BackupMetadata();
		auto json = checkJson(value, "BackupMetadata");
		json.has("name").into(info.name);
		json.needs("user").into(info.user);
		int time;
		json.needs("time").into(time);
		info.time = Time(std::chrono::hours(time));
		return json.ok(info);
	}
};

struct BackupInfo final {
	int playerIcon = 0;
	int playerColor1 = 0;
	int playerColor2 = 0;
	std::optional<int> playerGlow = 0;
	int starCount = 0;
	size_t levelCount = 0;
};

static bool DO_NOT_SAVE_GAME = false;
class $modify(AppDelegate) {
	void trySaveGame(bool idk) {
		if (DO_NOT_SAVE_GAME) return;
		AppDelegate::trySaveGame(idk);
	}
};

class Backup final : public CCObject {
private:
	std::filesystem::path m_path;
	BackupMetadata m_meta;
	bool m_autoRemove = false;
	std::optional<Task<BackupInfo>> m_infoTask;

	Backup(std::filesystem::path const& path) : m_path(path) {
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

public:
	static std::vector<Ref<Backup>> get(std::filesystem::path const& dir) {
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
	static Result<> cleanupAutomated(std::filesystem::path const& dir) {
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
	static Result<> create(std::filesystem::path const& backupsDir, bool autoRemove) {
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
	static Result<> migrate(std::filesystem::path const& backupsDir, std::filesystem::path const& existingDir) {
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
	static std::pair<size_t, size_t> migrateAll(std::filesystem::path const& backupsDir, std::filesystem::path const& path) {
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

	std::filesystem::path getPath() const {
		return m_path;
	}
	Time getTime() const {
		return m_meta.time;
	}
	std::string getUser() const {
		return m_meta.user;
	}
	std::chrono::hours getTimeSince() const {
		return std::chrono::duration_cast<std::chrono::hours>(Clock::now() - m_meta.time);
	}
	bool hasLocalLevels() const {
		return std::filesystem::exists(m_path / "CCLocalLevels.dat");
	}
	bool hasGameManager() const {
		return std::filesystem::exists(m_path / "CCGameManager.dat");
	}
	bool isAutoRemove() const {
		return m_autoRemove;
	}
	Task<BackupInfo> loadInfo() {
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
	void cancelLoadInfoIfNotComplete() {
		if (m_infoTask && !m_infoTask->isFinished()) {
			m_infoTask->cancel();
			m_infoTask = std::nullopt;
		}
	}
	Result<> restoreBackup() const {
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
	Result<> deleteBackup() const {
		std::error_code ec;
		std::filesystem::remove_all(m_path, ec);
		if (ec) {
			return Err("Unable to delete backup: {} (code {})", ec.message(), ec.value());
		}
		return Ok();
	}
};

class BackupsPopup;

class BackupNode : public CCNode {
protected:
	BackupsPopup* m_popup;
	Ref<Backup> m_backup;
	LoadingSpinner* m_loadingCircle;
	EventListener<Task<BackupInfo>> m_infoListener;

	bool init(BackupsPopup* popup, Ref<Backup> backup, float width) {
		if (!CCNode::init())
			return false;

		m_popup = popup;
		m_backup = backup;
		this->setContentSize(ccp(width, 40));

		auto bg = CCScale9Sprite::create("square02b_001.png");
		bg->setScale(.3f);
		bg->setContentSize(this->getContentSize() / bg->getScale());
		bg->setColor(ccBLACK);
		bg->setOpacity(140);
		this->addChildAtPosition(bg, Anchor::Center);

		auto name = CCLabelBMFont::create(backup->getUser().c_str(), "bigFont.fnt");
		name->limitLabelWidth(35, .3f, .05f);
		this->addChildAtPosition(name, Anchor::Left, ccp(20, -12));

		auto title = CCLabelBMFont::create(toAgoString(backup->getTime()).c_str(), "goldFont.fnt");
		title->setScale(.45f);
		title->setAnchorPoint({ .0f, .4f });
		this->addChildAtPosition(title, Anchor::Left, ccp(60, 10));

		m_loadingCircle = LoadingSpinner::create(20);
		this->addChildAtPosition(m_loadingCircle, Anchor::Left, ccp(20, 5));

		auto menu = CCMenu::create();
		menu->setContentWidth(100);
		menu->setAnchorPoint({ 1, .5f });
		menu->setScale(.75f);

		auto restoreSpr = ButtonSprite::create("Restore", "bigFont.fnt", "GJ_button_03.png", .8f);
		restoreSpr->setScale(.65f);
		auto restoreBtn = CCMenuItemSpriteExtra::create(
			restoreSpr, this, menu_selector(BackupNode::onRestore)
		);
		menu->addChild(restoreBtn);

		auto deleteSpr = CCSprite::createWithSpriteFrameName("GJ_resetBtn_001.png");
		auto deleteBtn = CCMenuItemSpriteExtra::create(
			deleteSpr, this, menu_selector(BackupNode::onDelete)
		);
		menu->addChild(deleteBtn);

		menu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::End)->setAxisReverse(true));
		this->addChildAtPosition(menu, Anchor::Right, ccp(-10, 0));

		m_infoListener.bind(this, &BackupNode::onInfo);
		m_infoListener.setFilter(backup->loadInfo());

		return true;
	}

	void onInfo(Task<BackupInfo>::Event* event) {
		if (auto info = event->getValue()) {
			if (m_loadingCircle) {
				m_loadingCircle->removeFromParent();
				m_loadingCircle = nullptr;
			}

			auto icon = SimplePlayer::create(info->playerIcon);
			icon->setColor(GameManager::get()->colorForIdx(info->playerColor1));
			icon->setSecondColor(GameManager::get()->colorForIdx(info->playerColor2));
			if (info->playerGlow) {
				icon->setGlowOutline(GameManager::get()->colorForIdx(*info->playerGlow));
			}
			icon->setScale(.65f);
			this->addChildAtPosition(icon, Anchor::Left, ccp(20, 5));

			auto agoSpr = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
			agoSpr->setScale(.5f);
			agoSpr->setAnchorPoint({ .0f, .5f });
			this->addChildAtPosition(agoSpr, Anchor::Left, ccp(45, 10));

			auto starSpr = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
			starSpr->setScale(.5f);
			starSpr->setAnchorPoint({ .0f, .5f });
			this->addChildAtPosition(starSpr, Anchor::Left, ccp(45, -10));

			auto starCount = m_backup->hasGameManager() ? std::to_string(info->starCount) : "N/A";
			auto starLabel = CCLabelBMFont::create(starCount.c_str(), "bigFont.fnt");
			starLabel->setScale(.4f);
			starLabel->setAnchorPoint({ .0f, .5f });
			this->addChildAtPosition(starLabel, Anchor::Left, ccp(60, -10));

			auto levelSpr = CCSprite::createWithSpriteFrameName("GJ_hammerIcon_001.png");
			levelSpr->setScale(.5f);
			levelSpr->setAnchorPoint({ .0f, .5f });
			this->addChildAtPosition(levelSpr, Anchor::Left, ccp(105, -10));

			auto levelCount = m_backup->hasLocalLevels() ? std::to_string(info->levelCount) + " levels" : "N/A";
			auto levelLabel = CCLabelBMFont::create(levelCount.c_str(), "bigFont.fnt");
			levelLabel->setScale(.4f);
			levelLabel->setAnchorPoint({ .0f, .5f });
			this->addChildAtPosition(levelLabel, Anchor::Left, ccp(120, -10));
		}
	}

	void onRestore(CCObject*);
	void onDelete(CCObject*);

public:
	static BackupNode* create(BackupsPopup* popup, Ref<Backup> backup, float width) {
		auto ret = new BackupNode();
		if (ret && ret->init(popup, backup, width)) {
			ret->autorelease();
			return ret;
		}
		CC_SAFE_DELETE(ret);
		return nullptr;
	}

	virtual ~BackupNode() {
		m_backup->cancelLoadInfoIfNotComplete();
	}
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

	bool setup(std::filesystem::path const& dir) override {
		m_dir = dir;
		m_noElasticity = true;

		this->setTitle(fmt::format("Backups for {}", GameManager::get()->m_playerName));

		m_list = ScrollLayer::create({ 310, 180 });
		m_list->m_contentLayer->setLayout(
			ColumnLayout::create()
				->setAxisReverse(true)
				->setAxisAlignment(AxisAlignment::End)
				->setAutoGrowAxis(m_list->getContentHeight())
		);
		m_mainLayer->addChildAtPosition(m_list, Anchor::Center, -m_list->getScaledContentSize() / 2);

		auto bottomMenu = CCMenu::create();
		bottomMenu->setContentWidth(m_size.width);

		auto importSpr = ButtonSprite::create("Import Backups", "goldFont.fnt", "GJ_button_05.png", .8f);
		auto importBtn = CCMenuItemSpriteExtra::create(
			importSpr, this, menu_selector(BackupsPopup::onImport)
		);
		bottomMenu->addChild(importBtn);

		auto createSpr = ButtonSprite::create("New Backup", "goldFont.fnt", "GJ_button_01.png", .8f);
		auto createBtn = CCMenuItemSpriteExtra::create(
			createSpr, this, menu_selector(BackupsPopup::onNew)
		);
		bottomMenu->addChild(createBtn);

		bottomMenu->setLayout(RowLayout::create()->setDefaultScaleLimits(.1f, .75f));
		m_mainLayer->addChildAtPosition(bottomMenu, Anchor::Bottom, ccp(0, 25));

		auto prevPageSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
		m_prevPageBtn = CCMenuItemSpriteExtra::create(
			prevPageSpr, this, menu_selector(BackupsPopup::onPage)
		);
		m_prevPageBtn->setTag(-1);
		m_buttonMenu->addChildAtPosition(m_prevPageBtn, Anchor::Left);

		auto nextPageSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
		nextPageSpr->setFlipX(true);
		m_nextPageBtn = CCMenuItemSpriteExtra::create(
			nextPageSpr, this, menu_selector(BackupsPopup::onPage)
		);
		m_nextPageBtn->setTag(1);
		m_buttonMenu->addChildAtPosition(m_nextPageBtn, Anchor::Right);

		m_pageLabel = CCLabelBMFont::create("", "bigFont.fnt");
		m_pageLabel->setAnchorPoint(ccp(1, 1));
		m_pageLabel->setScale(.3f);
		m_mainLayer->addChildAtPosition(m_pageLabel, Anchor::TopRight, ccp(-10, -5));

		m_importPick.bind(this, &BackupsPopup::onImportPicked);

		this->reloadAll();

		return true;
	}

	void onImportPicked(ImportTask::Event* ev) {
		if (auto res = ev->getValue()) {
			if (res->isOk()) {
				auto [imported, failed] = Backup::migrateAll(m_dir, **res);
				FLAlertLayer::create(
					"Imported backups",
					failed == 0 ?
						fmt::format("Imported <cy>{}</c> backups", imported) :
						fmt::format(
							"Imported <cy>{}</c> backups (<cr>{}</c> failed to import)",
							imported, failed
						),
					"OK"
				)->show();
				this->reloadAll();
			}
			else {
				FLAlertLayer::create("Error importing backups", res->unwrapErr(), "OK")->show();
			}
		}
	}

	void onImport(CCObject*) {
		createQuickPopup(
			"Import Backups",
			"You can <cp>import local backups</c> by selecting either a "
			"<cy>backup directory</c> or a <cj>folder of multiple backups</c>.",
			"Cancel", "Import",
			[popup = Ref(this)](auto, bool btn2) {
				if (btn2) {
					popup->m_importPick.setFilter(file::pick(file::PickMode::OpenFolder, file::FilePickOptions()));
				}
			}
		);
	}
	void onNew(CCObject*) {
		// Create new backup
		auto res = Backup::create(m_dir, false);
		if (res) {
			FLAlertLayer::create("Backed Up", "Backup has been created.", "OK")->show();
		}
		else {
			FLAlertLayer::create("Backup Failed", res.unwrapErr(), "OK")->show();
		}
		this->reloadAll();
	}
	void onPage(CCObject* sender) {
		this->gotoPage(m_page + sender->getTag());
	}

public:
	static BackupsPopup* create(std::filesystem::path const& dir) {
		auto ret = new BackupsPopup();
		if (ret && ret->initAnchored(350, 260, dir, "GJ_square05.png")) {
			ret->autorelease();
			return ret;
		}
		CC_SAFE_DELETE(ret);
		return nullptr;
	}

	std::filesystem::path getDir() const {
		return m_dir;
	}

	void gotoPage(size_t page) {
		m_list->m_contentLayer->removeAllChildren();

		if (m_backups.empty()) {
			m_page = 0;
			m_lastPage = 0;
			auto node = CCNode::create();
			node->setContentSize({ m_list->getContentWidth(), 30 });

			auto bg = CCScale9Sprite::create("square02b_001.png");
			bg->setScale(.3f);
			bg->setContentSize(node->getContentSize() / bg->getScale());
			bg->setColor(ccBLACK);
			bg->setOpacity(140);
			node->addChildAtPosition(bg, Anchor::Center);

			auto info = CCLabelBMFont::create("No Backups Found!", "bigFont.fnt");
			info->setScale(.45f);
			node->addChildAtPosition(info, Anchor::Center);

			m_list->m_contentLayer->addChild(node);
		}
		else {
			m_page = page;
			m_lastPage = (m_backups.size() - 1) / BACKUPS_PER_PAGE;
			if (m_page > m_lastPage) {
				m_page = m_lastPage;
			}

			for (
				size_t i = m_page * BACKUPS_PER_PAGE;
				i < (m_page + 1) * BACKUPS_PER_PAGE && i < m_backups.size();
				i += 1
			) {
				auto backup = m_backups.at(i);
				auto node = BackupNode::create(this, backup, m_list->getContentWidth());
				m_list->m_contentLayer->addChild(node);
			}
		}

		m_list->m_contentLayer->updateLayout();
		m_list->scrollToTop();

		m_pageLabel->setString(fmt::format(
			"Page {}/{} ({} backups)",
			m_page + 1, m_lastPage + 1, m_backups.size()
		).c_str());

		enableButton(m_prevPageBtn, m_page > 0);
		enableButton(m_nextPageBtn, m_page < m_lastPage);
	}
	void reloadAll() {
		m_backups = Backup::get(m_dir);
		this->gotoPage(0);
	}
};

void BackupNode::onRestore(CCObject*) {
	static bool TOGGLED = true;

	auto toggle = CCMenuItemExt::createTogglerWithStandardSprites(1.5f, [](auto* toggle) {
		TOGGLED = !toggle->isToggled();
	});
	auto popup = createQuickPopup(
		"Restore Backup",
		"Do you want to <cp>restore this backup</c>?\n"
		"<cj>The game will be restarted.</c>\n\n",
		"Cancel", "Restore",
		[self = Ref(this), toggle](auto, bool btn2) {
			if (btn2) {
				if (TOGGLED) {
					auto newRes = Backup::create(self->m_popup->getDir(), false);
					if (!newRes) {
						return FLAlertLayer::create("Unable to Backup", newRes.unwrapErr(), "OK")->show();
					}
				}
				auto res = self->m_backup->restoreBackup();
				if (!res) {
					return FLAlertLayer::create("Unable to Restore", res.unwrapErr(), "OK")->show();
				}
				DO_NOT_SAVE_GAME = true;
				game::restart();
			}
		}
	);
	auto toggleMenu = CCMenu::create();
	toggleMenu->setZOrder(20);

	toggleMenu->addChild(toggle);
	toggleMenu->addChild(CCLabelBMFont::create("Backup current progress first", "bigFont.fnt"));

	toggleMenu->setLayout(RowLayout::create()->setDefaultScaleLimits(.1f, .35f)->setGap(20));
	toggleMenu->setPosition(CCDirector::get()->getWinSize() / 2 - ccp(0, 17.5f));
	popup->m_mainLayer->addChild(toggleMenu);

	toggle->toggle(TOGGLED);

	handleTouchPriority(popup);
}
void BackupNode::onDelete(CCObject*) {
	createQuickPopup(
		"Delete Backup",
		"Are you sure you want to <cr>delete this backup</c>?\n"
		"<co>This action is IRREVERSIBLE!</c>",
		"Cancel", "Delete",
		[self = Ref(this)](auto, bool btn2) {
			if (btn2) {
				auto res = self->m_backup->deleteBackup();
				if (!res) {
					FLAlertLayer::create("Unable to Delete", res.unwrapErr(), "OK")->show();
				}
				self->m_popup->reloadAll();
			}
		}
	);
}

static std::chrono::hours backupRateToHours(std::string const& rate) {
	switch (hash(rate.c_str())) {
		case hash("Every Startup"): return std::chrono::hours(1);
		// Because people don't open their PC exactly every 24 hours
		case hash("Daily"): default: return std::chrono::hours(12);
		case hash("Every Other Day"): return std::chrono::hours(36);
		case hash("Every Three Days"): return std::chrono::hours(50);
		case hash("Weekly"): return std::chrono::hours(24 * 7);
	}
}

class FollowInAnotherParent : public CCAction {
protected:
	Ref<CCNode> m_toFollow;
	CCPoint m_offset;

public:
	static FollowInAnotherParent* create(CCNode* target, CCPoint const& offset) {
		auto ret = new FollowInAnotherParent();
		ret->m_toFollow = target;
		ret->m_offset = offset;
		ret->autorelease();
		return ret;
	}
	void step(float) override {
		auto worldPos = m_toFollow->getParent() ? 
			m_toFollow->getParent()->convertToWorldSpace(m_toFollow->getPosition()) : 
			m_toFollow->getPosition();
		auto backToPos = m_pTarget->getParent() ? 
			m_pTarget->getParent()->convertToNodeSpace(worldPos) : 
			worldPos;
		m_pTarget->setPosition(backToPos + m_offset);
	}
};

class SaveToCloudPopup : public Popup<>, public GJAccountBackupDelegate {
protected:
	bool setup() override {
		this->setTitle("Saving to Cloud...");
		GJAccountManager::get()->m_backupDelegate = this;
		// if (!GJAccountManager::get()->getAccountBackupURL()) {
		// }
		return true;
	}

public:
	static SaveToCloudPopup* create() {
		auto ret = new SaveToCloudPopup();
		if (ret && ret->initAnchored(250, 200, "square01_001.png")) {
			ret->autorelease();
			return ret;
		}
		CC_SAFE_DELETE(ret);
		return nullptr;
	}
};

class $modify(MenuLayer) {
	// struct Fields {
	// 	bool doQuitGame = false;
	// };

	bool init() {
		if (!MenuLayer::init()) {
			return false;
		}

		if (!Mod::get()->setSavedValue("shown-where-new-menu-is-in-menulayer", true)) {
			if (auto btn = this->querySelector("bottom-menu settings-button")) {
				auto worldPos = btn->getParent()->convertToWorldSpace(btn->getPosition());
				auto info = CCSprite::createWithSpriteFrameName("moved.png"_spr);
				info->setID("backups-moved-info"_spr);
				info->setZOrder(50);
				info->setAnchorPoint({ .5f, 0 });
				info->runAction(FollowInAnotherParent::create(btn, ccp(23, 25)));
				this->addChild(info);
			}
		}

		auto backupRate = Mod::get()->template getSettingValue<std::string>("auto-local-backup-rate");
		if (backupRate == "Never") {
			return true;
		}
		auto dir = dirs::getSaveDir() / "geode-backups";

		// Backups is sorted from latest to oldest
		auto backups = Backup::get(dir);
		if (backups.size() && backups.front()->getTimeSince() < backupRateToHours(backupRate)) {
			return true;
		}

		// Try cleaning up automated backups. If this fails, not a big deal honestly
		(void)Backup::cleanupAutomated(dir);

		// Create new backup
		auto res = Backup::create(dir, true);
		if (res) {
			log::info("Backed up CCGameManager & CCLocalLevels");
			Notification::create("Save Data has been Backed Up!", NotificationIcon::Success)->show();
		}
		else {
			log::error("Backup failed: {}", res.unwrapErr());
			Notification::create("Failed to back up Save Data", NotificationIcon::Error)->show();
		}

		return true;
	}

	// void onQuit(CCObject* sender) {
	// 	log::info("blah");
	// 	if (m_fields->doQuitGame) {
	// 		MenuLayer::onQuit(sender);
	// 	}
	// 	else {
	// 		createQuickPopup(
	// 			"Save to Cloud",
	// 			"Do you want to <cy>Save your Progress to the Cloud</c> before quitting?",
	// 			"Just Quit", "Save First",
	// 			[this](auto, bool btn2) {
	// 				m_fields->doQuitGame = true;
	// 				if (!btn2) {
	// 					this->onQuit(nullptr);
	// 				}
	// 				else {
	// 					SaveToCloudPopup::create()->show();
	// 				}
	// 			}
	// 		);
	// 	}
	// }
};
class $modify(OptionsLayer) {
	void customSetup() {
		OptionsLayer::customSetup();

		if (!Mod::get()->setSavedValue("shown-where-new-menu-is-in-optionslayer", true)) {
			if (auto btn = this->querySelector("options-menu account-button")) {
				auto worldPos = btn->getParent()->convertToWorldSpace(btn->getPosition());
				auto info = CCSprite::createWithSpriteFrameName("moved-2.png"_spr);
				info->setID("backups-moved-info"_spr);
				info->setZOrder(50);
				info->setAnchorPoint({ 1, .5f });
				info->setRotation(-5);
				info->runAction(FollowInAnotherParent::create(btn, ccp(-75, 10)));
				m_mainLayer->addChild(info);
			}
		}
	}
};
class $modify(MyAccountLayer, AccountLayer) {
	void customSetup() {
		AccountLayer::customSetup();

		auto menu = CCMenu::create();
		menu->setID("backups-menu"_spr);
		menu->setContentHeight(120);
		menu->setAnchorPoint({ .5f, 0 });

		auto spr = CircleButtonSprite::createWithSpriteFrameName(
			"backups.png"_spr, .9f, CircleBaseColor::Pink, CircleBaseSize::Medium
		);
		spr->setTopOffset({ 1, 1 });
		auto btn = CCMenuItemSpriteExtra::create(
			spr, this, menu_selector(MyAccountLayer::onBackups)
		);
		btn->setID("view-backups-btn"_spr);
		menu->addChild(btn);

		menu->setLayout(ColumnLayout::create()->setAxisAlignment(AxisAlignment::Start));
		m_mainLayer->addChildAtPosition(menu, Anchor::BottomLeft, ccp(100, 20), false);

		if (!Mod::get()->setSavedValue("shown-where-new-menu-is-in-accountlayer", true)) {
			auto worldPos = btn->getParent()->convertToWorldSpace(btn->getPosition());
			auto info = CCSprite::createWithSpriteFrameName("moved-3.png"_spr);
			info->setID("backups-moved-info"_spr);
			info->setZOrder(50);
			info->setAnchorPoint({ .5f, .5f });
			info->setRotation(10);
			info->runAction(FollowInAnotherParent::create(btn, ccp(30, 40)));
			m_mainLayer->addChild(info);
		}
	}
	void onBackups(CCObject*) {
		BackupsPopup::create(dirs::getSaveDir() / "geode-backups")->show();
	}
};
