#pragma once

#include "maicord/core/base.hpp"

// The freedesktop Secret Service (org.freedesktop.secrets: GNOME Keyring, KWallet's Secret Service, KeePassXC): look up,
// store and erase one secret by its attributes. Used by the platform crypto to keep the at-rest master key out of the
// data directory.
//
// Calls serialize access to a persistent private bus connection, separate from the UI loop. Each operation opens and
// closes its own Secret Service session, preserving bus caller approval between operations and reopening sessions after
// provider restarts. Any thread may call these functions. The session is the "plain" algorithm (secrets cross the bus
// unencrypted, which the bus's peer-uid check protects); a keyring that is locked and would need a prompt is reported as
// `locked` (no unlock prompt is invoked). A provider may request application access approval while reading/storing an
// unlocked wallet; those calls allow its native dialog time to complete. Secret buffers are wiped before being freed.

namespace maicord::dbus {

enum class SecretStatus : u8 {
    ok,
    not_found,    // no item has these attributes
    unavailable,  // no session bus, or nobody owns org.freedesktop.secrets
    locked,       // the keyring is locked: unlocking needs a prompt
    failed,       // the service refused or answered something unexpected
};

struct SecretAttribute {
    const char* name;
    const char* value;
};

const char* secret_status_name(SecretStatus status) noexcept;

// The secret of the first item whose attributes include all of `attributes`: its length in *length (the bytes copied up to
// `capacity`; ok only when it all fit).
SecretStatus secret_lookup(const SecretAttribute* attributes, u32 count, u8* secret, u32 capacity, u32* length) noexcept;

// Stores a secret in the default collection with these attributes, replacing an item that has exactly them.
SecretStatus secret_store(const char* label, const SecretAttribute* attributes, u32 count, const u8* secret,
                          u32 length) noexcept;

// Deletes the items whose attributes include all of `attributes`. not_found when there were none.
SecretStatus secret_erase(const SecretAttribute* attributes, u32 count) noexcept;

// Releases the private connection (shutdown/tests). The next operation reconnects.
void secret_service_disconnect() noexcept;

} // namespace maicord::dbus
