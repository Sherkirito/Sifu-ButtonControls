#pragma once
#include "profile.hpp"

namespace button_controls::customizer {
inline std::wstring button_text(Button key) {
    if(key>=Button::Count)return L"?";
    const std::string text=button_names[size_t(key)];return {text.begin(),text.end()};
}
inline std::wstring primary_legend(const Profile& p,Button physical) {
    switch(p.logical(physical)) {
        case Button::A:return L"辅助键 A / 抛掷前缀";
        case Button::B:return L"辅助键 B";
        case Button::X:return L"轻攻击 / 长按蓄力";
        case Button::Y:return L"重攻击 / 长按蓄力";
        case Button::LB:return L"格挡；+ "+button_text(p.avoid[0])+L" 闪高 / + "+button_text(p.avoid[1])+L" 闪低";
        case Button::LT:return L"按住：专注";
        case Button::RB:return L"短按：环境攻击 / 投掷";
        case Button::L3:return L"短按：腾挪";
        case Button::R3:return L"短按：互动 / 翻越";
        case Button::RT:return L"终结技";
        default:return L"";
    }
}
inline std::wstring secondary_legend(const Profile& p,Button physical) {
    switch(p.logical(physical)) {
        case Button::B:return L"长按：倒地追击";
        case Button::RB:return L"长按：拾取 / 丢弃";
        case Button::L3:return L"长按：奔跑";
        case Button::R3:return L"长按：训练退出";
        default:return L"";
    }
}

// Names and ActionType values are read from this build's original controller
// diagram. Focus-specific variants and every keyboard label are left native.
inline bool controller_legend(const Profile& p,std::string name,int action_type,std::wstring& result) {
    while(!name.empty() && name.back()>='0' && name.back()<='9')name.pop_back();
    if(!name.empty() && name.back()=='_')name.pop_back();
    Button physical=Button::Count;bool secondary=false;std::wstring lead;
    if(name=="XBOX_Move" && action_type==10){physical=Button::L3;lead=L"移动；按下：";}
    else if(name=="XBOX_Camera" && action_type==11){physical=Button::R3;lead=L"视角；按下：";}
    else if(name=="XBOX_LightAttack" && action_type==6)physical=Button::X;
    else if(name=="XBOX_HeavyAttack" && action_type==9)physical=Button::Y;
    else if(name=="XBOX_InteractVault" && action_type==8)physical=Button::A;
    else if(name=="XBOX_PickUpWeapon" && action_type==7)physical=Button::B;
    else if(name=="XBOX_AttackOnEnemyDown" && action_type==14){physical=Button::B;secondary=true;}
    else if(name=="XBOX_ThrowWeapon" && (action_type==3 || action_type==19)){physical=Button::RB;secondary=action_type==19;}
    else if(name=="XBOX_Dodge" && action_type==4)physical=Button::RT;
    else if(name=="XBOX_Run" && action_type==5){physical=Button::RT;secondary=true;}
    else if(name=="XBOX_Focus" && action_type==2)physical=Button::LT;
    else if(name=="XBOX_DeflectParry" && action_type==0)physical=Button::LB;
    else if(name=="XBOX_Guard" && action_type==1){result=L"";return true;}
    if(physical>=Button::Count)return false;
    result=lead+(secondary?secondary_legend(p,physical):primary_legend(p,physical));
    if(!lead.empty()) {
        const auto held=secondary_legend(p,physical);
        if(!held.empty())result+=L" / "+held;
    }
    return true;
}
}
