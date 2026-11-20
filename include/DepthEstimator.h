#pragma once
// DepthEstimator.h — C++ side of the AI bridge (Member 3 + Member 5)
//
// Runs ai/depth_estimation.py as a child process on a worker thread so the
// render loop never blocks (requirement 4.1). The contract between the two
// sides is documented in docs/INTERFACES.md.

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

enum class DepthJobState { Idle, Running, Finished };

struct DepthJobResult {
    bool ok = false;
    std::string rgbPath;     // preprocessed photo (EXIF-rotated, resized)  -> texture
    std::string depthPath;   // 16-bit PNG, 1.0 = nearest                   -> geometry
    std::string error;       // human-readable failure reason
    std::string log;         // full stdout/stderr of the Python process
    double seconds = 0.0;    // wall-clock time including Python start-up
};

struct DepthEstimatorConfig {
    std::string pythonExe;   // e.g. "python3", or a venv interpreter
    std::string scriptPath;  // ai/depth_estimation.py
    std::string outputDir;   // where results are written (cache/)
    std::string device = "auto";   // auto | cpu | cuda | mps
    std::string model = "small";   // small | base | large
    bool dummy = false;      // synthetic depth, no model (pipeline testing)
};

class DepthEstimator {
public:
    ~DepthEstimator();

    DepthEstimatorConfig config;

    // Returns false if a job is already running.
    bool start(const std::string& imagePath);
    bool busy() const { return state_.load() == DepthJobState::Running; }
    double elapsedSeconds() const;

    // Returns true exactly once per finished job and moves the result out.
    bool poll(DepthJobResult& out);

private:
    static DepthJobResult runJob(DepthEstimatorConfig cfg, std::string imagePath);

    std::thread worker_;
    std::atomic<DepthJobState> state_{DepthJobState::Idle};
    std::mutex mutex_;
    DepthJobResult result_;
    std::chrono::steady_clock::time_point startTime_;
};

// Picks a sensible interpreter: $P2S_PYTHON, then ai/.venv, then python3/python.
std::string defaultPythonExecutable(const std::string& projectRoot);
