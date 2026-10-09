#pragma once
#include <stdint.h>

// Callbacks run on the main thread; strings are valid only during the call.
enum GcStoreEvent {
    GC_STORE_DISABLED = 0,
    GC_STORE_CHECKING = 1,
    GC_STORE_PRODUCT = 2,
    GC_STORE_PURCHASING = 3,
    GC_STORE_CANCELLED = 4,
    GC_STORE_PENDING = 5,
    GC_STORE_ERROR = 6,
    GC_STORE_ENTITLEMENT = 7,
    GC_STORE_RESTORING = 8,
    GC_STORE_COMPLETE = 9
};
enum GcStoreFlags {
    GC_STORE_VERIFIED = 1,
    GC_STORE_REVOKED = 2,
    GC_STORE_GRACE_PERIOD = 4
};
typedef void (*GcStoreCallback)(int32_t event, const char* product_id,
    const char* message, int64_t valid_until_unix, uint32_t flags, void* context);

#ifdef __cplusplus
extern "C" {
#endif
// The callback and context must stay valid until stop.
void gc_store_initialize(const char* product_id, GcStoreCallback callback, void* context);
void gc_store_refresh(void);
void gc_store_purchase(void);
void gc_store_restore(void);
void gc_store_stop(void);
#ifdef __cplusplus
}
#endif
