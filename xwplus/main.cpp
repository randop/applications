/*** === X11 window plus === ***/

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/Xrandr.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */

struct AppConfig {
    bool verbose = true;
    int  targetW = 1920;
    int  targetH = 1080;
    double targetHz = 60.0;
};

/* ------------------------------------------------------------------ */
/*  Data structures                                                    */
/* ------------------------------------------------------------------ */

struct MonitorInfo {
    std::string outputName;
    std::string manufacturer;
    std::string modelName;
    std::string serialNumber;
    uint16_t    productCode = 0;
    uint32_t    serialRaw = 0;
    int         year = 0;
    int         week = 0;
};

struct ModeEntry {
    RRMode      id;
    std::string name;
    int         width;
    int         height;
    double      refresh;
    bool        isCurrent;
    bool        isPreferred;
};

/* ------------------------------------------------------------------ */
/*  EDID helpers                                                       */
/* ------------------------------------------------------------------ */

static std::string decodeManufacturer(uint16_t id)
{
    char c1 = static_cast<char>('A' + ((id >> 10) & 0x1F) - 1);
    char c2 = static_cast<char>('A' + ((id >>  5) & 0x1F) - 1);
    char c3 = static_cast<char>('A' + ((id >>  0) & 0x1F) - 1);
    auto valid = [](char c){ return (c >= 'A' && c <= 'Z') ? c : '?'; };
    return std::string{ valid(c1), valid(c2), valid(c3) };
}

static std::string extractDescriptorString(const uint8_t* desc)
{
    char buf[14];
    std::memcpy(buf, desc + 5, 13);
    buf[13] = '\0';
    for (int i = 0; i < 13; ++i) {
        if (buf[i] == 0x0A || buf[i] == '\0') {
            buf[i] = '\0';
            break;
        }
    }
    int len = static_cast<int>(std::strlen(buf));
    while (len > 0 && buf[len - 1] == ' ') buf[--len] = '\0';
    return std::string(buf);
}

static MonitorInfo parseEdid(const std::vector<uint8_t>& edid,
                             const std::string& outputName,
                             const AppConfig& cfg)
{
    MonitorInfo info;
    info.outputName = outputName;

    if (edid.size() < 128) {
        if (cfg.verbose)
            std::cerr << "  Warning: EDID too short (" << edid.size() << " bytes)" << std::endl;
        return info;
    }

    const uint8_t* d = edid.data();
    if (d[0] != 0x00 || d[1] != 0xFF || d[2] != 0xFF || d[3] != 0xFF ||
        d[4] != 0xFF || d[5] != 0xFF || d[6] != 0xFF || d[7] != 0x00) {
        if (cfg.verbose)
            std::cerr << "  Warning: Invalid EDID header" << std::endl;
    }

    uint16_t manuId = (static_cast<uint16_t>(d[8]) << 8) | d[9];
    info.manufacturer = decodeManufacturer(manuId);
    info.productCode  = static_cast<uint16_t>(d[10]) | (static_cast<uint16_t>(d[11]) << 8);
    info.serialRaw    = static_cast<uint32_t>(d[12]) |
                        (static_cast<uint32_t>(d[13]) << 8)  |
                        (static_cast<uint32_t>(d[14]) << 16) |
                        (static_cast<uint32_t>(d[15]) << 24);
    info.week = d[16];
    info.year = 1990 + d[17];

    for (int i = 0; i < 4; ++i) {
        int off = 54 + i * 18;
        const uint8_t* desc = d + off;
        if (desc[0] != 0x00 || desc[1] != 0x00) continue;
        if (desc[2] != 0x00 || desc[4] != 0x00) continue;

        uint8_t tag = desc[3];
        std::string text = extractDescriptorString(desc);
        if (text.empty()) continue;

        switch (tag) {
            case 0xFC: if (info.modelName.empty())     info.modelName     = text; break;
            case 0xFF: if (info.serialNumber.empty())  info.serialNumber  = text; break;
            case 0xFE: if (info.modelName.empty())     info.modelName     = text; break;
        }
    }

    if (info.serialNumber.empty() && info.serialRaw != 0)
        info.serialNumber = std::to_string(info.serialRaw);

    return info;
}

static std::vector<uint8_t> getOutputEdid(Display* display, RROutput output)
{
    std::vector<uint8_t> result;
    Atom edidAtom = XInternAtom(display, "EDID", False);
    if (edidAtom == None) return result;

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char* prop = nullptr;

    int status = XRRGetOutputProperty(display, output, edidAtom,
                                      0, 128, False, False,
                                      AnyPropertyType, &actualType,
                                      &actualFormat, &nitems, &bytesAfter, &prop);

    if (status == Success && prop && actualFormat == 8 && nitems > 0) {
        result.assign(prop, prop + nitems);
        XFree(prop);
    }
    return result;
}

/* ------------------------------------------------------------------ */
/*  RandR mode helpers                                                 */
/* ------------------------------------------------------------------ */

static const XRRModeInfo* findModeInfo(const XRRScreenResources* res, RRMode modeId)
{
    for (int i = 0; i < res->nmode; ++i) {
        if (res->modes[i].id == modeId)
            return &res->modes[i];
    }
    return nullptr;
}

static double calculateRefresh(const XRRModeInfo* mode)
{
    if (!mode || mode->hTotal == 0 || mode->vTotal == 0) return 0.0;
    return static_cast<double>(mode->dotClock) /
           (static_cast<double>(mode->hTotal) * static_cast<double>(mode->vTotal));
}

/* ------------------------------------------------------------------ */
/*  Monitor control API                                                */
/* ------------------------------------------------------------------ */

static bool isMonitorConnected(Display* display, XRRScreenResources* res, RROutput output)
{
    XRROutputInfo* info = XRRGetOutputInfo(display, res, output);
    if (!info) return false;
    bool connected = (info->connection == RR_Connected);
    XRRFreeOutputInfo(info);
    return connected;
}

static RRMode findTargetMode(Display* display, XRRScreenResources* res,
                             RROutput output, int w, int h, double targetHz,
                             double tolerance = 0.5)
{
    XRROutputInfo* outInfo = XRRGetOutputInfo(display, res, output);
    if (!outInfo) return None;

    RRMode best = None;
    double bestDiff = 1e9;

    for (int m = 0; m < outInfo->nmode; ++m) {
        RRMode modeId = outInfo->modes[m];
        const XRRModeInfo* modeInfo = findModeInfo(res, modeId);
        if (!modeInfo) continue;
        if (modeInfo->width == w && modeInfo->height == h) {
            double refresh = calculateRefresh(modeInfo);
            double diff = std::abs(refresh - targetHz);
            if (diff < bestDiff) {
                bestDiff = diff;
                best = modeId;
            }
        }
    }

    XRRFreeOutputInfo(outInfo);

    if (best != None && bestDiff <= tolerance)
        return best;
    return None;
}

static bool isCurrentMode(Display* display, XRRScreenResources* res,
                          RROutput output, RRMode targetMode)
{
    if (targetMode == None) return false;

    XRROutputInfo* outInfo = XRRGetOutputInfo(display, res, output);
    if (!outInfo) return false;

    bool match = false;
    if (outInfo->crtc != None) {
        XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(display, res, outInfo->crtc);
        if (crtcInfo) {
            match = (crtcInfo->mode == targetMode);
            XRRFreeCrtcInfo(crtcInfo);
        }
    }

    XRRFreeOutputInfo(outInfo);
    return match;
}

static bool switchToMode(Display* display, XRRScreenResources* res,
                         RROutput output, RRMode targetMode,
                         const AppConfig& cfg)
{
    if (targetMode == None) return false;

    XRROutputInfo* outInfo = XRRGetOutputInfo(display, res, output);
    if (!outInfo) return false;

    bool success = false;

    if (outInfo->crtc != None) {
        XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(display, res, outInfo->crtc);
        if (crtcInfo) {
            Status status = XRRSetCrtcConfig(display, res, outInfo->crtc,
                                               CurrentTime,
                                               crtcInfo->x, crtcInfo->y,
                                               targetMode,
                                               crtcInfo->rotation,
                                               crtcInfo->outputs,
                                               crtcInfo->noutput);
            if (status == RRSetConfigSuccess) {
                success = true;
            } else {
                const XRRModeInfo* mi = findModeInfo(res, targetMode);
                if (mi) {
                    int needW = crtcInfo->x + mi->width;
                    int needH = crtcInfo->y + mi->height;
                    int curW = DisplayWidth(display, DefaultScreen(display));
                    int curH = DisplayHeight(display, DefaultScreen(display));

                    if (needW > curW || needH > curH) {
                        XRRSetScreenSize(display, DefaultRootWindow(display),
                                         std::max(needW, curW),
                                         std::max(needH, curH),
                                         DisplayWidthMM(display, DefaultScreen(display)),
                                         DisplayHeightMM(display, DefaultScreen(display)));

                        status = XRRSetCrtcConfig(display, res, outInfo->crtc,
                                                    CurrentTime,
                                                    crtcInfo->x, crtcInfo->y,
                                                    targetMode,
                                                    crtcInfo->rotation,
                                                    crtcInfo->outputs,
                                                    crtcInfo->noutput);
                        success = (status == RRSetConfigSuccess);
                    }
                }
            }
            XRRFreeCrtcInfo(crtcInfo);
        }
    } else {
        RRCrtc freeCrtc = None;
        for (int i = 0; i < res->ncrtc; ++i) {
            RRCrtc crtc = res->crtcs[i];
            XRRCrtcInfo* ci = XRRGetCrtcInfo(display, res, crtc);
            if (!ci) continue;

            if (ci->mode == None) {
                for (int j = 0; j < outInfo->ncrtc; ++j) {
                    if (outInfo->crtcs[j] == crtc) {
                        freeCrtc = crtc;
                        break;
                    }
                }
            }
            XRRFreeCrtcInfo(ci);
            if (freeCrtc != None) break;
        }

        if (freeCrtc != None) {
            RROutput outputs[] = { output };
            Status status = XRRSetCrtcConfig(display, res, freeCrtc,
                                               CurrentTime,
                                               0, 0, targetMode,
                                               RR_Rotate_0,
                                               outputs, 1);
            success = (status == RRSetConfigSuccess);
        }
    }

    XRRFreeOutputInfo(outInfo);
    return success;
}

static void enableNumLock(Display* display)
{
    unsigned int num_lock_mask = Mod2Mask;
    XkbLockModifiers(display, XkbUseCoreKbd, num_lock_mask, num_lock_mask);
}

static int runMonitorSetup(Display* display, const AppConfig& cfg)
{
    int eventBase, errorBase;
    if (!XRRQueryExtension(display, &eventBase, &errorBase)) {
        if (cfg.verbose)
            std::cerr << "Error: XRandR extension not available" << std::endl;
        return EXIT_FAILURE;
    }

    Window root = DefaultRootWindow(display);
    XRRScreenResources* res = XRRGetScreenResources(display, root);
    if (!res) {
        if (cfg.verbose)
            std::cerr << "Error: Failed to get XRandR screen resources" << std::endl;
        return EXIT_FAILURE;
    }

    if (cfg.verbose) {
        std::cout << "Outputs found: " << res->noutput << std::endl;
        std::cout << "Target mode:   " << cfg.targetW << "x" << cfg.targetH
                  << " @" << cfg.targetHz << " Hz" << std::endl << std::endl;
    }

    for (int i = 0; i < res->noutput; ++i) {
        RROutput output = res->outputs[i];
        XRROutputInfo* outInfo = XRRGetOutputInfo(display, res, output);
        if (!outInfo) continue;

        std::string name(outInfo->name, outInfo->nameLen);
        bool connected = (outInfo->connection == RR_Connected);

        if (cfg.verbose) {
            std::cout << "========================================" << std::endl;
            std::cout << "Output:  " << name << std::endl;
            std::cout << "Status:  " << (connected ? "Connected" : "Disconnected") << std::endl;
        }

        if (!connected) {
            if (cfg.verbose)
                std::cout << "  -> Skipped (not connected)" << std::endl << std::endl;
            XRRFreeOutputInfo(outInfo);
            continue;
        }

        /* EDID */
        auto edid = getOutputEdid(display, output);
        if (!edid.empty()) {
            auto info = parseEdid(edid, name, cfg);
            if (cfg.verbose) {
                std::cout << "Vendor:  " << info.manufacturer;
                if (!info.modelName.empty()) std::cout << "  " << info.modelName;
                std::cout << std::endl;
            }
        }

        /* Enumerate modes */
        std::vector<ModeEntry> modes;
        RRMode currentModeId = None;

        if (outInfo->crtc != None) {
            XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(display, res, outInfo->crtc);
            if (crtcInfo) {
                currentModeId = crtcInfo->mode;
                XRRFreeCrtcInfo(crtcInfo);
            }
        }

        for (int m = 0; m < outInfo->nmode; ++m) {
            RRMode modeId = outInfo->modes[m];
            const XRRModeInfo* modeInfo = findModeInfo(res, modeId);
            if (!modeInfo) continue;

            ModeEntry e;
            e.id = modeId;
            e.name = std::string(modeInfo->name, modeInfo->nameLength);
            e.width = modeInfo->width;
            e.height = modeInfo->height;
            e.refresh = calculateRefresh(modeInfo);
            e.isCurrent = (modeId == currentModeId);
            e.isPreferred = (m < outInfo->npreferred);
            modes.push_back(e);
        }

        std::sort(modes.begin(), modes.end(), [](const ModeEntry& a, const ModeEntry& b) {
            if (a.width  != b.width)  return a.width  > b.width;
            if (a.height != b.height) return a.height > b.height;
            return a.refresh > b.refresh;
        });

        auto curIt = std::find_if(modes.begin(), modes.end(),
                                  [](const ModeEntry& m){ return m.isCurrent; });
        if (cfg.verbose) {
            if (curIt != modes.end()) {
                std::cout << "Active:  " << curIt->width << "x" << curIt->height
                          << " @" << curIt->refresh << " Hz" << std::endl;
            } else {
                std::cout << "Active:  (none / disabled)" << std::endl;
            }
        }

        /* Check & switch */
        RRMode target = findTargetMode(display, res, output,
                                       cfg.targetW, cfg.targetH, cfg.targetHz);
        if (target == None) {
            if (cfg.verbose)
                std::cout << "Target:  NOT SUPPORTED by this output" << std::endl;
        } else {
            const XRRModeInfo* tInfo = findModeInfo(res, target);
            double actualHz = tInfo ? calculateRefresh(tInfo) : 0.0;
            if (cfg.verbose)
                std::cout << "Target:  Supported (mode ID " << target
                          << ", actual " << actualHz << " Hz)" << std::endl;

            if (isCurrentMode(display, res, output, target)) {
                if (cfg.verbose)
                    std::cout << "Action:  Already optimal — no change needed" << std::endl;
            } else {
                if (cfg.verbose)
                    std::cout << "Action:  Currently NON-OPTIMAL — switching..." << std::endl;
                if (switchToMode(display, res, output, target, cfg)) {
                    if (cfg.verbose)
                        std::cout << "Result:  SUCCESS — switched to "
                                  << cfg.targetW << "x" << cfg.targetH
                                  << " @" << actualHz << " Hz" << std::endl;
                } else {
                    if (cfg.verbose)
                        std::cerr << "Result:  FAILED — could not apply mode" << std::endl;
                }
            }
        }

        /* Full mode table */
        if (cfg.verbose) {
            std::cout << std::endl << "All modes (" << modes.size() << "):" << std::endl;
            std::cout << "  Curr  Pref   Resolution    Refresh    Mode Name" << std::endl;
            std::cout << "  ----  ----   ----------    -------    ---------" << std::endl;
            for (const auto& m : modes) {
                char line[256];
                std::snprintf(line, sizeof(line),
                    "  %s   %s   %4dx%-5d  %6.2f Hz  %s",
                    m.isCurrent   ? "[*]" : "   ",
                    m.isPreferred ? "[+]" : "   ",
                    m.width, m.height, m.refresh, m.name.c_str());
                std::cout << line << std::endl;
            }
            std::cout << std::endl;
        }

        XRRFreeOutputInfo(outInfo);
    }

    XRRFreeScreenResources(res);
    return EXIT_SUCCESS;
}

int main()
{
    AppConfig cfg;
    cfg.verbose = false;

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        std::cerr << "Error: Could not open X11 display" << std::endl;
        return EXIT_FAILURE;
    }

    int rc = runMonitorSetup(display, cfg);
    if (rc != EXIT_SUCCESS) {
        XCloseDisplay(display);
        return rc;
    }

    enableNumLock(display);
    XSync(display, False);
    XCloseDisplay(display);
    return EXIT_SUCCESS;
}
