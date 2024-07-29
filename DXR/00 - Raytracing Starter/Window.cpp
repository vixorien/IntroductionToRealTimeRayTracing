
#include "Window.h"
#include "Graphics.h"
#include "Input.h"

#include <sstream>

namespace Window
{
	// Annonymous namespace to hold variables
	// only accessible in this file
	namespace
	{
		// Initialization tracking
		bool windowCreated = false;
		bool consoleCreated = false;

		// Window details
		std::wstring windowTitle;
		unsigned int windowWidth = 0;
		unsigned int windowHeight = 0;
		bool windowStats = false;
		HWND windowHandle = 0;
		bool hasFocus = false;
		bool isMinimized = false;
		
		// Game class pointer necessary
		// for window resizing callback
		Game* game;

		// Basic FPS tracking
		float fpsTimeElapsed = 0.0f;
		__int64 fpsFrameCounter = 0;

	}
}

// Getters for window-related information
unsigned int Window::Width() { return windowWidth; }
unsigned int Window::Height() { return windowHeight; }
float Window::AspectRatio() { return (float)windowWidth / windowHeight; }
HWND Window::Handle() { return windowHandle; }
bool Window::HasFocus() { return hasFocus; }
bool Window::IsMinimized() { return isMinimized; }

// --------------------------------------------------------
// Creates the actual window for our application
// 
// appInstance     - The OS-level application instance handle
//                   provided by WinMain()
// width           - Desired width of the window
// height          - Desired height of the window
// titleBarText    - Window's title bar text
// statsInTitleBar - Want debug stats (like FPS) in title bar?
// gameApp         - The Game object associated with this window
// --------------------------------------------------------
HRESULT Window::Create(
	HINSTANCE appInstance,
	unsigned int width, 
	unsigned int height, 
	std::wstring titleBarText,
	bool statsInTitleBar,
	Game* gameApp)
{
	// Verify
	if (windowCreated)
		return E_FAIL;

	// Save data
	windowWidth = width;
	windowHeight = height;
	windowTitle = titleBarText;
	windowStats = statsInTitleBar;
	game = gameApp;

	// Start window creation by filling out the
	// appropriate window class struct
	WNDCLASS wndClass = {}; // Zero out the memory
	wndClass.style = CS_HREDRAW | CS_VREDRAW;	// Redraw on horizontal or vertical movement/adjustment
	wndClass.lpfnWndProc = ProcessMessage;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = appInstance;						// Our app's handle
	wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);	// Default icon
	wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);		// Default arrow cursor
	wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wndClass.lpszMenuName = NULL;
	wndClass.lpszClassName = L"GraphicsWindowClass";

	// Attempt to register the window class we've defined
	if (!RegisterClass(&wndClass))
	{
		// Get the most recent error
		DWORD error = GetLastError();

		// If the class exists, that's actually fine.  Otherwise,
		// we can't proceed with the next step.
		if (error != ERROR_CLASS_ALREADY_EXISTS)
			return HRESULT_FROM_WIN32(error);
	}

	// Adjust the width and height so the "client size" matches
	// the width and height given (the inner-area of the window)
	RECT clientRect;
	SetRect(&clientRect, 0, 0, width, height);
	AdjustWindowRect(
		&clientRect,
		WS_OVERLAPPEDWINDOW,	// Has a title bar, border, min and max buttons, etc.
		false);					// No menu bar

	// Center the window to the screen
	RECT desktopRect;
	GetClientRect(GetDesktopWindow(), &desktopRect);
	int centeredX = (desktopRect.right / 2) - (clientRect.right / 2);
	int centeredY = (desktopRect.bottom / 2) - (clientRect.bottom / 2);

	// Actually ask Windows to create the window itself
	// using our settings so far.  This will return the
	// handle of the window, which we'll keep around for later
	windowHandle = CreateWindow(
		wndClass.lpszClassName,
		windowTitle.c_str(),
		WS_OVERLAPPEDWINDOW,
		centeredX,
		centeredY,
		clientRect.right - clientRect.left,	// Calculated width
		clientRect.bottom - clientRect.top,	// Calculated height
		0,			// No parent window
		0,			// No menu
		appInstance,// The app's handle
		0);			// No other windows in our application

	// Ensure the window was created properly
	if (windowHandle == NULL)
	{
		DWORD error = GetLastError();
		return HRESULT_FROM_WIN32(error);
	}

	// The window exists but is not visible yet
	// We need to tell Windows to show it, and how to show it
	windowCreated = true;
	ShowWindow(windowHandle, SW_SHOW);

	// Return an "everything is ok" HRESULT value
	return S_OK;

}


// --------------------------------------------------------
// Updates the window's title bar with several stats once
// per second, including:
//  - The window's width & height
//  - The current FPS and ms/frame
//  - The graphics API in use
// --------------------------------------------------------
void Window::UpdateStats(float totalTime)
{
	// Track frame count
	fpsFrameCounter++;
	float elapsed = totalTime - fpsTimeElapsed;

	// Only update once per second
	if (!windowStats || elapsed < 1.0f)
		return;

	// How long did each frame take?  (Approx)
	float mspf = 1000.0f / (float)fpsFrameCounter;

	// Quick and dirty title bar text (mostly for debugging)
	std::wostringstream output;
	output.precision(6);
	output << windowTitle <<
		"    Width: " << windowWidth <<
		"    Height: " << windowHeight <<
		"    FPS: " << fpsFrameCounter <<
		"    Frame Time: " << mspf << "ms" <<
		"    Graphics: " << Graphics::APIName();

	// Actually update the title bar and reset fps data
	SetWindowText(windowHandle, output.str().c_str());
	fpsFrameCounter = 0;
	fpsTimeElapsed += elapsed;
}


// --------------------------------------------------------
// Sends an OS-level window close message to our process, which
// will be handled by our message processing function
// --------------------------------------------------------
void Window::Quit()
{
	PostMessage(windowHandle, WM_CLOSE, 0, 0);
}


// --------------------------------------------------------
// Allocates a console window we can print to for debugging
// 
// bufferLines   - Number of lines in the overall console buffer
// bufferColumns - Numbers of columns in the overall console buffer
// windowLines   - Number of lines visible at once in the window
// windowColumns - Number of columns visible at once in the window
// --------------------------------------------------------
void Window::CreateConsoleWindow(int bufferLines, int bufferColumns, int windowLines, int windowColumns)
{
	// Only allow this once
	if (consoleCreated)
		return;

	// Our temp console info struct
	CONSOLE_SCREEN_BUFFER_INFO coninfo;

	// Get the console info and set the number of lines
	AllocConsole();
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
	coninfo.dwSize.Y = bufferLines;
	coninfo.dwSize.X = bufferColumns;
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);

	SMALL_RECT rect = {};
	rect.Left = 0;
	rect.Top = 0;
	rect.Right = windowColumns;
	rect.Bottom = windowLines;
	SetConsoleWindowInfo(GetStdHandle(STD_OUTPUT_HANDLE), TRUE, &rect);

	FILE* stream;
	freopen_s(&stream, "CONIN$", "r", stdin);
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);

	// Prevent accidental console window close
	HWND consoleHandle = GetConsoleWindow();
	HMENU hmenu = GetSystemMenu(consoleHandle, FALSE);
	EnableMenuItem(hmenu, SC_CLOSE, MF_GRAYED);

	consoleCreated = true;
}


// --------------------------------------------------------
// Handles messages that are sent to our window by the
// operating system.  Ignoring these would cause our program
// to hang and the OS would think it was unresponsive.
// --------------------------------------------------------
LRESULT Window::ProcessMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// Check the incoming message and handle any we care about
	switch (uMsg)
	{
		// This is the message that signifies the window closing
	case WM_DESTROY:
		PostQuitMessage(0); // Send a quit message to our own program
		return 0;

		// Prevent beeping when we "alt-enter" into fullscreen
	case WM_MENUCHAR:
		return MAKELRESULT(0, MNC_CLOSE);

		// Prevent the overall window from becoming too small
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		return 0;

		// Sent when the window size changes
	case WM_SIZE:
		// Don't adjust anything when minimizing,
		// since we end up with a width/height of zero
		// and that doesn't play well with the GPU
		isMinimized = wParam == SIZE_MINIMIZED;
		if (isMinimized)
			return 0;
		
		// Save the new client area dimensions.
		windowWidth = LOWORD(lParam);
		windowHeight = HIWORD(lParam);

		// Let other systems know
		Graphics::ResizeBuffers(windowWidth, windowHeight);
		game->OnResize();

		return 0;

		// Has the mouse wheel been scrolled?
	case WM_MOUSEWHEEL:
		Input::SetWheelDelta(GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
		return 0;

		// Is our focus state changing?
	case WM_SETFOCUS:	hasFocus = true;	return 0;
	case WM_KILLFOCUS:	hasFocus = false;	return 0;
	case WM_ACTIVATE:	hasFocus = (LOWORD(wParam) != WA_INACTIVE); return 0;
	}

	// Let Windows handle any messages we're not touching
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
