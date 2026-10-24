#pragma once
#include <cstdint>
#include <unordered_set>

namespace button_controls::customizer {
// Slate can consume menu keys before APlayerController::InputKey. Track the
// physical stream at that boundary, including keyboard modifiers, and consume
// the closing press's release even after the modal has disappeared.
class ModalKeys {
public:
    void observe(uint64_t key,bool down) {
        if(down)held_.insert(key);else held_.erase(key);
    }
    bool quarantined(uint64_t key,bool down) {
        const bool found=blocked_.count(key)!=0;
        if(!down)blocked_.erase(key);
        return found;
    }
    void close(){blocked_.insert(held_.begin(),held_.end());}
    void reset(){held_.clear();blocked_.clear();}
    bool released()const{return held_.empty();}
private:
    std::unordered_set<uint64_t> held_,blocked_;
};
}
