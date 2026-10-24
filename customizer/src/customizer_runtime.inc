// Included inside plugin.cpp's private namespace, after the stable native
// adapters. All engine calls run on the game thread; no D3D/Present hooks.
c::Profile active_profile;
uint64_t active_profile_revision=1;
std::wstring profile_path;
c::Editor editor;
c::UserPresets user_presets;
c::EditorGeometry editor_geometry;
Vector2 editor_canvas_size{};
std::unordered_set<uint64_t> editor_mouse_buttons;
c::ModalKeys modal_keys;
void* editor_controller=nullptr;
using InputNodeFn=void(*)(void*,void*);
using PostRenderFn=void(*)(void*,void*);
InputNodeFn original_input_node=nullptr;
PostRenderFn original_post_render=nullptr;
using SetRichTextFn=void(*)(void*,const void*);
SetRichTextFn original_set_rich_text=nullptr;
using SlateEventFn=bool(*)(void*,const void*);
using SlateEventPairFn=bool(*)(void*,const void*,const void*);
using SlateMouseMoveFn=bool(*)(void*,const void*,bool);
SlateEventFn original_slate_down=nullptr,original_slate_up=nullptr,original_slate_analog=nullptr,
    original_slate_char=nullptr,original_slate_mouse_up=nullptr;
SlateEventPairFn original_slate_mouse_down=nullptr,original_slate_wheel=nullptr;
SlateMouseMoveFn original_slate_move=nullptr;
void* custom_canvas_font=nullptr;
std::unordered_set<uint64_t> hint_widgets;

bool native_layout_supported() {
    // Native custom/preset profiles can change the meaning of the virtual
    // keys consumed by this adapter. Do not stack two remappers. Preserve those
    // profiles and pass through until the player selects DefaultGamepad.
    alignas(8) std::array<uint8_t,80> handler{};
    function<void*(*)(void*,bool)>(g::customizer_current_profile)(handler.data(),true);
    const auto db=function<void*(*)(const void*)>(g::customizer_profile_db)(handler.data());
    return db && name_string(static_cast<uint8_t*>(db)+24)=="DefaultGamepad";
}

bool derives_from(void* object,const char* base_name) {
    auto cls=object?field<void*>(object,16):nullptr;
    for(int depth=0;cls && depth<64;++depth,cls=field<void*>(cls,64))
        if(name_string(static_cast<uint8_t*>(cls)+24)==base_name)return true;
    return false;
}
bool widget_gamepad(void* widget,void* controller) {
    const auto data=field<void*>(widget,704);
    const auto explicit_type=data && derives_from(data,"InputMappingWidgetData")?field<uint8_t>(data,240):uint8_t{0};
    const bool current=controller && function<uint8_t(*)(void*)>(g::get_device_mode)(controller)==0;
    return policy::gamepad_hint_device(explicit_type,current);
}

void remember_hint_widget(void* widget) {
    // Dead hints accumulate between menu visits/level travel. A hard insertion
    // cap would eventually omit the current screen and leave it stale on Save.
    // Prune periodically, but always register the new live widget.
    static ULONGLONG next_prune=0;
    const auto tick=GetTickCount64();
    if(hint_widgets.size()>=4096 && tick>=next_prune) {
        next_prune=tick+1000;
        for(auto it=hint_widgets.begin();it!=hint_widgets.end();) {
            auto identity=*it;
            if(!function<void*(*)(const void*)>(g::get_weak_object)(&identity))it=hint_widgets.erase(it);
            else ++it;
        }
    }
    uint64_t identity=0;
    function<void(*)(void*,const void*)>(g::customizer_assign_weak)(&identity,widget);
    hint_widgets.insert(identity);
}

void hooked_set_rich_text(void* rich_text,const void* text) {
    if(!diagnostic_only && name_string(static_cast<uint8_t*>(rich_text)+24)=="ActionRichText" && native_layout_supported()) {
        auto label=field<void*>(rich_text,32);
        for(int depth=0;label && depth<8;++depth,label=field<void*>(label,32)) {
            if(!derives_from(label,"BP_Menu_Options_Remappin_Action_Text_C"))continue;
            const auto name=name_string(static_cast<uint8_t*>(label)+24);
            if(name.rfind("XBOX_",0)!=0)break;
            static uint64_t action_name=0;
            if(!action_name)function<void*(*)(void*,const wchar_t*,int)>(g::name_from_wide)(&action_name,L"ActionType",0);
            const auto cls=field<void*>(label,16);
            const auto property=function<void*(*)(void*,uint64_t)>(g::find_property_by_name)(cls,action_name);
            const int32_t offset=property?field<int32_t>(property,76):-1;
            if(!property || field<int32_t>(property,60)!=1 || offset<0 || offset>=field<int32_t>(cls,88))break;
            std::wstring value;
            if(!c::controller_legend(active_profile,name,field<uint8_t>(label,size_t(offset)),value))break;
            remember_hint_widget(label);
            Array string{reinterpret_cast<uint8_t*>(value.data()),int32_t(value.size()+1),int32_t(value.size()+1)};
            alignas(8) std::array<uint8_t,24> native_text{};
            // Use the const FString overload: it copies our borrowed buffer;
            // the game's FText destructor then releases its own allocation.
            function<void*(*)(void*,const Array*)>(g::customizer_text_from_string)(native_text.data(),&string);
            original_set_rich_text(rich_text,native_text.data());
            function<void(*)(void*)>(g::customizer_destroy_text)(native_text.data());
            return;
        }
    }
    original_set_rich_text(rich_text,text);
}

void refresh_hint_widgets() {
    // Native weak handles include the serial number, so a recycled UObject
    // address cannot turn a remembered hint into an unrelated widget.
    const std::vector<uint64_t> snapshot(hint_widgets.begin(),hint_widgets.end());
    size_t refreshed=0;
    for(auto identity:snapshot) {
        auto widget=function<void*(*)(const void*)>(g::get_weak_object)(&identity);
        if(!widget){hint_widgets.erase(identity);continue;}
        function<void(*)(void*)>(g::customizer_notify_widget)(widget);++refreshed;
    }
    if(trace_combat)log(("customizer: marked "+std::to_string(refreshed)+" live hint widgets for refresh").c_str());
}

void customizer_lifecycle(void* controller) {
    if(!editor.opened || editor_controller==controller)modal_keys.reset();
    if(editor_controller==controller) {
        editor.close();editor_controller=nullptr;modal_keys.reset();
        log("customizer: native input flush/disconnect closed modal and canceled capture");
    }
}

void ensure_canvas_font() {
    // Borrow a font already owned by the visible native UI. Do not synchronously
    // load assets or grow engine arrays from a drawing callback. Resolve afresh
    // for this frame rather than retaining a UObject across a level transition.
    custom_canvas_font=nullptr;
    auto font_class=function<void*(*)()>(g::customizer_font_class)();
    using FindObjectFn=void*(*)(void*,void*,const wchar_t*,bool);
    for(auto path:{L"/Game/UI/Fonts/Rubik.Rubik",L"/Game/UI/Fonts/SourceSansPro.SourceSansPro",L"/Game/UI/Fonts/Roboto.Roboto"}) {
        custom_canvas_font=function<FindObjectFn>(g::customizer_find_object)(font_class,nullptr,path,false);
        if(custom_canvas_font)break;
    }
}

const void* native_key(c::Button button) {
    if(button>=c::Button::Count)return nullptr;
    return reinterpret_cast<const void*>(image+bindings[size_t(button)].first);
}
void copy_native_key(void* destination,const void* source) {
    function<void*(*)(void*,const void*)>(g::customizer_copy_key)(destination,source);
}
void validate_native_key_lifetimes_once() {
    static bool checked=false;
    if(checked || !trace_combat)return;
    checked=true;bool ok=true;
    const auto destroy=function<c::KeyArgument::Destroy>(g::customizer_destroy_key);
    const auto consume=[&](void* owned){destroy(owned);return true;};
    for(size_t key=0;key<c::button_count && ok;++key) {
        alignas(8) std::array<uint8_t,24> source{},target{},source_global{},target_global{};
        const auto original=native_key(c::Button(key));
        const auto alternate=native_key(c::Button((key+1)%c::button_count));
        std::memcpy(source_global.data(),original,24);std::memcpy(target_global.data(),alternate,24);
        copy_native_key(source.data(),original);copy_native_key(target.data(),alternate);
        // Prime private copies, never the EKeys globals, so this check covers
        // non-empty native shared pointers instead of only name-only values.
        ok=function<bool(*)(const void*)>(g::customizer_valid_key)(source.data()) &&
           function<bool(*)(const void*)>(g::customizer_valid_key)(target.data());
        const auto source_ref=field<void*>(source.data(),16),target_ref=field<void*>(target.data(),16);
        ok=ok && source_ref && target_ref;
        const auto source_count=ok?field<int32_t>(source_ref,8):0;
        const auto target_count=ok?field<int32_t>(target_ref,8):0;
        for(int trial=0;trial<1000 && ok;++trial) {
            alignas(8) std::array<uint8_t,24> incoming{};copy_native_key(incoming.data(),source.data());
            {
                c::KeyArgument argument(incoming.data(),destroy);
                if(trial%3==0)argument.dispatch(incoming.data(),copy_native_key,consume);
                if(trial%3==1)argument.dispatch(target.data(),copy_native_key,consume);
                // Third branch deliberately swallows the incoming argument.
            }
            c::dispatch_key_copy(target.data(),copy_native_key,consume);
            ok=field<int32_t>(source_ref,8)==source_count && field<int32_t>(target_ref,8)==target_count &&
               std::memcmp(source_global.data(),original,24)==0 && std::memcmp(target_global.data(),alternate,24)==0;
        }
        destroy(source.data());destroy(target.data());
    }
    log(ok?"customizer: native key lifetime audit passed: 10000 argument transfers and 10000 synthetic copies; global keys and shared counts unchanged":
        "customizer: native key lifetime audit FAILED; do not release this candidate");
}
c::RouteScope route_scope(Context context) {
    switch(context) {
        case Context::Gameplay:return c::RouteScope::Gameplay;
        case Context::Focus:return c::RouteScope::Focus;
        case Context::Menu:return c::RouteScope::Menu;
        default:return c::RouteScope::Inactive;
    }
}
void sync_routes(void* controller,Runtime& runtime,Context context) {
    const auto releases=runtime.routes.transition(route_scope(context));
    runtime.controls.context(context,now());
    if(!releases)return;
    runtime.controls.interrupted(true);
    runtime.clear_input_buffer=runtime.reconcile_keys=true;
    for(size_t i=0;i<c::button_count;++i)if(releases&(uint32_t{1}<<i)) {
        c::dispatch_key_copy(native_key(c::Button(i)),copy_native_key,[&](void* owned){
            return original_input_key(controller,owned,1,0.f,true);
        });
        runtime.controls.key(Key(i),false,now(),0,false);
    }
    if(trace_combat)log("customizer: retired native held keys at context transition");
}
void sync_layout_support(void* controller,Runtime& runtime) {
    const bool supported=native_layout_supported();
    if(supported==runtime.native_supported)return;
    sync_routes(controller,runtime,Context::Inactive);
    runtime.routes.flush();runtime.routes.quarantine_held(runtime.physical_held);
    runtime.controls.interrupted(true);runtime.pulses.clear();runtime.rewriting=false;
    runtime.released_owned=0;runtime.clear_input_buffer=runtime.reconcile_keys=true;
    if(auto input=field<void*>(controller,0x3d8))original_flush_pressed_keys(input,false);
    runtime.native_supported=supported;
    log(supported?"customizer: default native gamepad profile restored; gameplay remapper active":
        "customizer: native custom/preset gamepad profile selected; gameplay remapper bypassed");
}
void* live_local_controller() {
    // Resolve through the engine's current viewport; never dereference an old
    // players-map entry after travel just to process a Slate event.
    const auto engine=*reinterpret_cast<void**>(image+g::customizer_engine);
    const auto viewport=engine?field<void*>(engine,1944):nullptr;
    auto controller=viewport?function<void*(*)(const void*,int)>(g::customizer_get_controller)(viewport,0):nullptr;
    return derives_from(controller,"FightingPlayerController")?controller:nullptr;
}
void flush_current_native_input(void* controller) {
    // Connection callbacks can leave a remapped virtual key cached in native
    // input. Flush the current live player's object, never a cached old pointer.
    if(controller!=live_local_controller())return;
    if(auto input=field<void*>(controller,0x3d8))original_flush_pressed_keys(input,false);
}
void ensure_profile(Runtime& runtime) {
    if(runtime.profile_revision==active_profile_revision)return;
    runtime.controls.configure(active_profile);
    runtime.released_owned=0;
    runtime.reconcile_keys=runtime.clear_input_buffer=true;
    runtime.profile_revision=active_profile_revision;
}
void load_profile() {
    HANDLE file=CreateFileW(profile_path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){log("customizer: no saved profile; using 0.1.5 layout");return;}
    LARGE_INTEGER size{};
    bool ok=GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<=8192;
    std::string source(ok?size_t(size.QuadPart):0,0);DWORD read=0;
    if(ok)ok=ReadFile(file,source.data(),DWORD(source.size()),&read,nullptr) && read==source.size();
    CloseHandle(file);
    std::string error;
    if(ok && c::parse(source,active_profile,error))log("customizer: validated saved profile loaded");
    else log(("customizer: invalid profile retained on disk; using defaults; reason="+error).c_str());
}
std::wstring preset_path(size_t slot) {
    return profile_path.substr(0,profile_path.find_last_of(L"\\/")+1)+L"ButtonControls.preset-"+std::to_wstring(slot+1)+L".ini";
}
void load_user_presets() {
    user_presets={};
    for(size_t i=0;i<user_presets.size();++i) {
        auto& slot=user_presets[i];
        HANDLE file=CreateFileW(preset_path(i).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) {
            slot.exists=GetLastError()!=ERROR_FILE_NOT_FOUND;
            if(slot.exists)slot.error="read";
            continue;
        }
        slot.exists=true;LARGE_INTEGER size{};
        bool ok=GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<=8192;
        std::string source(ok?size_t(size.QuadPart):0,0);DWORD read=0;
        if(ok)ok=ReadFile(file,source.data(),DWORD(source.size()),&read,nullptr) && read==source.size();
        CloseHandle(file);
        slot.readable=ok && c::parse(source,slot.profile,slot.error);
        if(!ok)slot.error="read_or_size";
    }
}
bool persist_profile(const c::Profile& profile,const std::wstring& destination=profile_path) {
    const auto data=c::serialize(profile);c::Profile verified;std::string error;
    if(!c::parse(data,verified,error) || verified!=profile)return false;
    const auto temp=destination+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    bool ok=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr) && written==data.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if(ok && GetFileAttributesW(destination.c_str())!=INVALID_FILE_ATTRIBUTES) {
        // Preserve the previous bytes, including a malformed user edit. Never
        // silently discard the only copy of a profile that failed to parse.
        ok=CopyFileW(destination.c_str(),(destination+L".bak").c_str(),FALSE)!=0;
    }
    if(ok)ok=MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok)DeleteFileW(temp.c_str());
    return ok;
}
void flush_for_editor(void* controller,Runtime& runtime) {
    runtime.controls.interrupted(true);
    runtime.routes.flush();runtime.released_owned=0;
    runtime.reconcile_keys=runtime.clear_input_buffer=true;
    if(auto input=field<void*>(controller,0x3d8))original_flush_pressed_keys(input,false);
}
void save_editor(void* controller,Runtime& runtime) {
    editor.save_requested=false;
    if(!runtime.native_supported){editor.status=L"请先在原生选项中选择默认手柄方案，再使用此工具。";return;}
    const auto issues=c::validate(editor.draft);
    const auto bad=std::find_if(issues.begin(),issues.end(),[](const auto& x){return x.severity==c::Severity::Error;});
    if(bad!=issues.end()) {
        editor.show_conflicts();editor.status=c::issue_text(bad->code);
        log(("customizer: save rejected: "+bad->code).c_str());return;
    }
    if(!persist_profile(editor.draft)) {editor.status=L"保存失败；当前配置仍然有效，草稿保留。";return;}
    active_profile=editor.draft;++active_profile_revision;
    refresh_hint_widgets();
    flush_for_editor(controller,runtime);
    ensure_profile(runtime);
    editor.mark_saved();
    log("customizer: profile saved atomically; input and hint revision published together");
}
void process_editor_requests(void* controller,Runtime& runtime) {
    const int requested=editor.slot_save_requested;
    editor.slot_save_requested=-1;
    if(requested>=0 && size_t(requested)<user_presets.size()) {
        if(!c::valid(c::validate(editor.draft)))editor.show_conflicts();
        else if(persist_profile(editor.draft,preset_path(size_t(requested)))) {
            user_presets[size_t(requested)]={true,true,editor.draft,{}};
            editor.slot_saved(size_t(requested),editor.draft);
            log("customizer: user preset slot saved atomically; gameplay revision unchanged");
        } else editor.status=L"方案保存失败；旧文件和当前游戏配置保留，草稿仍可编辑。";
    }
    if(editor.save_requested)save_editor(controller,runtime);
    if(!editor.opened) {
        modal_keys.close();runtime.ui_blocked|=runtime.physical_held;flush_for_editor(controller,runtime);
        editor_controller=nullptr;log("customizer: editor closed; held buttons quarantined until release");
    }
}
bool editor_scope(void* controller) {
    // The controller's legacy key-capture mode is checked by its own InputKey
    // at 0x186eaf4. Do not steal R3/F8 from native remapping, replay, photo mode
    // or cinematic contexts merely because they are all non-combat UI.
    return policy::editor_scope(function<GetContextFn>(g::get_context)(controller),
        fighting_pawn(controller)!=nullptr,field<uint8_t>(controller,0xd08)==0x15);
}
bool editor_input(void* controller,Runtime& runtime,const void* key,int event,bool gamepad,Context before) {
    const auto physical=identify(key);
    const bool button=gamepad && physical<Key::Motion;
    const auto key_name=name_string(key);
    if(button && (runtime.ui_blocked&bit(physical))) {
        if(event==1)runtime.ui_blocked&=~bit(physical);
        return true;
    }
    // Only a paused in-game menu exposes the editor. Start, View and the
    // normal menu A/B functions stay native unless this panel is actually open.
    const bool toggle=(key_name=="F8" || (gamepad && physical==Key::R3));
    if(!editor.opened && before==Context::Menu && editor_scope(controller) && event==0 && toggle) {
        validate_native_key_lifetimes_once();
        ensure_canvas_font();
        load_user_presets();editor.open(active_profile,user_presets);editor_controller=controller;flush_for_editor(controller,runtime);
        if(!runtime.native_supported)editor.status=L"请先在原生选项中选择默认手柄方案，再使用此工具。";
        log("customizer: editor opened from paused menu");return true;
    }
    if(!editor.opened)return false;
    if(editor_controller!=controller)return true;
    if(before!=Context::Menu || !editor_scope(controller)) {
        modal_keys.close();
        editor.close();runtime.ui_blocked|=runtime.physical_held;flush_for_editor(controller,runtime);
        editor_controller=nullptr;return true;
    }
    editor.release_barrier(modal_keys.released()?0:1);
    if(event!=0)return true;
    if(editor.capturing) {
        if(key_name=="Gamepad_Special_Left" || key_name=="Escape")editor.navigation(c::Navigation::CancelCapture);
        else if(button)editor.capture(c::Button(physical));
        else if(!gamepad) {
            const std::array<const char*,10> keys={"One","Two","Three","Four","Five","Six","Seven","Eight","Nine","Zero"};
            const auto found=std::find(keys.begin(),keys.end(),key_name);
            if(found!=keys.end())editor.capture(c::Button(found-keys.begin()));
        }
        return true;
    }
    if(key_name=="Gamepad_DPad_Up" || key_name=="Up")editor.navigation(c::Navigation::Up);
    if(key_name=="Gamepad_DPad_Down" || key_name=="Down")editor.navigation(c::Navigation::Down);
    if(key_name=="Gamepad_DPad_Left" || key_name=="Left")editor.navigation(c::Navigation::Left);
    if(key_name=="Gamepad_DPad_Right" || key_name=="Right")editor.navigation(c::Navigation::Right);
    if(key_name=="Gamepad_LeftShoulder" || key_name=="PageUp")editor.navigation(c::Navigation::PreviousTab);
    if(key_name=="Gamepad_RightShoulder" || key_name=="Tab" || key_name=="PageDown")editor.navigation(c::Navigation::Tab);
    if((button && physical==Key::LT) || key_name=="F6")editor.navigation(c::Navigation::Undo);
    if((button && physical==Key::RT) || key_name=="F7")editor.navigation(c::Navigation::Redo);
    if(key_name=="Home")editor.navigation(c::Navigation::RestoreSaved);
    if((button && physical==Key::A) || key_name=="Enter")editor.navigation(c::Navigation::Accept);
    if((button && physical==Key::B) || key_name=="Escape" || key_name=="F8")editor.navigation(c::Navigation::Back);
    if((button && physical==Key::X) || key_name=="F5")editor.navigation(c::Navigation::Save);
    if((button && physical==Key::Y) || key_name=="Delete")editor.navigation(c::Navigation::Reset);
    process_editor_requests(controller,runtime);
    return true;
}

bool slate_editor_event(const void* event,bool down) {
    const auto key=static_cast<const uint8_t*>(event)+24;
    const auto name=*reinterpret_cast<const uint64_t*>(key);
    modal_keys.observe(name,down);
    if(modal_keys.quarantined(name,down)) {
        const auto physical=identify(key);
        if(!down && physical<Key::Motion)for(auto& player:players) {
            player.second.ui_blocked&=~bit(physical);player.second.physical_held&=~bit(physical);
        }
        return true;
    }
    const auto text=name_string(key);
    const auto physical=identify(key);
    const bool gamepad=text.rfind("Gamepad_",0)==0;
    if(!editor.opened && (gamepad || text=="F8")) {
        if(auto controller=live_local_controller()) {
            auto& runtime=players[controller];ensure_profile(runtime);
            sync_layout_support(controller,runtime);
            const auto context=context_for(controller);
            sync_routes(controller,runtime,context);
            if(gamepad && physical<Key::Motion) {
                if(down)runtime.physical_held|=bit(physical);else runtime.physical_held&=~bit(physical);
                if(runtime.routes.observe_slate(c::Button(physical),down))return true;
            }
            if(down && !field<uint8_t>(const_cast<void*>(event),10) &&
               (text=="F8" || text=="Gamepad_RightThumbstick") &&
               context==Context::Menu && editor_scope(controller)) {
                if(text=="Gamepad_RightThumbstick")runtime.physical_held|=bit(Key::R3);
                editor_input(controller,runtime,key,0,gamepad,Context::Menu);
                return true;
            }
        }
    }
    if(!editor.opened || !editor_controller)return false;
    if(down && field<uint8_t>(const_cast<void*>(event),10))return true; // Slate repeat
    const auto found=players.find(editor_controller);
    if(found==players.end()){editor.close();editor_controller=nullptr;modal_keys.reset();return true;}
    auto& runtime=found->second;
    if(gamepad && physical<Key::Motion) {
        if(down)runtime.physical_held|=bit(physical);else runtime.physical_held&=~bit(physical);
    }
    editor_input(editor_controller,runtime,key,down?0:1,gamepad,context_for(editor_controller));
    return true;
}
bool hooked_slate_down(void* slate,const void* event) {
    if(!diagnostic_only && slate_editor_event(event,true))return true;
    return original_slate_down(slate,event);
}
bool hooked_slate_up(void* slate,const void* event) {
    if(!diagnostic_only && slate_editor_event(event,false))return true;
    return original_slate_up(slate,event);
}
bool hooked_slate_analog(void* slate,const void* event) {return editor.opened || original_slate_analog(slate,event);}
bool hooked_slate_char(void* slate,const void* event) {return editor.opened || original_slate_char(slate,event);}
bool hooked_slate_mouse_down(void* slate,const void* window,const void* event) {
    const auto identity=*reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(event)+56);
    if(!editor.opened){editor_mouse_buttons.erase(identity);return original_slate_mouse_down(slate,window,event);}
    editor_mouse_buttons.insert(identity);
    const auto found=players.find(editor_controller);
    if(found==players.end())return true;
    const auto key_name=name_string(static_cast<const uint8_t*>(event)+56);
    if(key_name=="RightMouseButton")editor.navigation(editor.capturing?c::Navigation::CancelCapture:c::Navigation::Back);
    else if(key_name=="LeftMouseButton" && editor_geometry.scale>0) {
        const auto hwnd=GetForegroundWindow();DWORD owner=0;GetWindowThreadProcessId(hwnd,&owner);
        RECT rect{};const auto point=field<Vector2>(const_cast<void*>(event),24);
        if(!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x)>1000000 || std::abs(point.y)>1000000)return true;
        POINT client{LONG(point.x),LONG(point.y)};
        if(owner==GetCurrentProcessId() && GetClientRect(hwnd,&rect) && ScreenToClient(hwnd,&client) && rect.right>0 && rect.bottom>0) {
            const float x=float(client.x)*editor_canvas_size.x/float(rect.right);
            const float y=float(client.y)*editor_canvas_size.y/float(rect.bottom);
            c::editor_click(editor,(x-editor_geometry.x)/editor_geometry.scale,(y-editor_geometry.y)/editor_geometry.scale);
        }
    }
    process_editor_requests(editor_controller,found->second);
    return true;
}
bool hooked_slate_mouse_up(void* slate,const void* event) {
    const auto identity=*reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(event)+56);
    if(editor_mouse_buttons.erase(identity)){editor.release_barrier(modal_keys.released()?0:1);return true;}
    if(!editor.opened)return original_slate_mouse_up(slate,event);
    editor.release_barrier(modal_keys.released()?0:1);return true;
}
bool hooked_slate_wheel(void* slate,const void* event,const void* gesture) {
    if(!editor.opened)return original_slate_wheel(slate,event,gesture);
    const auto delta=field<Vector2>(const_cast<void*>(event),96).y;
    if(!editor.capturing && editor.confirmation==c::Confirmation::None && std::isfinite(delta) && std::abs(delta)>.001f)
        for(int i=0;i<int(std::ceil(std::min(5.f,std::abs(delta))));++i)editor.navigation(delta>0?c::Navigation::Up:c::Navigation::Down);
    return true;
}
bool hooked_slate_move(void* slate,const void* event,bool synthetic) {return editor.opened || original_slate_move(slate,event,synthetic);}

// The native widget returns an owned, temporary mapping tree. Edit its FKey
// values using the game's copy assignment, preserving native shared references.
// Native texture lookup, Xbox/PS glyph choice and layout happen afterwards.
bool collect_node_keys(void* node,std::vector<void*>& keys,int depth=0) {
    if(depth>8 || keys.size()>256)return false;
    const auto& children=field<Array>(node,128);
    if(children.size<0 || children.size>32 || children.capacity<children.size || (children.size && !children.data))return false;
    if(children.size) {
        for(int i=0;i<children.size;++i)if(!collect_node_keys(children.data+i*144,keys,depth+1))return false;
        return true;
    }
    const auto& actions=field<Array>(node,96);
    if(actions.size<0 || actions.size>32 || actions.capacity<actions.size || (actions.size && !actions.data))return false;
    for(int i=0;i<actions.size;++i) {
        auto key=actions.data+i*40+16;
        if(identify(key)<Key::Motion)keys.push_back(key);
    }
    return keys.size()<=256;
}
bool replace_node_keys(void* node,const c::Button* sequence,size_t sequence_size,size_t& position) {
    std::vector<void*> keys;
    if(!collect_node_keys(node,keys) || (sequence && keys.size()!=sequence_size))return false;
    // Validate the entire tree before changing the first native FKey. Shape
    // errors must not leave a mixture of old and new glyphs in one hint.
    for(auto key:keys) {
        const auto replacement=sequence?sequence[position++]:active_profile.physical(c::Button(identify(key)));
        function<void*(*)(void*,const void*)>(g::customizer_copy_key)(key,native_key(replacement));
    }
    return true;
}
void hooked_input_node(void* widget,void* node) {
    original_input_node(widget,node);
    if(diagnostic_only || !node)return;
    const auto context=field<uint8_t>(widget,2128+81);
    if(!policy::is_combat_context(context) && context!=13)return;
    remember_hint_widget(widget);
    auto controller=function<void*(*)(void*)>(g::widget_player)(widget);
    if(!widget_gamepad(widget,controller))return;
    if(!native_layout_supported())return;
    const auto action=field<uint8_t>(widget,2128+80);
    const bool preset=field<uint8_t>(widget,2128+82)!=0;
    // Dynamic-context widgets also display Focus/menu actions. Their raw
    // controls are deliberately unchanged, so their glyphs must stay native.
    if(!policy::customizer_hint_owned(context,action,preset))return;
    size_t position=0;bool ok=true;
    const auto found=std::find(c::command_actions.begin(),c::command_actions.end(),action);
    if(!preset && found!=c::command_actions.end()) {
        const auto& keys=active_profile.commands[size_t(found-c::command_actions.begin())];
        ok=replace_node_keys(node,keys.data(),keys.size(),position) && position==keys.size();
    } else if(!preset && (action==g::Action_Avoid || action==g::Action_AttackHeavyFront || action==g::Action_PushbackCancel)) {
        const std::array<c::Button,2> chord={active_profile.physical(c::Button::LB),active_profile.avoid[action==g::Action_AttackHeavyFront?1:0]};
        ok=replace_node_keys(node,chord.data(),chord.size(),position) && position==chord.size();
    } else ok=replace_node_keys(node,nullptr,0,position);
    static bool reported=false;
    if(!ok && !reported){log("customizer: native hint tree shape mismatch; do not release this candidate");reported=true;}
}

struct CanvasColor {float r,g,b,a;};
using DrawTextFn=void(*)(void*,void*,const Array*,Vector2,Vector2,CanvasColor,float,CanvasColor,Vector2,bool,bool,bool,CanvasColor);
using DrawTextureFn=void(*)(void*,void*,Vector2,Vector2,Vector2,Vector2,CanvasColor,uint8_t,float,Vector2);
void canvas_text(void* canvas,void* font,const std::wstring& text,float x,float y,float scale,CanvasColor color) {
    Array string{reinterpret_cast<uint8_t*>(const_cast<wchar_t*>(text.c_str())),int32_t(text.size()+1),int32_t(text.size()+1)};
    function<DrawTextFn>(g::customizer_draw_text)(canvas,font,&string,{x,y},{scale,scale},color,0,{0,0,0,.5f},{1,1},false,false,false,{0,0,0,0});
}
void canvas_rect(void* canvas,float x,float y,float w,float h,CanvasColor color) {
    const auto texture=field<void*>(canvas,96);
    if(texture)function<DrawTextureFn>(g::customizer_draw_texture)(canvas,texture,{x,y},{w,h},{0,0},{1,1},color,2,0,{0,0});
}
void hooked_post_render(void* viewport,void* canvas) {
    original_post_render(viewport,canvas);
    if(diagnostic_only || !canvas || !editor.opened)return;
    // PostRender is a game-thread canvas callback. Do not retain UCanvas/UFont
    // across frames or call the renderer from the DLL's initialization thread.
    const auto engine=*reinterpret_cast<void**>(image+g::customizer_engine);
    if(!engine)return;
    // The debug canvas can retain render-target SizeX/SizeY while ClipX/ClipY
    // reflect the DPI-scaled visible window. Use the actual drawing bounds.
    const auto width=field<float>(canvas,48),height=field<float>(canvas,52);
    editor_geometry=c::EditorGeometry::canvas(width,height);editor_canvas_size={width,height};
    if(editor_geometry.scale<=0)return;
    ensure_canvas_font();
    auto font=custom_canvas_font;
    if(!font)return;
    const auto scale=editor_geometry.scale,x=editor_geometry.x,y=editor_geometry.y;
    const CanvasColor normal{.94f,.94f,.94f,1},muted{.65f,.69f,.75f,1},accent{1,.78f,.34f,1},danger{1,.38f,.4f,1};
    const auto text=[&](const std::wstring& s,float dx,float dy,CanvasColor color=CanvasColor{.94f,.94f,.94f,1},float font_scale=1.2f){
        canvas_text(canvas,font,s,x+dx*scale,y+dy*scale,font_scale*scale,color);
    };
    canvas_rect(canvas,x,y,1400*scale,900*scale,{.018f,.023f,.03f,.99f});
    canvas_rect(canvas,x,y,1400*scale,6*scale,{.8f,.08f,.08f,1});
    const auto errors=editor.error_count(),warnings=editor.warning_count();
    text(L"师父 · 操作配置编辑器",40,26,normal,1.5f);
    text(editor.dirty()?L"草稿有未保存修改":L"与已保存配置一致",920,30,editor.dirty()?accent:muted);
    text(L"草稿："+c::shortened(c::profile_title(editor.draft,editor.users),42),40,62,muted,1.05f);
    for(size_t i=0;i<4;++i) {
        const bool selected=size_t(editor.page)==i;
        canvas_rect(canvas,x+(40+200*float(i))*scale,y+86*scale,190*scale,42*scale,selected?CanvasColor{.3f,.07f,.09f,1}:CanvasColor{.06f,.07f,.09f,1});
        text(c::page_names[i],55+200*float(i),92,selected?accent:muted);
    }
    const auto player=players.find(editor_controller);
    const bool native_supported=player!=players.end() && player->second.native_supported;
    text(native_supported?L"保留每招输入步数；技能解锁与使用条件由游戏判断。":L"当前原生手柄方案非默认；可编辑和存槽，应用前请在原生选项选择默认。",40,138,native_supported?muted:accent,1.05f);
    const auto selected=editor.selected(),count=editor.count(),first=editor.first_visible();
    const auto issues=editor.issues();
    for(size_t i=first;i<std::min(count,first+9);++i) {
        const float line=182+52*float(i-first);
        if(i==selected)canvas_rect(canvas,x+24*scale,y+(line-8)*scale,856*scale,48*scale,{.22f,.055f,.065f,1});
        const auto color=editor.page==c::Page::Conflicts && i<issues.size()?(issues[i].severity==c::Severity::Error?danger:accent):normal;
        text(c::shortened(editor.title(i),45),40,line,color);
        if(editor.page==c::Page::Bindings && i<4) {
            for(size_t step=0;step<3;++step) {
                const bool focus=i==selected && editor.slot==step;
                canvas_rect(canvas,x+(500+105*float(step))*scale,y+(line-3)*scale,82*scale,33*scale,focus?CanvasColor{.4f,.16f,.055f,1}:CanvasColor{.09f,.10f,.13f,1});
                text(c::Editor::button(editor.draft.commands[i][step]),516+105*float(step),line,focus?accent:normal);
            }
        } else text(c::shortened(editor.pattern(i,i==selected),36),500,line,i==selected?accent:muted,1.1f);
    }
    canvas_rect(canvas,x+904*scale,y+174*scale,464*scale,490*scale,{.045f,.055f,.07f,1});
    const auto details=c::editor_details(editor);
    for(size_t i=0;i<std::min(size_t(15),details.size());++i)text(details[i],924,188+30*float(i),i?muted:normal,1.14f);
    text(std::to_wstring(selected+1)+L" / "+std::to_wstring(count)+L"   上下选择 / 鼠标滚轮",40,668,muted,1.05f);
    text(std::to_wstring(errors)+L" 个冲突  ·  "+std::to_wstring(warnings)+L" 条提醒",924,678,errors?danger:warnings?accent:normal,1.05f);
    const std::array<float,6> positions={40,230,420,610,800,1150};
    const std::array<std::wstring,6> labels={editor.page==c::Page::Bindings?L"A / Enter 录入":editor.page==c::Page::Presets?L"A 载入草稿":L"A 定位按键",
        L"X / F5 保存应用",editor.page==c::Page::Presets?L"Y 存入自定义":editor.page==c::Page::Bindings?L"Y 恢复当前行":L"此页只读",L"LT / F6 撤销",L"RT / F7 重做",editor.capturing?L"取消录入":L"B / Esc 关闭"};
    for(size_t i=0;i<labels.size();++i) {
        const bool enabled=i==2?(editor.page==c::Page::Bindings || editor.page==c::Page::Presets):i==3?editor.undo_count()>0:i==4?editor.redo_count()>0:true;
        canvas_rect(canvas,x+positions[i]*scale,y+714*scale,(i==5?190.f:180.f)*scale,48*scale,i==1 && errors?CanvasColor{.24f,.04f,.05f,1}:enabled?CanvasColor{.10f,.12f,.16f,1}:CanvasColor{.06f,.07f,.09f,1});
        text(labels[i],positions[i]+10,726,i==1 && errors?danger:enabled?normal:muted,1.05f);
    }
    text(L"LB 前页 · RB / Tab 后页 · Home 恢复已保存配置 · 鼠标可选行、点按钮",40,777,muted,1.02f);
    const auto status=c::wrap_columns(editor.status,138);
    for(size_t i=0;i<std::min(size_t(2),status.size());++i)text(status[i],40,815+27*float(i),accent,1.08f);
    if(editor.capturing)text(L"录入：1=A  2=B  3=X  4=Y  5=LB  6=LT  7=RB  8=L3  9=R3  0=RT；View / Esc 取消",40,864,muted,.98f);
    if(editor.confirmation!=c::Confirmation::None) {
        canvas_rect(canvas,x+330*scale,y+330*scale,740*scale,240*scale,{.045f,.05f,.065f,1});
        const auto lines=c::wrap_columns(editor.status,72);
        for(size_t i=0;i<std::min(size_t(3),lines.size());++i)text(lines[i],370,363+32*float(i),accent,1.25f);
        canvas_rect(canvas,x+430*scale,y+492*scale,250*scale,48*scale,{.30f,.07f,.09f,1});
        canvas_rect(canvas,x+720*scale,y+492*scale,250*scale,48*scale,{.10f,.12f,.16f,1});
        text(L"A / Enter 确认",460,503);text(L"B / Esc 取消",750,503);
    }
}
