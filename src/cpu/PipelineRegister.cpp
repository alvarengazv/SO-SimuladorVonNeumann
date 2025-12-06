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
    hasToken_ = false;
    stored_ = PipelineToken{};
    cv_.notify_all();
}

void PipelineRegister::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Notifica quaisquer wait() sem descartar um token possivelmente em trânsito. Isso é
    // usado para desfazer threads em preempção/desmontagem. Limpar o token aqui
    // pode descartar trabalho; mantenha-o intacto e apenas sinalize a parada.
    stopped_ = true;
    cv_.notify_all();
}

bool PipelineRegister::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !hasToken_;
}

bool PipelineRegister::debugHasToken() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasToken_;
}

bool PipelineRegister::debugStopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopped_;
}

PipelineToken PipelineRegister::debugPeek() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stored_;
}