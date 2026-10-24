#pragma once
#include <array>
#include <cstdint>

namespace button_controls::customizer {
// MSVC passes this 24-byte, non-trivial FKey by an owning pointer. The native
// InputKey callee destroys its argument. Borrowed EKeys globals must therefore
// be copied before dispatch, while a swallowed incoming argument needs its own
// destructor call. No native shared reference crosses CRT allocators.
template<class Copy,class Invoke>
auto dispatch_key_copy(const void* borrowed,Copy copy,Invoke invoke) {
    alignas(8) std::array<uint8_t,24> owned{};
    copy(owned.data(),borrowed);
    return invoke(owned.data()); // Native callee consumes/destroys this copy.
}
class KeyArgument {
public:
    using Destroy=void(*)(void*);
    KeyArgument(const void* incoming,Destroy destroy):incoming_(const_cast<void*>(incoming)),destroy_(destroy){}
    KeyArgument(const KeyArgument&)=delete;
    KeyArgument& operator=(const KeyArgument&)=delete;
    ~KeyArgument(){if(!transferred_)destroy_(incoming_);}
    template<class Copy,class Invoke>
    auto dispatch(const void* target,Copy copy,Invoke invoke) {
        if(target==incoming_) {
            transferred_=true;
            return invoke(incoming_);
        }
        return dispatch_key_copy(target,copy,invoke);
    }
private:
    void* incoming_;
    Destroy destroy_;
    bool transferred_=false;
};
}
