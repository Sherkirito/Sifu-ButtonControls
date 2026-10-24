#pragma once
#include <array>
#include "control_state.hpp"
#include "game_symbols.hpp"

namespace button_controls::input_policy {
namespace g=game;

inline constexpr std::array<uint8_t,20> controlled_actions={
    g::Action_AttackLightBackFront,g::Action_AttackHeavyBackFront,
    g::Action_AttackLightFrontFront,g::Action_AttackHeavyFrontFront,
    g::Action_Avoid,g::Action_PushbackCancel,g::Action_AttackKnockdownHold,
    g::Action_PickUpObject,g::Action_ContextualActionObject,g::Action_ThrowContextualObject,
    g::Action_ThrowObject,g::Action_ThrowFromPickUp,g::Action_DropObject,g::Action_TakedownValidation,
    g::Action_PushFromGrab,g::Action_CounterGrab,g::Action_AttackHeavyFront,g::Action_AttackKnockdown,
    g::Action_StopTraining,g::Action_FlyMode
};
inline constexpr std::array<uint8_t,6> light_actions={g::Action_AttackLight,g::Action_AttackLightAlt,
    g::Action_AttackLightHold,g::Action_LightRushAttack,g::Action_AttackLightGuard,g::Action_Disengage};
inline constexpr std::array<uint8_t,6> heavy_actions={g::Action_AttackHeavy,g::Action_AttackHeavyAlt,
    g::Action_AttackHeavyHold,g::Action_HeavyRushAttack,g::Action_AttackHeavyGuard,g::Action_Disengage};
inline constexpr std::array<uint8_t,3> dash_actions={g::Action_Dodge,g::Action_Rush,g::Action_DodgeAttack};
inline constexpr std::array<uint8_t,2> interaction_actions={g::Action_Action,g::Action_Traversal};
inline constexpr std::array<uint8_t,2> focus_prepare_actions={g::Action_PrepFocus,g::Action_PrepFocusHold};
inline constexpr std::array<uint8_t,22> auxiliary_actions={
    g::Action_AttackLight,g::Action_AttackLightAlt,g::Action_AttackLightHold,
    g::Action_LightRushAttack,g::Action_AttackLightGuard,g::Action_Disengage,
    g::Action_AttackHeavy,g::Action_AttackHeavyAlt,g::Action_AttackHeavyHold,
    g::Action_HeavyRushAttack,g::Action_AttackHeavyGuard,
    g::Action_Dodge,g::Action_Rush,g::Action_DodgeAttack,g::Action_Action,g::Action_Traversal,
    g::Action_PrepFocus,g::Action_PrepFocusHold,g::Action_BulletTime,
    g::Action_Guard,g::Action_Parry,g::Action_ParryFromDown
};

template<std::size_t N> constexpr bool contains(const std::array<uint8_t,N>& actions,uint8_t action) {
    for(auto candidate:actions)if(candidate==action)return true;
    return false;
}

// The native held-object handler accepts both IDs, but each movable supplies
// its own allowed inputs. Weapons usually expose 100; bottles expose 103.
// Prefer the native carried-input order, emit exactly one, and never substitute
// the ground-throw skill (99), pickup, or drop when throwing is unsupported.
constexpr uint8_t held_throw_action(const uint8_t* allowed,std::size_t count) {
    if (!allowed) return g::Action_None;
    bool contextual=false;
    for (std::size_t i=0;i<count;++i) {
        if (allowed[i]==g::Action_ThrowFromPickUp) return g::Action_ThrowFromPickUp;
        contextual|=allowed[i]==g::Action_ThrowContextualObject;
    }
    return contextual?g::Action_ThrowContextualObject:g::Action_None;
}

constexpr bool blocked_original(uint8_t action,uint32_t owned,bool training,bool dialog_choice=false) {
    return ((owned&bit(Key::X))&&contains(light_actions,action)) ||
           ((owned&bit(Key::Y))&&contains(heavy_actions,action)) ||
           ((owned&bit(Key::L3))&&contains(dash_actions,action)) ||
           ((owned&bit(Key::R3))&&contains(interaction_actions,action)) ||
           ((owned&bit(Key::LT))&&contains(focus_prepare_actions,action)) ||
           (training&&contains(interaction_actions,action)) ||
           ((training||dialog_choice)&&action==g::Action_BulletTime);
}

// Focus, menus, cinematic skip, dialogue choices and replay navigation keep
// their own native profiles. Only a native gameplay pad is suppressed here.
constexpr bool is_combat_context(uint8_t native_context) {
    return native_context==0 || native_context==11; // BasicGameplay / BulletTime
}
constexpr bool editor_scope(uint8_t native_context,bool has_pawn,bool capturing) {
    return has_pawn && native_context==1 && !capturing;
}
constexpr bool customizer_hint_owned(uint8_t native_context,uint8_t action,bool preset) {
    if(!is_combat_context(native_context) && native_context!=13)return false;
    if(preset)return native_context!=13;
    return contains(controlled_actions,action) || contains(auxiliary_actions,action);
}
constexpr bool gamepad_hint_device(uint8_t explicit_type,bool current_gamepad) {
    return explicit_type==1 ? true : explicit_type==2 ? false : current_gamepad;
}
constexpr bool suppress_gameplay_pad(Context context,uint8_t native_context,bool controller) {
    return controller&&is_combat_context(native_context)&&context!=Context::Gameplay;
}
}
