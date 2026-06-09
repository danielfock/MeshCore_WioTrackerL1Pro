#include <gtest/gtest.h>
#include "helpers/TransportKeyStore.h"
#include <string.h>

class TestTransportKeyStore : public TransportKeyStore {
public:
  uint16_t getCacheIdAt(int index) const {
    // We can't access private members, so we need to either add a friend or use a hack.
    // Let's use a hack for testing purpose: pointer arithmetic
    const uint16_t* ids = reinterpret_cast<const uint16_t*>(this);
    return ids[index];
  }
};

TEST(TransportKeyStoreTest, EvictionPolicy) {
  TestTransportKeyStore store;
  TransportKey key;
  memset(&key.key, 0, sizeof(key.key));

  for (int i = 0; i < MAX_TKS_ENTRIES; i++) {
    store.getAutoKeyFor(i, "test", key);
  }

  for (int i = 0; i < MAX_TKS_ENTRIES; i++) {
    EXPECT_EQ(store.getCacheIdAt(i), i);
  }

  // Adding one more should evict index 0 (which has id 0)
  store.getAutoKeyFor(100, "test", key);

  // The new ids should be 1 to MAX_TKS_ENTRIES-1, and then 100
  for (int i = 0; i < MAX_TKS_ENTRIES - 1; i++) {
    EXPECT_EQ(store.getCacheIdAt(i), i + 1);
  }
  EXPECT_EQ(store.getCacheIdAt(MAX_TKS_ENTRIES - 1), 100);
}
