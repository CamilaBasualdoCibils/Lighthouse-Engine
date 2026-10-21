#pragma once

#include <memory>
#include <string>

namespace Engine::Assets {

template <typename AssetType>
std::shared_ptr<AssetType> Load(const std::string& path);

} // namespace Engine::Assets
