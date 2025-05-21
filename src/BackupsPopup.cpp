#include "BackupsPopup.hpp"
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/utils/ranges.hpp>

constexpr size_t BACKUPS_PER_PAGE = 10;

static bool DO_NOT_SAVE_GAME = false;

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

bool BackupNode::init(BackupsPopup* popup, Ref<Backup> backup, float width) {
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

    if (backup->isAutoRemove()) {
        bg->setColor(ccc3(24, 69, 114));
    }

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

    auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    auto infoBtn = CCMenuItemSpriteExtra::create(
        infoSpr, this, menu_selector(BackupNode::onInfo)
    );
    menu->addChild(infoBtn);

    menu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::End)->setAxisReverse(true));
    this->addChildAtPosition(menu, Anchor::Right, ccp(-10, 0));

    m_infoListener.bind(this, &BackupNode::onLoadInfo);
    m_infoListener.setFilter(backup->loadInfo());

    return true;
}

void BackupNode::onLoadInfo(Task<BackupInfo>::Event* event) {
    if (auto info = event->getValue()) {
        if (m_loadingCircle) {
            m_loadingCircle->removeFromParent();
            m_loadingCircle = nullptr;
        }

        m_loadedLevelNames = info->levels;

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

        auto levelCount = m_backup->hasLocalLevels() ? std::to_string(m_loadedLevelNames.size()) + " levels" : "N/A";
        auto levelLabel = CCLabelBMFont::create(levelCount.c_str(), "bigFont.fnt");
        levelLabel->setScale(.4f);
        levelLabel->setAnchorPoint({ .0f, .5f });
        this->addChildAtPosition(levelLabel, Anchor::Left, ccp(120, -10));

        auto levelInfoMenu = CCMenu::create();
        levelInfoMenu->ignoreAnchorPointForPosition(false);
        levelInfoMenu->setContentSize(ccp(25, 25));
        
        auto levelInfoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        levelInfoSpr->setScale(.5f);
        auto levelInfoBtn = CCMenuItemSpriteExtra::create(
            levelInfoSpr, this, menu_selector(BackupNode::onLevels)
        );
        levelInfoMenu->addChildAtPosition(levelInfoBtn, Anchor::Center);
        this->addChildAtPosition(
            levelInfoMenu, Anchor::Left,
            ccp(120 + levelLabel->getScaledContentWidth() + 8, -10)
        );
    }
}
void BackupNode::onInfo(CCObject*) {
    auto content = fmt::format("Created on {:%Y/%m/%d}", m_backup->getTime());
    if (m_backup->isAutoRemove()) {
        createQuickPopup(
            "Backup Info",
            content + fmt::format(
                "\n<co>This backup will be automatically cleaned up after {} more "
                "backups have been made. If you'd like to preserve it, press</c> "
                "<cg>Preserve</c><co>.</c>",
                Mod::get()->getSettingValue<int64_t>("auto-backup-cleanup-limit") - 
                    static_cast<int64_t>(m_backup->getAutoRemoveOrder().value_or(0))
            ),
            "OK", "Preserve",
            [this](auto, bool btn2) {
                if (btn2) {
                    m_backup->preserve();
				    m_popup->reloadAll();
                }
            }
        );
    }
    else {
        FLAlertLayer::create("Backup Info", content, "OK")->show();
    }
}
void BackupNode::onLevels(CCObject*) {
    std::string text = "";
    for (size_t i = 0; i < m_loadedLevelNames.size(); i += 1) {
        if (i > 10) {
            text += ", <cj>...</c>";
            break;
        }
        if (i > 0) {
            text += ", ";
        }
        text += fmt::format("<c{}>{}</c>", (i % 2 ? "y" : "o"), m_loadedLevelNames.at(i));
    }
    FLAlertLayer::create("Levels in Backup", text, "OK")->show();
}

BackupNode* BackupNode::create(BackupsPopup* popup, Ref<Backup> backup, float width) {
    auto ret = new BackupNode();
    if (ret && ret->init(popup, backup, width)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

BackupNode::~BackupNode() {
    m_backup->cancelLoadInfoIfNotComplete();
}

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
					auto newRes = Backups::get()->createBackup(false);
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

bool BackupsPopup::setup() {
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

void BackupsPopup::onImportPicked(ImportTask::Event* ev) {
    if (auto res = ev->getValue()) {
        if (res->isOk()) {
            auto [imported, failed] = Backups::get()->migrateAllFrom(**res);
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

void BackupsPopup::onImport(CCObject*) {
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
void BackupsPopup::onNew(CCObject*) {
    // Create new backup
    auto res = Backups::get()->createBackup(false);
    if (res) {
        FLAlertLayer::create("Backed Up", "Backup has been created.", "OK")->show();
    }
    else {
        FLAlertLayer::create("Backup Failed", res.unwrapErr(), "OK")->show();
    }
    this->reloadAll();
}
void BackupsPopup::onPage(CCObject* sender) {
    this->gotoPage(m_page + sender->getTag());
}

BackupsPopup* BackupsPopup::create() {
    auto ret = new BackupsPopup();
    if (ret && ret->initAnchored(350, 260, "GJ_square05.png")) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void BackupsPopup::gotoPage(size_t page) {
    m_list->m_contentLayer->removeAllChildren();

    auto backups = Backups::get()->getAllBackups();
    if (backups.empty()) {
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
        m_lastPage = (backups.size() - 1) / BACKUPS_PER_PAGE;
        if (m_page > m_lastPage) {
            m_page = m_lastPage;
        }

        for (
            size_t i = m_page * BACKUPS_PER_PAGE;
            i < (m_page + 1) * BACKUPS_PER_PAGE && i < backups.size();
            i += 1
        ) {
            auto backup = backups.at(i);
            auto node = BackupNode::create(this, backup, m_list->getContentWidth());
            m_list->m_contentLayer->addChild(node);
        }
    }

    m_list->m_contentLayer->updateLayout();
    m_list->scrollToTop();

    m_pageLabel->setString(fmt::format(
        "Page {}/{} ({} backups)",
        m_page + 1, m_lastPage + 1, backups.size()
    ).c_str());

    enableButton(m_prevPageBtn, m_page > 0);
    enableButton(m_nextPageBtn, m_page < m_lastPage);
}
void BackupsPopup::reloadAll() {
    Backups::get()->invalidateCache();
    this->gotoPage(0);
}

class $modify(AppDelegate) {
	void trySaveGame(bool idk) {
		if (DO_NOT_SAVE_GAME) return;
		AppDelegate::trySaveGame(idk);
	}
};
