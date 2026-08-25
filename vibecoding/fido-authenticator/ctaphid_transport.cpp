#include "ctaphid_transport.h"
#include "u2f_protocol.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/rand.h>

namespace {

constexpr size_t PACKET_LEN = 64;
constexpr uint32_t CID_BROADCAST = 0xFFFFFFFF;

enum CtapHidCmd : uint8_t {
    CTAPHID_PING      = 0x01,
    CTAPHID_MSG       = 0x03,
    CTAPHID_INIT      = 0x06,
    CTAPHID_WINK      = 0x08,
    CTAPHID_CBOR      = 0x10,
    CTAPHID_CANCEL    = 0x11,
    CTAPHID_ERROR     = 0x3F,
    CTAPHID_KEEPALIVE = 0x3B,
};

enum CtapHidError : uint8_t {
    ERR_INVALID_CMD     = 0x01,
    ERR_INVALID_PAR     = 0x02,
    ERR_INVALID_LEN     = 0x03,
    ERR_INVALID_SEQ     = 0x04,
    ERR_MSG_TIMEOUT     = 0x05,
    ERR_CHANNEL_BUSY    = 0x06,
    ERR_OTHER           = 0x7F,
};

int g_fd = -1;

bool writePacket(const uint8_t buf[PACKET_LEN]) {
    ssize_t n = write(g_fd, buf, PACKET_LEN);
    return n == (ssize_t)PACKET_LEN;
}

void sendError(uint32_t cid, uint8_t code) {
    uint8_t pkt[PACKET_LEN] = {0};
    pkt[0] = (cid >> 24) & 0xFF; pkt[1] = (cid >> 16) & 0xFF;
    pkt[2] = (cid >> 8) & 0xFF;  pkt[3] = cid & 0xFF;
    pkt[4] = 0x80 | CTAPHID_ERROR;
    pkt[5] = 0; pkt[6] = 1;
    pkt[7] = code;
    writePacket(pkt);
}

void sendResponse(uint32_t cid, uint8_t cmd, const std::vector<uint8_t>& data) {
    size_t offset = 0;
    uint8_t pkt[PACKET_LEN];

    memset(pkt, 0, PACKET_LEN);
    pkt[0] = (cid >> 24) & 0xFF; pkt[1] = (cid >> 16) & 0xFF;
    pkt[2] = (cid >> 8) & 0xFF;  pkt[3] = cid & 0xFF;
    pkt[4] = 0x80 | cmd;
    pkt[5] = (data.size() >> 8) & 0xFF;
    pkt[6] = data.size() & 0xFF;
    size_t firstChunk = std::min(data.size(), (size_t)(PACKET_LEN - 7));
    memcpy(pkt + 7, data.data(), firstChunk);
    offset += firstChunk;
    writePacket(pkt);

    uint8_t seq = 0;
    while (offset < data.size()) {
        memset(pkt, 0, PACKET_LEN);
        pkt[0] = (cid >> 24) & 0xFF; pkt[1] = (cid >> 16) & 0xFF;
        pkt[2] = (cid >> 8) & 0xFF;  pkt[3] = cid & 0xFF;
        pkt[4] = seq++ & 0x7F;
        size_t chunk = std::min(data.size() - offset, (size_t)(PACKET_LEN - 5));
        memcpy(pkt + 5, data.data() + offset, chunk);
        offset += chunk;
        writePacket(pkt);
    }
}

void handleInit(uint32_t cid, const uint8_t* nonce8) {
    uint32_t newCid;
    if (cid == CID_BROADCAST) {
        RAND_bytes(reinterpret_cast<uint8_t*>(&newCid), sizeof(newCid));
        if (newCid == CID_BROADCAST || newCid == 0) newCid = 0x00000001;
    } else {
        newCid = cid; // re-init on an existing channel: keep same CID per spec
    }

    std::vector<uint8_t> resp;
    resp.insert(resp.end(), nonce8, nonce8 + 8);
    resp.push_back((newCid >> 24) & 0xFF);
    resp.push_back((newCid >> 16) & 0xFF);
    resp.push_back((newCid >> 8) & 0xFF);
    resp.push_back(newCid & 0xFF);
    resp.push_back(2);    // CTAPHID protocol version
    resp.push_back(1);    // device major version
    resp.push_back(0);    // device minor version
    resp.push_back(0);    // build version
    resp.push_back(0x00); // capabilities: no WINK, no CBOR (U2F/MSG only) —
                           // set bit 0x01 if you wire up a WINK LED blink,
                           // bit 0x04 once CTAP2/CBOR is implemented.

    sendResponse(cid == CID_BROADCAST ? newCid : cid, CTAPHID_INIT, resp);
}

} // namespace

bool CtapHidTransport::run(const char* hidgPath) {
    g_fd = open(hidgPath, O_RDWR);
    if (g_fd < 0) {
        perror("open hidg device");
        return false;
    }

    uint32_t curCid = 0;
    uint8_t curCmd = 0;
    uint16_t expectedLen = 0;
    std::vector<uint8_t> reassembly;
    bool inProgress = false;
    uint8_t nextSeq = 0;

    uint8_t pkt[PACKET_LEN];
    for (;;) {
        ssize_t n = read(g_fd, pkt, PACKET_LEN);
        if (n != (ssize_t)PACKET_LEN) {
            if (n < 0) { perror("read hidg"); continue; }
            continue;
        }

        uint32_t cid = (pkt[0] << 24) | (pkt[1] << 16) | (pkt[2] << 8) | pkt[3];
        uint8_t byte4 = pkt[4];

        bool isInit = (byte4 & 0x80) != 0;
        if (isInit) {
            uint8_t cmd = byte4 & 0x7F;
            uint16_t len = (pkt[5] << 8) | pkt[6];

            if (cmd == CTAPHID_INIT) {
                if (len != 8) { sendError(cid, ERR_INVALID_LEN); continue; }
                handleInit(cid, pkt + 7);
                inProgress = false; // any pending reassembly on other channels is abandoned
                continue;
            }
            if (cmd == CTAPHID_CANCEL) {
                inProgress = false;
                continue;
            }

            curCid = cid;
            curCmd = cmd;
            expectedLen = len;
            reassembly.assign(pkt + 7, pkt + 7 + std::min((size_t)len, (size_t)(PACKET_LEN - 7)));
            nextSeq = 0;
            inProgress = reassembly.size() < expectedLen;

            if (!inProgress) {
                goto dispatch;
            }
            continue;
        }

        // Continuation packet
        if (!inProgress || cid != curCid) {
            sendError(cid, ERR_INVALID_SEQ);
            continue;
        }
        uint8_t seq = byte4 & 0x7F;
        if (seq != nextSeq) {
            sendError(cid, ERR_INVALID_SEQ);
            inProgress = false;
            continue;
        }
        nextSeq++;
        size_t remaining = expectedLen - reassembly.size();
        size_t chunk = std::min(remaining, (size_t)(PACKET_LEN - 5));
        reassembly.insert(reassembly.end(), pkt + 5, pkt + 5 + chunk);

        if (reassembly.size() < expectedLen) continue;

        inProgress = false;

    dispatch:
        switch (curCmd) {
            case CTAPHID_PING:
                sendResponse(curCid, CTAPHID_PING, reassembly);
                break;
            case CTAPHID_MSG: {
                std::vector<uint8_t> response;
                U2fProtocol::handleApdu(reassembly, response);
                sendResponse(curCid, CTAPHID_MSG, response);
                break;
            }
            case CTAPHID_WINK:
                // TODO: blink a status LED via sysfs GPIO here.
                sendResponse(curCid, CTAPHID_WINK, {});
                break;
            case CTAPHID_CBOR:
                // CTAP2 not implemented in this build — see README. Reply
                // with a CTAP1_ERR-style error so clients that try CBOR
                // first still fail gracefully instead of hanging.
                sendError(curCid, ERR_INVALID_CMD);
                break;
            default:
                sendError(curCid, ERR_INVALID_CMD);
        }
    }

    close(g_fd);
    return true;
}
