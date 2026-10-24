#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <string>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../tools/minhook/include/MinHook.h"
#include "control_state.hpp"
#include "game_symbols.hpp"
#include "input_policy.hpp"
#include "press_router.hpp"
#include "editor.hpp"
#include "editor_view.hpp"
#include "modal_keys.hpp"
#include "legend.hpp"
#include "key_argument.hpp"

using namespace button_controls;
namespace g = button_controls::game;
namespace policy = button_controls::input_policy;
namespace c = button_controls::customizer;
using policy::controlled_actions;

namespace {
uintptr_t image;
FILE* log_file;
HMODULE module;
bool diagnostic_only = false;
bool trace_combat = false;
bool gameplay_logged = false;

template<class T> T& field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}
template<class T> T function(uintptr_t rva) { return reinterpret_cast<T>(image + rva); }
double now() { return GetTickCount64() / 1000.0; }

void log(const char* text) {
    if (log_file) { std::fprintf(log_file, "[%llu] %s\n", GetTickCount64(), text); std::fflush(log_file); }
}

struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Array { uint8_t* data; int32_t size, capacity; };

std::string utf8(const wchar_t* value) {
    if (!value) return "<null>";
    const int length=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    if (length<=0 || length>32768) return "<invalid>";
    std::string result(size_t(length),0);
    WideCharToMultiByte(CP_UTF8,0,value,-1,result.data(),length,nullptr,nullptr);
    result.pop_back();
    return result;
}

std::string name_string(const void* name) {
    wchar_t buffer[1024]{};
    function<uint32_t(*)(const void*,wchar_t*,uint32_t)>(g::fname_to_buffer)(name,buffer,1024);
    return utf8(buffer);
}

bool log_mounted_paks(bool report=true) {
    auto manager=function<void*(*)()>(g::platform_file_manager)();
    auto pak=function<void*(*)(void*,const wchar_t*)>(g::find_platform_file)(manager,L"PakFile");
    if (!pak) { if (report) log("resource diagnostic: PakFile layer not found"); return false; }
    bool mod_mounted=false;
    auto critical=reinterpret_cast<CRITICAL_SECTION*>(static_cast<uint8_t*>(pak)+56);
    EnterCriticalSection(critical);
    const auto& entries=field<Array>(pak,16);
    if (entries.size>=0 && entries.size<1024) {
        for (int32_t i=0;i<entries.size;++i) {
            auto entry=entries.data+i*16;
            auto file=field<void*>(entry,8);
            if (!file) continue;
            const auto& filename=field<Array>(file,8);
            const auto& mount=field<Array>(file,240);
            if (filename.size>0 && filename.size<32768 && mount.size>0 && mount.size<32768) {
                const auto path=reinterpret_cast<const wchar_t*>(filename.data);
                const auto slash=wcsrchr(path,L'/');
                const auto backslash=wcsrchr(path,L'\\');
                const auto basename=slash && (!backslash || slash>backslash)?slash+1:backslash?backslash+1:path;
                if (_wcsicmp(basename,L"ButtonControls_P.pak")==0) mod_mounted=true;
                const auto message="mounted pak: "+utf8(reinterpret_cast<const wchar_t*>(filename.data))+
                    " mount="+utf8(reinterpret_cast<const wchar_t*>(mount.data))+" order="+std::to_string(field<uint32_t>(entry,0));
                if (report) log(message.c_str());
            }
        }
    }
    LeaveCriticalSection(critical);
    return mod_mounted;
}
struct Runtime {
    ControlState controls;
    Context previous = Context::Inactive;
    std::vector<Action> pulses;
    Vector2 command_vector{};
    float avoid_height = -1;
    void* pawn = nullptr;
    void* pad = nullptr;
    void* player_input = nullptr;
    bool rewriting = false;
    uint32_t released_owned = 0;
    uintptr_t last_equipment = 0;
    bool equipment_logged = false;
    bool was_controller = false;
    bool clear_input_buffer = false;
    bool reconcile_keys = false;
    bool device_connected = true;
    uint8_t training_exit_phase = 0;
    uint8_t logged_training_state = 255;
    bool training_guard_logged = false;
    c::PressRouter routes;
    uint64_t profile_revision=0;
    uint32_t physical_held=0;
    uint32_t ui_blocked=0;
    bool native_supported=true;
    uint64_t lifecycle_revision=0;
};
std::unordered_map<void*, Runtime> players;
std::unordered_map<void*,void*> pad_owners;

struct LifecycleEvent { void* object; bool connection; bool connected; };
std::mutex lifecycle_mutex;
std::vector<LifecycleEvent> lifecycle_events;
void customizer_lifecycle(void* controller);
void flush_current_native_input(void* controller);

void queue_lifecycle(LifecycleEvent event) {
    if (diagnostic_only) return;
    std::lock_guard<std::mutex> lock(lifecycle_mutex);
    for (auto& pending:lifecycle_events) {
        if (pending.object==event.object && pending.connection==event.connection) {
            pending.connected=event.connected;
            return;
        }
    }
    lifecycle_events.push_back(event);
}

void apply_lifecycle_events() {
    std::vector<LifecycleEvent> pending;
    { std::lock_guard<std::mutex> lock(lifecycle_mutex); pending.swap(lifecycle_events); }
    for (const auto& event:pending) for (auto& player:players) {
        auto& runtime=player.second;
        if (event.connection ? player.first!=event.object : runtime.player_input!=event.object) continue;
        runtime.controls.interrupted(true);
        ++runtime.lifecycle_revision;
        runtime.routes.flush();
        runtime.physical_held=0;
        if(event.connection)flush_current_native_input(player.first);
        customizer_lifecycle(player.first);
        runtime.pulses.clear();runtime.released_owned=0;runtime.rewriting=false;
        runtime.reconcile_keys=runtime.clear_input_buffer=true;
        if (event.connection) {
            runtime.device_connected=event.connected;
            if (!event.connected) runtime.controls.context(Context::Inactive,now());
        }
        if (trace_combat) log(event.connection?(event.connected?"controller connected; input recovery pending":"controller disconnected; pending commands canceled"):"native input flush; pending commands canceled");
    }
}

void sync_device(Runtime& runtime,bool controller_device) {
    if (controller_device==runtime.was_controller) return;
    runtime.controls.interrupted(true);
    runtime.released_owned=0;
    runtime.clear_input_buffer=true;
    runtime.reconcile_keys=controller_device;
    runtime.was_controller=controller_device;
    if (trace_combat) log(controller_device?"device=controller":"device=keyboard_mouse");
}

void sync_player_input(Runtime& runtime,void* input) {
    if (runtime.player_input==input) return;
    runtime.controls.interrupted(true);
    runtime.released_owned=0;
    runtime.reconcile_keys=runtime.clear_input_buffer=true;
    runtime.player_input=input;
}

using InputKeyFn = bool(*)(void*, const void*, int, float, bool);
using UpdateStackFn = void(*)(void*, float, void*);
using GetContextFn = uint8_t(*)(void*);
using GetConditionFn = void*(*)(void*, uint8_t);
using ConsumeFn = void(*)(void*, uint8_t, bool);
using HasWeaponFn = bool(*)(void*, void**, bool);
using GetVectorFn = Vector2*(*)(void*, Vector2*, uint8_t, bool);
using UIKeyInfosFn = void(*)(void*,bool*,void*,void*,void*,void*,bool*,void*);
using FatalSerializeFn = void(*)(void*,const wchar_t*,uint8_t,const void*);
using NotifyActionFn = void(*)(void*,uint8_t);
using NormalizedInputDurationFn = float(*)(void*,uint8_t);
using ConnectionChangedFn = void(*)(void*,bool,int,int);
using FlushPressedKeysFn = void(*)(void*,bool);
InputKeyFn original_input_key;
UpdateStackFn original_update_stack;
GetVectorFn original_get_vector;
UIKeyInfosFn original_ui_key_infos;
FatalSerializeFn original_fatal_serialize;
NotifyActionFn original_notify_action;
NormalizedInputDurationFn original_normalized_input_duration;
ConnectionChangedFn original_connection_changed;
FlushPressedKeysFn original_flush_pressed_keys;

void hooked_connection_changed(void* controller,bool connected,int user,int id) {
    // Same local-player/controller-id test as ASCPlayerController's native
    // callback, verified at 0x187113c. Queue processing on the input thread.
    const auto local=field<void*>(controller,0x328);
    if (local && field<int32_t>(local,0x108)==id) queue_lifecycle({controller,true,connected});
    original_connection_changed(controller,connected,user,id);
}

void hooked_flush_pressed_keys(void* input,bool send_key_events) {
    // Enqueue before native synthesized releases can arrive at InputKey, so
    // an engine flush never becomes a short RB throw/environmental attack.
    queue_lifecycle({input,false,false});
    original_flush_pressed_keys(input,send_key_events);
}

const char* action_name(Action action) {
    static constexpr std::array<const char*,14> names={"Palm","Sweep","Crotch","Kick","AvoidHigh","AvoidLow","GroundAttack","Pickup","EnvironmentAttack","ThrowObject","DropObject","Takedown","DirectionalThrow","Interact"};
    return names[static_cast<size_t>(action)];
}

const char* native_action_name(uint8_t action) {
    switch (action) {
#define BC_ACTION(name) case g::Action_##name:return #name
    BC_ACTION(AttackLight); BC_ACTION(AttackLightAlt); BC_ACTION(AttackLightHold);
    BC_ACTION(AttackHeavy); BC_ACTION(AttackHeavyAlt); BC_ACTION(AttackHeavyHold);
    BC_ACTION(Avoid); BC_ACTION(PushbackCancel); BC_ACTION(AttackKnockdown); BC_ACTION(AttackKnockdownHold);
    BC_ACTION(PickUpObject); BC_ACTION(ContextualActionObject); BC_ACTION(ThrowContextualObject);
    BC_ACTION(ThrowObject); BC_ACTION(ThrowFromPickUp); BC_ACTION(DropObject);
    BC_ACTION(TakedownValidation); BC_ACTION(PushFromGrab); BC_ACTION(CounterGrab);
    BC_ACTION(AttackLightBackFront); BC_ACTION(AttackHeavyBackFront); BC_ACTION(AttackLightFrontFront); BC_ACTION(AttackHeavyFrontFront);
    BC_ACTION(LightRushAttack); BC_ACTION(HeavyRushAttack); BC_ACTION(AttackLightGuard); BC_ACTION(AttackHeavyGuard);
    BC_ACTION(Dodge); BC_ACTION(Rush); BC_ACTION(Action); BC_ACTION(Traversal); BC_ACTION(LaunchFocusExecution);
    BC_ACTION(StopTraining); BC_ACTION(SkipCinematic); BC_ACTION(TutorialInput);
    BC_ACTION(TrainingNextWeapon); BC_ACTION(TrainingPrevWeapon); BC_ACTION(TrainingEdit);
    BC_ACTION(DialogSentence0); BC_ACTION(DialogSentence1); BC_ACTION(DialogSentence2);
    BC_ACTION(MenuAction); BC_ACTION(MenuActionHold);
#undef BC_ACTION
    default:return nullptr;
    }
}

void hooked_notify_action(void* pad,uint8_t action) {
    if (trace_combat && !diagnostic_only) {
        const auto name=native_action_name(action);
        const auto owner=pad_owners.find(pad);
        const auto player=owner==pad_owners.end()?players.end():players.find(owner->second);
        if (name && player!=players.end()) {
            const auto& runtime=player->second;
            char message[180];
            std::snprintf(message,sizeof(message),"stack action=%u name=%s context=%u",unsigned(action),name,unsigned(runtime.controls.current_context()));
            log(message);
        }
    }
    original_notify_action(pad,action);
}

void hooked_fatal_serialize(void* device,const wchar_t* message,uint8_t verbosity,const void* category) {
    if (message) {
        const int length=WideCharToMultiByte(CP_UTF8,0,message,-1,nullptr,0,nullptr,nullptr);
        if(length>0 && length<1024*1024) {
            std::vector<char> utf8(size_t(length),0);
            WideCharToMultiByte(CP_UTF8,0,message,-1,utf8.data(),length,nullptr,nullptr);
            log(utf8.data());
        }
    }
    original_fatal_serialize(device,message,verbosity,category);
}

void* fighting_pawn(void* controller) {
    return function<void*(*)(void*)>(g::get_fighting_pawn)(controller);
}

bool training_active(void* controller) {
    if (!controller) return false;
    static uint64_t hud_name=0;
    if (!hud_name) function<void*(*)(void*,const wchar_t*,int)>(g::name_from_wide)(&hud_name,L"HUDTraining",0);
    if (!hud_name) return false;
    const auto cls=field<void*>(controller,16);
    static std::unordered_map<void*,int32_t> offsets;
    auto cached=offsets.find(cls);
    if (cached==offsets.end()) {
        const auto property=function<void*(*)(void*,uint64_t)>(g::find_property_by_name)(cls,hud_name);
        const auto offset=property && field<int32_t>(property,60)==8 ? field<int32_t>(property,76) : -1;
        cached=offsets.emplace(cls,offset>=0 && offset<=field<int32_t>(cls,88)-8 ? offset : -1).first;
        if (trace_combat && cached->second>=0) {
            char message[120];std::snprintf(message,sizeof(message),"training detector HUDTraining offset=%d",cached->second);log(message);
        }
    }
    if (cached->second<0) return false;
    const auto hud=field<void*>(controller,size_t(cached->second));
    // Verified against live before/inside-training snapshots and the controller
    // blueprint: CreateTrainingRoomWidget stores this strong reference, while
    // DestroyTrainingRoomWidget removes the widget and explicitly clears it.
    return hud && name_string(static_cast<uint8_t*>(field<void*>(hud,16))+24)=="BP_HUD_TrainingRoom_C";
}

Context context_for(void* controller);
bool native_layout_supported();

float hooked_normalized_input_duration(void* controller,uint8_t action) {
    // TrainingRoom_LD polls SkipCinematic's normalized duration directly; it
    // does not consume an action ticket. Keep cinematics and keyboard intact.
    apply_lifecycle_events();
    if (!diagnostic_only && controller && action==g::Action_SkipCinematic && native_layout_supported() &&
        function<uint8_t(*)(void*)>(g::get_device_mode)(controller)==0 && training_active(controller)) {
        const auto player=players.find(controller);
        if (player==players.end()) return 0.f;
        auto& runtime=player->second;
        runtime.controls.context(context_for(controller),now());
        runtime.controls.training(true);
        const auto progress=float(runtime.controls.training_exit_progress(now()));
        if (trace_combat && !runtime.training_guard_logged) {
            log("training exit query redirected: SkipCinematic -> physical R3; idle/A progress=0");
            runtime.training_guard_logged=true;
        }
        const uint8_t phase=progress>=1.f?2:progress>0.f?1:0;
        if (trace_combat && phase!=runtime.training_exit_phase) {
            char message[120];
            std::snprintf(message,sizeof(message),"training exit progress=%0.3f phase=%u",double(progress),unsigned(phase));
            log(message);
        }
        runtime.training_exit_phase=phase;
        return progress;
    }
    return original_normalized_input_duration(controller,action);
}

Context context_for(void* controller) {
    if (!controller) return Context::Inactive;
    const auto player=players.find(controller);
    if (player!=players.end() && !player->second.device_connected) return Context::Inactive;
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
    if (foreground_pid != GetCurrentProcessId()) return Context::Inactive;
    const auto type = function<GetContextFn>(g::get_context)(controller);
    if (type == 10) return Context::Focus;
    if (!policy::is_combat_context(type) || field<uint8_t>(controller, 1856 + 9)) return Context::Menu;
    if (!fighting_pawn(controller)) return Context::Inactive;
    return Context::Gameplay;
}

void* equipment_object(void* controller) {
    auto pawn = fighting_pawn(controller);
    if (!pawn) return nullptr;
    // Includes held bottles/throwables, not only ABaseWeapon subclasses.
    return function<void*(*)(void*)>(g::get_weak_object)(static_cast<uint8_t*>(pawn)+g::picked_up_object_offset);
}

uintptr_t equipment(void* controller) {
    auto pawn=fighting_pawn(controller);
    if (!pawn || !equipment_object(controller)) return 0;
    // Object index + serial distinguish a new item reusing the same address.
    return field<uintptr_t>(pawn,g::picked_up_object_offset);
}

uint8_t held_throw_input(void* pawn) {
    if (!pawn) return g::Action_None;
    const auto item=function<void*(*)(void*)>(g::get_weak_object)(static_cast<uint8_t*>(pawn)+g::picked_up_object_offset);
    if (!item) return g::Action_None;
    // m_currentPickedUpObject is a weak AInteractiveMovable. Its native typed
    // interaction component owns the available input IDs; use the same list
    // that UUseInteractiveObjectAbility::IterActionsForMovable intersects.
    const auto component=field<void*>(item,g::movable_interaction_component_offset);
    if (!component) return g::Action_None;
    const auto& inputs=field<Array>(component,g::movable_input_actions_offset);
    if (inputs.size<0 || inputs.size>g::Action_Count || inputs.capacity<inputs.size) return g::Action_None;
    return policy::held_throw_action(inputs.data,std::size_t(inputs.size));
}

bool interactive_dialog() {
    // Read the existing manager only; do not construct one during level load.
    // Get()'s RIP-relative instance and this const getter are verified against
    // the matching PDB/EXE. A reply selection must own DpadLeft over Bullet Time.
    const auto manager=*reinterpret_cast<void**>(image+g::dialog_manager_instance);
    return manager && function<bool(*)(void*)>(g::interactive_dialog)(manager);
}

const std::array<std::pair<uintptr_t, Key>,10> bindings = {{
        {g::key_FaceButton_Bottom,Key::A}, {g::key_FaceButton_Right,Key::B},
        {g::key_FaceButton_Left,Key::X}, {g::key_FaceButton_Top,Key::Y},
        {g::key_LeftShoulder,Key::LB}, {g::key_LeftTrigger,Key::LT},
        {g::key_RightShoulder,Key::RB}, {g::key_LeftThumbstick,Key::L3},
        {g::key_RightThumbstick,Key::R3}, {g::key_RightTrigger,Key::RT}
}};

Key identify(const void* key) {
    const auto name = *static_cast<const uint64_t*>(key);
    for (const auto& binding : bindings) {
        if (*reinterpret_cast<const uint64_t*>(image + binding.first) == name) return binding.second;
    }
    const auto name_text=name_string(key);
    if (name_text.rfind("Gamepad_LeftStick_",0)==0 || name_text.rfind("Gamepad_RightStick_",0)==0 ||
        name_text=="Gamepad_LeftX" || name_text=="Gamepad_LeftY" || name_text=="Gamepad_RightX" || name_text=="Gamepad_RightY") return Key::Motion;
    return Key::Other;
}

bool physical_held_keys(void* controller,uint32_t& mask) {
    if (!field<void*>(controller,0x3d8)) return false;
    mask=0;
    for (const auto& binding:bindings) {
        // Native FKey is FName + an optional shared detail pointer. Construct
        // a name-only value with empty details, so by-value native destruction
        // cannot decrement the global EKeys object's shared reference count.
        std::array<uint64_t,3> key={*reinterpret_cast<uint64_t*>(image+binding.first),0,0};
        if (function<bool(*)(void*,void*)>(g::is_input_key_down)(controller,key.data())) mask|=bit(binding.second);
    }
    return true;
}

void enable(void* pad, uint8_t action, bool active) {
    auto condition = function<GetConditionFn>(g::get_condition)(pad, action);
    if (!condition) {
        if (active && trace_combat) {
            char message[100];std::snprintf(message,sizeof(message),"native pulse skipped: action=%u condition=missing",unsigned(action));log(message);
        }
        return;
    }
    field<uint8_t>(condition, 32) = active;
    if (active) {
        field<float>(condition,16) = 0;
        field<int32_t>(condition,20) = 0;
        field<uint8_t>(condition,56) = 0;
    }
}

#include "customizer_runtime.inc"

bool hooked_input_key(void* controller, const void* key, int event, float amount, bool gamepad) {
    c::KeyArgument incoming(key,function<c::KeyArgument::Destroy>(g::customizer_destroy_key));
    const auto dispatch=[&](const void* target) {
        return incoming.dispatch(target,copy_native_key,[&](void* owned){
            return original_input_key(controller,owned,event,amount,gamepad);
        });
    };
    if (!diagnostic_only) apply_lifecycle_events();
    const auto before=diagnostic_only?Context::Inactive:context_for(controller);
    if (diagnostic_only)return dispatch(key);
    auto& pre_runtime=players[controller];
    ensure_profile(pre_runtime);
    sync_layout_support(controller,pre_runtime);
    if(!pre_runtime.native_supported)return dispatch(key);
    sync_routes(controller,pre_runtime,before);
    const auto physical=identify(key);
    if(gamepad && physical<Key::Motion) {
        if(event==0)pre_runtime.physical_held|=bit(physical);
        else if(event==1)pre_runtime.physical_held&=~bit(physical);
    }
    if(editor_input(controller,pre_runtime,key,event,gamepad,before))return true;
    c::PressRoute route={c::Button(physical),before==Context::Gameplay};
    const void* forwarded=key;
    if(gamepad && physical<Key::Motion) {
        route=pre_runtime.routes.event(c::Button(physical),event,route_scope(before),active_profile);
        if(!route.forward)return true;
        if(route.logical!=c::Button(physical))forwarded=native_key(route.logical);
    }
    const auto lifecycle_before=pre_runtime.lifecycle_revision;
    const bool result = dispatch(forwarded);
    apply_lifecycle_events();
    auto& runtime = players[controller];
    if(gamepad && physical<Key::Motion && event==0 && runtime.device_connected &&
       lifecycle_before!=runtime.lifecycle_revision &&
       runtime.routes.recover_inflight_press(c::Button(physical),route,route_scope(before)))
        runtime.physical_held|=bit(physical);
    sync_player_input(runtime,field<void*>(controller,0x3d8));
    const auto context = context_for(controller);
    sync_routes(controller,runtime,context);
    auto pawn = fighting_pawn(controller);
    if (runtime.pawn != pawn) {
        runtime.controls.interrupted(true);
        runtime.reconcile_keys=runtime.clear_input_buffer=true;
        runtime.pawn = pawn;
    }
    runtime.controls.context(context,now());
    runtime.controls.training(training_active(controller));
    // Synchronize before recording the press that switches device, so the
    // first new A/RT/RB press is not discarded by the next stack update.
    sync_device(runtime,function<uint8_t(*)(void*)>(g::get_device_mode)(controller)==0);
    if (gamepad && (event == 0 || event == 1)) {
        if(physical<Key::Motion && runtime.routes.quarantined(c::Button(physical)))return result;
        const auto logical_key = physical<Key::Motion?Key(route.logical):physical;
        if (event==1 && (runtime.controls.owned_mask()&bit(logical_key))) runtime.released_owned |= bit(logical_key);
        runtime.controls.key(logical_key,event == 0,now(),context == Context::Gameplay ? equipment(controller) : 0,before==Context::Gameplay && route.gameplay);
        if (trace_combat && logical_key!=Key::Motion) {
            char message[200];
            std::snprintf(message,sizeof(message),"key=%s event=%s context=%u equipment=%llx",name_string(key).c_str(),event==0?"down":"up",unsigned(context),static_cast<unsigned long long>(equipment(controller)));
            log(message);
        }
    } else if (!gamepad && event == 0) {
        runtime.controls.interrupted();
        runtime.released_owned=0;
    }
    return result;
}

void clear_gameplay_tickets(void* stack) {
    const auto consume = function<ConsumeFn>(g::consume_stack);
    for (const auto action : controlled_actions) consume(stack,action,true);
    for (const auto action : policy::auxiliary_actions) consume(stack,action,true);
}

void apply_pulse(void* pad, Runtime& runtime, Action pulse) {
    switch (pulse) {
    case Action::Palm: enable(pad,g::Action_AttackLightBackFront,true); break;
    case Action::Sweep: enable(pad,g::Action_AttackHeavyBackFront,true); break;
    case Action::Crotch: enable(pad,g::Action_AttackLightFrontFront,true); break;
    case Action::Kick: enable(pad,g::Action_AttackHeavyFrontFront,true); break;
    case Action::AvoidHigh:
    case Action::AvoidLow:
        runtime.avoid_height = pulse == Action::AvoidHigh ? -1.f : 1.f;
        enable(pad,g::Action_Avoid,true);
        if (pulse == Action::AvoidHigh) enable(pad,g::Action_PushbackCancel,true);
        break;
    case Action::GroundAttack: enable(pad,g::Action_AttackKnockdownHold,true); break;
    case Action::Pickup:
        enable(pad,g::Action_PickUpObject,true);
        enable(pad,g::Action_ContextualActionObject,true);
        break;
    // Empty-hand environmental throw is the ground handler's 99, with the
    // stock Environmental Mastery skill check. 103 belongs to carried items.
    case Action::EnvironmentAttack: enable(pad,g::Action_ThrowObject,true); break;
    case Action::ThrowObject: {
        const auto input=held_throw_input(runtime.pawn);
        if (trace_combat) {
            char message[140];std::snprintf(message,sizeof(message),"held throw native=%u name=%s",unsigned(input),native_action_name(input)?native_action_name(input):"unsupported");log(message);
        }
        if (input!=g::Action_None) enable(pad,input,true);
        break;
    }
    case Action::DropObject: enable(pad,g::Action_DropObject,true); break;
    case Action::Takedown: enable(pad,g::Action_TakedownValidation,true); break;
    case Action::DirectionalThrow:
        enable(pad,g::Action_PushFromGrab,true);
        enable(pad,g::Action_CounterGrab,true);
        break;
    case Action::Interact:
        enable(pad,g::Action_Action,true);
        enable(pad,g::Action_Traversal,true);
        break;
    }
}

bool command(uint8_t action) {
    return action == g::Action_AttackLightBackFront || action == g::Action_AttackHeavyBackFront ||
           action == g::Action_AttackLightFrontFront || action == g::Action_AttackHeavyFrontFront;
}

Vector2* hooked_get_vector(void* pad, Vector2* result, uint8_t action, bool raw) {
    const auto owner=pad_owners.find(pad);
    const auto player=owner==pad_owners.end()?players.end():players.find(owner->second);
    if (player!=players.end()) {
        auto& runtime = player->second;
        if (!runtime.rewriting || runtime.controls.current_context() != Context::Gameplay)
            return original_get_vector(pad,result,action,raw);
        if (action == g::Action_Avoid || action == g::Action_PushbackCancel) {
            *result = {runtime.avoid_height,0};
            return result;
        }
        if (command(action)) {
            *result = runtime.command_vector;
            return result;
        }
    }
    return original_get_vector(pad,result,action,raw);
}

void hooked_ui_key_infos(void* widget,bool* valid,void* node,void* texture,
                         void* key,void* label,bool* hold,void* separator) {
    if (diagnostic_only) {
        original_ui_key_infos(widget,valid,node,texture,key,label,hold,separator);
        return;
    }
    auto& action = field<uint8_t>(widget,2128+80);
    auto& axis = field<uint8_t>(widget,2101);
    const auto saved_action = action;
    const auto saved_axis = axis;
    const auto mapping_context=field<uint8_t>(widget,2128+81);
    const bool preset=field<uint8_t>(widget,2128+82)!=0;
    const bool gameplay_mapping = (policy::is_combat_context(mapping_context) || mapping_context==13) && !preset;
    auto controller = function<void*(*)(void*)>(g::widget_player)(widget);
    const bool controller_icons = widget_gamepad(widget,controller);
    bool button_avoid = false;
    if(!native_layout_supported()) {
        if(action==g::Action_StopTraining)action=g::Action_SkipCinematic;
        original_ui_key_infos(widget,valid,node,texture,key,label,hold,separator);
        action=saved_action;return;
    }
    if (!diagnostic_only && gameplay_mapping) {
        if (!controller_icons && action==g::Action_StopTraining) action=g::Action_SkipCinematic;
        if (controller_icons && (command(action) || action==g::Action_Avoid || action==g::Action_PushbackCancel || action==g::Action_AttackHeavyFront)) {
            if (action==g::Action_Avoid && axis==5) action=g::Action_AttackHeavyFront;
            button_avoid = action==g::Action_Avoid || action==g::Action_AttackHeavyFront;
            axis=9; // EControllerIconAxisTypes::None
        } else if (!controller_icons && action==g::Action_AttackHeavyFront) {
            action=g::Action_Avoid;
            axis=5; // Keyboard still displays the original low-avoid direction.
        }
    }
    original_ui_key_infos(widget,valid,node,texture,key,label,hold,separator);
    static std::unordered_set<uint64_t> reported;
    const auto slot=field<int32_t>(widget,2080);
    const uint64_t signature=uint64_t(saved_action) | (uint64_t(mapping_context)<<8) | (uint64_t(saved_axis)<<16) |
        (uint64_t(preset)<<24) | (uint64_t(controller_icons)<<25) | (uint64_t(uint8_t(slot))<<32);
    if (trace_combat && reported.size()<4096 && reported.insert(signature).second) {
        auto input=field<void*>(node,0);
        const auto root=input?name_string(static_cast<uint8_t*>(input)+24):"<none>";
        const auto icon_key=name_string(key);
        char message[1024];
        std::snprintf(message,sizeof(message),"ui action=%u context=%u preset=%u axis=%u slot=%d controller=%u valid=%u root=%s key=%s",
            unsigned(saved_action),unsigned(mapping_context),unsigned(preset),unsigned(saved_axis),slot,unsigned(controller_icons),unsigned(*valid),root.c_str(),icon_key.c_str());
        log(message);
    }
    if (button_avoid) *hold=false;
    action=saved_action;
    axis=saved_axis;
}

void patch_ticket_vectors(void* stack, Runtime& runtime) {
    auto& tickets = *static_cast<Array*>(stack);
    if (tickets.size < 0 || tickets.size > 512) return;
    for (int i=0;i<tickets.size;++i) {
        auto ticket = tickets.data + i*88;
        if (!ticket[29]) continue;
        const auto action = ticket[0];
        if (action == g::Action_Avoid || action == g::Action_PushbackCancel) {
            field<Vector2>(ticket,48) = {runtime.avoid_height,0};
        } else if (command(action)) {
            field<Vector2>(ticket,48) = runtime.command_vector;
        }
    }
}

void hooked_update_stack(void* stack, float delta, void* controller) {
    if (diagnostic_only || !controller) {
        original_update_stack(stack,delta,controller);
        return;
    }
    apply_lifecycle_events();
    auto& runtime = players[controller];
    ensure_profile(runtime);
    sync_layout_support(controller,runtime);
    if(!runtime.native_supported){runtime.rewriting=false;original_update_stack(stack,delta,controller);return;}
    const auto pad=static_cast<uint8_t*>(stack) - 1808;
    if (runtime.pad && runtime.pad!=pad) {
        const auto owner=pad_owners.find(runtime.pad);
        if (owner!=pad_owners.end() && owner->second==controller) pad_owners.erase(owner);
        runtime.controls.interrupted(true);
        runtime.reconcile_keys=runtime.clear_input_buffer=true;
    }
    runtime.pad=pad;
    sync_player_input(runtime,field<void*>(controller,0x3d8));
    pad_owners[runtime.pad]=controller;
    runtime.rewriting = false;
    const auto native_context=function<GetContextFn>(g::get_context)(controller);
    const auto context = context_for(controller);
    const auto time = now();
    sync_routes(controller,runtime,context);
    runtime.controls.context(context,time);
    runtime.controls.training(training_active(controller));
    if (trace_combat && runtime.logged_training_state!=uint8_t(runtime.controls.in_training())) {
        log(runtime.controls.in_training()?"training=1":"training=0");
        runtime.logged_training_state=uint8_t(runtime.controls.in_training());
    }
    if (!runtime.controls.in_training()) runtime.training_guard_logged=false;
    if (context != runtime.previous) {
        clear_gameplay_tickets(stack);
        runtime.reconcile_keys=true;
        runtime.previous = context;
        char message[160];
        std::snprintf(message,sizeof(message),"context=%u native=%u controller=%p",unsigned(context),unsigned(native_context),controller);
        log(message);
    }
    // Both the key-event handler and the stack callback run on the game thread.
    auto pawn = fighting_pawn(controller);
    if (runtime.pawn != pawn) {
        runtime.controls.interrupted(true);
        runtime.reconcile_keys=runtime.clear_input_buffer=true;
        runtime.pawn = pawn;
    }
    const bool controller_device=function<uint8_t(*)(void*)>(g::get_device_mode)(controller)==0;
    sync_device(runtime,controller_device);
    if (runtime.clear_input_buffer) {
        clear_gameplay_tickets(stack);
        runtime.clear_input_buffer=false;
    }
    if (context==Context::Gameplay && controller_device && runtime.reconcile_keys) {
        uint32_t physical=0;
        if (physical_held_keys(controller,physical)) {
            runtime.released_owned|=runtime.controls.owned_mask() & runtime.controls.held_mask() & ~physical;
            runtime.controls.reconcile_held(physical);
            runtime.reconcile_keys=false;
        }
    }
    if (context != Context::Gameplay || !controller_device) {
        runtime.controls.interrupted(true);
        if (policy::suppress_gameplay_pad(context,native_context,controller_device)) {
            for (auto action:controlled_actions) enable(runtime.pad,action,false);
            for (auto action:policy::auxiliary_actions) enable(runtime.pad,action,false);
            clear_gameplay_tickets(stack);
        }
        original_update_stack(stack,delta,controller);
        runtime.released_owned=0;
        return;
    }
    if (!gameplay_logged) { log("gameplay controller input active"); log_mounted_paks(); gameplay_logged=true; }
    runtime.rewriting = true;
    if (pawn && field<uint8_t>(pawn,0xcf8)) runtime.controls.interrupted();
    const auto held_item=equipment(controller);
    if (trace_combat && (!runtime.equipment_logged || held_item!=runtime.last_equipment)) {
        auto object=equipment_object(controller);
        const auto type=object?name_string(static_cast<uint8_t*>(field<void*>(object,16))+24):"unarmed";
        char message[220];
        std::snprintf(message,sizeof(message),"equipment from=%llx to=%llx type=%s",static_cast<unsigned long long>(runtime.last_equipment),static_cast<unsigned long long>(held_item),type.c_str());
        log(message);
        runtime.last_equipment=held_item;
        runtime.equipment_logged=true;
    }
    runtime.controls.tick(time,held_item);
    runtime.pulses = runtime.controls.take_events();
    for (auto action : controlled_actions) enable(pad,action,false);
    uint32_t owned=runtime.controls.owned_mask()|runtime.released_owned;
    for (auto pulse : runtime.pulses) {
        if (pulse == Action::Palm || pulse == Action::Crotch || pulse == Action::DirectionalThrow) owned|=bit(Key::X);
        if (pulse == Action::Sweep || pulse == Action::Kick) owned|=bit(Key::Y);
    }
    // Release frames and buffered native tickets share the same ownership
    // rule. A command, focus transition or deferred R3 cannot leak a follow-up.
    const bool dialog_choice=interactive_dialog();
    for (auto action:policy::auxiliary_actions) {
        if (policy::blocked_original(action,owned,runtime.controls.in_training(),dialog_choice)) {
            enable(pad,action,false);
            function<ConsumeFn>(g::consume_stack)(stack,action,true);
        }
    }
    for (auto pulse : runtime.pulses) {
        if (pulse == Action::Palm || pulse == Action::Sweep || pulse == Action::Crotch || pulse == Action::Kick) {
            original_get_vector(pad,&runtime.command_vector,g::Action_Move,false);
        }
        apply_pulse(pad,runtime,pulse);
        char message[100];std::snprintf(message,sizeof(message),"action=%u name=%s",unsigned(pulse),action_name(pulse));log(message);
    }
    // Supply vectors before native target selection and world-space conversion.
    original_update_stack(stack,delta,controller);
    patch_ticket_vectors(stack,runtime);
    runtime.released_owned=0;
}

bool matching_executable() {
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);
    HANDLE file = CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)==0;
    std::vector<uint8_t> buffer(1<<20);DWORD read=0;
    while(ok) {
        if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&read,nullptr)) {ok=false;break;}
        if(!read)break;
        ok=BCryptHashData(hash,buffer.data(),read,0)==0;
    }
    std::array<uint8_t,32> digest{};
    if(ok)ok=BCryptFinishHash(hash,digest.data(),DWORD(digest.size()),0)==0;
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);
    const char* expected="66845653ff78e4f1c31d7cfefccdcea435cbb5dc186451d443747b849c125e7e";
    char hex[65];for(size_t i=0;i<digest.size();++i)std::snprintf(hex+i*2,3,"%02x",digest[i]);
    return ok && std::strcmp(hex,expected)==0;
}

DWORD WINAPI initialize(void*) {
    wchar_t path[MAX_PATH];GetModuleFileNameW(module,path,MAX_PATH);
    std::wstring directory(path);directory.resize(directory.find_last_of(L"\\/")+1);
    log_file=_wfsopen((directory+L"ButtonControls.log").c_str(),L"w",_SH_DENYNO);
    log("Sifu Button Controls 0.2.1 editor candidate initializing");
    if(!matching_executable()) {log("Unsupported executable hash; no hooks installed.");return 0;}
    image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    profile_path=directory+L"ButtonControls.profile.ini";
    load_profile();
    const auto config=directory+L"ButtonControls.ini";
    diagnostic_only=GetPrivateProfileIntW(L"Debug",L"DiagnosticOnly",0,config.c_str())!=0;
    trace_combat=GetPrivateProfileIntW(L"Debug",L"TraceCombat",0,config.c_str())!=0;
    if (!diagnostic_only) {
        // Startup time varies with storage/cache. Wait on the actual mount,
        // rather than treating one sample after 1.5 seconds as definitive.
        bool mounted=false;
        for (int attempt=0;attempt<60;++attempt) {
            if (log_mounted_paks(false)) { mounted=true;break; }
            if (attempt==0) log("Waiting for paired ButtonControls_P.pak mount (up to 30 seconds).");
            Sleep(500);
        }
        log_mounted_paks();
        if (!mounted) {
            log("Required ButtonControls_P.pak is not mounted; no control or UI hooks installed. Reinstall the paired .pak and .sig, then restart Sifu.");
            return 0;
        }
    }
    if(MH_Initialize()!=MH_OK){log("MinHook initialization failed.");return 0;}
    if(MH_CreateHook(reinterpret_cast<void*>(image+g::input_key),reinterpret_cast<void*>(hooked_input_key),reinterpret_cast<void**>(&original_input_key))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::update_stack),reinterpret_cast<void*>(hooked_update_stack),reinterpret_cast<void**>(&original_update_stack))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::get_vector),reinterpret_cast<void*>(hooked_get_vector),reinterpret_cast<void**>(&original_get_vector))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::ui_key_infos),reinterpret_cast<void*>(hooked_ui_key_infos),reinterpret_cast<void**>(&original_ui_key_infos))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::notify_action),reinterpret_cast<void*>(hooked_notify_action),reinterpret_cast<void**>(&original_notify_action))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::normalized_input_duration),reinterpret_cast<void*>(hooked_normalized_input_duration),reinterpret_cast<void**>(&original_normalized_input_duration))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::controller_connection_changed),reinterpret_cast<void*>(hooked_connection_changed),reinterpret_cast<void**>(&original_connection_changed))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::flush_pressed_keys),reinterpret_cast<void*>(hooked_flush_pressed_keys),reinterpret_cast<void**>(&original_flush_pressed_keys))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::fatal_serialize),reinterpret_cast<void*>(hooked_fatal_serialize),reinterpret_cast<void**>(&original_fatal_serialize))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_input_node),reinterpret_cast<void*>(hooked_input_node),reinterpret_cast<void**>(&original_input_node))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_set_rich_text),reinterpret_cast<void*>(hooked_set_rich_text),reinterpret_cast<void**>(&original_set_rich_text))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_post_render),reinterpret_cast<void*>(hooked_post_render),reinterpret_cast<void**>(&original_post_render))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_slate_down),reinterpret_cast<void*>(hooked_slate_down),reinterpret_cast<void**>(&original_slate_down))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_slate_up),reinterpret_cast<void*>(hooked_slate_up),reinterpret_cast<void**>(&original_slate_up))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_slate_analog),reinterpret_cast<void*>(hooked_slate_analog),reinterpret_cast<void**>(&original_slate_analog))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_slate_char),reinterpret_cast<void*>(hooked_slate_char),reinterpret_cast<void**>(&original_slate_char))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_slate_mouse_down),reinterpret_cast<void*>(hooked_slate_mouse_down),reinterpret_cast<void**>(&original_slate_mouse_down))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_slate_mouse_up),reinterpret_cast<void*>(hooked_slate_mouse_up),reinterpret_cast<void**>(&original_slate_mouse_up))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_slate_wheel),reinterpret_cast<void*>(hooked_slate_wheel),reinterpret_cast<void**>(&original_slate_wheel))!=MH_OK ||
       MH_CreateHook(reinterpret_cast<void*>(image+g::customizer_slate_move),reinterpret_cast<void*>(hooked_slate_move),reinterpret_cast<void**>(&original_slate_move))!=MH_OK){log("Hook creation failed; no hooks enabled.");return 0;}
    if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK){
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        log("Hook enable failed; hooks disabled.");return 0;
    }
    log(diagnostic_only?"Diagnostic hooks installed; gameplay rewriting disabled.":"Input hooks installed.");
    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        module=instance;DisableThreadLibraryCalls(instance);
        const auto thread=CreateThread(nullptr,0,initialize,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);
    }
    return TRUE;
}
