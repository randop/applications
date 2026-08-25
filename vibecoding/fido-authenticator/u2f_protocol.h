#pragma once
#include <cstdint>
#include <vector>

// Handles U2F (CTAP1) raw APDU messages carried inside a CTAPHID_MSG frame.
class U2fProtocol {
public:
    // presencePin: sysfs GPIO number for the user-presence button (pulled to
    // GND on press). Pass -1 to disable the presence check entirely (NOT
    // recommended — means anything on the USB bus can silently mint/use
    // credentials with no physical confirmation).
    static void begin(int presenceGpio);

    static bool handleApdu(const std::vector<uint8_t>& apdu,
                            std::vector<uint8_t>& response);

private:
    static bool handleRegister(const std::vector<uint8_t>& body,
                                std::vector<uint8_t>& response);
    static bool handleAuthenticate(uint8_t p1, const std::vector<uint8_t>& body,
                                    std::vector<uint8_t>& response);
    static bool waitForUserPresence(uint32_t timeoutMs);
    static int s_presenceGpio;
};
