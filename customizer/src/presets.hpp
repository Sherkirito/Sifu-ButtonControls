#pragma once
#include "profile.hpp"

namespace button_controls::customizer {
inline constexpr size_t builtin_preset_count=4,user_preset_count=3;
inline constexpr size_t preset_count=builtin_preset_count+user_preset_count;
inline constexpr std::array<const wchar_t*,builtin_preset_count> preset_names={
    L"BAX · 当前习惯",L"ABX · 经典顺序",L"同前缀 · 轻重成对",L"肩键前缀 · 面键收招"};
inline constexpr std::array<const wchar_t*,builtin_preset_count> preset_notes={
    L"推掌 BAX，扫腿 ABY；保留你现在的操作。",
    L"推掌 ABX，扫腿 ABY；两招共享 AB 前缀。",
    L"推掌 BAX、扫腿 BAY；袭裆拳 AAX、正蹬 AAY。",
    L"辅助 A/B 移至 LB/RB；防守在 A，物品在 B。请先看完整预览。"};
inline Profile builtin_preset(size_t index) {
    Profile p;
    if(index==1)p.commands[0]={Button::A,Button::B,Button::X};
    if(index==2)p.commands={{{Button::B,Button::A,Button::X},{Button::B,Button::A,Button::Y},
                            {Button::A,Button::A,Button::X},{Button::A,Button::A,Button::Y}}};
    if(index==3){p.swap_binding(size_t(Button::A),Button::LB);p.swap_binding(size_t(Button::B),Button::RB);}
    return p;
}
struct UserPreset {bool exists=false,readable=false;Profile profile;std::string error;};
using UserPresets=std::array<UserPreset,user_preset_count>;
inline const Profile* preset_profile(size_t index,const UserPresets& users,Profile& temporary) {
    if(index<builtin_preset_count){temporary=builtin_preset(index);return &temporary;}
    index-=builtin_preset_count;
    return index<users.size() && users[index].readable?&users[index].profile:nullptr;
}
inline std::wstring preset_title(size_t index,const UserPresets& users) {
    if(index<builtin_preset_count)return preset_names[index];
    const auto slot=index-builtin_preset_count;
    if(slot>=users.size())return L"?";
    return L"自定义 "+std::to_wstring(slot+1)+(users[slot].readable?L"":users[slot].exists?L" · 文件无效":L" · 空位");
}
inline std::wstring profile_title(const Profile& p,const UserPresets& users) {
    for(size_t i=0;i<preset_count;++i){Profile temporary;const auto candidate=preset_profile(i,users,temporary);if(candidate && *candidate==p)return preset_title(i,users);}
    return L"自定义草稿";
}
}
