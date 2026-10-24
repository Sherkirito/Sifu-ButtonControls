#pragma once
#include "profile.hpp"

namespace button_controls::customizer {
enum class RouteScope { Gameplay, Focus, Menu, Inactive };
struct PressRoute { Button logical; bool gameplay; bool forward=true; };
class PressRouter {
public:
    // Multiple physical owners can target one native key at a context boundary
    // (a remapped Focus entry and raw LT inside Focus, for example). Only the
    // first down and final up reach UPlayerInput. A release never re-evaluates
    // the profile that happened to be active when the key went down.
    PressRoute event(Button physical,int event,RouteScope scope,const Profile& profile) {
        const auto index=size_t(physical);
        if(index>=button_count)return {physical,false,false};
        const auto mask=uint32_t{1}<<index;
        if(blocked_&mask) {
            if(event==1){active_&=~mask;blocked_&=~mask;slate_held_&=~mask;}
            return {physical,false,false};
        }
        if(!(active_&mask)) {
            // A native flush/menu may have consumed the original down. Let an
            // unmatched raw up clear engine state only if no virtual owner is
            // holding that target; never release somebody else's remapped LT.
            if(event!=0)return {physical,false,event==1 && owners_[index]==0 && scope!=RouteScope::Inactive};
            if(scope==RouteScope::Inactive){blocked_|=mask;return {physical,false,false};}
            const auto target=scope==RouteScope::Gameplay?profile.logical(physical):physical;
            if(target>=Button::Count)return {physical,false,false};
            routes_[index]={target,scope==RouteScope::Gameplay,true};
            active_|=mask;
            auto result=routes_[index];result.forward=++owners_[size_t(target)]==1;
            return result;
        }
        auto result=routes_[index];
        if(event==1) {
            active_&=~mask;
            result.forward=--owners_[size_t(result.logical)]==0;
        } else if(event==0)result.forward=false; // Duplicate down is not a new owner.
        return result;
    }

    // Return native keys that require a synthetic release before processing
    // input in the new scope. LT remains held while entering Focus, preserving
    // the game's hold-to-Focus behavior. All other old presses wait for a real
    // physical release before they can act in the new context.
    uint32_t transition(RouteScope scope) {
        if(scope==scope_)return 0;
        uint32_t released=0,preserved=0;
        for(size_t i=0;i<button_count;++i) {
            const auto mask=uint32_t{1}<<i;
            if(!(active_&mask) || (blocked_&mask))continue;
            const auto& route=routes_[i];
            if(scope==RouteScope::Focus && route.gameplay && route.logical==Button::LT) {
                preserved|=mask;continue;
            }
            if(--owners_[size_t(route.logical)]==0)released|=uint32_t{1}<<size_t(route.logical);
            blocked_|=mask;
        }
        // Slate may consume a menu's down without calling InputKey. Remember
        // those physical presses too, so closing a menu cannot become combat.
        blocked_|=slate_held_&~preserved;
        scope_=scope;
        return released;
    }

    // Called at Slate before the native menu can consume an input. The return
    // value tells the adapter to swallow a retired press/release. Native ups
    // have already been emitted by transition(), so swallowing is safe.
    bool observe_slate(Button physical,bool down) {
        const auto index=size_t(physical);
        if(index>=button_count)return false;
        const auto mask=uint32_t{1}<<index;
        const bool swallowed=(blocked_&mask)!=0;
        if(down)slate_held_|=mask;
        else {
            slate_held_&=~mask;
            if(swallowed){blocked_&=~mask;active_&=~mask;}
        }
        return swallowed;
    }
    bool quarantined(Button physical)const {
        return physical<Button::Count && (blocked_&(uint32_t{1}<<size_t(physical)));
    }
    void quarantine_held(uint32_t physical_mask) {
        // Used immediately after a native flush; no virtual target owns these
        // old physical holds, so they can only wait for their real releases.
        blocked_|=physical_mask&((uint32_t{1}<<button_count)-1);
        slate_held_|=blocked_;
    }
    bool recover_inflight_press(Button physical,const PressRoute& owner,RouteScope origin) {
        // A native callback may flush input synchronously inside InputKey.
        // That flush cancels older presses, but the down currently returning
        // from the native handler still needs its original release target.
        if(physical>=Button::Count || owner.logical>=Button::Count || origin==RouteScope::Inactive ||
           active_ || blocked_ || native_held_mask())return false;
        scope_=origin;active_=uint32_t{1}<<size_t(physical);
        routes_[size_t(physical)]=owner;owners_[size_t(owner.logical)]=1;
        return true;
    }
    uint32_t native_held_mask()const {
        uint32_t result=0;
        for(size_t i=0;i<button_count;++i)if(owners_[i])result|=uint32_t{1}<<i;
        return result;
    }
    void flush(){active_=blocked_=slate_held_=0;owners_.fill(0);scope_=RouteScope::Inactive;}
private:
    RouteScope scope_=RouteScope::Inactive;
    uint32_t active_=0,blocked_=0,slate_held_=0;
    std::array<PressRoute,button_count> routes_{};
    std::array<uint8_t,button_count> owners_{};
};
}
