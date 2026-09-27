// Uds — what the tool's UDS sequences share (CommissioningSequencer, FirmwareUpdater).
#pragma once

#include <QByteArray>

namespace Uds {
// The bench SecurityAccess key (tool/PROTOCOL.md A.4.1 `provision` sa_key, a product build's TI_UDS_KEY_FN):
// key[i] = seed[(i + 1) mod 4] ^ (0xA5 + i); empty for a seed that is not 4 bytes.
inline QByteArray benchKey(const QByteArray &seed)
{
    QByteArray k(4, '\0');
    for (int i = 0; i < 4 && seed.size() == 4; i++) {
        k[i] = static_cast<char>(static_cast<quint8>(seed[(i + 1) % 4]) ^ (0xA5 + i));
    }
    return k;
}
} // namespace Uds
