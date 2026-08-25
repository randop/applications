#pragma once

// Reads/writes /dev/hidgN (the USB HID gadget device node), implements
// CTAPHID channel init + packet fragmentation/reassembly per the CTAP spec,
// and dispatches complete messages to U2fProtocol. Runs forever; call from
// main() after FidoCrypto::begin() and U2fProtocol::begin().
class CtapHidTransport {
public:
    // Returns false only if the HID device node couldn't be opened at all.
    static bool run(const char* hidgPath = "/dev/hidg0");
};
