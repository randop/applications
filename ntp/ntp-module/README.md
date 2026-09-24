# NTP

## The NTP Request Buffer Structure
An NTP packet is exactly 48 bytes. For a client request:

1. Byte 0 (LI, VN, Mode): Set to `0x1B` (binary `00011011`).
  * LI (Bits 0-1): 00 (No warning).
  * VN (Bits 2-4): 011 (Version 3).
  * Mode (Bits 5-7): 011 (Client Mode). 
2. Bytes 1–47: Set to `0`.

---

## References
* [https://www.meinbergglobal.com/english/info/ntp-packet.htm](https://www.meinbergglobal.com/english/info/ntp-packet.htm)
