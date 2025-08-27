// Block configurable hotkeys for everyone EXCEPT target executables, with debug logs.
// Build (x64): cl /EHsc /O2 /W4 /DUNICODE /D_UNICODE blocker.cpp user32.lib shell32.lib /link /SUBSYSTEM:WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <vector>
#include <string>
#include <io.h>
#include <fcntl.h>

static HWND g_msgWindow = nullptr;
static HANDLE g_dirHandle = INVALID_HANDLE_VALUE;
static OVERLAPPED g_overlapped = {};
static BYTE g_buffer[1024];

struct HotkeyDef {
  UINT modifiers;
  UINT vk;
  wchar_t name[32];
};

static std::vector<HotkeyDef> g_hotkeys;
static std::vector<std::wstring> g_targetExes;
static bool g_debugEnabled = false;

static void Log(const wchar_t* fmt, ...) {
  if(!g_debugEnabled) return;

  wchar_t buf[1024];
  va_list args; va_start(args, fmt);
  _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
  va_end(args);
  wprintf(L"%s\n", buf);
}

static bool IsTargetExe(const wchar_t* exe) {
  for(const auto& target : g_targetExes) {
    if(_wcsicmp(exe, target.c_str()) == 0) {
      return true;
    }
  }
  return false;
}

static UINT ParseModifiers(const wchar_t* str) {
  UINT mods = 0;

  std::wstring upper = str;
  for(auto& c : upper) c = towupper(c);

  if(upper.find(L"WIN") != std::wstring::npos) mods |= MOD_WIN;
  if(upper.find(L"CTRL") != std::wstring::npos) mods |= MOD_CONTROL;
  if(upper.find(L"ALT") != std::wstring::npos) mods |= MOD_ALT;
  if(upper.find(L"SHIFT") != std::wstring::npos) mods |= MOD_SHIFT;
  return mods;
}

static UINT ParseKey(const wchar_t* str) {
  const wchar_t* key = wcsrchr(str, L'+');
  if(!key) key = str; else key++;

  // Handle function keys F1-F24
  if(wcsncmp(key, L"F", 1) == 0 && wcslen(key) > 1) {
    int fnum = _wtoi(key + 1);
    if(fnum >= 1 && fnum <= 24) return VK_F1 + fnum - 1;
  }

  if(wcslen(key) == 1) {
    wchar_t c = towupper(key[0]);
    if(c >= L'A' && c <= L'Z') return c;
    if(c >= L'0' && c <= L'9') return c;
  }

  return 0;
}

static bool GetForegroundExeBase(wchar_t* out, DWORD outCch) {
  if(!out || outCch == 0) return false;
  out[0] = L'\0';

  HWND hwnd = GetForegroundWindow();
  if(!hwnd) return false;

  DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
  if(!pid) return false;

  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if(!h) return false;

  wchar_t path[MAX_PATH]; DWORD cch = MAX_PATH;
  bool ok = !!QueryFullProcessImageNameW(h, 0, path, &cch);
  CloseHandle(h);
  if(!ok) return false;

  const wchar_t* base = path;
  for(const wchar_t* p = path; *p; ++p) if(*p == L'\\' || *p == L'/') base = p + 1;

  wcsncpy_s(out, outCch, base, _TRUNCATE);
  return true;
}

static bool LoadConfig() {
  std::vector<HotkeyDef> newHotkeys;
  std::vector<std::wstring> newTargetExes;

  FILE* f = nullptr;
  if(_wfopen_s(&f, L"config.txt", L"r") != 0) {
    Log(L"config.txt not found, using defaults");

    newTargetExes.push_back(L"parsecd.exe");

    const wchar_t* defaults[] = {
      L"Win+C", L"Win+V", L"Win+F", L"Win+Z", L"Win+Y", L"Win+S"
    };

    for(int i = 0; i < 6; ++i) {
      HotkeyDef h;
      h.modifiers = ParseModifiers(defaults[i]);
      h.vk = ParseKey(defaults[i]);
      wcscpy_s(h.name, defaults[i]);
      if(h.vk) newHotkeys.push_back(h);
    }

    if(newHotkeys.empty()) return false;

    g_hotkeys = newHotkeys;
    g_targetExes = newTargetExes;
    return true;
  }

  wchar_t line[256];
  while(fgetws(line, 256, f)) {
    line[wcscspn(line, L"\r\n")] = 0;

    if(line[0] == 0 || line[0] == L'#' || line[0] == L';') continue;

    if(wcsncmp(line, L"target=", 7) == 0) {
      std::wstring targets = line + 7;
      size_t start = 0;
      size_t pos = 0;

      while((pos = targets.find(L';', start)) != std::wstring::npos) {
        std::wstring exe = targets.substr(start, pos - start);
        if(!exe.empty()) {
          newTargetExes.push_back(exe);
          Log(L"Added target exe: %s", exe.c_str());
        }
        start = pos + 1;
      }

      if(start < targets.length()) {
        std::wstring exe = targets.substr(start);
        if(!exe.empty()) {
          newTargetExes.push_back(exe);
          Log(L"Added target exe: %s", exe.c_str());
        }
      }
      continue;
    }

    HotkeyDef h;
    h.modifiers = ParseModifiers(line);
    h.vk = ParseKey(line);
    wcscpy_s(h.name, line);

    if(h.vk) {
      newHotkeys.push_back(h);
      Log(L"Loaded hotkey: %s", h.name);
    } else {
      Log(L"Invalid hotkey: %s", line);
    }
  }

  fclose(f);

  if(newTargetExes.empty()) {
    newTargetExes.push_back(L"parsecd.exe");
    Log(L"Using default target: parsecd.exe");
  }

  if(newHotkeys.empty()) {
    Log(L"No valid hotkeys found in config");
    return false;
  }

  g_hotkeys = newHotkeys;
  g_targetExes = newTargetExes;
  return true;
}

static void ReloadConfig() {
  for(size_t i = 0; i < g_hotkeys.size(); ++i) {
    UnregisterHotKey(g_msgWindow, (int)(i + 1));
  }

  if(LoadConfig()) {
    Log(L"Config reloaded successfully");

    int registered = 0;
    for(size_t i = 0; i < g_hotkeys.size(); ++i) {
      if(RegisterHotKey(g_msgWindow, (int)(i + 1), g_hotkeys[i].modifiers, g_hotkeys[i].vk)) {
        registered++;
      } else {
        Log(L"RegisterHotKey failed for %s", g_hotkeys[i].name);
      }
    }
    Log(L"%d/%d hotkeys registered after reload", registered, (int)g_hotkeys.size());

    if(!g_debugEnabled) {
      MessageBoxW(nullptr, L"Configuration updated successfully", L"Blocker", MB_OK | MB_ICONINFORMATION);
    }
  } else {
    Log(L"Failed to reload config, keeping old configuration");

    for(size_t i = 0; i < g_hotkeys.size(); ++i) {
      RegisterHotKey(g_msgWindow, (int)(i + 1), g_hotkeys[i].modifiers, g_hotkeys[i].vk);
    }

    if(!g_debugEnabled) {
      MessageBoxW(nullptr, L"Failed to reload configuration. Invalid config detected, keeping previous settings.", L"Blocker - Config Error", MB_OK | MB_ICONERROR);
    }
  }
}

static bool RegisterHotkeys() {
  g_msgWindow = CreateWindowW(L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
  if(!g_msgWindow) {
    Log(L"CreateWindowW failed");
    return false;
  }

  int registered = 0;
  for(size_t i = 0; i < g_hotkeys.size(); ++i) {
    if(!RegisterHotKey(g_msgWindow, (int)(i + 1), g_hotkeys[i].modifiers, g_hotkeys[i].vk)) {
      Log(L"RegisterHotKey failed for %s", g_hotkeys[i].name);
    } else {
      registered++;
    }
  }

  Log(L"%d/%d hotkeys registered", registered, (int)g_hotkeys.size());
  return registered > 0;
}

static void UnregisterHotkeys() {
  if(!g_msgWindow) return;

  for(size_t i = 0; i < g_hotkeys.size(); ++i) {
    UnregisterHotKey(g_msgWindow, (int)(i + 1));
  }

  DestroyWindow(g_msgWindow);
  Log(L"Hotkeys unregistered");
}

static void KillRunningInstances() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if(snapshot == INVALID_HANDLE_VALUE) return;

  PROCESSENTRY32W pe32;
  pe32.dwSize = sizeof(PROCESSENTRY32W);

  DWORD currentPid = GetCurrentProcessId();

  if(Process32FirstW(snapshot, &pe32)) {
    do {
      if(pe32.th32ProcessID != currentPid && _wcsicmp(pe32.szExeFile, L"blocker.exe") == 0) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
        if(hProcess) {
          TerminateProcess(hProcess, 0);
          CloseHandle(hProcess);
          wprintf(L"Killed blocker.exe (PID: %lu)\n", pe32.th32ProcessID);
        }
      }
    } while(Process32NextW(snapshot, &pe32));
  }

  CloseHandle(snapshot);
}

static void StartDirectoryWatch() {
  g_dirHandle = CreateFileW(L".", FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

  if(g_dirHandle == INVALID_HANDLE_VALUE) {
    Log(L"Could not monitor directory for changes");
    return;
  }

  g_overlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

  ReadDirectoryChangesW(g_dirHandle, g_buffer, sizeof(g_buffer), FALSE,
                       FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                       nullptr, &g_overlapped, nullptr);

  Log(L"Monitoring config.txt for changes");
}

static bool CheckForConfigChange() {
  if(g_dirHandle == INVALID_HANDLE_VALUE) return false;

  DWORD bytesReturned;
  if(!GetOverlappedResult(g_dirHandle, &g_overlapped, &bytesReturned, FALSE)) {
    return false;
  }

  FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)g_buffer;

  while(info) {
    wchar_t filename[MAX_PATH];
    wcsncpy_s(filename, info->FileName, info->FileNameLength / sizeof(wchar_t));
    filename[info->FileNameLength / sizeof(wchar_t)] = 0;

    if(_wcsicmp(filename, L"config.txt") == 0) {
      ReadDirectoryChangesW(g_dirHandle, g_buffer, sizeof(g_buffer), FALSE,
                           FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                           nullptr, &g_overlapped, nullptr);
      return true;
    }

    if(info->NextEntryOffset == 0) break;
    info = (FILE_NOTIFY_INFORMATION*)((BYTE*)info + info->NextEntryOffset);
  }

  ReadDirectoryChangesW(g_dirHandle, g_buffer, sizeof(g_buffer), FALSE,
                       FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                       nullptr, &g_overlapped, nullptr);

  return false;
}

static void SendHotkeyToParsec(const HotkeyDef& hotkey) {
  struct ModKey { UINT flag; UINT vk; };
  const ModKey modKeys[] = {
    { MOD_CONTROL, VK_CONTROL },
    { MOD_ALT, VK_MENU },
    { MOD_SHIFT, VK_SHIFT },
    { MOD_WIN, VK_LWIN }
  };

  INPUT inputs[10] = {};
  int n = 0;

  for(int i = 0; i < 4; ++i) {
    if(hotkey.modifiers & modKeys[i].flag) {
      inputs[n].type = INPUT_KEYBOARD;
      inputs[n].ki.wVk = (WORD)modKeys[i].vk;
      inputs[n].ki.wScan = (WORD)MapVirtualKey(modKeys[i].vk, MAPVK_VK_TO_VSC);
      inputs[n].ki.dwFlags = KEYEVENTF_SCANCODE;
      n++;
    }
  }

  inputs[n].type = INPUT_KEYBOARD;
  inputs[n].ki.wVk = (WORD)hotkey.vk;
  inputs[n].ki.wScan = (WORD)MapVirtualKey(hotkey.vk, MAPVK_VK_TO_VSC);
  inputs[n].ki.dwFlags = KEYEVENTF_SCANCODE;
  n++;

  inputs[n].type = INPUT_KEYBOARD;
  inputs[n].ki.wVk = (WORD)hotkey.vk;
  inputs[n].ki.wScan = (WORD)MapVirtualKey(hotkey.vk, MAPVK_VK_TO_VSC);
  inputs[n].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
  n++;

  for(int i = 3; i >= 0; --i) {
    if(hotkey.modifiers & modKeys[i].flag) {
      inputs[n].type = INPUT_KEYBOARD;
      inputs[n].ki.wVk = (WORD)modKeys[i].vk;
      inputs[n].ki.wScan = (WORD)MapVirtualKey(modKeys[i].vk, MAPVK_VK_TO_VSC);
      inputs[n].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
      n++;
    }
  }

  SendInput(n, inputs, sizeof(INPUT));
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int) {
  int argc;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

  for(int i = 1; i < argc; ++i) {
    if(wcscmp(argv[i], L"--debug") == 0) {
      g_debugEnabled = true;
    } else if(wcscmp(argv[i], L"--kill") == 0) {
      KillRunningInstances();
      LocalFree(argv);
      return 0;
    }
  }
  LocalFree(argv);

  if(g_debugEnabled) {
    AllocConsole();
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
    freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
    setvbuf(stdout, nullptr, _IONBF, 0);
  }

  Log(L"Starting… PID=%lu", GetCurrentProcessId());

  if(!LoadConfig()) {
    Log(L"No valid hotkeys to register");
    return 1;
  }

  if(!RegisterHotkeys()) {
    return 1;
  }

  StartDirectoryWatch();

  MSG msg;

  while(true) {
    DWORD waitResult = MsgWaitForMultipleObjects(1, &g_overlapped.hEvent, FALSE, INFINITE, QS_ALLINPUT);

    if(waitResult == WAIT_OBJECT_0) {
      Sleep(100);
      if(CheckForConfigChange()) {
        Log(L"config.txt modified, reloading...");
        ReloadConfig();
      }
    } else if(waitResult == WAIT_OBJECT_0 + 1) {
      while(PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if(msg.message == WM_QUIT) {
          goto cleanup;
        }

        if(msg.message == WM_HOTKEY) {
          int hotkeyId = (int)msg.wParam;
          const HotkeyDef& hotkey = g_hotkeys[hotkeyId - 1];

          wchar_t exe[260];
          bool haveExe = GetForegroundExeBase(exe, 260);
          const bool isTarget = haveExe && IsTargetExe(exe);

          Log(L"%s detected. Foreground='%s' (isTarget=%d)", hotkey.name, haveExe ? exe : L"<unknown>", isTarget ? 1 : 0);

          if(isTarget) {
            Log(L"-> ALLOW %s to target app", hotkey.name);

            UnregisterHotKey(g_msgWindow, hotkeyId);
            SendHotkeyToParsec(hotkey);
            Sleep(10);
            RegisterHotKey(g_msgWindow, hotkeyId, hotkey.modifiers, hotkey.vk);
          } else {
            Log(L"-> SWALLOW %s", hotkey.name);
          }
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    } else {
      break;
    }
  }

cleanup:
  UnregisterHotkeys();

  if(g_overlapped.hEvent) {
    CloseHandle(g_overlapped.hEvent);
  }
  if(g_dirHandle != INVALID_HANDLE_VALUE) {
    CloseHandle(g_dirHandle);
  }

  return 0;
}