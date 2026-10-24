#include "../src/editor_view.hpp"
#include "../src/control_state.hpp"
#include <iostream>
#include <cstdlib>
#include <limits>
using namespace button_controls;
namespace c=button_controls::customizer;
static size_t checks=0;
static void expect(bool condition,const char* message){++checks;if(!condition){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}}
static void tap(ControlState& state,Key key,double at){state.key(key,true,at);state.key(key,false,at+.01);}
int main() {
    const std::array<Action,4> actions={Action::Palm,Action::Sweep,Action::Crotch,Action::Kick};
    for(size_t i=0;i<c::builtin_preset_count;++i) {
        const auto p=c::builtin_preset(i);expect(c::valid(c::validate(p)),"each shipped preset passes structural conflicts");
        c::Profile roundtrip;std::string error;
        expect(c::parse(c::serialize(p),roundtrip,error) && roundtrip==p,"preset strict format round trip");
        for(size_t j=0;j<4;++j) {
            ControlState state;state.configure(p);state.context(Context::Gameplay,0);
            for(size_t k=0;k<3;++k)tap(state,Key(p.logical(p.commands[j][k])),.01+.08*k);
            const auto events=state.take_events();expect(events.size()==1 && events[0]==actions[j],"preset emits intended command exactly once");
        }
        for(size_t j=0;j<2;++j) {
            ControlState state;state.configure(p);state.context(Context::Gameplay,0);
            state.key(Key::LB,true,.01);tap(state,Key(p.logical(p.avoid[j])),.08);
            const auto events=state.take_events();expect(events.size()==1 && events[0]==(j?Action::AvoidLow:Action::AvoidHigh),"preset retains both avoids");
        }
        for(size_t j=0;j<c::catalog_entries.size();++j)expect(c::catalog_pattern(p,j).operations==c::catalog_entries[j].operations,"preset retains original operation counts");
    }
    const c::Profile original;const auto classic=c::builtin_preset(1);
    c::Editor editor;editor.open(original);
    for(size_t i=0;i<4;++i){expect(size_t(editor.page)==i,"all four tabs accessible");editor.navigation(c::Navigation::Tab);}
    expect(editor.page==c::Page::Bindings,"tabs wrap");editor.navigation(c::Navigation::PreviousTab);
    expect(editor.page==c::Page::Conflicts,"previous tab reverses navigation");
    expect(editor.error_count()==0 && editor.warning_count()>0,"reminders separate from conflicts");
    editor.navigation(c::Navigation::Save);expect(editor.save_requested,"warnings do not block saving");
    editor.open(original);editor.show_page(c::Page::Presets);editor.select(1);editor.navigation(c::Navigation::Accept);
    expect(editor.draft==classic && editor.baseline==original && !editor.save_requested,"preset load changes draft only");
    expect(editor.dirty() && editor.undo_count()==1,"preset load joins history");
    editor.navigation(c::Navigation::Undo);expect(editor.draft==original && !editor.dirty() && editor.redo_count()==1,"undo restores entire profile");
    editor.navigation(c::Navigation::Redo);expect(editor.draft==classic && editor.dirty(),"redo restores preset");
    editor.navigation(c::Navigation::Undo);editor.select(2);editor.navigation(c::Navigation::Accept);
    expect(editor.redo_count()==0,"new branch clears stale redo history");
    const auto grouped=editor.draft;editor.navigation(c::Navigation::RestoreSaved);
    expect(editor.draft==original && !editor.dirty(),"restore-saved restores baseline without applying");
    editor.navigation(c::Navigation::Undo);expect(editor.draft==grouped,"restore-saved is undoable");
    editor.mark_saved();expect(!editor.dirty() && editor.baseline==grouped,"successful commit moves baseline");
    editor.select(1);editor.navigation(c::Navigation::Accept);editor.navigation(c::Navigation::Back);
    expect(editor.opened && editor.confirmation==c::Confirmation::Discard,"unsaved close is protected");
    editor.navigation(c::Navigation::Back);expect(editor.opened && editor.draft==classic && editor.confirmation==c::Confirmation::None,"cancel discard keeps draft");
    editor.navigation(c::Navigation::Back);editor.navigation(c::Navigation::Accept);expect(!editor.opened,"confirmed discard closes");
    editor.open(original);editor.show_page(c::Page::Presets);editor.select(c::builtin_preset_count);
    editor.navigation(c::Navigation::Accept);expect(editor.draft==original && !editor.dirty(),"empty slot does not load defaults");
    editor.navigation(c::Navigation::Reset);expect(editor.slot_save_requested==0 && !editor.users[0].exists && editor.baseline==original,"slot save waits for disk and never applies");
    editor.slot_saved(0,original);expect(editor.users[0].readable && editor.slot_save_requested==-1,"slot becomes visible after commit");
    editor.navigation(c::Navigation::Reset);expect(editor.confirmation==c::Confirmation::OverwriteSlot && editor.slot_save_requested==-1,"slot overwrite requires choice");
    editor.navigation(c::Navigation::Back);expect(editor.slot_save_requested==-1 && editor.users[0].profile==original,"cancel overwrite has no request");
    editor.navigation(c::Navigation::Reset);editor.navigation(c::Navigation::Accept);expect(editor.slot_save_requested==0,"confirm overwrite queues specific slot");
    auto invalid=original;invalid.commands[0][1]=c::Button::LT;
    editor.open(original);editor.replace_draft(invalid);editor.navigation(c::Navigation::Save);
    expect(!editor.save_requested && editor.page==c::Page::Conflicts && editor.error_count()>0,"conflicts cannot be applied");
    editor.navigation(c::Navigation::Accept);expect(editor.row==0 && editor.slot==1 && editor.page==c::Page::Bindings,"conflict locates exact step");
    editor.show_page(c::Page::Presets);editor.select(c::builtin_preset_count+1);editor.navigation(c::Navigation::Reset);
    expect(editor.slot_save_requested==-1 && editor.page==c::Page::Conflicts,"invalid draft cannot enter a slot");
    editor.open(original);editor.users[1]={true,false,original,"steps"};editor.show_page(c::Page::Presets);editor.select(c::builtin_preset_count+1);editor.navigation(c::Navigation::Accept);
    expect(editor.draft==original && !editor.dirty(),"malformed slot cannot load or rewrite data");
    for(int i=0;i<80;++i)editor.replace_draft(c::builtin_preset(size_t(i%3)));
    expect(editor.undo_count()<=32,"history bounded");
    editor.open(original);c::editor_click(editor,450,100);expect(editor.page==c::Page::Presets,"mouse tab uses same model");
    c::editor_click(editor,50,182+52);expect(editor.preset_row==1 && !editor.dirty(),"mouse previews before load");
    c::editor_click(editor,80,730);expect(editor.draft==classic && editor.dirty() && !editor.save_requested,"mouse load stays a draft");
    c::editor_click(editor,80,100);c::editor_click(editor,620,190);expect(editor.capturing && editor.slot==1,"mouse selects exact sequence slot");
    editor.capture(c::Button::Y);expect(editor.draft==classic,"mouse capture requires release barrier");
    c::editor_click(editor,800,100);expect(editor.page==c::Page::Bindings && editor.capturing,"capture blocks unrelated navigation");
    editor.release_barrier(0);editor.capture(c::Button::A);expect(!editor.capturing && editor.draft.commands[0][1]==c::Button::A,"fresh button completes capture");
    const auto before=editor.draft;c::editor_click(editor,-1,-1);expect(editor.draft==before,"outside clicks never edit or apply");
    const auto geometry=c::EditorGeometry::canvas(1920,1080);expect(geometry.scale>0 && geometry.x>=0 && geometry.y>=0,"geometry fits viewport");
    expect(c::EditorGeometry::canvas(0,1080).scale==0 && c::EditorGeometry::canvas(std::numeric_limits<float>::infinity(),1080).scale==0,"invalid geometry disables hit testing");
    editor.open(original);editor.show_page(c::Page::Catalogue);editor.select(20);const auto first=editor.first_visible();editor.select(first);
    expect(editor.first_visible()==first,"clicking visible rows does not jump the viewport");
    for(const auto page:{c::Page::Bindings,c::Page::Catalogue,c::Page::Presets,c::Page::Conflicts}) {
        editor.open(original);editor.show_page(page);
        for(size_t i=0;i<editor.count();++i){editor.select(i);expect(!editor.title(i).empty() && !editor.pattern(i,true).empty() && !c::editor_details(editor).empty(),"every row has usable details and pattern");}
    }
    std::cout<<checks<<" editor workflow and preset checks passed\n";
}
