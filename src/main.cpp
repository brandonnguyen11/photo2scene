// main.cpp — Photo2Scene entry point.
//
// Usage:
//   Photo2Scene [--image photo.jpg] [--depth depth.png] [--dummy]
//               [--size 1600x900] [--screenshot out.png]
//
//   --image       photo to load at start-up (depth is estimated automatically)
//   --depth       precomputed depth map (skips the Python step)
//   --dummy       use synthetic depth instead of the AI model (pipeline test)
//   --screenshot  render, save a PNG and exit (used for automated smoke tests)
//   --yaw/--pitch initial camera orbit in degrees (with --screenshot)
//   --export      with --screenshot: also write original-view renders to outputs/

#include "App.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--image") options.imagePath = next();
        else if (arg == "--depth") options.depthPath = next();
        else if (arg == "--screenshot") options.screenshotPath = next();
        else if (arg == "--world") options.worldId = next();
        else if (arg == "--dummy") options.dummyDepth = true;
        else if (arg == "--export") options.exportOriginalView = true;
        else if (arg == "--yaw") options.yawDeg = std::stof(next());
        else if (arg == "--pitch") options.pitchDeg = std::stof(next());
        else if (arg == "--size") {
            const std::string s = next();
            std::sscanf(s.c_str(), "%dx%d", &options.width, &options.height);
        } else if (arg == "--help" || arg == "-h") {
            std::printf("Photo2Scene [--image photo.jpg] [--depth depth.png] [--dummy] "
                        "[--size WxH] [--screenshot out.png] [--yaw deg] [--pitch deg] [--export]\n");
            return 0;
        } else {
            std::fprintf(stderr, "Unknown argument: %s (try --help)\n", arg.c_str());
            return 1;
        }
    }

    App app;
    if (!app.init(options)) {
        app.shutdown();
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}
