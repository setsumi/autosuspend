// autosuspend.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "autosuspend.h"

#define MAX_LOADSTRING 100
#define TMR_ACTIVE_ID 1

// Global Variables:
HINSTANCE g_hInst;                            // current instance
WCHAR g_szTitle[MAX_LOADSTRING];              // title bar text
WCHAR g_szWindowClass[MAX_LOADSTRING];        // main window class name
HWND  g_hMainWnd = nullptr;                   // main window handle
HHOOK g_hMouseHook = nullptr;
bool  g_bActivated = false;                   // target process activated flag
std::wstring g_sExeName;                      // target process exe name
int g_iActiveInterval = 15000;                // target app not-suspended interval
int g_iStartupDelay = 5000;                   // how long to wait for target process to run
std::wstring g_sExecCmd;                      // command to start target process

// Forward declarations of functions included in this code module:
ATOM             MyRegisterClass(HINSTANCE hInstance);
BOOL             InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
void MyUnhook();
void MyHook();
LRESULT CALLBACK LLHookMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
std::vector<HANDLE> GetProcessHandles(DWORD parentPid);
void SuspendResume(const WCHAR* executable, bool suspend);
bool suspend_process(HANDLE processHandle);
void resume_process(HANDLE processHandle);
void Quit(int code);
bool RunProcess(std::wstring cmd);

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

		int numArgs = 0;
		LPWSTR* parsedArgs = CommandLineToArgvW(GetCommandLineW(), &numArgs);
		if (numArgs <= 1) {
			MessageBox(nullptr, L"Suspend the process on mouse inactivity\n\n"
				L"Usage:\nautosuspend.exe <target.exe> [Unsuspend time seconds (default: 15)] "
				L"[Startup wait seconds (default: 5)] [exec command]\n\n"
				L"Example:\nautosuspend.exe manga_ocr.exe 15 5", L"autosuspend", MB_ICONINFORMATION | MB_OK);
			Quit(0);
		}
		if (numArgs >= 2) {
			g_sExeName = parsedArgs[1];
		}
		if (numArgs >= 3) {
			g_iActiveInterval = std::stoi(parsedArgs[2]);
			g_iActiveInterval = g_iActiveInterval * 1000;
		}
		if (numArgs >= 4) {
			g_iStartupDelay = std::stoi(parsedArgs[3]);
			g_iStartupDelay = g_iStartupDelay * 1000;
		}
		if (numArgs >= 5) {
			g_sExecCmd = parsedArgs[4];
		}
		LocalFree(parsedArgs);

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

		// Init app
		if (!g_sExecCmd.empty()) {
			if (!RunProcess(g_sExecCmd))
				throw std::exception("RunProcess() failed");
		}
		Sleep(g_iStartupDelay);
		SuspendResume(g_sExeName.c_str(), false);
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
		MessageBoxA(nullptr, e.what(), "autosuspend", MB_OK | MB_ICONERROR);
	}

	Quit(0);

	return (int)msg.wParam;
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
	case WM_TIMER:
		KillTimer(hWnd, TMR_ACTIVE_ID);
		g_bActivated = false;
		SuspendResume(g_sExeName.c_str(), false);
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
	if (g_hMouseHook)
	{
		UnhookWindowsHookEx(g_hMouseHook);
		g_hMouseHook = nullptr;
	}
}

//=======================================================================
void MyHook()
{
	MyUnhook();

	// set up hooks
	g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)LLHookMouseProc,
		GetModuleHandle(nullptr), NULL);
	if (!g_hMouseHook)
	{
		throw std::exception("SetWindowsHookEx(WH_MOUSE_LL) failed");
	}
}

//=======================================================================
LRESULT CALLBACK LLHookMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) // allowed to process message
	{
		switch (wParam)
		{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
			if (!g_bActivated)
			{
				g_bActivated = true;
				SuspendResume(g_sExeName.c_str(), true);
			}
			SetTimer(g_hMainWnd, TMR_ACTIVE_ID, g_iActiveInterval, nullptr);
			break;
		}
	}
	return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
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
		//MessageBox(nullptr, L"Process not found", L"autosuspend", MB_OK);
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
