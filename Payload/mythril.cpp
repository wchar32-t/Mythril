#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#pragma comment(lib, "spdlog.lib")
#pragma comment(lib, "fmt.lib")

#include <MinHook.h> 
#pragma comment(lib, "minhook.x32.lib")

#include <windows.h>

#include <thread>

std::shared_ptr<spdlog::logger> console;
std::shared_ptr<spdlog::logger> bugslife;

#define rebase(x)          (x - 0x400000 + (uintptr_t)GetModuleHandleA(NULL))

typedef int(__cdecl* LoadAsset)(LPCSTR, int);
auto load_asset = reinterpret_cast<LoadAsset>(rebase(0x41B0E0));

LoadAsset load_asset_o = 0;

std::string get_working_directory()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);

    std::string fullPath(buffer);
    size_t pos = fullPath.find_last_of("\\/");
    if (pos != std::string::npos)
        return fullPath.substr(0, pos + 1);  // Include trailing slash

    return "";  // fallback
}

int __cdecl detour_load_asset(LPCSTR path, int a1) {
    auto stdpath = std::string(path);
    console->warn("LoadAsset called with: {}", stdpath);
    
    std::string directory = get_working_directory();

    if (stdpath.rfind("@:\\", 0) == 0) {
        stdpath.replace(0, 3, directory);
        console->info("redirected drive path to: {}", stdpath);
        return load_asset_o(stdpath.c_str(), a1);
    }

    if (stdpath.rfind("Assets\\", 0) == 0) {
        stdpath = directory + stdpath;
        console->info("redirected reletive assets path to: {}", stdpath);
        return load_asset_o(stdpath.c_str(), a1);
    }

    if (stdpath.rfind("@:\\", 0) == 0 || stdpath.rfind("Assets\\", 0) == 0)
    {
        stdpath.replace(0, 3, directory);
        console->info("redirected path to: {}", stdpath);
        return load_asset_o(stdpath.c_str(), a1);
    }

    return load_asset_o(path, a1);
}

typedef void(__stdcall* outputdebugstringa)(LPCSTR);

outputdebugstringa output_debug_string_o;

void __stdcall detour_debug_string(LPCSTR output)
{
    auto stdoutput = std::string(output);

    bugslife->warn(stdoutput);

    return output_debug_string_o(output);
}


auto main_thread(HMODULE module) -> void
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    SetConsoleTitleA("Mythril");

    bugslife = spdlog::stdout_color_mt("bugslife");
    bugslife->set_pattern("[%T] [%^%n%$]: %v");
    bugslife->set_level(spdlog::level::trace);

    console = spdlog::stdout_color_mt("mythril");
    console->set_pattern("[%T] [%^%n%$]: %v");
    console->set_level(spdlog::level::trace);

    if (MH_Initialize() != MH_OK) {
        console->error("failed to initialize minhook!");
        return;
    }

    if (MH_CreateHook
    (
        &OutputDebugStringA, &detour_debug_string,
        reinterpret_cast<LPVOID*>(&output_debug_string_o)
    ) != MH_OK)
    {
        console->error("failed to create OutputDebugStringA hook!");
        return;
    }

    if (MH_EnableHook(&OutputDebugStringA) != MH_OK)
    {
        console->error("failed to enable OutputDebugStringA hook!");
        return;
    }

    if (MH_CreateHook
    (
        reinterpret_cast<LPVOID>(load_asset),
        reinterpret_cast<LPVOID>(&detour_load_asset),
        reinterpret_cast<LPVOID*>(&load_asset_o)
    ) != MH_OK)
    {
        console->error("failed to create LoadAsset hook!");
        return;
    }

    if (MH_EnableHook(reinterpret_cast<LPVOID>(load_asset)) != MH_OK)
    {
        console->error("failed to enable LoadAsset hook!");
        return;
    }

    console->info("created & enabled all hooks!");

}

int APIENTRY DllMain(
    HMODULE module,
    DWORD  reason,
    LPVOID reserved
)
{

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        std::thread(main_thread, module).detach();
    }

    return 1;
}

