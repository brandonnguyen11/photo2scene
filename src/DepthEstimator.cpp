#include "DepthEstimator.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string quote(const std::string& s) { return "\"" + s + "\""; }

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// First line containing "ERROR:" (the Python script prints its reason this way).
std::string extractError(const std::string& log) {
    std::istringstream in(log);
    std::string line, last;
    while (std::getline(in, line)) {
        if (line.find("ERROR:") != std::string::npos) return line.substr(line.find("ERROR:") + 7);
        if (!line.empty()) last = line;
    }
    return last.empty() ? "Depth estimation failed (no output)." : last;
}

int runCommand(std::string cmd) {
#ifdef _WIN32
    // cmd.exe strips the first and last quote of the whole line; wrap once more.
    cmd = "\"" + cmd + "\"";
    return std::system(cmd.c_str());
#else
    const int status = std::system(cmd.c_str());
    if (status == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

}  // namespace

DepthEstimator::~DepthEstimator() {
    if (worker_.joinable()) worker_.join();
}

bool DepthEstimator::start(const std::string& imagePath) {
    if (busy()) return false;
    if (worker_.joinable()) worker_.join();
    state_ = DepthJobState::Running;
    startTime_ = std::chrono::steady_clock::now();
    worker_ = std::thread([this, cfg = config, imagePath]() {
        DepthJobResult r = runJob(cfg, imagePath);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(r);
        }
        state_ = DepthJobState::Finished;
    });
    return true;
}

double DepthEstimator::elapsedSeconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

bool DepthEstimator::poll(DepthJobResult& out) {
    if (state_.load() != DepthJobState::Finished) return false;
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out = std::move(result_);
        result_ = DepthJobResult{};
    }
    state_ = DepthJobState::Idle;
    return true;
}

DepthJobResult DepthEstimator::runJob(DepthEstimatorConfig cfg, std::string imagePath) {
    const auto t0 = std::chrono::steady_clock::now();
    DepthJobResult r;

    std::error_code ec;
    fs::create_directories(fs::u8path(cfg.outputDir), ec);

    const std::string stem = fs::u8path(imagePath).stem().u8string();
    const fs::path outDir = fs::u8path(cfg.outputDir);
    const fs::path logPath = outDir / (stem + "_log.txt");
    const fs::path rgbPath = outDir / (stem + "_rgb.png");
    const fs::path depthPath = outDir / (stem + "_depth16.png");
    fs::remove(rgbPath, ec);
    fs::remove(depthPath, ec);

    if (!fs::exists(fs::u8path(cfg.scriptPath))) {
        r.error = "Python script not found: " + cfg.scriptPath;
        return r;
    }

    std::string cmd = quote(cfg.pythonExe) + " " + quote(cfg.scriptPath) + " --input " +
                      quote(imagePath) + " --output-dir " + quote(cfg.outputDir) + " --name " +
                      quote(stem) + " --device " + cfg.device + " --model " + cfg.model;
    if (cfg.dummy) cmd += " --dummy";
    cmd += " > " + quote(logPath.u8string()) + " 2>&1";

    const int exitCode = runCommand(cmd);
    r.log = readAll(logPath);
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (exitCode != 0) {
        r.error = extractError(r.log);
        if (exitCode == 127 || exitCode == 9009)  // "command not found" on POSIX / Windows
            r.error = "Python interpreter not found: '" + cfg.pythonExe +
                      "'. Set it under Image > Python settings.";
        return r;
    }
    if (!fs::exists(depthPath) || !fs::exists(rgbPath)) {
        r.error = "Python finished but output files are missing (see log).";
        return r;
    }
    r.ok = true;
    r.rgbPath = rgbPath.u8string();
    r.depthPath = depthPath.u8string();
    return r;
}

std::string defaultPythonExecutable(const std::string& projectRoot) {
    if (const char* env = std::getenv("P2S_PYTHON")) {
        if (*env) return env;
    }
    const fs::path root = fs::u8path(projectRoot);
#ifdef _WIN32
    const fs::path venv = root / "ai" / ".venv" / "Scripts" / "python.exe";
    if (fs::exists(venv)) return venv.u8string();
    return "python";
#else
    const fs::path venv = root / "ai" / ".venv" / "bin" / "python";
    if (fs::exists(venv)) return venv.u8string();
    return "python3";
#endif
}
