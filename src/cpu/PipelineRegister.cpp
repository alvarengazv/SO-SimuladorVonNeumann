#include "PipelineRegister.hpp"

void PipelineRegister::push(const PipelineToken &token) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&]() { return !hasToken_ || stopped_; });
    if (stopped_) {
        return;
    }
    stored_ = token;
    hasToken_ = true;
    cv_.notify_all();
}

bool PipelineRegister::pop(PipelineToken &out) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&]() { return hasToken_ || stopped_; });
    if (!hasToken_) {
        return false;
    }
    out = stored_;
    hasToken_ = false;
    cv_.notify_all();
    return true;
}

void PipelineRegister::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hasToken_ && stored_.terminate) {
        return;
    }
    hasToken_ = false;
    stored_ = PipelineToken{};
    cv_.notify_all();
}

void PipelineRegister::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    hasToken_ = false;
    stored_ = PipelineToken{};
    cv_.notify_all();
}

bool PipelineRegister::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !hasToken_;
}

void PipelineRegister::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = false;
    hasToken_ = false;
    stored_ = PipelineToken{};
    cv_.notify_all();
}