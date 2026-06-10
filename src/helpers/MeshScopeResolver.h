#pragma once

#include <Arduino.h>
#include <Packet.h>
#include "ContactInfo.h"
#include "TransportKeyStore.h"

struct MeshScopeResolver {
  virtual ~MeshScopeResolver() = default;

  // Used when MyMesh needs to encrypt/send data and requests the appropriate scope key
  virtual bool resolveScope(const ContactInfo& contact, TransportKey& out_key) = 0;

  virtual bool resolvePacketToScope(mesh::Packet* packet, TransportKey& out_key) = 0;

  // Triggered when an incoming packet contains raw region codes.
  // The application layer uses the packet to convert these codes and tracks the sender mapping.
  virtual void recordContactRegion(const ContactInfo& contact, const TransportKey& scope) = 0;
};
