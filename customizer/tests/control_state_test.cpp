#include "../src/control_state.hpp"
#include "../src/input_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace button_controls;
static int checks = 0;
static void expect(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static bool only(ControlState& state, Action action) {
    const auto events = state.take_events();
    return events.size() == 1 && events[0] == action;
}
static void tap(ControlState& state, Key key, double time, uintptr_t equipped = 0) {
    state.key(key, true, time, equipped);
    state.key(key, false, time + 0.01, equipped);
}
static ControlState fresh() { ControlState result; result.context(Context::Gameplay, 0); return result; }

int main() {
    for (auto finish : {Key::X, Key::Y}) {
        auto state = fresh();
        tap(state, finish == Key::X ? Key::B : Key::A, 0.01);
        tap(state, finish == Key::X ? Key::A : Key::B, 0.08); tap(state, finish, 0.15);
        expect(only(state, finish == Key::X ? Action::Palm : Action::Sweep), "BAX / ABY command");
        state = fresh();
        tap(state, Key::B, 0.01); tap(state, Key::B, 0.08); tap(state, finish, 0.15);
        expect(only(state, finish == Key::X ? Action::Crotch : Action::Kick), "BB command");
    }
    {
        auto s = fresh();
        s.key(Key::B,true,.01); s.key(Key::A,true,.08); s.key(Key::X,true,.15);
        expect(only(s,Action::Palm), "overlapping physical holds do not turn BAX into throw");
        s.tick(.8); expect(s.take_events().empty(), "command-owned B does not ground attack");
        s.key(Key::B,false,.9); s.key(Key::B,true,1); s.tick(1.31);
        expect(only(s,Action::GroundAttack), "B re-arms after release");
    }
    {
        auto s = fresh();
        s.key(Key::LB,true,.01); s.key(Key::B,true,.02);
        expect(only(s,Action::AvoidLow), "LB+B avoids low");
        s.key(Key::LB,false,.03); s.tick(.8);
        expect(s.take_events().empty(), "releasing LB while B remains held cannot ground attack");
    }
    {
        auto s = fresh();
        s.key(Key::LB,true,.01); tap(s,Key::A,.02); tap(s,Key::B,.08); tap(s,Key::X,.15);
        const auto e=s.take_events();
        expect(e.size()==2 && e[0]==Action::AvoidHigh && e[1]==Action::AvoidLow, "guarded A/B never form attack command");
        s.key(Key::LB,false,.2); tap(s,Key::A,.25); tap(s,Key::B,.3); tap(s,Key::Y,.35);
        expect(only(s,Action::Sweep), "fresh sequence after guard works");
    }
    {
        auto s=fresh(); tap(s,Key::A,.01); tap(s,Key::B,.05); tap(s,Key::X,.7);
        expect(s.take_events().empty(), "expired command leaves X as ordinary attack");
        s=fresh(); tap(s,Key::A,.01); tap(s,Key::L3,.03); tap(s,Key::B,.05); tap(s,Key::X,.1);
        expect(s.take_events().empty(), "dash cancels partial command");
    }
    for(auto first:{Key::A,Key::X}) {
        auto s=fresh();s.key(first,true,.01);s.key(first==Key::A?Key::X:Key::A,true,.02);
        expect(only(s,Action::DirectionalThrow), "both orders of A+X preserve throw");
    }
    {
        auto s=fresh();s.key(Key::B,true,.01);s.tick(.32);
        expect(only(s,Action::GroundAttack), "long B ground attack");
        s.tick(.8);expect(s.take_events().empty(), "long B fires once");
        s=fresh();s.key(Key::B,true,.01);s.key(Key::LB,true,.1);s.key(Key::LB,false,.2);s.tick(.8);
        expect(s.take_events().empty(), "entering guard cancels B hold ownership");
        for (auto unrelated : {Key::L3,Key::R3,Key::LT,Key::Other}) {
            s=fresh();s.key(Key::B,true,.01);tap(s,unrelated,.1);s.tick(.8);
            expect(s.take_events().empty(), "unrelated action cancels pending B hold");
        }
    }
    for(uintptr_t equipment:{uintptr_t{0},uintptr_t{42}}) {
        auto s=fresh();s.key(Key::RB,true,.01,equipment);s.key(Key::RB,false,.15,equipment);
        expect(only(s,equipment?Action::ThrowObject:Action::EnvironmentAttack), "RB short release follows initial equipment state");
        s=fresh();s.key(Key::RB,true,.01,equipment);s.tick(.32,equipment);
        expect(only(s,equipment?Action::DropObject:Action::Pickup), "RB long action");
        s.tick(.8,equipment?0:42);s.key(Key::RB,false,.9,equipment?0:42);
        expect(s.take_events().empty(), "pickup/drop cannot cascade or throw on release");
    }
    {
        auto s=fresh();s.key(Key::RB,true,.01,42);s.tick(.32,0);s.key(Key::RB,false,.4,0);
        expect(s.take_events().empty(), "disarm during RB press cancels pending action");
        s=fresh();s.key(Key::RB,true,.01,0);s.key(Key::RB,false,.15,42);
        expect(s.take_events().empty(), "external pickup cannot turn short RB into weapon throw");
        s=fresh();s.key(Key::RB,true,.01,0);s.key(Key::RB,false,.32,0);
        expect(only(s,Action::Pickup), "long RB release between frames still picks up once");
        s=fresh();s.key(Key::B,true,.01);s.key(Key::B,false,.32);
        expect(only(s,Action::GroundAttack), "long B release between frames still attacks once");
    }
    for(auto context:{Context::Menu,Context::Focus,Context::Inactive}) {
        auto s=fresh();tap(s,Key::A,.01);s.context(context,.03);tap(s,Key::B,.05);tap(s,Key::X,.08);
        expect(s.take_events().empty(), "non-gameplay contexts pass through without custom actions");
        s.key(Key::RT,true,.1);s.context(Context::Gameplay,.2);s.tick(.21);
        expect(s.take_events().empty(), "RT held across context boundary cannot takedown");
        s.key(Key::RT,false,.3);s.key(Key::RT,true,.4);
        expect(only(s,Action::Takedown), "RT re-arms on fresh gameplay press");
    }
    {
        auto s=fresh();tap(s,Key::RT,.01);expect(only(s,Action::Takedown),"RT takedown");
        s.key(Key::RT,true,.1);s.key(Key::RT,true,.2);expect(only(s,Action::Takedown),"repeated press notifications ignored");
        s=fresh();tap(s,Key::A,.01);tap(s,Key::B,.03);s.interrupted();tap(s,Key::Y,.05);
        expect(s.take_events().empty(),"hit interrupts partial command");
        s=fresh();s.key(Key::LT,true,.01);tap(s,Key::RT,.02);
        expect(s.take_events().empty(),"LT+RT cannot takedown before focus context enters");
    }
    for (auto unrelated : {Key::A,Key::X,Key::Y,Key::RB,Key::RT}) {
        auto s=fresh();s.key(Key::B,true,.01);tap(s,unrelated,.1);
        s.take_events();s.tick(.8);s.key(Key::B,false,.9);
        expect(s.take_events().empty(),"an attack, RB or RT cancels a previous B hold");
    }
    for (auto unrelated : {Key::A,Key::B,Key::X,Key::Y,Key::LB,Key::LT,Key::L3,Key::R3,Key::RT,Key::Other}) {
        for (uintptr_t equipped : {uintptr_t{0},uintptr_t{42}}) {
            auto s=fresh();s.key(Key::RB,true,.01,equipped);
            tap(s,unrelated,.1,equipped);s.take_events();
            s.tick(.8,equipped);s.key(Key::RB,false,.9,equipped);
            const auto e=s.take_events();
            bool rb_action=false;
            for (auto action:e) rb_action|=action==Action::Pickup || action==Action::EnvironmentAttack || action==Action::ThrowObject || action==Action::DropObject;
            expect(!rb_action,"another combat action cancels a pending RB hold/release");
        }
    }
    for (uintptr_t initial : {uintptr_t{0},uintptr_t{42}}) {
        const auto changed=initial?0:42;
        for (double release : {.2,.8}) {
            auto s=fresh();s.key(Key::RB,true,.01,initial);
            s.tick(.08,changed);s.tick(.12,initial);s.key(Key::RB,false,release,initial);
            expect(s.take_events().empty(),"a transient equip/disarm cancels RB even if the initial object returns");
        }
    }
    for (auto finish : {Key::X,Key::Y}) {
        auto s=fresh();tap(s,finish==Key::X?Key::B:Key::A,.01);tap(s,Key::Motion,.04);
        tap(s,finish==Key::X?Key::A:Key::B,.08);tap(s,Key::Motion,.11);tap(s,finish,.15);
        expect(only(s,finish==Key::X?Action::Palm:Action::Sweep),"movement/stick-direction notifications do not cancel BAX / ABY commands");
    }
    {
        auto s=fresh();s.key(Key::RB,true,.01,42);tap(s,Key::Motion,.1,42);s.tick(.32,42);
        expect(only(s,Action::DropObject),"walking/aiming does not cancel the RB hold");
    }
    {
        auto s=fresh();s.key(Key::L3,true,.01);s.key(Key::R3,true,.02);s.key(Key::RT,true,.03);s.take_events();
        s.interrupted(true);
        expect(s.owned(Key::L3) && s.owned(Key::R3) && s.owned(Key::RT),"device handoff blocks every previously held command key");
        tap(s,Key::B,.1);tap(s,Key::A,.15);tap(s,Key::X,.2);
        expect(only(s,Action::Palm),"a fresh first command after device synchronization works");
    }
    for (auto unrelated : {Key::L3,Key::R3,Key::LB,Key::LT,Key::RB,Key::RT,Key::Y,Key::Other}) {
        auto s=fresh();s.key(Key::A,true,.01);tap(s,unrelated,.08);s.take_events();tap(s,Key::X,.15);
        expect(s.take_events().empty(),"an interrupted held A prefix cannot later become an accidental A+X throw");
    }
    {
        auto s=fresh();tap(s,Key::A,.01);tap(s,Key::B,.08);tap(s,Key::X,.15);
        expect(s.take_events().empty(),"retired ABX no longer emits a palm");
        s=fresh();tap(s,Key::B,.01);tap(s,Key::A,.08);tap(s,Key::Y,.15);
        expect(s.take_events().empty(),"BAY cannot become the ABY sweep");
        s=fresh();s.key(Key::B,true,.01);s.key(Key::A,true,.20);s.tick(.39);s.key(Key::X,true,.41);
        expect(only(s,Action::Palm),"A cancels B ground hold while BAX prefix remains valid");
        s.tick(1);s.key(Key::B,false,1.01);s.key(Key::A,false,1.02);s.key(Key::X,false,1.03);
        expect(s.take_events().empty(),"held B/A/X and their releases cannot append ground attack or throw");
        s=fresh();tap(s,Key::B,.01);tap(s,Key::A,.08);tap(s,Key::X,.50);
        expect(s.take_events().empty(),"BAX adjacent gap expires");
        s=fresh();s.key(Key::B,true,.01);s.tick(.32);s.take_events();tap(s,Key::A,.34);tap(s,Key::X,.40);
        expect(s.take_events().empty(),"already activated B hold cannot become a new BAX prefix");
        s=fresh();tap(s,Key::B,.01);tap(s,Key::A,.08);tap(s,Key::B,.15);tap(s,Key::Y,.22);
        expect(only(s,Action::Sweep),"latest AB suffix can select sweep without cross-order confusion");
        s=fresh();tap(s,Key::A,.01);tap(s,Key::B,.08);tap(s,Key::A,.15);tap(s,Key::X,.22);
        expect(only(s,Action::Palm),"latest BA suffix can select palm");
    }
    {
        auto s=fresh();s.training(true);s.key(Key::R3,true,.01);
        expect(s.take_events().empty(),"training R3 does not interact on initial press");
        expect(s.training_exit_progress(.51)>.49 && s.training_exit_progress(.51)<.51,"training exit progress uses a one-second physical hold");
        s.key(Key::R3,false,.60);
        expect(only(s,Action::Interact),"short training R3 interacts only on release");
        expect(s.training_exit_progress(.61)==0,"R3 release resets the exit progress");
        s.key(Key::R3,true,.70);
        expect(s.training_exit_progress(1.71)==1,"long R3 reaches training exit threshold");
        s.key(Key::R3,false,1.8);
        expect(s.take_events().empty(),"long training R3 cannot interact or vault on release");
        s=fresh();s.training(true);tap(s,Key::A,.01);tap(s,Key::A,.1);
        expect(s.training_exit_progress(2)==0,"A cannot start training exit");
        s=fresh();tap(s,Key::R3,.01);
        expect(s.take_events().empty(),"R3 outside training remains native rather than synthesized");
    }
    for (auto other : {Key::A,Key::B,Key::X,Key::Y,Key::LB,Key::LT,Key::RB,Key::L3,Key::RT,Key::Other}) {
        auto s=fresh();s.training(true);s.key(Key::R3,true,.01);tap(s,other,.2);s.take_events();
        expect(s.training_exit_progress(1.2)==0,"another combat action cancels pending training exit");
        s.key(Key::R3,false,1.3);
        expect(s.take_events().empty(),"canceled exit cannot emit an interaction on release");
    }
    for (auto context : {Context::Menu,Context::Focus,Context::Inactive}) {
        auto s=fresh();s.training(true);s.key(Key::R3,true,.01);s.context(context,.2);s.context(Context::Gameplay,.4);
        expect(s.training_exit_progress(1.2)==0,"training exit cannot leak through menu/focus/window transitions");
        s.key(Key::R3,false,1.3);expect(s.take_events().empty(),"returning with R3 held cannot interact on release");
        s.key(Key::R3,true,1.4);expect(s.training_exit_progress(2.41)==1,"a fresh R3 press rearms training exit");
    }
    {
        auto s=fresh();s.training(true);s.key(Key::R3,true,.01);tap(s,Key::Motion,.2);
        expect(s.training_exit_progress(1.02)==1,"movement and camera input do not cancel training exit hold");
        s.interrupted();expect(s.training_exit_progress(1.1)==0,"being hit cancels training exit");
        s.key(Key::R3,false,1.2);expect(s.take_events().empty(),"hit-canceled R3 cannot interact on release");
        s=fresh();s.key(Key::R3,true,.01);s.training(true);
        expect(s.training_exit_progress(2)==0,"entering training with R3 held cannot exit");
        s.key(Key::R3,false,2.1);expect(s.take_events().empty(),"held R3 at training entry cannot interact");
    }
    {
        auto s=fresh();tap(s,Key::B,.01);s.key(Key::A,true,.08);tap(s,Key::X,.50);
        expect(s.take_events().empty(),"an expired BA prefix cannot become an accidental held A+X throw");
        s=fresh();s.key(Key::A,true,.01);tap(s,Key::B,.08);tap(s,Key::X,.50);
        expect(s.take_events().empty(),"an expired AB prefix cannot become an accidental held A+X throw");
        s=fresh();s.key(Key::A,true,.01);tap(s,Key::X,.50);
        expect(only(s,Action::DirectionalThrow),"a genuine unprefixed held A+X chord still works after a pause");
        s=fresh();s.key(Key::Other,true,.01);tap(s,Key::A,.02);s.key(Key::Other,true,.04);tap(s,Key::B,.06);tap(s,Key::Y,.09);
        expect(s.take_events().empty(),"overlapping distinct unmapped buttons cannot hide a sequence interruption");
    }
    for(uintptr_t initial:{uintptr_t{0},uintptr_t{42}}) {
        auto s=fresh();s.key(Key::RB,true,.01,initial);tap(s,Key::Motion,.1,initial?0:42);s.tick(.15,initial);s.key(Key::RB,false,.2,initial);
        expect(s.take_events().empty(),"equipment changes observed on motion notifications cancel the RB press");
    }
    {
        auto s=fresh();s.key(Key::B,true,.01);tap(s,Key::A,.35);
        expect(only(s,Action::GroundAttack),"a physically completed B hold is not lost when another key arrives before the next game frame");
        s=fresh();s.key(Key::RB,true,.01);tap(s,Key::X,.35);
        expect(only(s,Action::Pickup),"a physically completed RB hold is not lost when another key arrives before the next game frame");
        s=fresh();s.key(Key::B,true,.01);s.key(Key::A,true,.08);s.key(Key::X,true,.15);
        s.take_events();s.context(Context::Menu,.2);s.context(Context::Gameplay,.3);s.key(Key::X,false,.4);s.key(Key::A,false,.41);s.key(Key::B,false,.42);
        expect(s.take_events().empty(),"releasing a command held across menus cannot append a combat action");
    }
    for(auto key:{Key::RB,Key::B,Key::R3}) {
        auto s=fresh();s.training(true);s.key(key,true,.01);s.interrupted(true);s.reconcile_held(0);s.tick(2);
        expect(s.take_events().empty(),"a missing physical release during recovery cannot synthesize a long or short action");
        expect(!s.held(key)&&!s.owned(key),"recovery clears a released phantom key");
    }
    for (auto first:{Key::A,Key::B}) for(auto final:{Key::X,Key::Y}) {
        if(first==Key::A && final==Key::X)continue;
        auto s=fresh();tap(s,first,.01);s.key(Key::B,true,.08);s.tick(.39);tap(s,final,.43);
        expect(only(s,first==Key::A?Action::Sweep:final==Key::X?Action::Crotch:Action::Kick),"a held AB/BB prefix keeps the full command window without a premature ground attack");
        s.tick(.8);s.key(Key::B,false,.9);
        expect(s.take_events().empty(),"a command-prefix B cannot append a ground attack on timeout or release");
    }
    {
        auto s=fresh();tap(s,Key::A,.01);s.key(Key::B,true,.08);s.tick(.8);s.key(Key::B,false,.9);
        expect(s.take_events().empty(),"an abandoned AB command cannot silently become a B hold");
        s.key(Key::B,true,1);s.tick(1.31);
        expect(only(s,Action::GroundAttack),"a fresh standalone B still rearms the normal ground hold");
        s=fresh();s.reconcile_held(~uint32_t{0});
        expect(s.held_mask()==bit(Key::Motion)-1,"recovery accepts only the ten physical command keys");
    }
    {
        auto s=fresh();s.key(Key::RB,true,.01);s.interrupted(true);s.reconcile_held(bit(Key::RB));s.tick(1);
        expect(s.take_events().empty(),"a button still held after reconnect remains blocked");
        s.key(Key::RB,false,1.1);expect(s.take_events().empty(),"releasing a held reconnect button cannot throw or pick up");
        tap(s,Key::RB,1.2);expect(only(s,Action::EnvironmentAttack),"a fresh press works after reconnect and release");
        s=fresh();s.reconcile_held(bit(Key::RB));s.tick(1);s.key(Key::RB,false,1.1);
        expect(s.take_events().empty(),"an unobserved pre-existing physical hold cannot become a new action");
        s=fresh();tap(s,Key::B,.01);tap(s,Key::A,.08);s.reconcile_held(0);tap(s,Key::X,.15);
        expect(only(s,Action::Palm),"recovery does not erase a fresh properly released sequence");
        s=fresh();tap(s,Key::B,.01);tap(s,Key::A,.08);tap(s,Key::X,.15);s.reconcile_held(0);
        expect(only(s,Action::Palm),"recovery does not erase a newly completed command");
    }
    for (auto closing:{Key::A,Key::B,Key::X,Key::Y,Key::LB,Key::LT,Key::RB,Key::L3,Key::R3,Key::RT}) {
        auto s=fresh();s.training(true);s.key(closing,true,.01,0,false);s.tick(1.5);
        expect(s.owned(closing),"a press that closes UI remains owned until release");
        expect(s.take_events().empty()&&s.training_exit_progress(1.5)==0,"a closing UI press cannot attack, pick up, takedown or exit training");
        s.key(closing,false,1.6);
        expect(s.take_events().empty(),"releasing a closing UI press cannot append an action");
    }
    {
        auto s=fresh();s.key(Key::A,true,.01,0,false);s.key(Key::A,false,.02);tap(s,Key::B,.08);tap(s,Key::Y,.15);
        expect(s.take_events().empty(),"the A that confirms a shrine/menu cannot become the next sweep prefix");
        s=fresh();s.key(Key::RT,true,.01,0,false);s.key(Key::RT,false,.02);tap(s,Key::RT,.1);
        expect(only(s,Action::Takedown),"a fresh RT after Focus confirmation works once");
        s=fresh();s.key(Key::RB,true,.01);s.key(Key::A,true,.4,0,false);s.key(Key::RB,false,.5);
        expect(s.take_events().empty(),"a native UI closure cancels an old hold before elapsed-time processing");
        s=fresh();tap(s,Key::B,.01);tap(s,Key::A,.08);s.key(Key::Other,true,.1,0,false);tap(s,Key::X,.15);
        expect(s.take_events().empty(),"a UI interaction between game frames cancels an old command prefix");
    }
    for (double base:{0.,1000.,1000000.,100000000.}) {
        auto s=fresh();tap(s,Key::B,base+.01);tap(s,Key::A,base+.08);tap(s,Key::X,base+.48);
        expect(only(s,Action::Palm),"the inclusive 400ms command boundary is stable after long system uptimes");
        s=fresh();s.key(Key::B,true,base+.01);s.tick(base+.31);
        expect(only(s,Action::GroundAttack),"the 300ms B hold boundary is stable after long system uptimes");
        s=fresh();s.key(Key::RB,true,base+.01);s.key(Key::RB,false,base+.31);
        expect(only(s,Action::Pickup),"RB released at exactly 300ms is a long press, not a short attack");
        s=fresh();s.training(true);s.key(Key::R3,true,base+.01);
        expect(s.training_exit_progress(base+1.01)==1,"the exact one-second exit boundary is stable after long system uptimes");
        s.key(Key::R3,false,base+1.01);
        expect(s.take_events().empty(),"release at exactly one second cannot become short R3 interaction");
    }
    {
        auto s=fresh();s.key(Key::RT,true,.01);s.key(Key::LT,true,.02);
        expect(s.take_events().empty(),"LT cancels an RT-first takedown still queued in the same game frame");
        s.key(Key::RT,false,.03);s.key(Key::LT,false,.04);tap(s,Key::RT,.1);
        expect(only(s,Action::Takedown),"a later independent takedown rearms after both triggers release");
        s=fresh();tap(s,Key::RT,.01);s.key(Key::LT,true,.03);
        expect(s.take_events().empty(),"a fast RT tap queued before LT still yields to Focus");
        s=fresh();tap(s,Key::RT,.01);
        expect(only(s,Action::Takedown),"RT outside a Focus transition keeps its immediate takedown");
    }
    // Ablation exposed gaps between FSM ownership and native follow-up filters.
    // Exercise the real policy with masks produced by the real state machine.
    for (auto finish:{Key::X,Key::Y}) {
        auto s=fresh();s.key(finish==Key::X?Key::B:Key::A,true,.01);
        s.key(finish==Key::X?Key::A:Key::B,true,.08);s.key(finish,true,.15);s.take_events();
        const auto attack=finish==Key::X?game::Action_AttackLightHold:game::Action_AttackHeavyHold;
        expect(input_policy::blocked_original(attack,s.owned_mask(),false),"a held final command key cannot reappear as a native charged attack on following frames");
    }
    {
        auto s=fresh();s.key(Key::LB,true,.01);s.key(Key::A,true,.02);s.take_events();
        s.key(Key::LB,false,.05);tap(s,Key::X,.10);
        expect(s.take_events().empty(),"an A still held after avoiding cannot become an unintended late directional throw");
        s=fresh();s.reconcile_held(bit(Key::L3)|bit(Key::R3)|bit(Key::LT));
        for (auto action:{game::Action_Rush,game::Action_Action,game::Action_PrepFocusHold})
            expect(input_policy::blocked_original(action,s.owned_mask(),false),"an unobserved key held through recovery cannot start its native action");
        s.reconcile_held(0);
        expect(!input_policy::blocked_original(game::Action_Rush,s.owned_mask(),false),"a released recovery key no longer suppresses a later native sprint");
    }
    std::cout << checks << " behavior checks passed\n";
}
