#include "transport.h"

bool IsoTpAssembler::feed(quint32 ecu, const QByteArray &data, QByteArray &out, bool *first)
{
    if (first)
        *first = false;
    if (data.isEmpty())
        return false;
    const quint8 pci = quint8(data[0]);
    switch (pci >> 4) {
        case 0x0: {  // single frame
            const int len = pci & 0x0F;
            if (len == 0 || len > data.size() - 1)
                return false;
            partial.remove(ecu);
            out = data.mid(1, len);
            return true;
        }
        case 0x1: {  // first frame: 12-bit length, then data
            if (data.size() < 3)
                return false;
            Partial p;
            p.expected = ((pci & 0x0F) << 8) | quint8(data[1]);
            p.data = data.mid(2);
            if (p.expected <= p.data.size()) {  // shouldn't happen, but be kind
                out = p.data.left(p.expected);
                partial.remove(ecu);
                return true;
            }
            partial[ecu] = p;
            if (first)
                *first = true;
            return false;
        }
        case 0x2: {  // consecutive frame
            auto it = partial.find(ecu);
            if (it == partial.end())
                return false;
            if ((pci & 0x0F) != (it->next_sn & 0x0F)) {  // lost a frame: give up on this one
                partial.erase(it);
                return false;
            }
            it->next_sn++;
            it->data.append(data.mid(1));
            if (it->data.size() >= it->expected) {
                out = it->data.left(it->expected);
                partial.erase(it);
                return true;
            }
            return false;
        }
        default:  // flow control from the other side, or junk
            return false;
    }
}
