#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "ext-input-trigger-action-v1-client-protocol.h"
#include "ext-input-trigger-registration-v1-client-protocol.h"

namespace fs = std::filesystem;

// --- RAII Wrappers for Wayland Objects ---

template <typename T, void (*Deleter)(T*)>
struct WlDeleter
{
    void operator()(T* obj) const { Deleter(obj); }
};

using WlDisplay = std::unique_ptr<wl_display, WlDeleter<wl_display, wl_display_disconnect>>;
using WlRegistry = std::unique_ptr<wl_registry, WlDeleter<wl_registry, wl_registry_destroy>>;
using WlTrigger = std::unique_ptr<ext_input_trigger_v1, WlDeleter<ext_input_trigger_v1, ext_input_trigger_v1_destroy>>;
using WlAction =
    std::unique_ptr<ext_input_trigger_action_v1, WlDeleter<ext_input_trigger_action_v1, ext_input_trigger_action_v1_destroy>>;
using WlRegistrationManager = std::unique_ptr<
    ext_input_trigger_registration_manager_v1,
    WlDeleter<ext_input_trigger_registration_manager_v1, ext_input_trigger_registration_manager_v1_destroy>>;
using WlActionManager = std::unique_ptr<
    ext_input_trigger_action_manager_v1,
    WlDeleter<ext_input_trigger_action_manager_v1, ext_input_trigger_action_manager_v1_destroy>>;
using WlControl = std::unique_ptr<
    ext_input_trigger_action_control_v1,
    WlDeleter<ext_input_trigger_action_control_v1, ext_input_trigger_action_control_v1_destroy>>;

// --- Base Client Class ---

class WaylandClient
{
public:
    WaylandClient()
    {
        display_.reset(wl_display_connect(nullptr));
        if (!display_)
        {
            throw std::runtime_error("Failed to connect to Wayland display");
        }

        registry_.reset(wl_display_get_registry(display_.get()));
        wl_registry_add_listener(registry_.get(), &registry_listener_, this);
        roundtrip();
    }

    void roundtrip()
    {
        if (wl_display_roundtrip(display_.get()) == -1)
        {
            throw std::runtime_error("Wayland roundtrip failed");
        }
    }

    void dispatch()
    {
        if (wl_display_dispatch(display_.get()) == -1)
        {
            throw std::runtime_error("Wayland dispatch failed");
        }
    }

    wl_display* display() const { return display_.get(); }
    ext_input_trigger_registration_manager_v1* registration_manager() const { return registration_manager_.get(); }
    ext_input_trigger_action_manager_v1* action_manager() const { return action_manager_.get(); }

protected:
    WlDisplay display_;
    WlRegistry registry_;
    WlRegistrationManager registration_manager_;
    WlActionManager action_manager_;

private:
    static void handle_global(
        void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
    {
        auto* self = static_cast<WaylandClient*>(data);
        std::string_view iface{interface};

        if (iface == "ext_input_trigger_registration_manager_v1")
        {
            self->registration_manager_.reset(static_cast<ext_input_trigger_registration_manager_v1*>(wl_registry_bind(
                registry, name, &ext_input_trigger_registration_manager_v1_interface, std::min<uint32_t>(version, 1))));
        }
        else if (iface == "ext_input_trigger_action_manager_v1")
        {
            self->action_manager_.reset(static_cast<ext_input_trigger_action_manager_v1*>(wl_registry_bind(
                registry, name, &ext_input_trigger_action_manager_v1_interface, std::min<uint32_t>(version, 1))));
        }
    }

    static void handle_global_remove(void*, wl_registry*, uint32_t) {}

    static constexpr wl_registry_listener registry_listener_ = {
        .global = handle_global,
        .global_remove = handle_global_remove,
    };
};

// --- Child Logic ---

class ChildClient : public WaylandClient
{
public:
    ChildClient(std::string_view id, std::string_view token) : id_(id)
    {
        if (!action_manager_)
        {
            throw std::runtime_error("ext_input_trigger_action_manager_v1 not available (in child process)");
        }

        action_.reset(ext_input_trigger_action_manager_v1_get_input_trigger_action(action_manager_.get(), token.data()));
        ext_input_trigger_action_v1_add_listener(action_.get(), &action_listener_, this);

        std::cout << std::format("Child {} listening for events... (Press Ctrl+C to quit)\n", id_);
    }

    void run()
    {
        while (true)
        {
            dispatch();
        }
    }

private:
    std::string id_;
    WlAction action_;

    static void on_begin(
        void* data, ext_input_trigger_action_v1*, uint32_t, const char*)
    {
        auto* self = static_cast<ChildClient*>(data);
        std::cout << std::format("Begin {}\n", self->id_) << std::flush;
    }

    static void on_end(
        void* data, ext_input_trigger_action_v1*, uint32_t, const char*)
    {
        auto* self = static_cast<ChildClient*>(data);
        std::cout << std::format("End {}\n", self->id_) << std::flush;
    }

    static void on_unavailable(void*, ext_input_trigger_action_v1*)
    {
        std::cerr << "Action unavailable (parent likely closed)\n" << std::flush;
        std::exit(EXIT_FAILURE);
    }

    static constexpr ext_input_trigger_action_v1_listener action_listener_ = {
        .begin = on_begin,
        .end = on_end,
        .unavailable = on_unavailable,
    };
};

// --- Parent Logic ---

class ParentClient : public WaylandClient
{
public:
    ParentClient(std::string_view executable_path) : executable_path_(executable_path)
    {
        if (!registration_manager_ || !action_manager_)
        {
            throw std::runtime_error(std::format(
                "Required globals not available (registration_manager: {}, action_manager: {})",
                registration_manager_ ? "ok" : "missing",
                action_manager_ ? "ok" : "missing"));
        }

        register_trigger();
    }

    void run()
    {
        spawn_children();
        std::cout << "Parent running. Keep this process alive to maintain the trigger.\n";
        std::cout << "Press Ctrl+C to exit and clean up trigger.\n";

        while (true)
        {
            dispatch();
        }
    }

private:
    std::string executable_path_;
    WlTrigger trigger_;
    std::string token_;
    WlControl control_;

    void register_trigger()
    {
        std::cout << "Registering trigger (Ctrl + Alt + A)...\n";
        trigger_.reset(ext_input_trigger_registration_manager_v1_register_keyboard_sym_trigger(
            registration_manager_.get(),
            EXT_INPUT_TRIGGER_REGISTRATION_MANAGER_V1_MODIFIERS_CTRL |
                EXT_INPUT_TRIGGER_REGISTRATION_MANAGER_V1_MODIFIERS_ALT,
            XKB_KEY_A));

        ext_input_trigger_v1_add_listener(trigger_.get(), &trigger_listener_, this);

        control_ = WlControl{
            ext_input_trigger_registration_manager_v1_get_action_control(
                registration_manager_.get(), "Split Client Trigger")};

        ext_input_trigger_action_control_v1_add_listener(control_.get(), &control_listener_, &token_);
        ext_input_trigger_action_control_v1_add_input_trigger_event(control_.get(), trigger_.get());

        roundtrip();

        if (token_.empty())
        {
            throw std::runtime_error("Failed to get token");
        }
        std::cout << std::format("Got token: {}\n", token_);
    }

    void spawn_children()
    {
        std::string env_setup;
        if (const char* val = std::getenv("WAYLAND_DISPLAY"))
        {
            env_setup += std::format("export WAYLAND_DISPLAY={}; ", val);
        }
        if (const char* val = std::getenv("XDG_RUNTIME_DIR"))
        {
            env_setup += std::format("export XDG_RUNTIME_DIR={}; ", val);
        }

        std::string session_name = std::format("split_client_{}", getpid());
        // Using ; read to keep the pane open if the client crashes or exits
        constexpr auto cmd_fmt = "{} {} --child {} {}; read";
        std::string cmd_client1 = std::format(cmd_fmt, env_setup, executable_path_, token_, 1);
        std::string cmd_client2 = std::format(cmd_fmt, env_setup, executable_path_, token_, 2);

        // Construct tmux command: new-session -> split-window -> attach
        std::string tmux_cmd = std::format(
            "tmux new-session -d -s {} '{}' \\; split-window -h '{}' \\; attach-session -t {}",
            session_name,
            cmd_client1,
            cmd_client2,
            session_name);

        std::string term_cmd = std::format("gnome-terminal -- {}", tmux_cmd);

        std::cout << "Launching terminal with clients...\n";
        pid_t pid = fork();
        if (pid == 0)
        {
            // Child process (launcher)
            int ret = system(term_cmd.c_str());
            if (ret != 0)
            {
                std::cerr << "Failed to launch gnome-terminal, trying inline tmux...\n";
                execlp("sh", "sh", "-c", tmux_cmd.c_str(), nullptr);
            }
            std::exit(0);
        }
    }

    static void on_trigger_done(void*, ext_input_trigger_v1*) {}
    static void on_trigger_failed(void*, ext_input_trigger_v1*)
    {
        std::cerr << "Trigger registration failed\n";
        std::exit(EXIT_FAILURE);
    }

    static constexpr ext_input_trigger_v1_listener trigger_listener_ = {
        .done = on_trigger_done,
        .failed = on_trigger_failed,
    };

    static void on_control_done(void* data, ext_input_trigger_action_control_v1*, const char* token)
    {
        auto* target_str = static_cast<std::string*>(data);
        *target_str = token;
    }

    static constexpr ext_input_trigger_action_control_v1_listener control_listener_ = {
        .done = on_control_done,
    };
};

int main(int argc, char** argv)
{
    try
    {
        if (argc >= 4 && std::string_view(argv[1]) == "--child")
        {
            ChildClient client(argv[3], argv[2]);
            client.run();
        }
        else
        {
            // Get absolute path to self
            fs::path self_path = fs::read_symlink("/proc/self/exe");
            ParentClient client(self_path.string());
            client.run();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << std::format("Error: {}\n", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
