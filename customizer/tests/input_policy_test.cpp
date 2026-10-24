#include <cstdlib>
#include <iostream>
#include "../src/input_policy.hpp"

using namespace button_controls;
namespace p=button_controls::input_policy;
namespace g=button_controls::game;
int checks=0;
void expect(bool value,const char* message) {
    ++checks;
    if (!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}

int main() {
    // Captured from actual native interaction components, not inferred from
    // the abstract ThrowObject event. The previous uniform 100 route passed
    // FSM tests while bottles rejected that input entirely.
    const uint8_t bottle_inputs[]={102,103,101,99,98};
    const uint8_t pipe_inputs[]={102,101,99,100};
    const uint8_t both_throw_inputs[]={103,101,100,102};
    const uint8_t no_held_throw[]={102,101,99,98};
    expect(p::held_throw_action(bottle_inputs,5)==g::Action_ThrowContextualObject,"a bottle uses its allowed contextual held throw");
    expect(p::held_throw_action(pipe_inputs,4)==g::Action_ThrowFromPickUp,"a weapon uses its allowed carried throw");
    expect(p::held_throw_action(both_throw_inputs,4)==g::Action_ThrowFromPickUp,"an item exposing both uses the native carried-input preference and emits one ID");
    expect(p::held_throw_action(no_held_throw,4)==g::Action_None,"ground throw, pickup and drop cannot substitute for unsupported held throw");
    expect(p::held_throw_action(nullptr,0)==g::Action_None,"missing held-item capabilities cannot trigger an unrelated action");
    expect(p::held_throw_action(bottle_inputs,0)==g::Action_None,"an empty capability list leaves the item untouched");
    // A consumed final button must suppress every native attack family,
    // including the frame in which the physical button has just been released.
    for (auto action:{g::Action_AttackLight,g::Action_AttackLightAlt,g::Action_AttackLightHold,
                      g::Action_LightRushAttack,g::Action_AttackLightGuard,g::Action_Disengage}) {
        expect(p::blocked_original(action,bit(Key::X),false),"an owned X cannot add a native attack");
    }
    for (auto action:{g::Action_AttackHeavy,g::Action_AttackHeavyAlt,g::Action_AttackHeavyHold,
                      g::Action_HeavyRushAttack,g::Action_AttackHeavyGuard,g::Action_Disengage}) {
        expect(p::blocked_original(action,bit(Key::Y),false),"an owned Y cannot add a native attack");
    }
    expect(!p::blocked_original(g::Action_AttackHeavy,bit(Key::X),false),"owning X leaves a later independent Y usable");
    expect(!p::blocked_original(g::Action_AttackLight,bit(Key::Y),false),"owning Y leaves a later independent X usable");
    for (auto action:{g::Action_Dodge,g::Action_Rush,g::Action_DodgeAttack})
        expect(p::blocked_original(action,bit(Key::L3),false),"a carried L3 cannot dash, sprint or start a dash attack");
    for (auto action:{g::Action_Action,g::Action_Traversal}) {
        expect(p::blocked_original(action,bit(Key::R3),false),"a carried R3 cannot interact after a transition");
        expect(p::blocked_original(action,0,true),"training defers interaction until a short R3 release");
        expect(!p::blocked_original(action,0,false),"ordinary scene interaction remains native");
    }
    for (auto action:{g::Action_PrepFocus,g::Action_PrepFocusHold})
        expect(p::blocked_original(action,bit(Key::LT),false),"a held LT cannot reopen Focus after a menu transition");
    expect(p::blocked_original(g::Action_BulletTime,0,true),"training AI's DpadLeft cannot also activate Bullet Time");
    expect(!p::blocked_original(g::Action_BulletTime,0,false),"the combat modifier remains available outside training");
    expect(p::blocked_original(g::Action_BulletTime,0,false,true),"dialogue choice owns DpadLeft over Bullet Time");
    expect(!p::blocked_original(g::Action_DialogSentence0,0,false,true),"dialogue choice is not consumed by the modifier guard");
    expect(!p::blocked_original(g::Action_AttackLight,0,false,true),"interactive dialogue preserves native decisions about ordinary combat");

    const auto all=(bit(Key::Count)-1);
    for (auto action:{g::Action_MenuAction,g::Action_MenuActionHold,g::Action_MenuCancel,g::Action_InGameMenu,
                      g::Action_LaunchFocusExecution,g::Action_DialogSentence0,g::Action_DialogSentence1,
                      g::Action_DialogSentence2,g::Action_SkipCinematic,g::Action_ReplayMenu,g::Action_TutorialInput}) {
        expect(!p::blocked_original(action,all,true),"ownership cannot suppress native menu, dialogue, cinematic or Focus confirmation");
        expect(!p::contains(p::controlled_actions,action),"native navigation and scene confirmations are not custom combat pulses");
    }
    expect(!p::suppress_gameplay_pad(Context::Gameplay,0,true),"foreground gameplay keeps native attacks enabled");
    expect(p::is_combat_context(11),"Bullet Time remains combat rather than being mistaken for a menu");
    expect(!p::suppress_gameplay_pad(Context::Gameplay,11,true),"Bullet Time supports the same combat controls");
    for (auto context:{Context::Inactive,Context::Menu,Context::Focus}) for(uint8_t native:{uint8_t{0},uint8_t{11}})
        expect(p::suppress_gameplay_pad(context,native,true),"a gameplay pad behind an overlay or in the background cannot run combat");
    for (auto context:{Context::Gameplay,Context::Inactive,Context::Menu,Context::Focus}) {
        for (uint8_t native:{uint8_t{1},uint8_t{6},uint8_t{7},uint8_t{10},uint8_t{13}})
            expect(!p::suppress_gameplay_pad(context,native,true),"non-gameplay profiles retain their own native input");
        expect(!p::suppress_gameplay_pad(context,0,false),"keyboard and mouse do not receive controller suppression");
    }
    bool unique_custom=true,unique_auxiliary=true,disjoint=true;
    for (std::size_t i=0;i<p::controlled_actions.size();++i)
        for (std::size_t j=0;j<i;++j)
            unique_custom&=p::controlled_actions[i]!=p::controlled_actions[j];
    for (std::size_t i=0;i<p::auxiliary_actions.size();++i) {
        disjoint&=!p::contains(p::controlled_actions,p::auxiliary_actions[i]);
        for (std::size_t j=0;j<i;++j)
            unique_auxiliary&=p::auxiliary_actions[i]!=p::auxiliary_actions[j];
    }
    expect(unique_custom,"custom native actions have unique ownership");
    expect(unique_auxiliary,"auxiliary native actions are unique");
    expect(disjoint,"native follow-up filtering cannot overwrite an injected custom pulse");
    std::cout<<checks<<" native input boundary checks passed\n";
}
