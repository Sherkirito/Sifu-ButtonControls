#pragma once
#include "catalog_data.hpp"
#include "profile.hpp"
#include "legend.hpp"
#include "game_symbols.hpp"

namespace button_controls::customizer {
struct NativePattern {std::wstring text;uint16_t dependencies=0;size_t operations=0;bool supported=false;};
inline NativePattern native_pattern(const Profile& p,uint8_t action) {
    namespace g=game;
    const auto key=[&](Button b){return button_text(p.physical(b));};
    const auto dep=[](Button b){return uint16_t(1u<<(6+size_t(b)));};
    const auto found=std::find(command_actions.begin(),command_actions.end(),action);
    if(found!=command_actions.end()) {
        const auto row=size_t(found-command_actions.begin());std::wstring text;
        for(auto b:p.commands[row]){if(!text.empty())text+=L" > ";text+=button_text(b);}
        return {text,uint16_t(1u<<row),3,true};
    }
    switch(action) {
        case g::Action_AttackLight:return {key(Button::X),dep(Button::X),1,true};
        case g::Action_AttackHeavy:return {key(Button::Y),dep(Button::Y),1,true};
        case g::Action_AttackLightHold:return {L"长按 "+key(Button::X),dep(Button::X),1,true};
        case g::Action_AttackHeavyHold:return {L"长按 "+key(Button::Y),dep(Button::Y),1,true};
        case g::Action_AttackHeavyGuard:return {key(Button::LB)+L" + "+key(Button::Y),uint16_t(dep(Button::LB)|dep(Button::Y)),2,true};
        case g::Action_PushFromGrab:return {key(Button::X)+L" + "+key(Button::A),uint16_t(dep(Button::X)|dep(Button::A)),2,true};
        case g::Action_DodgeAttack:return {L"方向 + "+key(Button::L3),dep(Button::L3),2,true};
        case g::Action_LightRushAttack:return {L"奔跑 "+key(Button::L3)+L" + "+key(Button::X),uint16_t(dep(Button::L3)|dep(Button::X)),2,true};
        case g::Action_HeavyRushAttack:return {L"奔跑 "+key(Button::L3)+L" + "+key(Button::Y),uint16_t(dep(Button::L3)|dep(Button::Y)),2,true};
        case g::Action_Disengage:return {key(Button::X)+L" > "+key(Button::Y),uint16_t(dep(Button::X)|dep(Button::Y)),2,true};
        case g::Action_ThrowContextualObject:return {L"短按 "+key(Button::RB),dep(Button::RB),1,true};
        case g::Action_Parry:case g::Action_ParryFromDown:return {key(Button::LB),dep(Button::LB),1,true};
        case g::Action_PushbackCancel:return {key(Button::LB)+L" + "+button_text(p.avoid[0]),uint16_t(1u<<4),2,true};
        default:return {L"原生情境操作",0,0,false};
    }
}
inline NativePattern catalog_pattern(const Profile& p,size_t row) {
    if(row>=catalog_entries.size())return {};
    const auto& entry=catalog_entries[row];
    if(entry.focus)return {L"专注模式原生操作",0,0,true};
    NativePattern result;result.supported=entry.count>0;
    for(size_t i=0;i<entry.count;++i) {
        const auto part=native_pattern(p,entry.actions[i]);
        if(i)result.text+=L" > ";
        result.text+=part.text;result.dependencies|=part.dependencies;
        result.operations+=part.operations;result.supported&=part.supported;
    }
    return result;
}
inline size_t first_dependency(uint16_t mask) {
    for(size_t i=0;i<16;++i)if(mask&(1u<<i))return i;
    return 16;
}
}
