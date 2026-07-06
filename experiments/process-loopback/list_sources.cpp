// M2 experiment (part 3, req #1): data source for Plugin Play's "pick your input" UI.
//
// In the driverless model the user selects the DJ APP inside Plugin Play (not a
// device). This enumerates every process that currently has an audio session on
// the default render endpoint — exactly the list Plugin Play would show, with the
// PID we then hand to process-loopback capture.
//
// Prints: state, PID, executable, friendly session name.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <psapi.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

static std::string exeForPid(DWORD pid)
{
    if (pid == 0) return "(system)";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (! h) return "(access denied)";
    char path[MAX_PATH]; DWORD sz = MAX_PATH;
    std::string name = "(unknown)";
    if (QueryFullProcessImageNameA(h, 0, path, &sz))
    {
        std::string full = path;
        auto slash = full.find_last_of("\\/");
        name = (slash == std::string::npos) ? full : full.substr(slash + 1);
    }
    CloseHandle(h);
    return name;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered: partial output survives a crash
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) { fprintf(stderr, "CoInit failed\n"); return 2; }

    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
    { fprintf(stderr, "enumerator failed\n"); return 2; }
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
    { fprintf(stderr, "no default render endpoint\n"); return 2; }

    ComPtr<IAudioSessionManager2> mgr;
    if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**) mgr.GetAddressOf())))
    { fprintf(stderr, "session manager failed\n"); return 2; }

    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(mgr->GetSessionEnumerator(&sessions))) { fprintf(stderr, "session enum failed\n"); return 2; }

    int count = 0; sessions->GetCount(&count);
    printf("Audio sessions on the default render endpoint (%d):\n\n", count);
    printf("  %-8s  %-6s  %-28s  %s\n", "STATE", "PID", "EXECUTABLE", "SESSION NAME");
    printf("  %-8s  %-6s  %-28s  %s\n", "-----", "---", "----------", "------------");

    int usable = 0;
    for (int i = 0; i < count; ++i)
    {
        ComPtr<IAudioSessionControl> ctl;
        if (FAILED(sessions->GetSession(i, &ctl))) continue;
        ComPtr<IAudioSessionControl2> ctl2;
        if (FAILED(ctl.As(&ctl2))) continue;

        DWORD pid = 0; ctl2->GetProcessId(&pid);
        if (ctl2->IsSystemSoundsSession() == S_OK) continue;  // skip system dings

        AudioSessionState state = AudioSessionStateInactive;
        ctl->GetState(&state);
        const char* st = state == AudioSessionStateActive ? "ACTIVE"
                        : state == AudioSessionStateInactive ? "idle" : "expired";

        LPWSTR disp = nullptr; std::string name = "";
        if (SUCCEEDED(ctl->GetDisplayName(&disp)) && disp && disp[0])
        {
            char buf[256]; WideCharToMultiByte(CP_UTF8, 0, disp, -1, buf, sizeof(buf), nullptr, nullptr);
            name = buf;
        }
        if (disp) CoTaskMemFree(disp);

        printf("  %-8s  %-6lu  %-28s  %s\n", st, (unsigned long) pid, exeForPid(pid).c_str(), name.c_str());
        usable++;
    }

    printf("\n%d selectable source(s). Plugin Play would list these and pass the chosen\n"
           "PID to AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK. Launch a DJ app / media\n"
           "player and re-run to see it appear.\n", usable);

    // NB: no CoUninitialize() — ComPtrs above would then Release() after COM
    // teardown (AV). Process is exiting; let the OS reclaim.
    return 0;
}
