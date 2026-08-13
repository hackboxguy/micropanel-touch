#pragma once

#include <filesystem>
#include <string>

namespace micropanel_touch::platform {

enum class StoragePersistence {
    persistent,
    volatile_storage,
    unavailable,
};

struct StorageHealth {
    StoragePersistence persistence{StoragePersistence::unavailable};
    std::string diagnostic;
};

/**
 * Classifies filesystem types that are known to lose writes on reboot.  Other
 * filesystem types remain eligible as persistent storage; policy about their
 * durability belongs to the image, not to this portable application layer.
 */
StorageHealth classify_storage_filesystem(long filesystem_magic);

/** Inspect the mounted filesystem containing path. */
StorageHealth inspect_storage(const std::filesystem::path& path);

}  // namespace micropanel_touch::platform
