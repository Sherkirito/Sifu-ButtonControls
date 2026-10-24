#pragma once
#include "editor.hpp"
#include <cmath>

namespace button_controls::customizer {
struct EditorGeometry {
    float scale=0,x=0,y=0;
    static EditorGeometry canvas(float width,float height) {
        if(!std::isfinite(width) || !std::isfinite(height) || width<320 || height<240)return {};
        const float s=std::min(width/1500.f,height/980.f);
        return {s,(width-1400*s)*.5f,(height-900*s)*.5f};
    }
};
inline bool inside(float x,float y,float left,float top,float width,float height) {
    return x>=left && y>=top && x<left+width && y<top+height;
}
// Coordinates are in the same 1400x900 units as the native Canvas view.
inline void editor_click(Editor& editor,float x,float y) {
    if(!editor.opened)return;
    if(editor.confirmation!=Confirmation::None) {
        if(inside(x,y,430,492,250,48))editor.navigation(Navigation::Accept);
        if(inside(x,y,720,492,250,48))editor.navigation(Navigation::Back);
        return;
    }
    if(editor.capturing) {
        if(inside(x,y,1150,714,190,48))editor.navigation(Navigation::CancelCapture);
        return;
    }
    for(size_t i=0;i<4;++i)if(inside(x,y,40+200*float(i),86,190,42))editor.show_page(Page(i));
    if(inside(x,y,24,174,856,468)) {
        const auto index=editor.first_visible()+size_t((y-174)/52);
        if(index<editor.count()) {
            editor.select(index);
            if(editor.page==Page::Bindings && x>=500) {
                if(index<4)editor.slot=std::min(size_t(2),size_t((x-500)/105));
                editor.navigation(Navigation::Accept);
            }
        }
    }
    const std::array<float,6> positions={40,230,420,610,800,1150};
    const std::array<Navigation,6> actions={Navigation::Accept,Navigation::Save,Navigation::Reset,Navigation::Undo,Navigation::Redo,Navigation::Back};
    for(size_t i=0;i<positions.size();++i)if(inside(x,y,positions[i],714,i==5?190.f:180.f,48))editor.navigation(actions[i]);
}
inline std::vector<std::wstring> wrap_columns(const std::wstring& value,size_t columns) {
    std::vector<std::wstring> result;std::wstring line;size_t used=0;
    for(auto ch:value) {
        const size_t width=ch<128?1:2;
        if(ch==L'\n' || (!line.empty() && used+width>columns)){result.push_back(line);line.clear();used=0;}
        if(ch!=L'\n'){line+=ch;used+=width;}
    }
    if(!line.empty())result.push_back(line);
    return result;
}
inline std::wstring shortened(const std::wstring& value,size_t columns) {
    const auto lines=wrap_columns(value,columns);
    return lines.empty()?L"":lines[0]+(lines.size()>1?L"...":L"");
}
inline std::vector<std::wstring> editor_details(const Editor& editor) {
    std::vector<std::wstring> result;
    const auto add=[&](const std::wstring& text){const auto lines=wrap_columns(text,38);result.insert(result.end(),lines.begin(),lines.end());};
    if(editor.page==Page::Presets) {
        add(editor.title(editor.preset_row));
        if(editor.preset_row<builtin_preset_count)add(preset_notes[editor.preset_row]);
        else add(L"A 载入这个方案；Y 将当前草稿存入。存槽不会直接应用到战斗。" );
        Profile temporary;const auto p=preset_profile(editor.preset_row,editor.users,temporary);
        if(p) {
            for(size_t i=0;i<6;++i)add(std::wstring(binding_names[i])+L"  "+Editor::binding_pattern(*p,i));
            size_t changed=0;for(size_t i=0;i<16;++i)changed+=Editor::binding_pattern(*p,i)!=Editor::binding_pattern(editor.draft,i);
            add(L"载入后会改变 "+std::to_wstring(changed)+L" 行。X 保存前可撤销。" );
        } else add(L"空位或文件无效。原文件不会被自动覆盖。" );
        return result;
    }
    if(editor.page==Page::Conflicts) {
        const auto issues=editor.issues();
        if(editor.conflict_row>=issues.size()){add(L"没有阻止保存的冲突。游戏解锁、距离和情境条件仍由原生判断。" );return result;}
        const auto& issue=issues[editor.conflict_row];
        add(issue.severity==Severity::Error?L"冲突：需要修改后才能保存。":L"提醒：可以保存。" );
        add(issue_text(issue.code));
        add(std::wstring(binding_names[issue.row])+L"  "+Editor::binding_pattern(editor.draft,issue.row));
        if(issue.code=="duplicate_command")add(L"同时占用："+std::wstring(binding_names[issue.other])+L"  "+Editor::binding_pattern(editor.draft,issue.other));
        if(issue.row<4)add(L"定位：第 "+std::to_wstring(issue.slot+1)+L" 步。" );
        add(L"A 可直接定位到对应按键。" );return result;
    }
    size_t binding=editor.row;
    if(editor.page==Page::Catalogue) {
        const auto& entry=catalog_entries[editor.catalog_row];add(entry.title);add(entry.note);
        add(L"原输入数量："+std::to_wstring(entry.operations)+L" 步。" );
        const auto pattern=catalog_pattern(editor.draft,editor.catalog_row);
        if(entry.focus){add(L"专注内部保持原生；这里显示预览。" );return result;}
        add(pattern.text);binding=first_dependency(pattern.dependencies);
        add(L"A 定位关联功能；共用功能的招式会联动。" );
    } else {
        add(binding_names[binding]);
        add(L"已保存："+Editor::binding_pattern(editor.baseline,binding));
        add(L"当前草稿："+Editor::binding_pattern(editor.draft,binding));
        add(binding<4?L"固定三次按下；重复键必须先松开。":binding<6?L"按住防守键后按第二键，共两步。":L"交换基础功能会同步迁移相关出招；不会覆盖另一个功能。" );
        if(binding>=6)add(function_names[binding-6]);
    }
    if(binding<16) {
        size_t total=0;
        for(size_t i=0;i<catalog_entries.size();++i)total+=(catalog_pattern(editor.draft,i).dependencies&(uint16_t(1u)<<binding))!=0;
        add(L"关联招式 / 技能条目："+std::to_wstring(total));
    }
    return result;
}
}
