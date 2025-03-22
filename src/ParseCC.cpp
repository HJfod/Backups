#include "ParseCC.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <cppcodec/base64_url.hpp>

using namespace geode::prelude;

Result<std::string> cc::parseCompressedCCFile(std::filesystem::path const& path) {
    auto readRes = file::readBinary(path);
    if (!readRes) {
        return Err("Unable to read file: {}", readRes.unwrapErr());
    }
    auto data = *readRes;

    auto decompressed = ZipUtils::decompressString2(data.data(), true, data.size(), 11);
    log::info("decompressed: {}", decompressed.substr(0, 100));

    return Ok(decompressed);

    // auto readRes = file::readBinary(path);
    // if (!readRes) {
    //     return Err("Unable to read file: {}", readRes.unwrapErr());
    // }
    // auto data = *readRes;

    // // XOR with key 11
    // for (auto& c : data) {
    //     c ^= 11;
    // }
    // // URL-safe base64 decode
    // data = cppcodec::base64_url::decode(reinterpret_cast<char*>(data.data()), data.size());
    // // ZipUtils::ccDeflateMemory();
}
