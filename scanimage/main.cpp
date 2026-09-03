// Image scanner using libsane (SANE API) and libpng.
//
// Build:
//   g++ -std=c++23 -O2 main.cpp -o imagescanner  -lsane -lpng
//
// Usage:
//   ./imagescanner [device_name] [output.png] [dpi]
//
//   With no arguments, it lists devices if none is given, or scans the
//   first detected device at 300 dpi into "scan.png".
//
// Requires: libsane-dev, libpng-dev (e.g. `sudo apt install libsane-dev libpng-dev`)

#include <sane/sane.h>
#include <png.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <print>

namespace {

void sane_check(SANE_Status st, const char* what) {
    if (st != SANE_STATUS_GOOD) {
        throw std::runtime_error(std::string(what) + ": " + sane_strstatus(st));
    }
}

// Try to set a named option to an integer/fixed value if it exists. Ignores
// failures silently (option may not exist on this backend, or be read-only).
void try_set_int_option(SANE_Handle h, const char* name, int value) {
    const SANE_Option_Descriptor* desc;
    int n = 0;
    sane_control_option(h, 0, SANE_ACTION_GET_VALUE, nullptr, nullptr); // no-op probe not required
    for (int i = 1; (desc = sane_get_option_descriptor(h, i)) != nullptr; ++i) {
        if (desc->name && std::strcmp(desc->name, name) == 0) {
            if (desc->type == SANE_TYPE_INT) {
                SANE_Int v = value;
                sane_control_option(h, i, SANE_ACTION_SET_VALUE, &v, nullptr);
            } else if (desc->type == SANE_TYPE_FIXED) {
                SANE_Fixed v = SANE_FIX(static_cast<double>(value));
                sane_control_option(h, i, SANE_ACTION_SET_VALUE, &v, nullptr);
            }
            return;
        }
    }
    (void)n;
}

std::string pick_device() {
    const SANE_Device** devices = nullptr;
    sane_check(sane_get_devices(&devices, SANE_FALSE), "sane_get_devices");
    if (!devices || !devices[0]) {
        throw std::runtime_error("No SANE devices found");
    }
    std::println("Devices found:");
    for (int i = 0; devices[i]; ++i) {
        std::println("  [{}] {} ({} {})", i, devices[i]->name,
                      devices[i]->vendor ? devices[i]->vendor : "",
                      devices[i]->model ? devices[i]->model : "");
    }
    return devices[0]->name;
}

void write_png(const std::string& path, int width, int height, int channels,
               const std::vector<unsigned char>& pixels) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) throw std::runtime_error("Could not open output file: " + path);

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { std::fclose(fp); throw std::runtime_error("png_create_write_struct failed"); }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, nullptr); std::fclose(fp); throw std::runtime_error("png_create_info_struct failed"); }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        throw std::runtime_error("libpng error during write");
    }

    png_init_io(png, fp);

    int color_type = (channels == 1) ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB;

    png_set_IHDR(png, info, width, height, 8, color_type,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<png_bytep> rows(height);
    size_t stride = static_cast<size_t>(width) * channels;
    for (int y = 0; y < height; ++y) {
        rows[y] = const_cast<png_bytep>(&pixels[y * stride]);
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
}

} // namespace

int main(int argc, char** argv) {
    std::string device_name = (argc > 1) ? argv[1] : "";
    std::string output_path = (argc > 2) ? argv[2] : "scan.png";
    int dpi = (argc > 3) ? std::atoi(argv[3]) : 300;

    SANE_Int version = 0;
    SANE_Handle handle = nullptr;

    try {
        sane_check(sane_init(&version, nullptr), "sane_init");

        if (device_name.empty()) {
            device_name = pick_device();
        }

        sane_check(sane_open(device_name.c_str(), &handle), "sane_open");

        try_set_int_option(handle, "resolution", dpi);

        sane_check(sane_start(handle), "sane_start");

        SANE_Parameters params{};
        sane_check(sane_get_parameters(handle, &params), "sane_get_parameters");

        int channels;
        switch (params.format) {
            case SANE_FRAME_GRAY: channels = 1; break;
            case SANE_FRAME_RGB:  channels = 3; break;
            default:
                throw std::runtime_error("Unsupported frame format (only Gray/RGB handled)");
        }

        int width = params.pixels_per_line;
        int height = params.lines; // may be -1 if unknown ahead of time

        std::vector<unsigned char> buffer;
        buffer.reserve(width * channels * (height > 0 ? height : 1024));

        std::vector<SANE_Byte> chunk(65536);
        SANE_Int len = 0;
        SANE_Status st;
        while ((st = sane_read(handle, chunk.data(), static_cast<SANE_Int>(chunk.size()), &len)) == SANE_STATUS_GOOD) {
            buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + len);
        }
        if (st != SANE_STATUS_EOF) {
            sane_check(st, "sane_read");
        }

        if (height <= 0) {
            size_t stride = static_cast<size_t>(width) * channels;
            height = static_cast<int>(buffer.size() / stride);
        }

        sane_close(handle);
        handle = nullptr;
        sane_exit();

        write_png(output_path, width, height, channels, buffer);
        std::println("Wrote {} ({}x{}, {} channel(s))", output_path, width, height, channels);

    } catch (const std::exception& e) {
        std::println(stderr, "Error: {}", e.what());
        if (handle) sane_close(handle);
        sane_exit();
        return 1;
    }

    return 0;
}
