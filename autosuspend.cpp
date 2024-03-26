// autosuspend.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "autosuspend.h"
#include "args.hxx"

#pragma comment(lib, "Wtsapi32")

#define APP_TITLE "autosuspend"
#define MAX_LOADSTRING 100
#define TMR_ACTIVE_ID 1
#define TMR_HOOKRESET_ID 2
#define HOOK_MOUSEMOVE  1
#define HOOK_MOUSECLICK 2
#define HOOK_KEYBOARD   4

// Global Variables:
HINSTANCE g_hInst;                            // current instance
WCHAR g_szTitle[MAX_LOADSTRING];              // title bar text
WCHAR g_szWindowClass[MAX_LOADSTRING];        // main window class name
HWND  g_hMainWnd = nullptr;                   // main window handle
HHOOK g_hMouseHook = nullptr, g_hKbdHook = nullptr;
bool  g_bActivated = false;                   // target process activated flag
std::wstring g_sExeName;                      // target process exe name
int g_iActiveInterval = 20000;                // target app not-suspended interval
int g_iStartupDelay = 5000;                   // how long to wait for target process to run
std::wstring g_sExecCmd;                      // command to start target process
UINT g_uiHookParam = 0;                       // what input to track
HWINEVENTHOOK g_hWinEventHook = nullptr;

// Forward declarations of functions included in this code module:
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
void MyUnhook();
void MyHook();
LRESULT CALLBACK LLHookMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LLHookKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
std::vector<HANDLE> GetProcessHandles(DWORD parentPid);
void SuspendResume(const WCHAR* executable, bool suspend);
bool suspend_process(HANDLE processHandle);
void resume_process(HANDLE processHandle);
void Quit(int code);
bool RunProcess(std::wstring cmd);
void get_command_line_args(int* argc, char*** argv);
std::wstring utf8_utf16(std::string str);
void ShowMessage(std::wstring wmsg, UINT type, HWND hwnd = nullptr);
void ShowMessage(std::string msg, UINT type, HWND hwnd = nullptr);
void Unsuspend();
void CALLBACK winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
	LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
void HookReset();

//=======================================================================
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	MSG msg = { 0 };

	try
	{
		// Initialize global strings
		LoadStringW(hInstance, IDS_APP_TITLE, g_szTitle, MAX_LOADSTRING);
		LoadStringW(hInstance, IDC_AUTOSUSPEND, g_szWindowClass, MAX_LOADSTRING);
		MyRegisterClass(hInstance);

		// convert args to UTF8 for use in args::ArgumentParser
		int argc = 0;
		char** argv = nullptr;
		get_command_line_args(&argc, &argv);

		args::ArgumentParser parser("Suspend the process on keyboard/mouse inactivity.",
			"Example:\nautosuspend.exe manga_ocr.exe 20 5 /mc");
		parser.LongPrefix("/");
		args::HelpFlag help(parser, "help", "Display this help menu", { "help" });
		args::ValueFlag<std::string> arg_run(parser, "cmd", "Execute command", { "run" });
		args::Group group1(parser, "The target process:", args::Group::Validators::All);
		args::Positional<std::string> arg_exeName(group1, "exe", "Target executable name");
		args::Positional<int> arg_interval(group1, "interval", "Unsuspend time (sec)");
		args::Positional<int> arg_delay(group1, "delay", "Startup wait for target process (sec)");
		args::Group group2(parser, "Unsuspend the process on:", args::Group::Validators::AtLeastOne);
		args::Flag arg_mouseMove(group2, "mm", "Mouse move", { "mm" });
		args::Flag arg_mouseClick(group2, "mc", "Mouse click", { "mc" });
		args::Flag arg_keyb(group2, "kb", "Key press", { "kb" });
		try
		{
			parser.ParseCLI(argc, argv);
		}
		catch (args::Help)
		{
			std::stringstream ss;
			ss << parser;
			ShowMessage(ss.str(), MB_ICONINFORMATION | MB_OK);
			return 0;
		}
		catch (args::ParseError e)
		{
			std::stringstream ss;
			ss << e.what() << "\n\n";
			ss << parser;
			ShowMessage(ss.str(), MB_ICONINFORMATION | MB_OK);
			return 1;
		}
		catch (args::ValidationError e)
		{
			std::stringstream ss;
			ss << e.what() << "\n\n";
			ss << parser;
			ShowMessage(ss.str(), MB_ICONINFORMATION | MB_OK);
			return 1;
		}
		if (arg_run) g_sExecCmd = utf8_utf16(args::get(arg_run));
		if (arg_exeName) g_sExeName = utf8_utf16(args::get(arg_exeName));
		if (arg_interval) g_iActiveInterval = args::get(arg_interval) * 1000;
		if (arg_delay) g_iStartupDelay = args::get(arg_delay) * 1000;
		if (arg_mouseMove) g_uiHookParam |= HOOK_MOUSEMOVE;
		if (arg_mouseClick) g_uiHookParam |= HOOK_MOUSECLICK;
		if (arg_keyb) g_uiHookParam |= HOOK_KEYBOARD;

		free(argv);

		// Perform application initialization:
#ifdef _DEBUG
		if (!InitInstance(hInstance, nCmdShow))
#else
		if (!InitInstance(hInstance, SW_HIDE))
#endif
		{
			return FALSE;
		}
		HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_AUTOSUSPEND));

		// INIT APP
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS); // to less break under heavy CPU load

		if (!g_sExecCmd.empty()) {
			if (!RunProcess(g_sExecCmd))
				throw std::exception("RunProcess() failed");
		}

		Sleep(g_iStartupDelay);
		SuspendResume(g_sExeName.c_str(), false);

		// register system notifications to repair hooks
		if (FALSE == WTSRegisterSessionNotification(g_hMainWnd, NOTIFY_FOR_THIS_SESSION))
			throw std::exception("WTSRegisterSessionNotification() failed");
		g_hWinEventHook = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH,
			EVENT_SYSTEM_DESKTOPSWITCH, NULL, winEventProc, 0, 0,
			WINEVENT_OUTOFCONTEXT /* | WINEVENT_SKIPOWNPROCESS */);
		if (!g_hWinEventHook)
			throw std::exception("SetWinEventHook() failed");

		MyHook();

		// Main message loop:
		while (GetMessage(&msg, nullptr, 0, 0))
		{
			if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}
	catch (std::exception& e)
	{
		ShowMessage(e.what(), MB_OK | MB_ICONERROR);
	}

	Quit(0);

	return (int)msg.wParam;
}

//=======================================================================
void CALLBACK winEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject,
	LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
	HookReset();
}

//=======================================================================
void HookReset()
{
	SetTimer(g_hMainWnd, TMR_HOOKRESET_ID, 500, nullptr);
}

//=======================================================================
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_AUTOSUSPEND));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_AUTOSUSPEND);
	wcex.lpszClassName = g_szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

//=======================================================================
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	g_hInst = hInstance; // Store instance handle in our global variable

	g_hMainWnd = CreateWindowW(g_szWindowClass, g_szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, 320, 240, nullptr, nullptr, hInstance, nullptr);

	if (!g_hMainWnd)
	{
		return FALSE;
	}

	ShowWindow(g_hMainWnd, nCmdShow);
	UpdateWindow(g_hMainWnd);

	return TRUE;
}

//=======================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SETTINGCHANGE:
	case WM_DISPLAYCHANGE:
	case WM_DEVICECHANGE:
	case WM_WTSSESSION_CHANGE:
		HookReset();
		break;
	case WM_TIMER:
		KillTimer(hWnd, wParam); // one time timers
		switch (wParam)
		{
		case TMR_ACTIVE_ID:
			g_bActivated = false;
			SuspendResume(g_sExeName.c_str(), false);
			break;
		case TMR_HOOKRESET_ID:
			MyHook();
			break;
		}
		break;
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(g_hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		// TODO: Add any drawing code that uses hdc here...
		EndPaint(hWnd, &ps);
	}
	break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

//=======================================================================
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

//=======================================================================
void MyUnhook()
{
	// release hooks
	if (g_hMouseHook) {
		UnhookWindowsHookEx(g_hMouseHook);
		g_hMouseHook = nullptr;
	}
	if (g_hKbdHook) {
		UnhookWindowsHookEx(g_hKbdHook);
		g_hKbdHook = nullptr;
	}
}

//=======================================================================
void MyHook()
{
#ifdef _DEBUG
	Beep(500, 200);
#endif // _DEBUG

	// set up hooks
	MyUnhook();
	if ((g_uiHookParam & HOOK_MOUSEMOVE) == HOOK_MOUSEMOVE ||
		(g_uiHookParam & HOOK_MOUSECLICK) == HOOK_MOUSECLICK) {
		g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)LLHookMouseProc,
			GetModuleHandle(nullptr), NULL);
		if (!g_hMouseHook) throw std::exception("SetWindowsHookEx(WH_MOUSE_LL) failed");
	}
	if ((g_uiHookParam & HOOK_KEYBOARD) == HOOK_KEYBOARD) {
		g_hKbdHook = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)LLHookKeyboardProc,
			GetModuleHandle(NULL), NULL);
		if (!g_hKbdHook) throw std::exception("SetWindowsHookEx(WH_KEYBOARD_LL) failed");
	}
}

//=======================================================================
void Unsuspend()
{
	if (!g_bActivated)
	{
		g_bActivated = true;
		SuspendResume(g_sExeName.c_str(), true);
	}
	SetTimer(g_hMainWnd, TMR_ACTIVE_ID, g_iActiveInterval, nullptr);
}

//=======================================================================
LRESULT CALLBACK LLHookMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) // allowed to process message
	{
		switch (wParam)
		{
		case WM_MOUSEMOVE:
		case WM_MOUSEWHEEL:
			if ((g_uiHookParam & HOOK_MOUSEMOVE) == HOOK_MOUSEMOVE) {
				Unsuspend();
			}
			break;
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
			if ((g_uiHookParam & HOOK_MOUSECLICK) == HOOK_MOUSECLICK) {
				Unsuspend();
			}
			break;
		}
	}
	return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

//=======================================================================
LRESULT CALLBACK LLHookKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) // allowed to process message now
	{
		Unsuspend();
	}
	return CallNextHookEx(g_hKbdHook, nCode, wParam, lParam);
}

//=======================================================================
std::vector<HANDLE> GetProcessHandles(DWORD parentPid)
{
	std::vector<HANDLE> handles;
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
	{
		//std::cerr << "Failed to create snapshot." << std::endl;
		return handles;
	}

	PROCESSENTRY32 pe = { 0 };
	pe.dwSize = sizeof(PROCESSENTRY32);
	if (!Process32First(hSnapshot, &pe))
	{
		//std::cerr << "Failed to get first process." << std::endl;
		CloseHandle(hSnapshot);
		return handles;
	}

	do {
		if (pe.th32ParentProcessID == parentPid)
		{
			HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe.th32ProcessID);
			if (hProcess != NULL)
			{
				handles.push_back(hProcess);
				// Recursively find child processes
				std::vector<HANDLE> childHandles = GetProcessHandles(pe.th32ProcessID);
				handles.insert(handles.end(), childHandles.begin(), childHandles.end());
			}
		}
	} while (Process32Next(hSnapshot, &pe));

	CloseHandle(hSnapshot);
	return handles;
}

//=======================================================================
void SuspendResume(const WCHAR* executable, bool resume)
{
#ifdef _DEBUG
	if (resume)
		MessageBeep(MB_OK);
	else
		MessageBeep(MB_ICONSTOP);
#endif // _DEBUG

	HANDLE hProcess = nullptr;
	DWORD procPid = 0;
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe = { 0 };
	pe.dwSize = sizeof(PROCESSENTRY32);
	if (Process32First(hSnapshot, &pe))
	{
		do {
			if (_tcsicmp(pe.szExeFile, executable) == 0)
			{
				procPid = pe.th32ProcessID;
				hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe.th32ProcessID);
				if (hProcess == NULL)
					throw std::exception("SuspendResume(): OpenProcess(PROCESS_ALL_ACCESS) failed");
				break;
			}
		} while (Process32Next(hSnapshot, &pe));
	}
	CloseHandle(hSnapshot);

	if (procPid != 0)
	{
		std::vector<HANDLE> handles = GetProcessHandles(procPid);
		if (hProcess) handles.insert(handles.begin(), hProcess);
		for (HANDLE h : handles)
		{
			if (resume) {
				resume_process(h);
			}
			else {
				if (!suspend_process(h))
					throw std::exception("SuspendResume() : suspend_process() failed");
			}
			CloseHandle(h); // Remember to close the handles when done
		}
	}
	else {
		// Process not found
		Quit(0);
	}
}

//=======================================================================
static auto nt_suspend_process = reinterpret_cast<LONG(__stdcall*)(HANDLE)>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSuspendProcess"));
static auto nt_resume_process = reinterpret_cast<void(__stdcall*)(HANDLE)>(GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtResumeProcess"));

bool suspend_process(HANDLE processHandle)
{
	LONG ret = nt_suspend_process(processHandle);
	return (ret >= 0);
}

void resume_process(HANDLE processHandle)
{
	nt_resume_process(processHandle);
}

//=======================================================================
void Quit(int code)
{
	WTSUnRegisterSessionNotification(g_hMainWnd);
	if (g_hWinEventHook) {
		UnhookWinEvent(g_hWinEventHook);
	}
	MyUnhook();
	exit(code);
}

//=======================================================================
bool RunProcess(std::wstring cmd)
{
	PROCESS_INFORMATION pi = { 0 };
	STARTUPINFO si = { 0 };
	si.cb = sizeof(STARTUPINFO);
	if (CreateProcess(NULL, cmd.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	else {
		return false;
	}
	return true;
}

//=======================================================================
void get_command_line_args(int* argc, char*** argv)
{
	// Get the command line arguments as wchar_t strings
	wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), argc);
	if (!wargv) { *argc = 0; *argv = NULL; return; }

	// Count the number of bytes necessary to store the UTF-8 versions of those strings
	int n = 0;
	for (int i = 0; i < *argc; i++)
		n += WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);

	// Allocate the argv[] array + all the UTF-8 strings
	*argv = (char**)malloc((*argc + 1) * sizeof(char*) + n);
	if (!*argv) { *argc = 0; return; }

	// Convert all wargv[] --> argv[]
	char* arg = (char*)&((*argv)[*argc + 1]);
	for (int i = 0; i < *argc; i++)
	{
		(*argv)[i] = arg;
		arg += WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, arg, n, NULL, NULL);
	}
	(*argv)[*argc] = NULL;
	LocalFree(wargv);
}

//=======================================================================
std::wstring utf8_utf16(std::string str)
{
	std::wstring wide;
	int result = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
	if (result <= 0) throw std::exception("utf8_utf16() failed");

	wide.resize((size_t)result + 10);
	result = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wide[0], (int)wide.size());
	if (result <= 0) throw std::exception("utf8_utf16() failed");
	return wide;
}

//=======================================================================
void ShowMessage(std::wstring wmsg, UINT type, HWND hwnd)
{
	MessageBoxW(hwnd, wmsg.c_str(), _T(APP_TITLE), type);
}
void ShowMessage(std::string msg, UINT type, HWND hwnd)
{
	ShowMessage(utf8_utf16(msg), type, hwnd);
}

//=======================================================================
