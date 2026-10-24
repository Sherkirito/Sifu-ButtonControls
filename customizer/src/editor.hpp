#pragma once
#include "profile.hpp"
#include "presets.hpp"
#include "catalog.hpp"

namespace button_controls::customizer {
enum class Navigation { Up,Down,Left,Right,Accept,Back,Save,Reset,CancelCapture,Tab,PreviousTab,Undo,Redo,RestoreSaved };
enum class Page { Bindings,Catalogue,Presets,Conflicts };
enum class Confirmation { None,Discard,OverwriteSlot };
inline constexpr std::array<const wchar_t*,4> page_names={L"改键",L"招式预览",L"方案",L"冲突"};
inline constexpr std::array<const wchar_t*,16> binding_names={
    L"推掌",L"扫腿",L"袭裆拳",L"上步正蹬",L"闪避高位 / 中位",L"闪避低位",
    L"基础 · 辅助 A / 抛掷",L"基础 · 倒地追击 / 辅助 B",L"基础 · 轻攻击",L"基础 · 重攻击",
    L"基础 · 防守",L"基础 · 专注",L"基础 · 物品",L"基础 · 腾挪 / 奔跑",L"基础 · 互动 / 翻越",L"基础 · 终结技"};

// Captures, presets, reset and history affect the draft. Only the runtime
// adapter can persist a slot or publish a new gameplay/hint revision.
class Editor {
public:
    bool opened=false,capturing=false,capture_ready=false,save_requested=false;
    Page page=Page::Bindings;
    Confirmation confirmation=Confirmation::None;
    size_t row=0,slot=0,catalog_row=0,preset_row=0,conflict_row=0;
    int slot_save_requested=-1;
    Profile draft,baseline;
    UserPresets users;
    std::wstring status;

    void open(const Profile& p,const UserPresets& library={}) {
        draft=baseline=p;users=library;opened=true;
        capturing=capture_ready=save_requested=false;slot_save_requested=-1;
        confirmation=Confirmation::None;page=Page::Bindings;
        row=slot=catalog_row=preset_row=conflict_row=0;undo_.clear();redo_.clear();viewport_.fill(0);
        status=L"修改只进草稿；X 保存并应用。LT 撤销，RT 重做。";
    }
    void close() {opened=capturing=capture_ready=save_requested=false;confirmation=Confirmation::None;slot_save_requested=-1;}
    bool dirty() const {return draft!=baseline;}
    size_t undo_count() const {return undo_.size();}
    size_t redo_count() const {return redo_.size();}
    void mark_saved() {baseline=draft;save_requested=false;status=L"已保存并应用；操作和提示使用同一份配置。";}
    std::vector<Issue> issues() const {return analyse(draft);}
    size_t error_count() const {
        const auto list=issues();return std::count_if(list.begin(),list.end(),[](const auto& i){return i.severity==Severity::Error;});
    }
    size_t warning_count() const {const auto list=issues();return list.size()-error_count();}
    size_t selected() const {
        return page==Page::Catalogue?catalog_row:page==Page::Presets?preset_row:page==Page::Conflicts?conflict_row:row;
    }
    size_t count() const {
        return page==Page::Catalogue?catalog_entries.size():page==Page::Presets?preset_count:page==Page::Conflicts?std::max(size_t(1),issues().size()):16;
    }
    size_t first_visible() const {return viewport_[size_t(page)];}
    void select(size_t index) {
        if(index>=count())return;
        if(page==Page::Bindings){row=index;slot=0;}
        if(page==Page::Catalogue)catalog_row=index;
        if(page==Page::Presets)preset_row=index;
        if(page==Page::Conflicts)conflict_row=index;
        auto& first=viewport_[size_t(page)];
        if(index<first)first=index;
        if(index>=first+9)first=index-8;
    }
    void show_page(Page next) {if(!capturing && confirmation==Confirmation::None){page=next;select(std::min(selected(),count()-1));}}
    void replace_draft(const Profile& replacement) {
        if(replacement==draft)return;
        remember(undo_,draft);redo_.clear();draft=replacement;
        select(std::min(selected(),count()-1));
    }
    void slot_saved(size_t index,const Profile& profile) {
        if(index>=users.size())return;
        users[index]={true,true,profile,{}};slot_save_requested=-1;
        status=L"已存入自定义 "+std::to_wstring(index+1)+L"；游戏当前配置未改变。X 才会应用草稿。";
    }
    void navigation(Navigation n) {
        if(!opened)return;
        if(capturing) {
            if(n==Navigation::CancelCapture){capturing=capture_ready=false;status=L"已取消录入。";}
            return;
        }
        if(confirmation!=Confirmation::None) {
            if(n==Navigation::Back || n==Navigation::CancelCapture){confirmation=Confirmation::None;status=L"已取消；草稿保留。";}
            else if(n==Navigation::Accept) {
                if(confirmation==Confirmation::Discard)close();
                else {confirmation=Confirmation::None;slot_save_requested=int(preset_row-builtin_preset_count);}
            }
            return;
        }
        if(n==Navigation::Back) {
            if(dirty()){confirmation=Confirmation::Discard;status=L"放弃未保存的修改并关闭？A 确认，B 返回编辑。";}
            else close();
            return;
        }
        if(n==Navigation::Tab || n==Navigation::PreviousTab) {
            show_page(Page((size_t(page)+(n==Navigation::Tab?1:3))%4));return;
        }
        if(n==Navigation::Undo || n==Navigation::Redo) {
            auto& from=n==Navigation::Undo?undo_:redo_;auto& to=n==Navigation::Undo?redo_:undo_;
            if(!from.empty()){remember(to,draft);draft=from.back();from.pop_back();select(std::min(selected(),count()-1));status=n==Navigation::Undo?L"已撤销草稿修改。":L"已重做草稿修改。";}
            return;
        }
        if(n==Navigation::RestoreSaved){replace_draft(baseline);status=L"已恢复本次已保存配置；仍可撤销。";return;}
        if(n==Navigation::Save){request_save();return;}
        if(n==Navigation::Up){select((selected()+count()-1)%count());return;}
        if(n==Navigation::Down){select((selected()+1)%count());return;}
        if(page==Page::Catalogue) {
            if(n==Navigation::Left)select((catalog_row+count()-10)%count());
            if(n==Navigation::Right)select((catalog_row+10)%count());
            if(n==Navigation::Accept) {
                const auto target=first_dependency(catalog_pattern(draft,catalog_row).dependencies);
                if(target<16){page=Page::Bindings;select(target);status=L"已定位关联功能；同组招式会联动。";}
                else status=L"该条目使用专注模式原生操作。";
            }
            return;
        }
        if(page==Page::Presets) {
            if(n==Navigation::Accept) {
                Profile temporary;const auto p=preset_profile(preset_row,users,temporary);
                if(p){replace_draft(*p);status=L"已载入草稿；LT 可撤销，X 保存后生效。";}
                else status=L"此位置没有可读取的方案；Y 可把当前草稿存入。";
            }
            if(n==Navigation::Reset) {
                if(preset_row<builtin_preset_count){status=L"内置方案保持原样；请选择下面的自定义存档位。";return;}
                if(!valid(validate(draft))){show_conflicts();return;}
                const auto index=preset_row-builtin_preset_count;
                if(users[index].exists){confirmation=Confirmation::OverwriteSlot;status=L"覆盖这个自定义方案？A 确认，B 取消；旧文件保留为 .bak。";}
                else slot_save_requested=int(index);
            }
            return;
        }
        if(page==Page::Conflicts) {
            if(n==Navigation::Accept) {
                const auto list=issues();if(conflict_row>=list.size())return;
                const auto issue=list[conflict_row];page=Page::Bindings;select(std::min(size_t(15),issue.row));slot=issue.slot;
                status=issue_text(issue.code);
            }
            return;
        }
        if(n==Navigation::Left && row<4)slot=(slot+2)%3;
        if(n==Navigation::Right && row<4)slot=(slot+1)%3;
        if(n==Navigation::Accept){capturing=true;capture_ready=false;status=L"松开全部按钮，再按目标键；View / Esc 取消。";}
        if(n==Navigation::Reset) {
            Profile replacement=draft;const Profile defaults;
            if(row<4)for(size_t i=0;i<3;++i)replacement.commands[row][i]=draft.physical(defaults.commands[row][i]);
            else if(row<6)replacement.avoid[row-4]=draft.physical(defaults.avoid[row-4]);
            else replacement.swap_binding(row-6,Button(row-6));
            replace_draft(replacement);status=L"当前行已恢复默认；LT 可撤销，X 保存后生效。";
        }
    }
    void release_barrier(uint32_t physically_held) {if(opened && capturing && !physically_held)capture_ready=true;}
    void capture(Button physical) {
        if(!opened || !capturing || !capture_ready || physical>=Button::Count)return;
        Profile replacement=draft;
        if(row<4)replacement.commands[row][slot]=physical;
        else if(row<6)replacement.avoid[row-4]=physical;
        else replacement.swap_binding(row-6,physical);
        replace_draft(replacement);capturing=capture_ready=false;
        status=L"已录入草稿；冲突页会实时列出问题。";
    }
    void show_conflicts() {page=Page::Conflicts;select(0);status=L"存在阻止保存的冲突；A 定位到对应按键。";}
    void request_save() {if(valid(validate(draft)))save_requested=true;else show_conflicts();}
    static std::wstring button(Button b) {return button_text(b);}
    static std::wstring binding_pattern(const Profile& p,size_t r,size_t selected_slot=99) {
        if(r<4) {
            std::wstring result;
            for(size_t i=0;i<3;++i){if(i)result+=L" > ";result+=(i==selected_slot?L"[":L"")+button(p.commands[r][i])+(i==selected_slot?L"]":L"");}
            return result;
        }
        if(r<6)return button(p.physical(Button::LB))+L" + "+button(p.avoid[r-4]);
        return r<16?button(p.layout[r-6]):L"?";
    }
    std::wstring title(size_t r) const {
        if(page==Page::Catalogue)return catalog_entries.at(r).title;
        if(page==Page::Presets)return preset_title(r,users);
        if(page==Page::Conflicts) {
            const auto list=issues();if(r>=list.size())return L"没有冲突";
            const auto& i=list[r];return std::wstring(i.severity==Severity::Error?L"冲突 · ":L"提醒 · ")+binding_names[std::min(size_t(15),i.row)];
        }
        return binding_names.at(r);
    }
    std::wstring pattern(size_t r,bool selected_row=false) const {
        if(page==Page::Catalogue) {
            const auto current=catalog_pattern(draft,r);
            return current.text+(current.operations?L" ("+std::to_wstring(current.operations)+L"步)":L"");
        }
        if(page==Page::Presets) {
            Profile temporary;const auto p=preset_profile(r,users,temporary);
            return !p?L"Y 存入":*p==draft?L"与草稿一致":L"A 载入草稿";
        }
        if(page==Page::Conflicts) {
            const auto list=issues();if(r>=list.size())return L"可以保存";
            return binding_pattern(draft,list[r].row);
        }
        return binding_pattern(draft,r,selected_row?slot:99);
    }
private:
    std::vector<Profile> undo_,redo_;
    std::array<size_t,4> viewport_{};
    static void remember(std::vector<Profile>& history,const Profile& profile) {
        if(history.size()>=32)history.erase(history.begin());
        history.push_back(profile);
    }
};
}
