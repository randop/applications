# Luckfox Pico FIDO/U2F Security Key — USB HID (CTAPHID) Transport

## Why this is a better fit than the ESP32/BLE build

The Luckfox Pico's SoC has a real USB OTG controller that supports **device
mode**, and it runs Linux — so this uses the Linux **USB gadget** framework
to present a genuine USB HID interface speaking CTAPHID, the exact transport
real USB security keys (YubiKey, SoloKeys, etc.) use. No BLE pairing dance,
no host/device role mismatch, no extra chips: plug the OTG port into a PC
and it enumerates immediately as a security key.

## Architecture

```
PC (Chrome) <--USB HID (CTAPHID)--> /dev/hidg0 <--> fido-authenticator daemon
                                                        |-- ctaphid_transport: packet framing
                                                        |-- u2f_protocol:     REGISTER/AUTHENTICATE
                                                        |-- crypto:           OpenSSL P-256 + HMAC
```

Same storage-free credential design as before: one master secret on disk
(`/etc/fido/master.key`, mode 600), every site's keypair re-derived on
demand via `HMAC(masterSecret, rpIdHash || nonce)`. The "key handle" Chrome
stores and hands back later is just `nonce || MAC` — nothing per-site is
persisted.

## One-time device setup

1. Confirm your kernel has USB gadget/HID-function support:
   ```
   zcat /proc/config.gz 2>/dev/null | grep -E "CONFIGFS|USB_GADGET"
   ```
   If `CONFIG_USB_CONFIGFS_F_HID` isn't set, enable it in the Luckfox SDK's
   kernel config and rebuild the kernel image.
2. Confirm the OTG port is wired for device mode in the device tree
   (`dr_mode = "otg"` or `"peripheral"` on the relevant USB node) — this is
   usually already the case on Pico boards using the OTG port specifically
   (not a USB-host-only port some carrier boards break out separately).
3. Make sure OpenSSL is available for the target (see the Makefile header
   comment) — many minimal Buildroot images don't ship it by default.

## Build

```bash
export PATH=$PATH:/path/to/luckfox-pico/tools/linux/toolchain/<your-toolchain>/bin
make CROSS=<your-cross-prefix>-
```

## Deploy

```bash
scp fido-authenticator root@<pico-ip>:/usr/local/bin/
scp scripts/setup_usb_gadget.sh root@<pico-ip>:/usr/local/bin/
scp scripts/S99fidokey root@<pico-ip>:/etc/init.d/
ssh root@<pico-ip> chmod +x /usr/local/bin/setup_usb_gadget.sh /etc/init.d/S99fidokey
```

Test it live first before relying on the init script:
```bash
ssh root@<pico-ip>
/usr/local/bin/setup_usb_gadget.sh
/usr/local/bin/fido-authenticator
```
Watch `dmesg` on the connected PC — you should see a new HID device appear
the moment `UDC` gets bound in the gadget script.

## Wiring the presence button

Wire a momentary button between your chosen GPIO and GND. Update
`USER_PRESENCE_GPIO` in `src/main.cpp` to match — the number is the *sysfs*
GPIO number, not the silkscreen pin label; check the Luckfox pinout doc or
`cat /sys/kernel/debug/gpio` to map physical pin → sysfs number for your
specific board revision. **Don't skip this** — without it the daemon fails
closed (refuses all REGISTER/AUTHENTICATE) rather than silently allowing
unattended credential use, which is the safer default but also useless
until you wire the button.

## Testing against Chrome

1. Plug the Pico's OTG port into your PC via USB.
2. Confirm enumeration: `lsusb` should show idVendor `1209`, and
   `dmesg | tail` should show a new HID input device.
3. Go to `chrome://settings/securityKeys` or `webauthn.io` and register a
   security key — Chrome should prompt "touch your security key" almost
   immediately (USB HID keys are auto-discovered, unlike BLE).
4. Press the physical button when prompted.
5. `chrome://device-log` if anything looks off — it'll show the raw
   CTAPHID exchange.

## What's still stubbed (same gaps as before, now easier to close)

1. **CTAP2/CBOR still isn't implemented** — `CTAPHID_CBOR` currently returns
   an error. This build speaks U2F/CTAP1 over real USB HID, which covers
   classic WebAuthn `navigator.credentials` flows but not passkey-only
   prompts. Since you're on full Linux now, adding CBOR is much easier than
   it would've been on the ESP32 — `libcbor` or `tinycbor` are normal
   Buildroot packages (`BR2_PACKAGE_LIBCBOR`), no vendoring needed.
2. **Attestation cert is a placeholder** — see `attestation_cert.h`. Because
   you have OpenSSL and a filesystem, the cleanest move now is to generate
   `attest_key.pem` / `attest_cert.der` once at provisioning time and load
   them from `/etc/fido/` at runtime instead of compiling bytes in.
3. **Master secret isn't hardware-protected.** It's a root-owned 600 file,
   which stops a normal user on the box but not someone pulling the eMMC/SD
   card. If your Luckfox variant has any secure storage / OTP fuse region,
   that's the right place for it; otherwise, full-disk encryption on the
   data partition closes most of this gap.
4. **No FIDO Alliance certification** — this follows the specs but hasn't
   been through conformance testing or MDS listing.

## Where your NFC module fits in later

Once this USB path is solid, the PN532-based reader (if that's what you
confirm you have) can add NFC as a *second* transport for tap-based use with
phones/NFC-capable readers — same `crypto.cpp`/`u2f_protocol.cpp` reused
underneath, just a third transport module (`nfc_transport.cpp`) alongside
`ctaphid_transport.cpp`, using the PN532's card-emulation mode instead of
USB HID framing. Worth doing as a follow-up once USB registration/auth is
confirmed working end-to-end against Chrome — easier to debug one transport
at a time.
