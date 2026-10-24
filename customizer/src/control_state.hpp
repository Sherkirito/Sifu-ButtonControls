#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "profile.hpp"

namespace button_controls {

enum class Key : uint8_t { A, B, X, Y, LB, LT, RB, L3, R3, RT, Motion, Other, Count };
enum class Context : uint8_t { Gameplay, Focus, Menu, Inactive };
enum class Action : uint8_t { Palm, Sweep, Crotch, Kick, AvoidHigh, AvoidLow,
    GroundAttack, Pickup, EnvironmentAttack, ThrowObject, DropObject, Takedown,
    DirectionalThrow, Interact };

constexpr uint32_t bit(Key key) { return uint32_t{1} << static_cast<unsigned>(key); }

struct Settings {
    double sequence_gap = 0.40;
    double b_hold = 0.30;
    double rb_hold = 0.30;
    double training_exit_hold = 1.0;
};

// A press has one owner. It cannot change from an avoid to a ground attack,
// or from pickup to drop, just because the surrounding state changed.
class ControlState {
public:
    explicit ControlState(Settings settings = {}) : settings_(settings) {}

    // Call only after Profile validation. The layout layer translates physical
    // buttons before they reach this state machine. The editor migrates linked
    // physical command records when a basic role is swapped.
    void configure(const customizer::Profile& profile) {
        interrupted(true);
        const auto defaults=customizer::Profile{};
        custom_commands_=false;
        for(size_t i=0;i<4;++i)for(size_t j=0;j<3;++j) {
            command_keys_[i][j]=Key(profile.logical(profile.commands[i][j]));
            custom_commands_|=command_keys_[i][j]!=Key(defaults.commands[i][j]);
        }
        for(size_t i=0;i<2;++i)avoid_keys_[i]=Key(profile.logical(profile.avoid[i]));
    }

    void context(Context context, double now) {
        if (context_ == context) return;
        context_ = context;
        cancel();
        blocked_ |= held_;
        b_pending_ = rb_pending_ = r3_pending_ = false;
        events_.clear();
        last_step_ = now;
    }

    void cancel() {
        sequence_ = Sequence::Empty;
        custom_prefix_.clear();
    }

    void training(bool active) {
        if (training_ == active) return;
        training_ = active;
        r3_pending_ = false;
        blocked_ |= held_ & bit(Key::R3);
    }

    bool in_training() const { return training_; }
    double training_exit_progress(double now) const {
        if (!training_ || !r3_pending_ || !held(Key::R3) || context_ != Context::Gameplay || owned(Key::R3)) return 0;
        if (reached(now,r3_start_,settings_.training_exit_hold)) return 1;
        const auto progress = (now - r3_start_) / settings_.training_exit_hold;
        return progress < 0 ? 0 : progress > 1 ? 1 : progress;
    }

    void interrupted(bool all_keys = false) {
        const auto pending=custom_prefix_mask();
        cancel();
        blocked_ |= pending | (all_keys ? held_ : held_ & (bit(Key::A) | bit(Key::B) | bit(Key::RB) | (training_ ? bit(Key::R3) : 0)));
        b_pending_ = rb_pending_ = r3_pending_ = false;
        events_.clear();
    }

    void key(Key key, bool down, double now, uintptr_t equipped = 0, bool gameplay_press = true) {
        if (!gameplay_press) interrupted(true);
        // Complete elapsed holds before the next event changes ownership. This
        // makes a 0.30s hold behave consistently when a frame is delayed.
        tick(now,equipped);
        if (key == Key::Motion) return;
        // Several unrelated physical buttons map to Other. It is an interrupt
        // notification, not one shared held bit that can swallow a second key.
        if (key == Key::Other) {
            if (down && context_ == Context::Gameplay) {
                cancel_b_hold();cancel_rb_hold();cancel_r3_hold();cancel_held_prefix();
            }
            return;
        }
        const auto mask = bit(key);
        const bool was_down = (held_ & mask) != 0;
        if (down == was_down) return; // Ignore repeat notifications.
        if (down) held_ |= mask;
        else held_ &= ~mask;

        const bool blocked = (blocked_ & mask) != 0;
        if (!down) {
            if (key == Key::R3) {
                if (r3_pending_ && !blocked && training_ && context_ == Context::Gameplay && !reached(now,r3_start_,settings_.training_exit_hold))
                    emit(Action::Interact);
                r3_pending_ = false;
            }
            if (key == Key::RB && rb_pending_ && !blocked && context_ == Context::Gameplay) {
                if (rb_equipped_ == equipped) {
                    if (!reached(now,rb_start_,settings_.rb_hold)) {
                        emit(rb_equipped_ ? Action::ThrowObject : Action::EnvironmentAttack);
                    } else {
                        emit(rb_equipped_ ? Action::DropObject : Action::Pickup);
                    }
                }
                rb_pending_ = false;
            }
            if (key == Key::B) {
                if (b_pending_ && !blocked && context_ == Context::Gameplay && reached(now,b_start_,settings_.b_hold)) {
                    cancel();
                    emit(Action::GroundAttack);
                }
                b_pending_ = false;
            }
            blocked_ &= ~mask;
            return;
        }
        if (!gameplay_press) {
            // The native handler can close a menu/Focus before our callback
            // resumes. That closing press still belongs to the previous UI.
            blocked_|=mask;
            return;
        }
        if (blocked || context_ != Context::Gameplay) return;

        // A newer combat press takes over unfinished holds. Movement axes do
        // not arrive here, so walking while holding RB remains possible.
        if (key != Key::RB) cancel_rb_hold();
        if (key != Key::B) cancel_b_hold();
        if (key != Key::R3) cancel_r3_hold();

        if (held(Key::LB) && (key==avoid_keys_[0] || key==avoid_keys_[1])) {
            cancel_held_prefix();
            emit(key==avoid_keys_[0]?Action::AvoidHigh:Action::AvoidLow);
            blocked_|=mask;
            return;
        }
        // Resolve a fully entered command before an RT/RB/L3/R3 final press
        // can trigger the utility assigned to that same button.
        if(custom_commands_ && custom_command_key(key,now))return;

        if (key == Key::R3 && training_) {
            cancel_held_prefix();
            r3_pending_ = true;
            r3_start_ = now;
            return;
        }

        if (key == Key::LB || key == Key::LT || key == Key::L3 || key == Key::R3 || key == Key::Other) {
            cancel_held_prefix();
            if (key == Key::LT) {
                // Input events can arrive RT-first within the same game frame.
                // Focus owns that frame's uncommitted RT confirmation either way.
                events_.erase(std::remove(events_.begin(),events_.end(),Action::Takedown),events_.end());
            }
            return;
        }
        if (key == Key::RB) {
            cancel_held_prefix();
            rb_pending_ = true;
            rb_start_ = now;
            rb_equipped_ = equipped;
            return;
        }
        if (key == Key::RT) {
            cancel_held_prefix();
            // Focus can enter one tick after LT. Do not let a same-frame
            // confirmation become a takedown during that transition.
            if (!held(Key::LT)) emit(Action::Takedown);
            blocked_ |= mask;
            return;
        }
        if ((key == Key::A || key == Key::B) && held(Key::LB))return;

        // Preserve both A+X press orders. An established BA prefix owns A so
        // overlapping B->A->X cannot become a throw. AB also protects its A
        // while waiting for the sweep's Y, even though ABX is no longer a move.
        if (!held(Key::LB) &&
            ((key == Key::A && held(Key::X) && !owned(Key::X)) ||
             (key == Key::X && held(Key::A) && !owned(Key::A) && sequence_ != Sequence::AB && sequence_ != Sequence::BA))) {
            cancel();
            emit(Action::DirectionalThrow);
            blocked_ |= bit(Key::A) | bit(Key::X);
            return;
        }

        if (key == Key::A) {
            sequence_ = sequence_ == Sequence::B || sequence_ == Sequence::AB || sequence_ == Sequence::BB
                      ? Sequence::BA : Sequence::A;
            last_step_ = now;
        } else if (key == Key::B) {
            sequence_ = sequence_ == Sequence::A || sequence_ == Sequence::BA ? Sequence::AB :
                        sequence_ == Sequence::B ? Sequence::BB : Sequence::B;
            // Once AB/BB is established, this B belongs to a command prefix.
            // Its 0.30s ground hold must not pre-empt a valid 0.40s final key.
            // An independent fresh B keeps the usual ground-attack hold.
            b_pending_ = sequence_ == Sequence::B;
            b_start_ = now;
            last_step_ = now;
        } else if (key == Key::X || key == Key::Y) {
            const bool accepted = !held(Key::LB) &&
                ((sequence_ == Sequence::BA && key == Key::X) ||
                 (sequence_ == Sequence::AB && key == Key::Y) || sequence_ == Sequence::BB);
            if (accepted) {
                const bool light = key == Key::X;
                emit(sequence_ == Sequence::BA ? Action::Palm : sequence_ == Sequence::AB ? Action::Sweep
                                               : light ? Action::Crotch : Action::Kick);
                // All currently held prefix/final keys belong to the command.
                blocked_ |= held_ & (bit(Key::A) | bit(Key::B) | bit(key));
                b_pending_ = false;
            } else {
                cancel_held_prefix();
            }
            cancel();
        }
    }

    void tick(double now, uintptr_t equipped = 0) {
        if (context_ != Context::Gameplay) { expire(now); return; }
        observe_equipment(equipped);
        if (b_pending_ && held(Key::B) && !owned(Key::B) && reached(now,b_start_,settings_.b_hold)) {
            b_pending_ = false;
            blocked_ |= bit(Key::B);
            cancel();
            emit(Action::GroundAttack);
        }
        if (rb_pending_ && held(Key::RB) && !owned(Key::RB) && reached(now,rb_start_,settings_.rb_hold)) {
            rb_pending_ = false;
            blocked_ |= bit(Key::RB);
            // A disarm/equip during the press cancels it rather than changing intent.
            if (rb_equipped_ == equipped) emit(equipped ? Action::DropObject : Action::Pickup);
        }
        expire(now);
    }

    bool held(Key key) const { return (held_ & bit(key)) != 0; }
    bool owned(Key key) const { return (blocked_ & bit(key)) != 0; }
    uint32_t held_mask() const { return held_; }
    uint32_t owned_mask() const { return blocked_|custom_prefix_mask(); }
    void reconcile_held(uint32_t physically_held) {
        physically_held&=bit(Key::Motion)-1;
        const auto unobserved_presses=physically_held & ~held_;
        blocked_=(blocked_ & physically_held) | unobserved_presses;
        held_=physically_held;
        // A missing release cancels ownership silently; it must not synthesize
        // an RB short action or a training interaction during device recovery.
        if (!held(Key::B)) b_pending_=false;
        if (!held(Key::RB)) rb_pending_=false;
        if (!held(Key::R3)) r3_pending_=false;
    }
    Context current_context() const { return context_; }

    std::vector<Action> take_events() {
        std::vector<Action> result;
        result.swap(events_);
        return result;
    }

private:
    // Runtime timestamps come from a millisecond counter. A microsecond
    // tolerance absorbs subtraction rounding after long Windows uptimes.
    static constexpr double time_tolerance=0.000001;
    static bool reached(double now,double start,double duration) {
        return now-start+time_tolerance>=duration;
    }
    enum class Sequence { Empty, A, B, AB, BA, BB };
    uint32_t custom_prefix_mask() const {
        uint32_t mask=0;
        for(auto key:custom_prefix_)mask|=bit(key);
        return held_&mask;
    }
    bool custom_command_key(Key key,double now) {
        if(held(Key::LB) || held(Key::LT)) {cancel_held_prefix();return false;}
        const auto before=custom_prefix_;
        const auto previous_size=before.size();
        auto candidate=before;candidate.push_back(key);
        for(size_t i=0;i<command_keys_.size();++i) {
            const auto& command=command_keys_[i];
            if(candidate.size()>=3 && std::equal(command.begin(),command.end(),candidate.end()-3)) {
                const std::array<Action,4> actions={Action::Palm,Action::Sweep,Action::Crotch,Action::Kick};
                emit(actions[i]);
                for(auto k:command)blocked_|=held_&bit(k);
                b_pending_=false;cancel();return true;
            }
        }
        // Preserve the original X+A enemy throw, but a multi-step prefix owns
        // its A until completion, timeout or cancellation.
        if(previous_size<2 && ((key==Key::A && held(Key::X) && !owned(Key::X)) ||
            (key==Key::X && held(Key::A) && !owned(Key::A)))) {
            emit(Action::DirectionalThrow);blocked_|=bit(Key::A)|bit(Key::X);
            b_pending_=false;cancel();return true;
        }
        size_t length=0;
        for(size_t n=1;n<=2 && n<=candidate.size();++n)for(const auto& command:command_keys_)
            if(std::equal(candidate.end()-n,candidate.end(),command.begin()))length=n;
        uint32_t previous_mask=0;
        for(auto k:before)previous_mask|=bit(k);
        // Earlier held inputs retain ownership when the recognizer advances
        // or restarts from a suffix. Otherwise a held X from an abandoned
        // custom prefix could later become an unrelated charged attack.
        blocked_|=held_&previous_mask&~bit(key);
        if(!length) {
            if(previous_size>=2)blocked_|=held_&previous_mask;
            custom_prefix_.clear();
        } else {
            custom_prefix_.assign(candidate.end()-length,candidate.end());last_step_=now;
            if(length>=2)b_pending_=false;
        }
        // B retains ground-attack hold only as an independent first press.
        if(key==Key::B && length<2) {b_pending_=true;b_start_=now;}
        return length || key==Key::A || key==Key::B || key==Key::X || key==Key::Y;
    }
    void cancel_held_prefix() {
        blocked_|=custom_prefix_mask();
        blocked_ |= held_ & (bit(Key::A) | bit(Key::B));
        cancel();
    }
    void cancel_b_hold() {
        if (b_pending_) blocked_ |= held_ & bit(Key::B);
        b_pending_ = false;
    }
    void cancel_rb_hold() {
        if (rb_pending_) blocked_ |= held_ & bit(Key::RB);
        rb_pending_ = false;
    }
    void cancel_r3_hold() {
        if (r3_pending_) blocked_ |= held_ & bit(Key::R3);
        r3_pending_ = false;
    }
    void observe_equipment(uintptr_t equipped) {
        // Remember any observed identity change, even when the same item is
        // picked up again before the hold threshold or release.
        if (rb_pending_ && rb_equipped_ != equipped) cancel_rb_hold();
    }
    void emit(Action action) { events_.push_back(action); }
    void expire(double now) {
        if(!custom_prefix_.empty() && now-last_step_>settings_.sequence_gap+time_tolerance) {
            if(custom_prefix_.size()>=2 || custom_prefix_.front()==Key::X || custom_prefix_.front()==Key::Y)
                cancel_held_prefix();
            else custom_prefix_.clear();
        }
        if (sequence_ != Sequence::Empty && now - last_step_ > settings_.sequence_gap+time_tolerance) {
            // An A belonging to an abandoned multi-key command is not a new
            // throw chord. A standalone A+X chord retains its native meaning.
            if (sequence_ == Sequence::AB || sequence_ == Sequence::BA || sequence_ == Sequence::BB) cancel_held_prefix();
            else cancel();
        }
    }
    Settings settings_;
    Context context_ = Context::Inactive;
    Sequence sequence_ = Sequence::Empty;
    std::array<std::array<Key,3>,4> command_keys_{};
    std::array<Key,2> avoid_keys_={Key::A,Key::B};
    bool custom_commands_=false;
    std::vector<Key> custom_prefix_;
    uint32_t held_ = 0;
    uint32_t blocked_ = 0;
    bool b_pending_ = false;
    bool rb_pending_ = false;
    bool r3_pending_ = false;
    bool training_ = false;
    double last_step_ = 0;
    double b_start_ = 0;
    double rb_start_ = 0;
    double r3_start_ = 0;
    uintptr_t rb_equipped_ = 0;
    std::vector<Action> events_;
};

} // namespace button_controls
