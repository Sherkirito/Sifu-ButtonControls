#pragma once
#include <array>
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>
#include <cstdint>

namespace button_controls::customizer {

// Serialized values are physical Xbox buttons. Menu/Focus have separate native
// profiles and never consult this layout. A layout is a permutation: changing
// one basic button swaps the previous occupant instead of silently duplicating it.
enum class Button : uint8_t { A, B, X, Y, LB, LT, RB, L3, R3, RT, Count };
inline constexpr size_t button_count = size_t(Button::Count);
inline constexpr std::array<const char*,button_count> button_names = {"A","B","X","Y","LB","LT","RB","L3","R3","RT"};
inline constexpr std::array<const wchar_t*,button_count> function_names = {
    L"辅助键 A / 抛掷前缀",L"倒地追击 / 辅助键 B",L"轻攻击（含蓄力、奔跑、反击）",L"重攻击（含蓄力、奔跑、反击）",
    L"防守 / 格挡",L"专注入口",L"物品短按 / 长按",L"腾挪 / 奔跑",L"互动 / 翻越 / 训练退出",L"终结技"};
inline constexpr std::array<const char*,4> command_ids = {"Palm","Sweep","Crotch","Kick"};
inline constexpr std::array<const wchar_t*,4> command_titles = {L"推掌",L"扫腿",L"袭裆拳（解锁后生效）",L"上步正蹬（解锁后生效）"};
inline constexpr std::array<uint8_t,4> command_actions = {109,112,110,113};

struct Profile {
    uint32_t schema = 1;
    std::array<Button,button_count> layout = {Button::A,Button::B,Button::X,Button::Y,Button::LB,Button::LT,Button::RB,Button::L3,Button::R3,Button::RT};
    std::array<std::array<Button,3>,4> commands = {{{Button::B,Button::A,Button::X},{Button::A,Button::B,Button::Y},
        {Button::B,Button::B,Button::X},{Button::B,Button::B,Button::Y}}};
    // Avoids retain two physical operations: hold guard, then press a button.
    // The first input follows the basic guard mapping; only the second is edited.
    std::array<Button,2> avoid = {Button::A,Button::B};
    bool operator==(const Profile& p) const { return schema==p.schema && layout==p.layout && commands==p.commands && avoid==p.avoid; }
    bool operator!=(const Profile& p) const { return !(*this==p); }
    Button physical(Button logical) const { return layout.at(size_t(logical)); }
    Button logical(Button physical_key) const {
        const auto found=std::find(layout.begin(),layout.end(),physical_key);
        return found==layout.end()?Button::Count:Button(found-layout.begin());
    }
    void swap_binding(size_t logical,Button physical_key) {
        if(logical>=button_count || physical_key>=Button::Count)return;
        const auto old_layout=layout;
        const auto previous=std::find(layout.begin(),layout.end(),physical_key);
        if(previous!=layout.end())std::iter_swap(layout.begin()+logical,previous);
        // Commands are serialized as physical keys, but swapping a base role
        // must carry its dependants with it. Otherwise swapping A and X would
        // turn an auxiliary prefix into a swallowed light/charged attack.
        const auto migrate=[&](Button& key) {
            const auto role=std::find(old_layout.begin(),old_layout.end(),key);
            if(role!=old_layout.end())key=layout[size_t(role-old_layout.begin())];
        };
        for(auto& command:commands)for(auto& key:command)migrate(key);
        for(auto& key:avoid)migrate(key);
    }
};

enum class Severity { Warning, Error };
struct Issue { Severity severity; std::string code; size_t row; size_t other; size_t slot=0; };
inline bool valid(const std::vector<Issue>& issues) {
    return std::none_of(issues.begin(),issues.end(),[](const auto& x){return x.severity==Severity::Error;});
}
inline std::vector<Issue> validate(const Profile& p) {
    std::vector<Issue> out;
    const auto issue=[&](Severity level,const char* code,size_t row=0,size_t other=0,size_t slot=0){out.push_back({level,code,row,other,slot});};
    if(p.schema!=1)issue(Severity::Error,"schema");
    std::array<bool,button_count> seen{};
    for(size_t i=0;i<button_count;++i) {
        const auto b=size_t(p.layout[i]);
        if(b>=button_count)issue(Severity::Error,"button",6+i);
        else if(seen[b])issue(Severity::Error,"layout_duplicate",6+i);
        else seen[b]=true;
    }
    if(!valid(out))return out;
    for(size_t i=0;i<p.commands.size();++i) {
        for(auto b:p.commands[i])if(b>=Button::Count)issue(Severity::Error,"button",i);
        if(!valid(out))continue;
        for(size_t j=0;j<i;++j)if(p.commands[i]==p.commands[j])issue(Severity::Error,"duplicate_command",i,j);
        for(size_t k=0;k<3;++k) {
            const auto logical=p.logical(p.commands[i][k]);
            // LT changes native context before the command resolver runs. It
            // cannot be borrowed without changing Focus behavior, which is out
            // of scope. Report it rather than creating an unreliable binding.
            if(logical==Button::LT)issue(Severity::Error,"focus_reserved",i,0,k);
            if(logical==Button::LB)issue(Severity::Error,"guard_reserved",i,0,k);
            if(k<2 && (logical==Button::RB || logical==Button::RT || logical==Button::L3 || logical==Button::R3))
                issue(Severity::Error,"utility_prefix",i,0,k);
            if(k<2 && (logical==Button::X || logical==Button::Y))issue(Severity::Error,"attack_prefix",i,0,k);
        }
        // A completed avoid cannot also be an intermediate command input.
        // Commands have priority only when their final press completes them.
        for(auto a:p.avoid)if(p.commands[i][0]==p.physical(Button::LB) && p.commands[i][1]==a)
            issue(Severity::Error,"avoid_prefix",i);
    }
    for(size_t i=0;i<2;++i) {
        const auto a=p.avoid[i];
        if(a>=Button::Count)issue(Severity::Error,"button",4+i);
        else if(a==p.physical(Button::LB) || a==p.physical(Button::LT))issue(Severity::Error,"avoid_modifier",4+i);
        else if(p.logical(a)==Button::RB || p.logical(a)==Button::L3 || p.logical(a)==Button::R3 || p.logical(a)==Button::RT)
            issue(Severity::Error,"avoid_utility",4+i);
    }
    if(p.avoid[0]==p.avoid[1])issue(Severity::Error,"duplicate_avoid",4,5);
    // Both Guard+light and Guard+heavy have registered native action families.
    // Reserve them even before a dependent skill/moveset becomes available.
    for(size_t i=0;i<2;++i)if(p.avoid[i]==p.physical(Button::Y))issue(Severity::Error,"resilient_collision",4+i);
    for(size_t i=0;i<2;++i)if(p.avoid[i]==p.physical(Button::X))issue(Severity::Error,"guard_light_collision",4+i);
    return out;
}

// Timing advisories do not make supported profiles impossible to save.
inline std::vector<Issue> analyse(const Profile& p) {
    auto result=validate(p);
    if(!valid(result))return result;
    for(size_t i=0;i<p.commands.size();++i)
        if(p.logical(p.commands[i][0])==Button::B)
            result.push_back({Severity::Warning,"ground_hold_prefix",i,0,0});
    return result;
}

inline const wchar_t* issue_text(const std::string& code) {
    if(code=="duplicate_command")return L"两招使用了相同的三步输入，请改开。";
    if(code=="layout_duplicate")return L"基础按键重复；请用交换功能改键。";
    if(code=="focus_reserved")return L"专注入口键需要保留，不能加入搓招。";
    if(code=="guard_reserved")return L"防守键需要保留为闪避修饰键，不能加入搓招。";
    if(code=="utility_prefix")return L"物品、终结、腾挪和互动键不能作为搓招前两步；可先交换基础按键。";
    if(code=="avoid_modifier" || code=="avoid_utility")return L"闪避的第二步请使用面键；基础按键可在下方交换。";
    if(code=="resilient_collision")return L"与防守＋重攻击的后期招式冲突，请换一个按键。";
    if(code=="guard_light_collision")return L"与原生防守＋轻攻击冲突；请先交换基础按键，再配置闪避。";
    if(code=="duplicate_avoid")return L"高位和低位闪避不能使用同一组合。";
    if(code=="attack_prefix")return L"前两步会占掉普攻和蓄力；请先把该键交换为辅助键。";
    if(code=="ground_hold_prefix")return L"第一步共用倒地追击键，请短按；按住 0.30 秒会触发追击。这条提醒不阻止保存。";
    return L"配置不合法，未应用；当前操作保持原样。";
}

inline Button parse_button(const std::string& name) {
    const auto it=std::find(button_names.begin(),button_names.end(),name);
    return it==button_names.end()?Button::Count:Button(it-button_names.begin());
}
inline std::string serialize(const Profile& p) {
    std::ostringstream s;
    s<<"; Sifu in-game customizer. Three inputs per command. Gameplay only.\nSchema=1\n";
    for(size_t i=0;i<button_count;++i)s<<"Key."<<button_names[i]<<'='<<button_names[size_t(p.layout[i])]<<'\n';
    for(size_t i=0;i<4;++i) {
        s<<command_ids[i]<<'=';
        for(size_t k=0;k<3;++k)s<<(k?",":"")<<button_names[size_t(p.commands[i][k])];
        s<<'\n';
    }
    s<<"AvoidHigh="<<button_names[size_t(p.avoid[0])]<<"\nAvoidLow="<<button_names[size_t(p.avoid[1])]<<'\n';
    return s.str();
}

// Parse into a temporary profile and commit only after full validation. Reject
// unknown, duplicate and missing fields, extra operations, and oversized input.
// This same parser is used for load/import and the editor's save verification.
inline bool parse(const std::string& source,Profile& destination,std::string& error) {
    if(source.size()>8192) {error="size";return false;}
    Profile candidate;
    std::array<bool,17> seen{};
    std::istringstream stream(source);std::string line;
    while(std::getline(stream,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty() || line[0]==';' || line[0]=='#')continue;
        const auto eq=line.find('=');
        if(eq==std::string::npos) {error="syntax";return false;}
        const auto key=line.substr(0,eq), data=line.substr(eq+1);
        size_t index=99;
        if(key=="Schema") {index=0;if(data!="1"){error="schema";return false;}}
        else if(key.rfind("Key.",0)==0) {
            const auto logical=parse_button(key.substr(4)), physical=parse_button(data);
            if(logical>=Button::Count || physical>=Button::Count){error="button";return false;}
            index=1+size_t(logical);candidate.layout[size_t(logical)]=physical;
        } else {
            auto command=std::find(command_ids.begin(),command_ids.end(),key);
            if(command!=command_ids.end()) {
                const auto row=size_t(command-command_ids.begin());index=11+row;
                std::istringstream tokens(data);std::string token;size_t count=0;
                while(std::getline(tokens,token,',')) {
                    const auto b=parse_button(token);
                    if(b>=Button::Count || count>=3){error="steps";return false;}
                    candidate.commands[row][count++]=b;
                }
                if(count!=3 || data.empty() || data.back()==','){error="steps";return false;}
            } else if(key=="AvoidHigh" || key=="AvoidLow") {
                const auto row=key=="AvoidHigh"?0u:1u;index=15+row;
                candidate.avoid[row]=parse_button(data);
                if(candidate.avoid[row]>=Button::Count){error="button";return false;}
            }
        }
        if(index>=seen.size()){error="unknown";return false;}
        if(seen[index]){error="duplicate_field";return false;}
        seen[index]=true;
    }
    if(std::find(seen.begin(),seen.end(),false)!=seen.end()){error="missing";return false;}
    const auto issues=validate(candidate);
    const auto fail=std::find_if(issues.begin(),issues.end(),[](const auto& i){return i.severity==Severity::Error;});
    if(fail!=issues.end()){error=fail->code;return false;}
    destination=candidate;error.clear();return true;
}
}
