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
void suspend_process(HANDLE processHandle);
void resume_process(HANDLE processHandle);
void Quit(int code);

//=======================================================================
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	MSG msg;

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
				L"Usage:\nautosuspend.exe <target.exe> [active time seconds (default: 15)] "
				L"[startup delay seconds (default: 5)]\n\n"
				L"Example:\nautosuspend.exe manga_ocr.exe 15", L"autosuspend", MB_ICONINFORMATION | MB_OK);
			Quit(0);
		}
		if (numArgs >= 2) {
			g_sExeName = parsedArgs[1];
		}
		if (numArgs >= 3) {
			std::wstringstream wss(parsedArgs[2]);
			wss >> g_iActiveInterval;
			g_iActiveInterval = g_iActiveInterval * 1000;
		}
		if (numArgs >= 4) {
			std::wstringstream wss(parsedArgs[3]);
			wss >> g_iStartupDelay;
			g_iStartupDelay = g_iStartupDelay * 1000;
		}
		LocalFree(parsedArgs);

		// Perform application initialization:
		if (!InitInstance(hInstance, SW_HIDE/*nCmdShow*/))
		{
			return FALSE;
		}
		HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_AUTOSUSPEND));

		// Init app
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
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

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
#ifdef _DEBUG
		MessageBeep(MB_ICONSTOP);
#endif
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
#ifdef _DEBUG
				MessageBeep(MB_OK);
#endif
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
			if (resume)
				resume_process(h);
			else
				suspend_process(h);
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

void suspend_process(HANDLE processHandle)
{
	nt_suspend_process(processHandle);
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
