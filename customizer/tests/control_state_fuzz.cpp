#include "../src/control_state.hpp"
#include <cstdlib>
#include <iostream>
#include <random>

using namespace button_controls;
void require(bool condition,unsigned step,const char* message) {
    if (!condition) { std::cerr<<"FAIL seed=0x53494655 step="<<step<<": "<<message<<'\n';std::exit(1); }
}

int main() {
    std::mt19937 random(0x53494655);
    ControlState state;
    uint32_t physical=0;
    uintptr_t equipped=0,rb_initial=0;
    unsigned rb_actions=0,b_actions=0;
    double time=0;
    constexpr unsigned steps=500000;
    for (unsigned step=0;step<steps;++step) {
        const auto previous=physical;
        time+=double(random()%150)/1000;
        const auto operation=random()%20;
        if (operation<13) {
            const auto key=static_cast<Key>(random()%static_cast<unsigned>(Key::Count));
            const bool down=(random()%2)!=0;
            if (key<Key::Motion) {
                if (down && !(physical&bit(key))) {
                    if (key==Key::RB) { rb_actions=0;rb_initial=equipped; }
                    if (key==Key::B) b_actions=0;
                }
                if (down) physical|=bit(key);else physical&=~bit(key);
            }
            state.key(key,down,time,equipped,random()%30!=0);
        } else if (operation==13) {
            state.context(static_cast<Context>(random()%4),time);
        } else if (operation==14) {
            state.training((random()%2)!=0);
        } else if (operation==15) {
            state.interrupted((random()%2)!=0);
        } else if (operation==16) {
            // Recovery may have missed releases and/or inherited held keys.
            state.interrupted(true);
            physical=random()%(bit(Key::Motion));
            if ((physical&bit(Key::RB)) && !(previous&bit(Key::RB))) { rb_actions=0;rb_initial=equipped; }
            if ((physical&bit(Key::B)) && !(previous&bit(Key::B))) b_actions=0;
            state.reconcile_held(physical);
        } else if (operation==17) {
            equipped=(random()%3)*0x10001u;
            state.tick(time,equipped);
        } else {
            state.tick(time,equipped);
        }
        const auto events=state.take_events();
        require(state.current_context()==Context::Gameplay || events.empty(),step,"non-gameplay produced a custom action");
        for (auto action:events) {
            if (action==Action::Pickup || action==Action::EnvironmentAttack || action==Action::DropObject || action==Action::ThrowObject) {
                require(++rb_actions<=1,step,"one physical RB press produced multiple actions");
                require(((previous|physical)&bit(Key::RB))!=0,step,"RB action without a physical press");
                require((action==Action::DropObject || action==Action::ThrowObject)==(rb_initial!=0),step,"RB changed intent across equipment states");
            }
            if (action==Action::GroundAttack) {
                require(++b_actions<=1,step,"one B press produced repeated ground attacks");
                require(((previous|physical)&bit(Key::B))!=0,step,"ground attack without a B press");
            }
        }
        const auto progress=state.training_exit_progress(time);
        require(progress>=0 && progress<=1,step,"training progress outside its normalized range");
        if (!state.in_training() || state.current_context()!=Context::Gameplay || !(physical&bit(Key::R3)) || state.owned(Key::R3))
            require(progress==0,step,"inactive or canceled training hold retained progress");
        require(state.held_mask()==physical,step,"key tracking disagreed with notifications/recovery snapshot");
    }
    std::cout<<steps<<" deterministic mixed-input events passed ownership/context invariants\n";
}
