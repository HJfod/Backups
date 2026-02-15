#include "Backup.hpp"
#include "BackupsPopup.hpp"
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/OptionsLayer.hpp>
#include <Geode/modify/AccountLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace geode::prelude;

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

$execute {
	listenForSettingChanges<std::filesystem::path>("backup-directory", +[](std::filesystem::path dir) {
		(void)Backups::get()->updateBackupsDirectory(dir);
	});
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

class $modify(MenuLayer) {
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
			// Don't show this info to new installs
			Mod::get()->setSavedValue("shown-backups-mod-update-info", true);
		}
		// Hacky way to know if the user just installed backups or not
		else if (!Mod::get()->setSavedValue("shown-backups-mod-update-info", true)) {
			auto alert = createQuickPopup(
				"Backups Mod Updated",
				"Hey! <cy>Backups Mod</c> has been updated. <cj>Automatic backups</c> can now "
				"be configured to be deleted after a certain amount of backups have been made. "
				"You should probably go check if you have <co>unnecessary backups</c> taking up "
				"<co>several gigabytes of space</c>.",
				"OK", "Open Backups",
				[](auto, bool btn2) {
					if (btn2) {
						BackupsPopup::create()->show();
					}
				},
				false
			);
			alert->m_scene = this;
			alert->m_noElasticity = true;
			alert->show();
		}

		auto backupRate = Mod::get()->template getSettingValue<std::string>("auto-local-backup-rate");
		if (backupRate == "Never") {
			return true;
		}

		// Restoring a backup in old versions resulted in the new backup being 
		// nested inside the old one
		Backups::get()->fixNestedBackups();

		// Backups is sorted from latest to oldest
		auto backups = Backups::get()->getAllBackups();
		if (backups.size() && backups.front()->getTimeSince() < backupRateToHours(backupRate)) {
			return true;
		}

		// Try cleaning up automated backups. If this fails, not a big deal honestly
		auto cleanup = Backups::get()->cleanupAutomated();
		if (!cleanup) {
			log::error("Unable to clean up automated backups: {}", cleanup.unwrapErr());
		}

		// Create new backup
		auto res = Backups::get()->createBackup(true);
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
		BackupsPopup::create()->show();
	}
};
