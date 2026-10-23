#pragma once

#include <nova/scene/scene.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace nova {

// Stack of scenes. push/pop/replace are queued and applied at the end of the frame (FIFO),
// so a scene is never destroyed while its own code is running. Only the top scene updates.
class SceneStack {
public:
    explicit SceneStack(Services& services) : services_(services) {}
    ~SceneStack() { clear(); }
    SceneStack(const SceneStack&) = delete;
    SceneStack& operator=(const SceneStack&) = delete;

    template <class T, class... Args>
    void push(Args&&... args) { queue(Op::Push, std::make_unique<T>(std::forward<Args>(args)...)); }
    template <class T, class... Args>
    void replace(Args&&... args) { queue(Op::Replace, std::make_unique<T>(std::forward<Args>(args)...)); }
    // Refused (with an error log) when it would leave the stack empty.
    void pop() { queue(Op::Pop, nullptr); }

    // Engine
    void pushScene(std::unique_ptr<Scene> scene) { queue(Op::Push, std::move(scene)); }
    // Returns true if any push/pop/replace was applied (a scene transition happened).
    bool applyPending();
    void clear();
    Scene* top() { return scenes_.empty() ? nullptr : scenes_.back().get(); }
    size_t size() const { return scenes_.size(); }
    bool empty() const { return scenes_.empty(); }
    bool hasPending() const { return !ops_.empty(); }

    // Visits scenes bottom-up, starting at the topmost opaque scene.
    template <class F>
    void forEachVisible(F&& fn) {
        if (scenes_.empty()) return;
        size_t start = scenes_.size() - 1;
        while (start > 0 && !scenes_[start]->opaque) --start;
        for (size_t i = start; i < scenes_.size(); ++i) fn(*scenes_[i]);
    }

private:
    enum class Op { Push, Pop, Replace };
    struct Pending {
        Op op;
        std::unique_ptr<Scene> scene;
    };

    void queue(Op op, std::unique_ptr<Scene> scene) { ops_.push_back({op, std::move(scene)}); }
    void enter(std::unique_ptr<Scene> scene);
    void exitTop();

    Services& services_;
    std::vector<std::unique_ptr<Scene>> scenes_;
    std::vector<Pending> ops_;
    ScopeId nextScope_ = 1;
};

} // namespace nova
