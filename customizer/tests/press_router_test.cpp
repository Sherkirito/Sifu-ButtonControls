#include "../src/press_router.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
namespace c=button_controls::customizer;
using B=c::Button;
using S=c::RouteScope;
static size_t checks=0;
static void expect(bool value,const char* label) {
    ++checks;if(!value){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}
}
static uint32_t bit(B b){return uint32_t{1}<<size_t(b);}
int main() {
    c::Profile p;
    c::PressRouter router;
    router.transition(S::Gameplay);
    p.swap_binding(size_t(B::LT),B::RB);
    auto r=router.event(B::RB,0,S::Gameplay,p);
    expect(r.logical==B::LT && r.forward,"remapped Focus entry down");
    expect(router.transition(S::Focus)==0,"hold-to-Focus preserves the entry key");
    expect(!router.event(B::LT,1,S::Focus,p).forward,"unmatched raw up cannot release a virtual Focus owner");
    r=router.event(B::LT,0,S::Focus,p);
    expect(r.logical==B::LT && !r.forward,"raw LT in Focus adds an owner without another down");
    r=router.event(B::RB,1,S::Focus,p);
    expect(!r.forward && router.native_held_mask()==bit(B::LT),"releasing one owner does not release another");
    r=router.event(B::LT,1,S::Focus,p);
    expect(r.forward && r.logical==B::LT && !router.native_held_mask(),"final owner releases LT exactly once");
    r=router.event(B::LT,1,S::Focus,p);
    expect(r.forward && !router.native_held_mask(),"unowned raw release may clear flushed native state");

    router.flush();router.transition(S::Gameplay);
    router.observe_slate(B::X,true);router.event(B::X,0,S::Gameplay,p);
    expect(router.transition(S::Menu)==bit(B::X),"pause retires gameplay attack");
    expect(router.quarantined(B::X),"held attack cannot navigate a menu");
    expect(router.observe_slate(B::X,true),"repeat remains quarantined");
    expect(router.observe_slate(B::X,false),"real release is swallowed after synthetic native release");
    expect(!router.quarantined(B::X),"fresh physical press re-arms");
    r=router.event(B::X,0,S::Menu,p);
    expect(r.forward && !r.gameplay && r.logical==B::X,"fresh menu key remains raw");
    expect(router.transition(S::Gameplay)==bit(B::X),"return to combat retires menu key");
    expect(!router.event(B::X,1,S::Gameplay,p).forward,"closing menu press cannot attack on release");

    router.flush();router.transition(S::Menu);
    router.observe_slate(B::A,true); // Slate consumes this; InputKey never sees it.
    expect(router.transition(S::Gameplay)==0,"consumed menu down has no native release to emit");
    expect(router.quarantined(B::A),"consumed menu down is still quarantined across context change");
    expect(router.observe_slate(B::A,false),"consumed closing press release cannot enter gameplay");

    router.flush();router.transition(S::Focus);
    router.observe_slate(B::RT,true);router.event(B::RT,0,S::Focus,p);
    expect(router.transition(S::Gameplay)==bit(B::RT),"Focus confirmation retires before gameplay");
    expect(!router.event(B::RT,2,S::Gameplay,p).forward,"held confirmation cannot repeat as takedown");
    router.observe_slate(B::RT,false);
    expect(router.event(B::RT,0,S::Gameplay,p).forward,"new RT press re-arms normally");
    router.flush();router.quarantine_held(bit(B::RB));router.transition(S::Gameplay);
    expect(!router.event(B::RB,0,S::Gameplay,p).forward,"old held key waits for release after native profile flush");
    expect(!router.event(B::RB,1,S::Gameplay,p).forward,"flush quarantine release emits no short item action");
    expect(router.event(B::RB,0,S::Gameplay,p).forward,"fresh press works after flush quarantine");
    router.flush();router.transition(S::Gameplay);
    p=c::Profile{};p.swap_binding(size_t(B::X),B::RT);
    const auto inflight=router.event(B::RT,0,S::Gameplay,p);
    expect(inflight.logical==B::X,"capture a remapped down before native dispatch");
    router.flush(); // Simulate a lifecycle callback inside the native handler.
    expect(router.recover_inflight_press(B::RT,inflight,S::Gameplay),"in-flight press re-pins after synchronous native flush");
    expect(router.transition(S::Gameplay)==0,"recovery restores the event's original scope");
    r=router.event(B::RT,1,S::Gameplay,c::Profile{});
    expect(r.forward && r.logical==B::X && !router.native_held_mask(),"post-flush release targets X instead of physical RT");
    expect(!router.recover_inflight_press(B::RT,inflight,S::Inactive),"inactive/disconnected press is never resurrected");

    // Check the native event stream against the router's ownership accounting
    // under profile changes, UI-consumed events, device flushes and every scope.
    std::mt19937 random(0x53494655);
    router.flush();S scope=S::Inactive;uint32_t native=0;
    for(int n=0;n<300000;++n) {
        const auto op=random()%12;
        if(op<3) {
            scope=S(random()%4);
            const auto release=router.transition(scope);
            expect((release&~native)==0,"transition only releases currently held native keys");
            native&=~release;
        } else if(op==3) {
            router.flush();native=0;scope=S::Inactive; // Native FlushPressedKeys clears both.
        } else if(op==4) {
            p.swap_binding(random()%c::button_count,B(random()%c::button_count));
        } else {
            const B key=B(random()%c::button_count);const int event=int(random()%3);
            // Some events go straight through InputKey, others have a Slate
            // pre-pass, and some are entirely consumed by a native menu.
            const bool through_slate=(random()%3)==0;
            bool swallowed=false;
            if(through_slate && event!=2)swallowed=router.observe_slate(key,event==0);
            const bool consumed=scope==S::Menu && (random()%4)==0;
            if(!swallowed && !consumed) {
                const auto route=router.event(key,event,scope,p);
                if(route.forward) {
                    if(event==0){expect(!(native&bit(route.logical)),"no duplicate native down");native|=bit(route.logical);}
                    if(event==1){expect(!(router.native_held_mask()&bit(route.logical)),"up cannot release another virtual target owner");native&=~bit(route.logical);}
                    if(event==2)expect(native&bit(route.logical),"repeat requires a live native owner");
                }
            }
        }
        expect(native==router.native_held_mask(),"native held state equals live target owners");
    }
    std::cout<<checks<<" press-router checks passed (300000 mixed transitions/events)\n";
}
