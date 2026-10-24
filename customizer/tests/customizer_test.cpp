#include "../src/control_state.hpp"
#include "../src/editor.hpp"
#include "../src/press_router.hpp"
#include "../src/modal_keys.hpp"
#include "../src/legend.hpp"
#include "../src/input_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
using namespace button_controls;
namespace c=button_controls::customizer;
static int checks=0;
static void expect(bool condition,const char* message) {
    ++checks;if(!condition){std::cerr<<"FAIL "<<message<<'\n';std::exit(1);}
}
static void tap(ControlState& state,Key key,double at) {state.key(key,true,at);state.key(key,false,at+.01);}
static ControlState state_for(const c::Profile& profile) {
    ControlState result;result.configure(profile);result.context(Context::Gameplay,0);return result;
}
static bool has_only(ControlState& state,Action action) {
    auto e=state.take_events();return e.size()==1 && e[0]==action;
}
int main() {
    const c::Profile defaults;
    expect(c::valid(c::validate(defaults)),"defaults validate");
    std::string error;auto parsed=defaults;
    expect(c::parse(c::serialize(defaults),parsed,error) && parsed==defaults,"round trip");
    const auto original=c::serialize(defaults);
    for(auto broken:{original+"Palm=B,A,X\n",original+"Extra=A\n",original.substr(0,original.find("Kick=")),
        std::string("Schema=2\n"),std::string(9000,'A')}) {
        parsed=defaults;expect(!c::parse(broken,parsed,error) && parsed==defaults,"invalid import is atomic");
    }
    for(auto replacement:{"Palm=B,X\n","Palm=B,A,X,Y\n","Palm=B,A,X,\n","Palm=B,A,Z\n","Palm=B,A,\n"}) {
        auto text=original;const auto pos=text.find("Palm=");text.replace(pos,text.find('\n',pos)-pos+1,replacement);
        expect(!c::parse(text,parsed,error),"input count / token cannot be bypassed");
    }
    auto p=defaults;
    p.commands[0]=p.commands[1];expect(!c::valid(c::validate(p)),"duplicate commands rejected");
    p=defaults;p.commands[0][0]=c::Button::LT;expect(!c::valid(c::validate(p)),"Focus entry excluded");
    p=defaults;p.commands[0][0]=c::Button::X;expect(!c::valid(c::validate(p)),"light prefix cannot make tap and charge unreachable");
    p=defaults;p.commands[0][1]=c::Button::Y;expect(!c::valid(c::validate(p)),"heavy prefix cannot swallow a native attack");
    p=defaults;p.avoid[0]=c::Button::Y;expect(!c::valid(c::validate(p)),"post-unlock guard heavy collision");
    p=defaults;p.avoid[0]=c::Button::X;expect(!c::valid(c::validate(p)),"native guard light family remains reachable");
    p=defaults;p.avoid[0]=p.avoid[1];expect(!c::valid(c::validate(p)),"avoid distinction");
    p=defaults;p.layout[0]=p.layout[1];expect(!c::valid(c::validate(p)),"duplicate base mapping");
    p=defaults;p.swap_binding(size_t(c::Button::L3),c::Button::RT);
    expect(p.physical(c::Button::L3)==c::Button::RT && p.physical(c::Button::RT)==c::Button::L3,"swap keeps occupants");
    expect(c::valid(c::validate(p)),"base permutation validates");
    for(size_t i=0;i<c::button_count;++i)expect(p.logical(p.physical(c::Button(i)))==c::Button(i),"inverse layout");
    // Valid commands use auxiliary prefixes and can end on a utility button.
    p=defaults;p.commands[0]={c::Button::A,c::Button::A,c::Button::RT};
    expect(c::valid(c::validate(p)),"new three-step command");
    auto s=state_for(p);tap(s,Key::A,.01);tap(s,Key::A,.08);tap(s,Key::RT,.15);
    expect(has_only(s,Action::Palm),"custom RT final does not takedown");
    // Defence in depth for the recognizer: unsupported imported profiles are
    // rejected above, but even an internal caller cannot leak a held attack.
    p.commands[0]={c::Button::X,c::Button::B,c::Button::RT};
    s=state_for(p);s.key(Key::X,true,.01);
    expect(s.owned_mask()&bit(Key::X),"pending attack prefix owns native attack");
    s.interrupted();expect(s.owned(Key::X),"hit-cancelled prefix cannot become held attack");
    s.key(Key::X,false,.02);expect(!s.owned(Key::X),"ownership clears on release");
    p=defaults;p.commands[0]={c::Button::X,c::Button::X,c::Button::Y};
    s=state_for(p);s.key(Key::X,true,.01);s.key(Key::X,true,.03);tap(s,Key::Y,.1);
    expect(s.take_events().empty(),"repeat notifications cannot satisfy two steps");
    s=state_for(p);tap(s,Key::X,.01);tap(s,Key::X,.08);tap(s,Key::Y,.15);
    expect(has_only(s,Action::Palm),"release required between same-button steps");
    s=state_for(p);tap(s,Key::X,.01);tap(s,Key::X,.08);s.context(Context::Menu,.09);s.context(Context::Gameplay,.1);tap(s,Key::Y,.15);
    expect(s.take_events().empty(),"menu cancels custom prefix");
    s=state_for(p);tap(s,Key::X,.01);tap(s,Key::X,.6);tap(s,Key::Y,.7);
    expect(s.take_events().empty(),"per-step timeout");
    s=state_for(p);s.key(Key::X,true,.01);s.tick(.5);
    expect(s.owned(Key::X),"expired held attack prefix cannot charge");
    p=defaults;p.avoid[0]=c::Button::X;
    // Defence in depth for the lower-level state machine; this profile is
    // deliberately rejected by the user-facing validator above.
    s=state_for(p);s.key(Key::LB,true,.01);s.key(Key::X,true,.03);
    expect(has_only(s,Action::AvoidHigh) && s.owned(Key::X),"custom avoid owns native light attack");
    // A base swap moves every dependent input without changing its role/count.
    p=defaults;p.swap_binding(size_t(c::Button::X),c::Button::Y);
    expect(p.commands[0][2]==c::Button::Y && p.commands[1][2]==c::Button::X,"attack dependants migrate together");
    s=state_for(p);tap(s,Key::B,.01);tap(s,Key::A,.08);tap(s,Key::X,.15);
    expect(has_only(s,Action::Palm),"migrated physical final still invokes the same native role");
    for(size_t role=0;role<c::button_count;++role)for(size_t target=0;target<c::button_count;++target) {
        p=defaults;p.swap_binding(role,c::Button(target));
        expect(c::valid(c::validate(p)),"every base swap preserves valid dependants");
        for(size_t i=0;i<4;++i)for(size_t k=0;k<3;++k)
            expect(p.logical(p.commands[i][k])==defaults.commands[i][k],"swaps preserve command roles");
        for(size_t i=0;i<2;++i)expect(p.logical(p.avoid[i])==defaults.avoid[i],"swaps preserve avoid roles");
        p.swap_binding(role,c::Button(role));expect(p==defaults,"inverse swap restores dependants exactly");
    }
    c::Editor editor;editor.open(defaults);editor.navigation(c::Navigation::Accept);
    editor.capture(c::Button::X);expect(editor.draft==defaults,"opening A cannot capture itself");
    editor.release_barrier(0);editor.capture(c::Button::X);
    expect(editor.draft.commands[0][0]==c::Button::X,"capture after full release");
    expect(editor.draft.commands[0].size()==3,"editor does not add/remove steps");
    editor.navigation(c::Navigation::Right);editor.navigation(c::Navigation::Accept);editor.navigation(c::Navigation::CancelCapture);
    expect(!editor.capturing,"View cancels capture without changing pattern");
    editor.navigation(c::Navigation::Reset);expect(editor.draft==defaults,"row reset");
    editor.draft.swap_binding(size_t(c::Button::A),c::Button::X);
    editor.draft.commands[0][0]=editor.draft.physical(c::Button::A);
    editor.navigation(c::Navigation::Reset);
    expect(c::valid(c::validate(editor.draft)),"row reset follows current base roles");
    editor.navigation(c::Navigation::Save);expect(editor.save_requested,"save is explicit");
    editor.navigation(c::Navigation::Back);expect(editor.confirmation==c::Confirmation::Discard,"dirty draft asks before discard");
    editor.navigation(c::Navigation::Accept);expect(!editor.opened,"confirmed close discards draft");
    editor.open(defaults);editor.navigation(c::Navigation::Tab);
    expect(editor.page==c::Page::Catalogue && !editor.capturing,"catalogue is a separate preview, not another capture input");
    editor.catalog_row=1;editor.navigation(c::Navigation::Accept);
    expect(editor.page==c::Page::Bindings && editor.row==0 && !editor.capturing,"move preview locates linked edit without capturing its accept");
    editor.navigation(c::Navigation::Tab);editor.catalog_row=44;editor.navigation(c::Navigation::Accept);
    expect(editor.page==c::Page::Catalogue && !editor.capturing,"Focus skill stays explicitly native");
    for(size_t i=0;i<c::catalog_entries.size();++i) {
        const auto& entry=c::catalog_entries[i];
        const auto preview=c::catalog_pattern(defaults,i);
        expect(preview.supported,"every extracted catalogue entry has an explicit preview");
        expect(preview.operations==entry.operations,"preview preserves original operation count");
        if(!entry.focus)expect(preview.dependencies!=0,"each gameplay entry exposes shared binding dependencies");
    }
    c::PressRouter router;
    p=defaults;p.swap_binding(size_t(c::Button::X),c::Button::RT);
    router.transition(c::RouteScope::Gameplay);
    auto route=router.event(c::Button::RT,0,c::RouteScope::Gameplay,p);
    expect(route.logical==c::Button::X && route.gameplay && route.forward,"route gameplay press");
    route=router.event(c::Button::RT,1,c::RouteScope::Menu,defaults);
    expect(route.logical==c::Button::X && route.gameplay,"release follows original owner across menu/profile change");
    router.transition(c::RouteScope::Menu);
    route=router.event(c::Button::RT,0,c::RouteScope::Menu,p);
    expect(route.logical==c::Button::RT && !route.gameplay,"menu press remains physical");
    route=router.event(c::Button::RT,1,c::RouteScope::Gameplay,p);
    expect(route.logical==c::Button::RT && !route.gameplay,"menu closing press does not enter gameplay mapping");
    c::ModalKeys modal;
    modal.observe(123,true);expect(!modal.released(),"keyboard accept participates in release barrier");
    modal.observe(123,false);expect(modal.released(),"accept must release before capture");
    modal.observe(456,true);modal.close();
    expect(modal.quarantined(456,true),"closing key repeat cannot reach underlying menu");
    modal.observe(456,false);expect(modal.quarantined(456,false),"closing key release is swallowed after panel closes");
    expect(!modal.quarantined(456,true),"fresh closing key re-arms after release");
    modal.observe(789,true);modal.reset();expect(modal.released(),"native focus flush clears stale capture barrier");
    expect(input_policy::editor_scope(1,true,false),"editor is available from in-game menu context");
    expect(!input_policy::editor_scope(1,false,false),"main menu cannot open gameplay editor");
    expect(!input_policy::editor_scope(1,true,true),"native key capture keeps R3/F8 ownership");
    for(auto context:{0,2,4,6,7,8,10,11,13})
        expect(!input_policy::editor_scope(uint8_t(context),true,false),"replay, photo, cinematic, Focus and custom capture contexts stay native");
    std::wstring legend;
    expect(c::controller_legend(defaults,"XBOX_Move_2",10,legend) && legend.find(L"奔跑")!=std::wstring::npos,"stick diagram includes click and hold roles");
    expect(!c::controller_legend(defaults,"XBOX_Move_1",20,legend),"Focus movement legend stays native");
    expect(!c::controller_legend(defaults,"XBOX_LightAttack_1",15,legend),"Focus attack legend stays native");
    expect(!c::controller_legend(defaults,"KEYBOARD_Guard",1,legend),"keyboard diagram remains native");
    p=defaults;p.swap_binding(size_t(c::Button::A),c::Button::X);
    expect(c::controller_legend(p,"XBOX_LightAttack",6,legend) && legend==L"辅助键 A / 抛掷前缀","physical diagram label follows swapped role");
    expect(c::controller_legend(p,"XBOX_DeflectParry",0,legend) && legend.find(L"+ X 闪高")!=std::wstring::npos,"guard legend follows migrated avoid button");
    p=defaults;p.swap_binding(size_t(c::Button::L3),c::Button::RB);
    expect(c::controller_legend(p,"XBOX_ThrowWeapon_3",19,legend) && legend==L"长按：奔跑","secondary diagram label follows remapped hold role");
    for(auto action:{game::Action_LaunchFocusExecution,game::Action_ApplyFocus,game::Action_FocusVitalPointSelection})
        expect(!input_policy::customizer_hint_owned(13,action,false),"dynamic Focus icons remain native");
    expect(!input_policy::customizer_hint_owned(1,game::Action_AttackLight,false),"menu glyphs remain native");
    expect(!input_policy::customizer_hint_owned(13,game::Action_AttackLight,true),"ambiguous dynamic presets remain native");
    expect(input_policy::customizer_hint_owned(13,game::Action_AttackLightBackFront,false),"dynamic combat glyphs use profile");
    expect(input_policy::customizer_hint_owned(0,game::Action_PrepFocus,false),"gameplay Focus entry follows base mapping");
    expect(input_policy::gamepad_hint_device(1,false),"explicit gamepad preview overrides last keyboard device");
    expect(!input_policy::gamepad_hint_device(2,true),"explicit keyboard preview stays native");
    expect(input_policy::gamepad_hint_device(0,true) && !input_policy::gamepad_hint_device(0,false),"automatic preview follows current device");
    std::mt19937 random(2138710);
    for(int n=0;n<2000;++n) {
        p=defaults;
        for(int k=0;k<10;++k)p.swap_binding(random()%c::button_count,c::Button(random()%c::button_count));
        p.avoid={p.physical(c::Button::A),p.physical(c::Button::B)};
        for(auto& command:p.commands) {
            command[0]=p.physical(c::Button(random()%2));
            command[1]=p.physical(c::Button(random()%2));
            const std::array<c::Button,8> finals={c::Button::A,c::Button::B,c::Button::X,c::Button::Y,
                c::Button::RB,c::Button::L3,c::Button::R3,c::Button::RT};
            command[2]=p.physical(finals[random()%finals.size()]);
        }
        const auto issues=c::validate(p);
        if(c::valid(issues)) {
            parsed=defaults;expect(c::parse(c::serialize(p),parsed,error) && parsed==p,"generated valid profile round trip");
            const std::array<Action,4> actions={Action::Palm,Action::Sweep,Action::Crotch,Action::Kick};
            for(size_t i=0;i<4;++i) {
                s=state_for(p);
                for(size_t k=0;k<3;++k)tap(s,Key(p.logical(p.commands[i][k])),.01+.08*k);
                expect(has_only(s,actions[i]),"every valid profile emits intended action once");
            }
        }
    }
    std::cout<<checks<<" customizer checks passed\n";
}
