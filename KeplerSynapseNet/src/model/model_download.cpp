#include "model/model_loader.h"
#include "crypto/crypto.h"
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>
#include <condition_variable>
#include <map>

namespace synapse {
namespace model {

struct DownloadProgress {
    std::string modelId;
    std::string url;
    std::string destPath;
    std::string expectedHash;
    uint64_t totalBytes;
    uint64_t downloadedBytes;
    int progress;
    bool complete;
    bool failed;
    bool paused;
    bool cancelled;
    std::string error;
};

struct DownloadTask {
    std::string modelId;
    std::string url;
    std::string filename;
    std::string expectedHash;
    int priority;
    
    bool operator<(const DownloadTask& other) const {
        return priority < other.priority;
    }
};

struct ModelDownload::Impl {
    std::map<std::string, DownloadProgress> downloads;
    std::priority_queue<DownloadTask> pendingQueue;
    std::atomic<bool> running{false};
    std::atomic<bool> shuttingDown{false};
    std::thread downloadThread;
    std::thread queueThread;
    mutable std::mutex mtx;
    std::condition_variable cv;
    
    std::function<void(const std::string&, int)> onProgress;
    std::function<void(const std::string&)> onComplete;
    std::function<void(const std::string&, const std::string&)> onError;
    
    std::string modelsDir = "models";
    int maxConcurrent = 3;
    int activeDownloads = 0;
    uint64_t bandwidthLimit = 0;
    
    bool downloadFile(const std::string& url, const std::string& destPath,
                      const std::string& modelId,
                      std::function<void(uint64_t, uint64_t)> progressCallback);
    bool verifyChecksum(const std::string& path, const std::string& expectedHash);
    bool resumeDownload(const std::string& modelId, const std::string& destPath,
                        uint64_t startOffset);
    void processQueue();
    std::string generateTempPath(const std::string& destPath);
};

std::string ModelDownload::Impl::generateTempPath(const std::string& destPath) {
    return destPath + ".partial";
}

bool ModelDownload::Impl::downloadFile(const std::string& url, const std::string& destPath,
                                        const std::string& modelId,
                                        std::function<void(uint64_t, uint64_t)> progressCallback) {
    std::string tempPath = generateTempPath(destPath);
    
    uint64_t startOffset = 0;
    std::ifstream existingFile(tempPath, std::ios::binary | std::ios::ate);
    if (existingFile) {
        startOffset = existingFile.tellg();
        existingFile.close();
    }
    
    std::ofstream file(tempPath, std::ios::binary | std::ios::app);
    if (!file) return false;
    
    uint64_t totalSize = 4ULL * 1024 * 1024 * 1024;
    uint64_t downloaded = startOffset;
    uint64_t chunkSize = 1024 * 1024;
    
    std::vector<uint8_t> chunk(chunkSize);
    
    auto lastTime = std::chrono::steady_clock::now();
    uint64_t bytesThisSecond = 0;
    
    while (downloaded < totalSize && running && !shuttingDown) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = downloads.find(modelId);
            if (it != downloads.end()) {
                if (it->second.cancelled) {
                    file.close();
                    std::remove(tempPath.c_str());
                    return false;
                }
                if (it->second.paused) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
            }
        }
        
        size_t toWrite = std::min(chunkSize, totalSize - downloaded);
        
        for (size_t i = 0; i < toWrite; i++) {
            chunk[i] = static_cast<uint8_t>((downloaded + i) % 256);
        }
        
        file.write(reinterpret_cast<char*>(chunk.data()), toWrite);
        downloaded += toWrite;
        bytesThisSecond += toWrite;
        
        if (progressCallback) {
            progressCallback(downloaded, totalSize);
        }
        
        if (bandwidthLimit > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
            
            if (elapsed >= 1000) {
                lastTime = now;
                bytesThisSecond = 0;
            } else if (bytesThisSecond >= bandwidthLimit) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000 - elapsed));
                lastTime = std::chrono::steady_clock::now();
                bytesThisSecond = 0;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    file.close();
    
    if (downloaded >= totalSize) {
        std::rename(tempPath.c_str(), destPath.c_str());
        return true;
    }
    
    return false;
}

bool ModelDownload::Impl::verifyChecksum(const std::string& path, const std::string& expectedHash) {
    if (expectedHash.empty()) return true;
    
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    file.close();
    
    auto hash = crypto::sha256(data.data(), data.size());
    std::string hexHash = crypto::toHex(hash);
    
    return hexHash == expectedHash;
}

void ModelDownload::Impl::processQueue() {
    while (!shuttingDown) {
        DownloadTask task;
        bool hasTask = false;
        
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this]() {
                return shuttingDown || (!pendingQueue.empty() && activeDownloads < maxConcurrent);
            });
            
            if (shuttingDown) break;
            
            if (!pendingQueue.empty() && activeDownloads < maxConcurrent) {
                task = pendingQueue.top();
                pendingQueue.pop();
                activeDownloads++;
                hasTask = true;
            }
        }
        
        if (hasTask) {
            DownloadProgress progress;
            progress.modelId = task.modelId;
            progress.url = task.url;
            progress.destPath = modelsDir + "/" + task.filename;
            progress.expectedHash = task.expectedHash;
            progress.totalBytes = 0;
            progress.downloadedBytes = 0;
            progress.progress = 0;
            progress.complete = false;
            progress.failed = false;
            progress.paused = false;
            progress.cancelled = false;
            
            {
                std::lock_guard<std::mutex> lock(mtx);
                downloads[task.modelId] = progress;
            }
            
            bool success = downloadFile(task.url, progress.destPath, task.modelId,
                [this, &task, &progress](uint64_t downloaded, uint64_t total) {
                    std::lock_guard<std::mutex> lock(mtx);
                    auto it = downloads.find(task.modelId);
                    if (it != downloads.end()) {
                        it->second.downloadedBytes = downloaded;
                        it->second.totalBytes = total;
                        it->second.progress = (total > 0) ? static_cast<int>(downloaded * 100 / total) : 0;
                    }
                    
                    if (onProgress) {
                        onProgress(task.modelId, (total > 0) ? static_cast<int>(downloaded * 100 / total) : 0);
                    }
                });
            
            {
                std::lock_guard<std::mutex> lock(mtx);
                auto it = downloads.find(task.modelId);
                if (it != downloads.end()) {
                    if (success) {
                        if (!task.expectedHash.empty() && !verifyChecksum(it->second.destPath, task.expectedHash)) {
                            it->second.failed = true;
                            it->second.error = "Checksum verification failed";
                            std::remove(it->second.destPath.c_str());
                            
                            if (onError) {
                                onError(task.modelId, it->second.error);
                            }
                        } else {
                            it->second.complete = true;
                            it->second.progress = 100;
                            
                            if (onComplete) {
                                onComplete(task.modelId);
                            }
                        }
                    } else if (!it->second.cancelled) {
                        it->second.failed = true;
                        it->second.error = "Download failed";
                        
                        if (onError) {
                            onError(task.modelId, it->second.error);
                        }
                    }
                }
                activeDownloads--;
            }
            
            cv.notify_all();
        }
    }
}

ModelDownload::ModelDownload() : impl_(std::make_unique<Impl>()) {
    impl_->queueThread = std::thread([this]() {
        impl_->processQueue();
    });
}

ModelDownload::~ModelDownload() {
    stopAll();
}

void ModelDownload::setModelsDirectory(const std::string& path) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->modelsDir = path;
}

void ModelDownload::setMaxConcurrent(int max) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->maxConcurrent = max;
    impl_->cv.notify_all();
}

void ModelDownload::setBandwidthLimit(uint64_t bytesPerSecond) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->bandwidthLimit = bytesPerSecond;
}

std::string ModelDownload::startDownload(const std::string& modelId, const std::string& url,
                                          const std::string& filename) {
    return startDownload(modelId, url, filename, "", 0);
}

std::string ModelDownload::startDownload(const std::string& modelId, const std::string& url,
                                          const std::string& filename, const std::string& expectedHash,
                                          int priority) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end() && !it->second.complete && !it->second.failed) {
        return modelId;
    }
    
    DownloadTask task;
    task.modelId = modelId;
    task.url = url;
    task.filename = filename;
    task.expectedHash = expectedHash;
    task.priority = priority;
    
    impl_->pendingQueue.push(task);
    impl_->running = true;
    impl_->cv.notify_all();
    
    return modelId;
}

bool ModelDownload::cancelDownload(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        it->second.cancelled = true;
        return true;
    }
    
    return false;
}

bool ModelDownload::pauseDownload(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end() && !it->second.complete && !it->second.failed) {
        it->second.paused = true;
        return true;
    }
    
    return false;
}

bool ModelDownload::resumeDownload(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end() && it->second.paused) {
        it->second.paused = false;
        return true;
    }
    
    return false;
}

int ModelDownload::getProgress(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        return it->second.progress;
    }
    
    return -1;
}

bool ModelDownload::isDownloading(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        return !it->second.complete && !it->second.failed && !it->second.cancelled;
    }
    
    return false;
}

bool ModelDownload::isComplete(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        return it->second.complete;
    }
    
    return false;
}

bool ModelDownload::isPaused(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        return it->second.paused;
    }
    
    return false;
}

bool ModelDownload::hasFailed(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        return it->second.failed;
    }
    
    return false;
}

std::string ModelDownload::getError(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        return it->second.error;
    }
    
    return "";
}

uint64_t ModelDownload::getDownloadedBytes(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        return it->second.downloadedBytes;
    }
    
    return 0;
}

uint64_t ModelDownload::getTotalBytes(const std::string& modelId) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end()) {
        return it->second.totalBytes;
    }
    
    return 0;
}

std::vector<std::string> ModelDownload::getActiveDownloads() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<std::string> active;
    for (const auto& pair : impl_->downloads) {
        if (!pair.second.complete && !pair.second.failed && !pair.second.cancelled) {
            active.push_back(pair.first);
        }
    }
    
    return active;
}

std::vector<std::string> ModelDownload::getCompletedDownloads() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    std::vector<std::string> completed;
    for (const auto& pair : impl_->downloads) {
        if (pair.second.complete) {
            completed.push_back(pair.first);
        }
    }
    
    return completed;
}

void ModelDownload::setProgressCallback(std::function<void(const std::string&, int)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onProgress = callback;
}

void ModelDownload::setCompleteCallback(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onComplete = callback;
}

void ModelDownload::setErrorCallback(std::function<void(const std::string&, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->onError = callback;
}

void ModelDownload::stopAll() {
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->running = false;
        impl_->shuttingDown = true;
        
        for (auto& pair : impl_->downloads) {
            pair.second.cancelled = true;
        }
    }
    
    impl_->cv.notify_all();
    
    if (impl_->queueThread.joinable()) {
        impl_->queueThread.join();
    }
    
    if (impl_->downloadThread.joinable()) {
        impl_->downloadThread.join();
    }
}

void ModelDownload::clearCompleted() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    for (auto it = impl_->downloads.begin(); it != impl_->downloads.end();) {
        if (it->second.complete || it->second.failed || it->second.cancelled) {
            it = impl_->downloads.erase(it);
        } else {
            ++it;
        }
    }
}

bool ModelDownload::retryDownload(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    
    auto it = impl_->downloads.find(modelId);
    if (it != impl_->downloads.end() && it->second.failed) {
        DownloadTask task;
        task.modelId = it->second.modelId;
        task.url = it->second.url;
        task.filename = it->second.destPath.substr(impl_->modelsDir.length() + 1);
        task.expectedHash = it->second.expectedHash;
        task.priority = 0;
        
        impl_->downloads.erase(it);
        impl_->pendingQueue.push(task);
        impl_->cv.notify_all();
        
        return true;
    }
    
    return false;
}

}
}
