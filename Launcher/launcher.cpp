#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#pragma comment(lib, "spdlog.lib")
#pragma comment(lib, "fmt.lib")

#include <Windows.h>

#include <iostream>
#include <thread>

const std::string game_executable = "BugsLife.exe";
const std::string payload_path = "mythril.dll";

auto console = spdlog::stdout_color_mt("mythril");

auto create_and_suspend() -> PROCESS_INFORMATION
{
    STARTUPINFOA startup_info = { sizeof(startup_info) };
    PROCESS_INFORMATION process_info = {};

    if (!CreateProcessA(
        game_executable.c_str(),
        nullptr, nullptr, nullptr,
        FALSE, CREATE_SUSPENDED,
        nullptr, nullptr,
        &startup_info, &process_info
    )) {
        return PROCESS_INFORMATION{};
    }
    return process_info;
}

auto inject_payload(PROCESS_INFORMATION process) -> int
{
    void* remote_mem = VirtualAllocEx
    (
        process.hProcess,
        nullptr,
        4096,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE
    );

    if (!remote_mem)
        return 1;

    if (!WriteProcessMemory
    (
        process.hProcess,
        remote_mem,
        payload_path.c_str(),
        payload_path.size() + 1,
        nullptr)
        ) {
        VirtualFreeEx(process.hProcess, remote_mem, 0, MEM_RELEASE);
        return 2;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC load_library = GetProcAddress(kernel32, "LoadLibraryA");

    if (!load_library)
    {
        VirtualFreeEx(process.hProcess, remote_mem, 0, MEM_RELEASE);
        return 3;
    }

    HANDLE remote_thread = CreateRemoteThread
    (
        process.hProcess,
        nullptr,
        0,
        (LPTHREAD_START_ROUTINE)load_library,
        remote_mem,
        0,
        nullptr
    );

    if (!remote_thread)
    {
        VirtualFreeEx(process.hProcess, remote_mem, 0, MEM_RELEASE);
        return 4;
    }

    WaitForSingleObject(remote_thread, INFINITE);

    CloseHandle(remote_thread);
    VirtualFreeEx(process.hProcess, remote_mem, 0, MEM_RELEASE);

    return 0;
}

auto main(int argc, char* argv[]) -> int
{
    system("cls");
    SetConsoleTitleA("Mythril");

    console->set_pattern("[%T] [%^%n%$]: %v");
    console->set_level(spdlog::level::trace);

    console->debug("creating process for {}...", game_executable);
    auto bugs = create_and_suspend();
    if (bugs.hProcess == nullptr)
    {
        spdlog::error("Failed to create process!");
        return 0;
    }

    console->debug("injecting payload...");
    int result = inject_payload(bugs);

    if (result == 0)
    {
        console->info("payload injected successfully!");
    }
    else
    {
        TerminateProcess(bugs.hProcess, 0);
        CloseHandle(bugs.hProcess);
        console->error("failed to inject payload with error code: {}", result);
        return result;
    }

    console->debug("resuming process...");
    ResumeThread(bugs.hThread);
    CloseHandle(bugs.hThread);
    CloseHandle(bugs.hProcess);
    return 0;
}

