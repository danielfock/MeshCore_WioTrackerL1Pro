Let's see if we should define a constant for the inactivity period.
In `examples/simple_room_server/MyMesh.h`, I can add `#define MAX_CLIENT_INACTIVE_SECS (12 * 60 * 60)` or maybe `(4 * 60 * 60)` = 4 hours.
Let's use `(12 * 60 * 60)` for 12 hours.

Wait, if I remove a client from `acl` while round-robin polling is happening in `MyMesh::loop()`?
`MyMesh::loop()` has:
```cpp
    next_client_idx = (next_client_idx + 1) % acl.getNumClients(); // round robin polling for each client
```
If we remove a client, `acl.getNumClients()` decreases. If we remove a client before `next_client_idx`, then `next_client_idx` will shift to a different client. If `next_client_idx` is exactly `acl.getNumClients()`, then the `% acl.getNumClients()` will just wrap it to 0. But if `acl.getNumClients()` drops to 0, `% 0` will crash!
Wait, `next_client_idx` is only used if `acl.getNumClients() > 0`:
```cpp
  if (millisHasNowPassed(next_push) && acl.getNumClients() > 0) {
    ...
    auto client = acl.getClientByIdx(next_client_idx);
    ...
    next_client_idx = (next_client_idx + 1) % acl.getNumClients();
  }
```
If we evict clients later in `loop()`, and `acl.getNumClients()` drops to 0, it's fine because the next loop will skip the `> 0` check.
But what if `next_client_idx` is now `>= acl.getNumClients()` after eviction?
Then in the next loop, if `acl.getNumClients() > 0`, it will do `acl.getClientByIdx(next_client_idx)` which is out of bounds!
Ah! `getClientByIdx` does not do bounds checking:
```cpp
  ClientInfo* getClientByIdx(int idx) { return &clients[idx]; }
```
So we MUST fix `next_client_idx` if it goes out of bounds, or simply do `if (next_client_idx >= acl.getNumClients()) next_client_idx = 0;` after eviction!
Let's add `next_client_idx = 0;` or similar, or `if (next_client_idx >= acl.getNumClients()) next_client_idx = 0;` after checking for eviction!

Actually, a simpler way is to just do:
```cpp
  // TODO: periodically check for OLD/inactive entries in known_clients[], and evict
  static uint32_t next_evict_check = 0;
  if (millisHasNowPassed(next_evict_check)) {
    next_evict_check = futureMillis(60000);  // check every 1 minute
    uint32_t curr_time = getRTCClock()->getCurrentTime();
    for (int i = acl.getNumClients() - 1; i >= 0; i--) {
      auto c = acl.getClientByIdx(i);
      if (!c->isAdmin() && c->last_activity > 0 && (curr_time - c->last_activity > MAX_CLIENT_INACTIVE_SECS)) {
        MESH_DEBUG_PRINTLN("Evicting inactive client %02X", (uint32_t)c->id.pub_key[0]);
        acl.removeClient(i);
        if (next_client_idx >= acl.getNumClients()) {
           next_client_idx = 0;
        }
      }
    }
  }
```

Wait, `MAX_CLIENT_INACTIVE_SECS` needs to be defined. I'll define it in `MyMesh.h`:
```cpp
#ifndef MAX_CLIENT_INACTIVE_SECS
  #define MAX_CLIENT_INACTIVE_SECS  (12 * 60 * 60)
#endif
```

And in `src/helpers/ClientACL.cpp`, I will add `bool ClientACL::removeClient(int idx)`:
```cpp
bool ClientACL::removeClient(int idx) {
  if (idx < 0 || idx >= num_clients) return false;
  num_clients--;
  for (int i = idx; i < num_clients; i++) {
    clients[i] = clients[i + 1];
  }
  return true;
}
```
And add its declaration in `src/helpers/ClientACL.h`:
```cpp
  bool removeClient(int idx);
```

Wait, does `applyPermissions` in `ClientACL.cpp` use `num_clients--` and shifting?
Let's modify `applyPermissions` to use `removeClient`:
```cpp
    c = getClient(pubkey, key_len);
    if (c == NULL) return false;   // partial pubkey not found

    removeClient(c - clients);
```
Yes, this would deduplicate code!
Let's see:
```cpp
<<<<<<< SEARCH
  if ((perms & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {  // guest role is not persisted in contacts
    c = getClient(pubkey, key_len);
    if (c == NULL) return false;   // partial pubkey not found

    num_clients--;   // delete from contacts[]
    int i = c - clients;
    while (i < num_clients) {
      clients[i] = clients[i + 1];
      i++;
    }
  } else {
=======
  if ((perms & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {  // guest role is not persisted in contacts
    c = getClient(pubkey, key_len);
    if (c == NULL) return false;   // partial pubkey not found

    removeClient(c - clients);
  } else {
>>>>>>> REPLACE
```
This is a nice small improvement.
