// Block configurable hotkeys for everyone EXCEPT parsec.exe, with debug logs.
// Build (x64): cl /EHsc /O2 /W4 /DUNICODE /D_UNICODE blocker.cpp user32.lib

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static HWND g_msgWindow = nullptr;

struct HotkeyDef {
  UINT modifiers;
  UINT vk;
  const wchar_t* name;
};

// Configure which hotkeys to block here
static const HotkeyDef g_hotkeys[] = {
  { MOD_WIN, 'C', L"Win+C" },
  { MOD_WIN, 'V', L"Win+V" },
  { MOD_WIN, 'F', L"Win+F" },
  { MOD_WIN, 'Z', L"Win+Z" },
  { MOD_WIN, 'Y', L"Win+Y" },
  { MOD_WIN, 'S', L"Win+S" }
};
static const int g_numHotkeys = sizeof(g_hotkeys) / sizeof(g_hotkeys[0]);

static void Log(const wchar_t* fmt, ...) {
  wchar_t buf[1024];
  va_list args; va_start(args, fmt);
  _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
  va_end(args);
  wprintf(L"%s\n", buf);
  OutputDebugStringW(buf);
  OutputDebugStringW(L"\n");
}

static bool GetForegroundExeBase(wchar_t* out, DWORD outCch) {
  if(!out || outCch == 0) return false;
  out[0] = L'\0';

  HWND hwnd = GetForegroundWindow();
  if(!hwnd) { Log(L"[DBG] GetForegroundWindow failed"); return false; }

  DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
  if(!pid) { Log(L"[DBG] GetWindowThreadProcessId failed"); return false; }

  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if(!h) { Log(L"[DBG] OpenProcess(%lu) failed: %lu", pid, GetLastError()); return false; }

  wchar_t path[MAX_PATH]; DWORD cch = MAX_PATH;
  bool ok = !!QueryFullProcessImageNameW(h, 0, path, &cch);
  CloseHandle(h);
  if(!ok) { Log(L"[DBG] QueryFullProcessImageNameW failed: %lu", GetLastError()); return false; }

  const wchar_t* base = path;
  for(const wchar_t* p = path; *p; ++p) if(*p == L'\\' || *p == L'/') base = p + 1;

  wcsncpy_s(out, outCch, base, _TRUNCATE);
  return true;
}

static bool RegisterHotkeys() {
  g_msgWindow = CreateWindowW(L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
  if(!g_msgWindow) {
    Log(L"[ERR] CreateWindowW failed: %lu", GetLastError());
    return false;
  }

  int registered = 0;
  for(int i = 0; i < g_numHotkeys; ++i) {
    if(!RegisterHotKey(g_msgWindow, i + 1, g_hotkeys[i].modifiers, g_hotkeys[i].vk)) {
      Log(L"[WARN] RegisterHotKey failed for %s: %lu", g_hotkeys[i].name, GetLastError());
    } else {
      registered++;
    }
  }

  Log(L"[DBG] %d/%d hotkeys registered successfully.", registered, g_numHotkeys);
  return registered > 0;
}

static void UnregisterHotkeys() {
  if(!g_msgWindow) return;

  for(int i = 0; i < g_numHotkeys; ++i) {
    UnregisterHotKey(g_msgWindow, i + 1);
  }

  DestroyWindow(g_msgWindow);
  Log(L"[DBG] Hotkeys unregistered.");
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

  // Press modifiers
  for(int i = 0; i < 4; ++i) {
    if(hotkey.modifiers & modKeys[i].flag) {
      inputs[n].type = INPUT_KEYBOARD;
      inputs[n].ki.wVk = (WORD)modKeys[i].vk;
      inputs[n].ki.wScan = (WORD)MapVirtualKey(modKeys[i].vk, MAPVK_VK_TO_VSC);
      inputs[n].ki.dwFlags = KEYEVENTF_SCANCODE;
      n++;
    }
  }

  // Press main key
  inputs[n].type = INPUT_KEYBOARD;
  inputs[n].ki.wVk = (WORD)hotkey.vk;
  inputs[n].ki.wScan = (WORD)MapVirtualKey(hotkey.vk, MAPVK_VK_TO_VSC);
  inputs[n].ki.dwFlags = KEYEVENTF_SCANCODE;
  n++;

  // Release main key
  inputs[n].type = INPUT_KEYBOARD;
  inputs[n].ki.wVk = (WORD)hotkey.vk;
  inputs[n].ki.wScan = (WORD)MapVirtualKey(hotkey.vk, MAPVK_VK_TO_VSC);
  inputs[n].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
  n++;

  // Release modifiers (reverse order)
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

int wmain() {
  // unbuffer stdout so logs appear immediately
  setvbuf(stdout, nullptr, _IONBF, 0);

  Log(L"[DBG] Starting… PID=%lu", GetCurrentProcessId());

  if(!RegisterHotkeys()) {
    return 1;
  }

  MSG msg;
  while(GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if(msg.message == WM_HOTKEY) {
      int hotkeyId = (int)msg.wParam;
      const HotkeyDef& hotkey = g_hotkeys[hotkeyId - 1];

      wchar_t exe[260];
      bool haveExe = GetForegroundExeBase(exe, 260);
      const bool isParsec = haveExe && (_wcsicmp(exe, L"parsecd.exe") == 0);

      Log(L"[DBG] %s detected. Foreground='%s' (haveExe=%d, isParsec=%d)",
          hotkey.name, haveExe ? exe : L"<unknown>", haveExe ? 1 : 0, isParsec ? 1 : 0);

      if(isParsec) {
        Log(L"[DBG] -> ALLOW %s to Parsec", hotkey.name);

        // Temporarily unregister to avoid triggering our own hotkey
        UnregisterHotKey(g_msgWindow, hotkeyId);

        SendHotkeyToParsec(hotkey);

        // Re-register after short delay
        Sleep(10);
        RegisterHotKey(g_msgWindow, hotkeyId, hotkey.modifiers, hotkey.vk);
      } else {
        Log(L"[DBG] -> SWALLOW %s", hotkey.name);
      }
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  UnregisterHotkeys();
  return 0;
}