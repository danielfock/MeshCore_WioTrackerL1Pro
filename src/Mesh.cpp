bool MyMesh::onPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret,
                            uint8_t *path, uint8_t path_len, uint8_t extra_type,
                            uint8_t *extra, uint8_t extra_len, uint32_t path_timestamp) {
  (void)packet;
  (void)secret;
  (void)extra_type;
  (void)extra;
  (void)extra_len;

  int i = matching_peer_indexes[sender_idx];

  if (i >= 0 && i < acl.getNumClients()) {
    auto client = acl.getClientByIdx(i);

    if (path_timestamp > 0 && path_timestamp <= client->last_timestamp) {
      MESH_DEBUG_PRINTLN("onPeerPathRecv: possible replay attack detected");
      return false;
    }

    if (path_timestamp > 0) {
      client->last_timestamp = path_timestamp;
    }

    MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t)path_len);

    // store a copy of path, for sendDirect()
    client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
    client->last_activity = getRTCClock()->getCurrentTime();
  } else {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
  }

  // NOTE: no reciprocal path send!!
  return false;
}