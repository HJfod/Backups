#pragma once

#include <string>
#include <filesystem>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

namespace cc {
    Result<std::string> parseCompressedCCFile(std::filesystem::path const& path, std::function<bool()> cancelled);
}
