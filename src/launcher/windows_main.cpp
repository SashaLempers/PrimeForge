// SPDX-License-Identifier: Apache-2.0

#include "primeforge/mvp/launcher.hpp"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr UINT message_start = WM_APP + 1U;
constexpr UINT message_log = WM_APP + 2U;
constexpr UINT message_finished = WM_APP + 3U;
constexpr int hotkey_stop = 1;
constexpr int control_status = 1001;
constexpr int control_log = 1002;
constexpr int control_start = 1003;
constexpr int control_stop = 1004;

struct ChildCompletion {
    DWORD exit_code{};
    primeforge::mvp::LauncherAction action{};
};

struct AppState {
    std::filesystem::path launcher_executable;
    std::filesystem::path engine_executable;
    std::filesystem::path working_directory;
    primeforge::mvp::LauncherCampaignPaths campaign;
    HWND status{};
    HWND log{};
    HWND start{};
    HWND stop{};
    std::jthread worker;
    std::atomic_bool running{false};
    bool close_when_finished{};
};

[[nodiscard]] std::wstring windows_error(const DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0U, reinterpret_cast<wchar_t*>(&buffer), 0U, nullptr);
    std::wstring message = size == 0U ? L"erreur Windows " + std::to_wstring(code)
                                      : std::wstring{buffer, size};
    if (buffer != nullptr) LocalFree(buffer);
    return message;
}

[[nodiscard]] std::wstring decode_output(const char* bytes, const DWORD size) {
    if (size == 0U) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes,
                                    static_cast<int>(size), nullptr, 0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count == 0) {
        code_page = CP_ACP;
        flags = 0U;
        count = MultiByteToWideChar(code_page, flags, bytes, static_cast<int>(size), nullptr, 0);
    }
    if (count <= 0) return L"[sortie illisible]\r\n";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    static_cast<void>(MultiByteToWideChar(code_page, flags, bytes, static_cast<int>(size),
                                          result.data(), count));
    return result;
}

[[nodiscard]] std::wstring quote_argument(const std::wstring_view argument) {
    std::wstring result{L'"'};
    std::size_t backslashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0U;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0U;
            result.push_back(character);
        }
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

[[nodiscard]] std::filesystem::path current_executable() {
    std::wstring buffer(32'768U, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
    if (size == 0U || size == buffer.size()) {
        throw std::runtime_error("cannot locate PrimeForge Launcher executable");
    }
    buffer.resize(size);
    return std::filesystem::path{buffer};
}

[[nodiscard]] std::filesystem::path find_repository_root(std::filesystem::path start) {
    start = std::filesystem::absolute(std::move(start));
    while (!start.empty()) {
        if (std::filesystem::is_regular_file(start / "CMakeLists.txt") &&
            std::filesystem::is_regular_file(start / "examples" / "mvp" / "search.yaml")) {
            return start;
        }
        const auto parent = start.parent_path();
        if (parent == start) break;
        start = parent;
    }
    return {};
}

[[nodiscard]] std::filesystem::path requested_configuration(
    const std::filesystem::path& launcher) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) throw std::runtime_error("cannot parse launcher command line");
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> owner{arguments, &LocalFree};
    if (count == 3 && std::wstring_view{arguments[1]} == L"--config") {
        return std::filesystem::absolute(arguments[2]);
    }
    if (count != 1) {
        throw std::invalid_argument(
            "usage: primeforge-launcher.exe [--config search.yaml]");
    }
    const auto packaged = launcher.parent_path() / "search.yaml";
    if (std::filesystem::is_regular_file(packaged)) return packaged;
    const auto repository = find_repository_root(launcher.parent_path());
    if (!repository.empty()) return repository / "examples" / "mvp" / "search.yaml";
    throw std::runtime_error(
        "search.yaml introuvable; utilisez --config avec son chemin complet");
}

[[nodiscard]] std::filesystem::path choose_working_directory(
    const std::filesystem::path& launcher,
    const std::filesystem::path& configuration) {
    const auto repository = find_repository_root(configuration.parent_path());
    if (!repository.empty()) return repository;
    if (configuration.parent_path() == launcher.parent_path()) return launcher.parent_path();
    return configuration.parent_path();
}

[[nodiscard]] std::vector<std::wstring> child_arguments(
    const primeforge::mvp::LauncherAction action,
    const primeforge::mvp::LauncherCampaignPaths& paths) {
    using primeforge::mvp::LauncherAction;
    if (action == LauncherAction::search) {
        return {L"search", L"--config", paths.configuration.wstring(), L"--prp-backend",
                L"auto", L"--stop-file", paths.stop_request.wstring()};
    }
    if (action == LauncherAction::resume) {
        return {L"resume", L"--checkpoint", paths.checkpoint.wstring(), L"--prp-backend",
                L"auto", L"--stop-file", paths.stop_request.wstring()};
    }
    return {L"verify", L"--result", paths.results.wstring()};
}

void post_log(const HWND window, std::wstring text) {
    auto message = std::make_unique<std::wstring>(std::move(text));
    if (PostMessageW(window, message_log, 0U,
                     reinterpret_cast<LPARAM>(message.get())) != FALSE) {
        static_cast<void>(message.release());
    }
}

void run_child(const HWND window, AppState* const state,
               const primeforge::mvp::LauncherAction action) {
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (CreatePipe(&read_pipe, &write_pipe, &attributes, 0U) == FALSE) {
        post_log(window, L"Impossible de créer le canal de journal : " +
                             windows_error(GetLastError()) + L"\r\n");
        auto result = new ChildCompletion{GetLastError(), action};
        static_cast<void>(PostMessageW(window, message_finished, 0U,
                                       reinterpret_cast<LPARAM>(result)));
        return;
    }
    static_cast<void>(SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0U));

    std::wstring command = quote_argument(state->engine_executable.wstring());
    for (const auto& argument : child_arguments(action, state->campaign)) {
        command += L" ";
        command += quote_argument(argument);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        state->working_directory.c_str(), &startup, &process);
    CloseHandle(write_pipe);
    write_pipe = nullptr;
    if (created == FALSE) {
        const DWORD error = GetLastError();
        CloseHandle(read_pipe);
        post_log(window, L"Impossible de lancer primeforge.exe : " + windows_error(error) +
                             L"\r\n");
        auto result = new ChildCompletion{error, action};
        static_cast<void>(PostMessageW(window, message_finished, 0U,
                                       reinterpret_cast<LPARAM>(result)));
        return;
    }

    CloseHandle(process.hThread);
    std::array<char, 4'096U> buffer{};
    DWORD bytes_read = 0U;
    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &bytes_read, nullptr) != FALSE && bytes_read != 0U) {
        post_log(window, decode_output(buffer.data(), bytes_read));
    }
    CloseHandle(read_pipe);
    static_cast<void>(WaitForSingleObject(process.hProcess, INFINITE));
    DWORD exit_code = 1U;
    static_cast<void>(GetExitCodeProcess(process.hProcess, &exit_code));
    CloseHandle(process.hProcess);
    auto result = new ChildCompletion{exit_code, action};
    static_cast<void>(PostMessageW(window, message_finished, 0U,
                                   reinterpret_cast<LPARAM>(result)));
}

void append_log(const HWND edit, const std::wstring& text) {
    const LRESULT length = SendMessageW(edit, WM_GETTEXTLENGTH, 0U, 0U);
    static_cast<void>(SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(length),
                                   static_cast<LPARAM>(length)));
    static_cast<void>(SendMessageW(edit, EM_REPLACESEL, FALSE,
                                   reinterpret_cast<LPARAM>(text.c_str())));
    static_cast<void>(SendMessageW(edit, EM_SCROLLCARET, 0U, 0U));
}

void set_status(AppState& state, const std::wstring& text) {
    SetWindowTextW(state.status, text.c_str());
}

void request_stop(AppState& state) {
    if (!state.running.load()) return;
    try {
        primeforge::mvp::StopRequestFile{state.campaign.stop_request}.request();
        set_status(state, L"Arrêt demandé — fin du candidat courant et écriture du checkpoint…");
        EnableWindow(state.stop, FALSE);
        append_log(state.log, L"\r\n[launcher] Arrêt propre demandé.\r\n");
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "PrimeForge Launcher", MB_OK | MB_ICONERROR);
    }
}

void start_campaign(const HWND window, AppState& state) {
    if (state.running.exchange(true)) return;
    try {
        if (state.worker.joinable()) state.worker.join();
        primeforge::mvp::StopRequestFile{state.campaign.stop_request}.clear();
        const auto action = primeforge::mvp::select_launcher_action(state.campaign);
        const auto name = primeforge::mvp::launcher_action_name(action);
        append_log(state.log, L"\r\n[launcher] Action automatique : " +
                                  std::wstring{name.begin(), name.end()} + L"\r\n");
        if (action == primeforge::mvp::LauncherAction::search) {
            set_status(state, L"Recherche en cours (CPU + RTX en mode automatique)…");
        } else if (action == primeforge::mvp::LauncherAction::resume) {
            set_status(state, L"Reprise automatique depuis le checkpoint…");
        } else {
            set_status(state, L"Campagne terminée — vérification des résultats…");
        }
        EnableWindow(state.start, FALSE);
        EnableWindow(state.stop,
                     action == primeforge::mvp::LauncherAction::verify ? FALSE : TRUE);
        state.worker = std::jthread{[window, &state, action] { run_child(window, &state, action); }};
    } catch (const std::exception& error) {
        state.running.store(false);
        EnableWindow(state.start, TRUE);
        EnableWindow(state.stop, FALSE);
        set_status(state, L"Impossible de démarrer.");
        MessageBoxA(window, error.what(), "PrimeForge Launcher", MB_OK | MB_ICONERROR);
    }
}

void layout_controls(const HWND window, AppState& state) {
    RECT client{};
    GetClientRect(window, &client);
    constexpr int margin = 16;
    constexpr int button_width = 150;
    constexpr int button_height = 36;
    constexpr int status_height = 42;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    MoveWindow(state.status, margin, margin, width - 2 * margin, status_height, TRUE);
    MoveWindow(state.start, margin, height - margin - button_height, button_width,
               button_height, TRUE);
    MoveWindow(state.stop, margin * 2 + button_width, height - margin - button_height,
               button_width, button_height, TRUE);
    MoveWindow(state.log, margin, margin + status_height, width - 2 * margin,
               height - status_height - button_height - 3 * margin, TRUE);
}

LRESULT CALLBACK window_procedure(const HWND window, const UINT message,
                                  const WPARAM wparam, const LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<AppState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_CREATE: {
        state->status = CreateWindowExW(0U, L"STATIC", L"Initialisation…", WS_CHILD | WS_VISIBLE,
                                        0, 0, 0, 0, window,
                                        reinterpret_cast<HMENU>(
                                            static_cast<INT_PTR>(control_status)),
                                        nullptr, nullptr);
        state->log = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"PrimeForge Launcher\r\n",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
                ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_log)), nullptr, nullptr);
        state->start = CreateWindowExW(0U, L"BUTTON", L"Démarrer / Reprendre",
                                       WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0,
                                       window,
                                       reinterpret_cast<HMENU>(
                                           static_cast<INT_PTR>(control_start)),
                                       nullptr, nullptr);
        state->stop = CreateWindowExW(0U, L"BUTTON", L"Arrêter proprement",
                                      WS_CHILD | WS_VISIBLE | WS_DISABLED, 0, 0, 0, 0, window,
                                      reinterpret_cast<HMENU>(
                                          static_cast<INT_PTR>(control_stop)),
                                      nullptr, nullptr);
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        for (const HWND control : {state->status, state->log, state->start, state->stop}) {
            static_cast<void>(SendMessageW(control, WM_SETFONT,
                                           reinterpret_cast<WPARAM>(font), TRUE));
        }
        static_cast<void>(RegisterHotKey(window, hotkey_stop, MOD_CONTROL | MOD_NOREPEAT,
                                         static_cast<UINT>('C')));
        PostMessageW(window, message_start, 0U, 0U);
        return 0;
    }
    case WM_SIZE:
        if (state != nullptr) layout_controls(window, *state);
        return 0;
    case WM_COMMAND:
        if (state != nullptr && HIWORD(wparam) == BN_CLICKED) {
            if (LOWORD(wparam) == control_start) start_campaign(window, *state);
            if (LOWORD(wparam) == control_stop) request_stop(*state);
        }
        return 0;
    case WM_HOTKEY:
        if (state != nullptr && wparam == hotkey_stop) request_stop(*state);
        return 0;
    case message_start:
        if (state != nullptr) start_campaign(window, *state);
        return 0;
    case message_log: {
        std::unique_ptr<std::wstring> text{reinterpret_cast<std::wstring*>(lparam)};
        if (state != nullptr && text != nullptr) append_log(state->log, *text);
        return 0;
    }
    case message_finished: {
        std::unique_ptr<ChildCompletion> completion{
            reinterpret_cast<ChildCompletion*>(lparam)};
        if (state == nullptr || completion == nullptr) return 0;
        state->running.store(false);
        try {
            primeforge::mvp::StopRequestFile{state->campaign.stop_request}.clear();
        } catch (const std::exception& error) {
            append_log(state->log, L"[launcher] Impossible d'effacer le signal d'arrêt : " +
                                      decode_output(error.what(),
                                                    static_cast<DWORD>(std::char_traits<char>::length(error.what()))) +
                                      L"\r\n");
        }
        EnableWindow(state->start, TRUE);
        EnableWindow(state->stop, FALSE);
        if (completion->exit_code != 0U) {
            set_status(*state, L"PrimeForge s'est arrêté avec une erreur. Consultez le journal.");
        } else if (std::filesystem::is_regular_file(state->campaign.manifest)) {
            set_status(*state, L"Campagne terminée et résultats disponibles.");
        } else if (std::filesystem::is_regular_file(state->campaign.checkpoint)) {
            set_status(*state, L"Arrêt propre confirmé — checkpoint prêt pour la reprise.");
            SetWindowTextW(state->start, L"Reprendre");
        } else {
            set_status(*state, L"Opération terminée.");
        }
        append_log(state->log, L"[launcher] Code de sortie : " +
                                  std::to_wstring(completion->exit_code) + L"\r\n");
        if (state->close_when_finished) DestroyWindow(window);
        return 0;
    }
    case WM_CLOSE:
        if (state != nullptr && state->running.load()) {
            state->close_when_finished = true;
            request_stop(*state);
            set_status(*state, L"Fermeture après création du checkpoint…");
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(window, hotkey_stop);
        PostQuitMessage(0);
        return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    try {
        AppState state;
        state.launcher_executable = current_executable();
        state.engine_executable = state.launcher_executable.parent_path() / "primeforge.exe";
        if (!std::filesystem::is_regular_file(state.engine_executable)) {
            throw std::runtime_error("primeforge.exe doit être placé à côté du lanceur");
        }
        const auto configuration = requested_configuration(state.launcher_executable);
        state.working_directory =
            choose_working_directory(state.launcher_executable, configuration);
        state.campaign = primeforge::mvp::make_launcher_campaign_paths(
            configuration, state.working_directory);

        constexpr wchar_t class_name[] = L"PrimeForgeLauncherWindow";
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = window_procedure;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        window_class.lpszClassName = class_name;
        if (RegisterClassExW(&window_class) == 0U) {
            throw std::runtime_error("cannot register PrimeForge Launcher window");
        }
        const HWND window = CreateWindowExW(
            0U, class_name, L"PrimeForge — recherche de nombres premiers",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 920, 680, nullptr, nullptr,
            instance, &state);
        if (window == nullptr) throw std::runtime_error("cannot create PrimeForge window");
        ShowWindow(window, show_command);
        UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0U, 0U) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (state.worker.joinable()) state.worker.join();
        return static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "PrimeForge Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
}
