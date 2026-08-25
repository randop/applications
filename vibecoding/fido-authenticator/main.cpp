#include <cstdio>
#include "crypto.h"
#include "u2f_protocol.h"
#include "ctaphid_transport.h"

// Adjust to whichever GPIO your presence button is wired to (sysfs numbering,
// i.e. the number you'd `echo N > /sys/class/gpio/export`). Find the right
// number for a given physical pin via `cat /sys/kernel/debug/gpio` or the
// Luckfox pinout doc for your specific board revision.
static const int USER_PRESENCE_GPIO = 54;

int main() {
    if (!FidoCrypto::begin()) {
        fprintf(stderr, "FATAL: crypto init failed (check /etc/fido is writable)\n");
        return 1;
    }

    U2fProtocol::begin(USER_PRESENCE_GPIO);

    fprintf(stderr, "FIDO USB authenticator ready on /dev/hidg0.\n");
    fprintf(stderr, "Plug the Pico's OTG port into your PC — it should show up\n");
    fprintf(stderr, "as a USB HID security key immediately (no pairing step,\n");
    fprintf(stderr, "unlike the earlier BLE design).\n");

    if (!CtapHidTransport::run("/dev/hidg0")) {
        fprintf(stderr, "FATAL: could not open /dev/hidg0 — did setup_usb_gadget.sh run?\n");
        return 1;
    }
    return 0;
}
