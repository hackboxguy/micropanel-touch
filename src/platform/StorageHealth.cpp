#include "platform/StorageHealth.h"

#include <cerrno>
#include <cstring>
#include <sys/statfs.h>

namespace micropanel_touch::platform {
namespace {

constexpr long kOverlayfsSuperMagic = 0x794c7630L;
constexpr long kTmpfsMagic = 0x01021994L;
constexpr long kRamfsMagic = 0x858458f6L;

}  // namespace

StorageHealth classify_storage_filesystem(long filesystem_magic) {
    if (filesystem_magic == kOverlayfsSuperMagic) {
        return {StoragePersistence::volatile_storage,
                "data directory is backed by overlayfs rather than persistent storage"};
    }
    if (filesystem_magic == kTmpfsMagic || filesystem_magic == kRamfsMagic) {
        return {StoragePersistence::volatile_storage,
                "data directory is backed by tmpfs rather than persistent storage"};
    }
    return {StoragePersistence::persistent, {}};
}

StorageHealth inspect_storage(const std::filesystem::path& path) {
    struct statfs status {};
    if (::statfs(path.c_str(), &status) != 0) {
        return {StoragePersistence::unavailable,
                "unable to inspect data filesystem: " + std::string(std::strerror(errno))};
    }
    return classify_storage_filesystem(static_cast<long>(status.f_type));
}

}  // namespace micropanel_touch::platform
