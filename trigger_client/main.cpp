#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "ext-input-trigger-action-v1-client-protocol.h"
#include "ext-input-trigger-registration-v1-client-protocol.h"

/*
 * Standalone Wayland client that registers:
 * 1. A keyboard-sym trigger for Ctrl+Shift+C
 * 2. A keyboard-sym trigger for Alt+X
 * 3. A keyboard-code trigger for Alt+Z (scancode 44)
 *
 * It prints specific messages for begin/end events for each trigger.
 */

namespace
{
ext_input_trigger_registration_manager_v1* registration_manager = nullptr;
ext_input_trigger_action_manager_v1* action_manager = nullptr;

void registry_global(
    void* /*data*/, struct wl_registry* registry, uint32_t name, char const* interface, uint32_t version)
{
    if (strcmp(interface, "ext_input_trigger_registration_manager_v1") == 0)
    {
        registration_manager = static_cast<ext_input_trigger_registration_manager_v1*>(wl_registry_bind(
            registry, name, &ext_input_trigger_registration_manager_v1_interface, std::min<uint32_t>(version, 1)));
    }
    else if (strcmp(interface, "ext_input_trigger_action_manager_v1") == 0)
    {
        action_manager = static_cast<ext_input_trigger_action_manager_v1*>(wl_registry_bind(
            registry, name, &ext_input_trigger_action_manager_v1_interface, std::min<uint32_t>(version, 1)));
    }
}

void registry_global_remove(void*, struct wl_registry*, uint32_t)
{
}

wl_registry_listener const registry_listener = {
    registry_global,
    registry_global_remove,
};

class TriggerBase
{
public:
    TriggerBase(
        wl_display* display,
        std::string name,
        std::string begin_msg,
        std::string end_msg)
        : display_{display},
          name_{std::move(name)},
          begin_msg_{std::move(begin_msg)},
          end_msg_{std::move(end_msg)}
    {
    }

    TriggerBase(TriggerBase const&) = delete;
    TriggerBase& operator=(TriggerBase const&) = delete;
    TriggerBase(TriggerBase&&) = delete;
    TriggerBase& operator=(TriggerBase&&) = delete;

    virtual ~TriggerBase()
    {
        if (action_)
            ext_input_trigger_action_v1_destroy(action_);
        if (trigger_)
            ext_input_trigger_v1_destroy(trigger_);
    }

    std::string const& name() const { return name_; }
    bool ok() const { return action_ != nullptr; }

protected:
    // Must be called by the derived constructor after it has created `trigger_`
    void finish_setup()
    {
        bool failed = false;
        ext_input_trigger_v1_add_listener(trigger_, &trigger_listener_, &failed);

        ext_input_trigger_action_control_v1* control =
            ext_input_trigger_registration_manager_v1_get_action_control(registration_manager, name_.c_str());

        std::string token;
        ext_input_trigger_action_control_v1_add_listener(control, &control_listener_, &token);
        ext_input_trigger_action_control_v1_add_input_trigger_event(control, trigger_);

        // Roundtrip to obtain the compositor-issued token and any trigger failure
        wl_display_roundtrip(display_);

        if (failed)
        {
            ext_input_trigger_action_control_v1_destroy(control);
            return;
        }

        if (token.empty())
        {
            std::cerr << "Failed to get token for " << name_ << "\n";
            ext_input_trigger_action_control_v1_destroy(control);
            return;
        }

        action_ = ext_input_trigger_action_manager_v1_get_input_trigger_action(action_manager, token.c_str());
        if (action_)
        {
            std::cout << "Got " << name_ << " action\n";
            ext_input_trigger_action_v1_add_listener(action_, &action_listener_, this);
        }
        else
        {
            std::cerr << "Failed to get action for " << name_ << "\n";
        }

        ext_input_trigger_action_control_v1_destroy(control);
    }

    wl_display* display_;
    std::string name_;
    std::string begin_msg_;
    std::string end_msg_;
    ext_input_trigger_v1* trigger_{nullptr};
    ext_input_trigger_action_v1* action_{nullptr};

private:
    static void on_trigger_done(void* /*data*/, ext_input_trigger_v1* /*trigger*/)
    {
    }

    static void on_trigger_failed(void* data, ext_input_trigger_v1* /*trigger*/)
    {
        std::cerr << "Trigger registration failed\n" << std::flush;
        *static_cast<bool*>(data) = true;
    }

    static void on_action_begin(
        void* data, ext_input_trigger_action_v1* /*action*/, uint32_t /*serial*/, char const* /*token*/)
    {
        auto* self = static_cast<TriggerBase*>(data);
        std::cout << self->begin_msg_ << "\n" << std::flush;
    }

    static void on_action_end(
        void* data, ext_input_trigger_action_v1* /*action*/, uint32_t /*serial*/, char const* /*message*/)
    {
        auto* self = static_cast<TriggerBase*>(data);
        std::cout << self->end_msg_ << "\n" << std::flush;
    }

    static void on_action_unavailable(void* /*data*/, ext_input_trigger_action_v1* /*action*/)
    {
        std::cerr << "Action unavailable\n" << std::flush;
    }

    static void on_control_done(
        void* data, ext_input_trigger_action_control_v1* /*control*/, char const* token)
    {
        auto* target = static_cast<std::string*>(data);
        std::cerr << "Received token: " << token << '\n';
        *target = token;
    }

    static ext_input_trigger_v1_listener const trigger_listener_;
    static ext_input_trigger_action_v1_listener const action_listener_;
    static ext_input_trigger_action_control_v1_listener const control_listener_;
};

ext_input_trigger_v1_listener const TriggerBase::trigger_listener_ = {
    .done   = on_trigger_done,
    .failed = on_trigger_failed,
};

ext_input_trigger_action_v1_listener const TriggerBase::action_listener_ = {
    .begin       = on_action_begin,
    .end         = on_action_end,
    .unavailable = on_action_unavailable,
};

ext_input_trigger_action_control_v1_listener const TriggerBase::control_listener_ = {
    .done = on_control_done,
};

class SymTrigger : public TriggerBase
{
public:
    SymTrigger(
        wl_display* display,
        std::string name,
        uint32_t modifiers,
        uint32_t keysym,
        std::string begin_msg,
        std::string end_msg)
        : TriggerBase{display, std::move(name), std::move(begin_msg), std::move(end_msg)}
    {
        std::cout << "Registering " << name_ << " sym trigger...\n";
        trigger_ = ext_input_trigger_registration_manager_v1_register_keyboard_sym_trigger(
            registration_manager, modifiers, keysym);
        finish_setup();
    }
};

class CodeTrigger : public TriggerBase
{
public:
    CodeTrigger(
        wl_display* display,
        std::string name,
        uint32_t modifiers,
        uint32_t scancode,
        std::string begin_msg,
        std::string end_msg)
        : TriggerBase{display, std::move(name), std::move(begin_msg), std::move(end_msg)}
    {
        std::cout << "Registering " << name_ << " keycode trigger (scancode " << scancode << ")...\n";
        trigger_ = ext_input_trigger_registration_manager_v1_register_keyboard_code_trigger(
            registration_manager, modifiers, scancode);
        finish_setup();
    }
};

} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    wl_display* display = wl_display_connect(nullptr);
    if (!display)
    {
        std::fprintf(stderr, "Failed to connect to Wayland display\n");
        return EXIT_FAILURE;
    }

    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, nullptr);

    // Roundtrip to get globals
    wl_display_roundtrip(display);

    if (!registration_manager || !action_manager)
    {
        std::cerr << "Required globals not available\n";
        wl_display_disconnect(display);
        return EXIT_FAILURE;
    }

    // Register keyboard sym trigger: Ctrl + Shift + C
    SymTrigger ctrl_shift_c{
        display,
        "CTRL + SHIFT + c (AKA CTRL + C)",
        EXT_INPUT_TRIGGER_REGISTRATION_MANAGER_V1_MODIFIERS_SHIFT |
            EXT_INPUT_TRIGGER_REGISTRATION_MANAGER_V1_MODIFIERS_CTRL,
        XKB_KEY_C,
        "Hello from CTRL + SHIFT + c",
        "Bye from CTRL + SHIFT + c"};

    // Register keyboard sym trigger: Alt + X
    SymTrigger alt_x{
        display,
        "ALT + x",
        EXT_INPUT_TRIGGER_REGISTRATION_MANAGER_V1_MODIFIERS_ALT,
        XKB_KEY_x,
        "Hello from ALT + x",
        "Bye from ALT + x"};

    SymTrigger duplicate_alt_x{
        display,
        "Duplicate ALT + x",
        EXT_INPUT_TRIGGER_REGISTRATION_MANAGER_V1_MODIFIERS_ALT,
        XKB_KEY_x,
        "Hello from duplicate ALT + x",
        "Bye from duplicate ALT + x"};

    // Register keyboard sym trigger: Right Alt + S (side-specific modifier)
    SymTrigger right_alt_s{
        display,
        "RIGHT ALT + s",
        EXT_INPUT_TRIGGER_REGISTRATION_MANAGER_V1_MODIFIERS_ALT_RIGHT,
        XKB_KEY_s,
        "Hello from RIGHT ALT + s",
        "Bye from RIGHT ALT + s"};

    // Register keyboard code trigger: Alt + Z (scancode 44)
    // Scancode 44 is the physical 'Z' key position on QWERTY keyboards
    CodeTrigger alt_z{
        display,
        "ALT + Z (scancode 44)",
        EXT_INPUT_TRIGGER_REGISTRATION_MANAGER_V1_MODIFIERS_ALT,
        44,
        "Hello from ALT + Z (keycode trigger)",
        "Bye from ALT + Z (keycode trigger)"};

    auto const status = [](TriggerBase const& t) { return t.ok() ? "OK" : "FAILED"; };
    std::cout << "\nTrigger registration summary:\n";
    std::cout << "  [" << status(ctrl_shift_c) << "] Ctrl+Shift+C (keysym trigger)\n";
    std::cout << "  [" << status(alt_x) << "] Alt+X (keysym trigger)\n";
    std::cout << "  [" << status(duplicate_alt_x) << "] Duplicate Alt+X (keysym trigger)\n";
    std::cout << "  [" << status(right_alt_s) << "] Right Alt+S (keysym trigger, side-specific modifier)\n";
    std::cout << "  [" << status(alt_z) << "] Alt+Z (keycode trigger - works regardless of layout)\n\n";

    // Enter the dispatch loop
    while (wl_display_dispatch(display) != -1)
    {
    }

    // Triggers are destroyed here by their destructors (before the managers)

    if (registration_manager)
        ext_input_trigger_registration_manager_v1_destroy(registration_manager);
    if (action_manager)
        ext_input_trigger_action_manager_v1_destroy(action_manager);

    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    return 0;
}
