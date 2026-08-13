#include "platform/StorageHealth.h"

#include <cassert>

int main() {
    using micropanel_touch::platform::StoragePersistence;
    using micropanel_touch::platform::classify_storage_filesystem;

    const auto overlay = classify_storage_filesystem(0x794c7630L);
    assert(overlay.persistence == StoragePersistence::volatile_storage);
    assert(overlay.diagnostic.find("overlayfs") != std::string::npos);

    const auto tmpfs = classify_storage_filesystem(0x01021994L);
    assert(tmpfs.persistence == StoragePersistence::volatile_storage);
    assert(tmpfs.diagnostic.find("tmpfs") != std::string::npos);

    const auto ext4 = classify_storage_filesystem(0xef53L);
    assert(ext4.persistence == StoragePersistence::persistent);
    assert(ext4.diagnostic.empty());

    const auto missing = micropanel_touch::platform::inspect_storage("/no/such/micropanel-touch-data");
    assert(missing.persistence == StoragePersistence::unavailable);
    assert(!missing.diagnostic.empty());
}
