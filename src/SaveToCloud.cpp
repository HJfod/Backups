#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GJAccountBackupDelegate.hpp>
#include <Geode/ui/Popup.hpp>

using namespace geode::prelude;

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

// class $modify(MenuLayer) {
	// struct Fields {
	// 	bool doQuitGame = false;
	// };

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
// };
