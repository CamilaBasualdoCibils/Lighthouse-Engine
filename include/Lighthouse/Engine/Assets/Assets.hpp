#pragma once

#include <memory>
#include <string>

namespace Lighthouse::Assets {

template <typename AssetType>
std::shared_ptr<AssetType> Load(const std::string& path);

} // namespace Lighthouse::Assets
