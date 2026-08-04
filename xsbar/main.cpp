#include <X11/Xlib.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

int main()
{
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::fprintf(stderr, "unable to open display\n");
        return 2;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);

    std::tm utc_tm;

    // Manila = UTC+8
    std::tm manila_tm;

    while (true) {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);

        gmtime_r(&time_t_now, &utc_tm);
        std::ostringstream utc_oss;
        utc_oss << "🌍 " << std::put_time(&utc_tm, "%Y-%m-%d %I:%M %p");

        auto manila_now = now + std::chrono::hours(8);
        auto manila_time_t = std::chrono::system_clock::to_time_t(manila_now);
        gmtime_r(&manila_time_t, &manila_tm);
        std::ostringstream manila_oss;
        manila_oss << "🇵🇭 " << std::put_time(&manila_tm, "%Y-%m-%d %I:%M:%S %p");

        std::string display = utc_oss.str() + "  " + manila_oss.str();
        XStoreName(dpy, root, display.c_str());
        XFlush(dpy);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    XCloseDisplay(dpy);
    return 0;
}
