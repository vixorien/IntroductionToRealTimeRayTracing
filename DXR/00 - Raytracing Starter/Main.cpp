
#include <Windows.h>
#include <crtdbg.h>

#include "Window.h"
#include "Graphics.h"
#include "Game.h"
#include "Input.h"

// --------------------------------------------------------
// Entry point for a graphical (non-console) Windows application
// --------------------------------------------------------
int WINAPI WinMain(
	_In_ HINSTANCE hInstance,			// The handle to this app's instance
	_In_opt_ HINSTANCE hPrevInstance,	// A handle to the previous instance of the app (always NULL)
	_In_ LPSTR lpCmdLine,				// Command line params
	_In_ int nCmdShow)					// How the window should be shown (we ignore this)
{
#if defined(DEBUG) | defined(_DEBUG)
	// Enable memory leak detection as a quick and dirty
	// way of determining if we forgot to clean something up
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	// Do we also want a console window?  Probably only in debug mode
	Window::CreateConsoleWindow(500, 120, 32, 120);
	printf("Console window created successfully.\n");
#endif

	// Set up app initialization details
	unsigned int windowWidth = 1280;
	unsigned int windowHeight = 720;
	const wchar_t* windowTitle = L"DXR Basic Implementation";
	bool statsInTitleBar = true;
	bool vsync = false;

	// The main application object
	Game game;

	// Create the window and verify
	HRESULT windowResult = Window::Create(
		hInstance, 
		windowWidth, 
		windowHeight, 
		windowTitle, 
		statsInTitleBar, 
		&game);
	if (FAILED(windowResult))
		return windowResult;

	// Initialize the graphics API and verify
	HRESULT graphicsResult = Graphics::Initialize(
		Window::Width(), 
		Window::Height(), 
		Window::Handle(),
		vsync);
	if (FAILED(graphicsResult))
		return graphicsResult;

	// Initalize the input system, which requires the window handle
	Input::Initialize(Window::Handle());

	// Now the game itself can be initialzied
	game.Initialize();

	// Time tracking
	LARGE_INTEGER perfFreq{};
	double perfSeconds = 0;
	__int64 startTime = 0;
	__int64 currentTime = 0;
	__int64 previousTime = 0;

	// Query for accurate timing information
	QueryPerformanceFrequency(&perfFreq);
	perfSeconds = 1.0 / (double)perfFreq.QuadPart;

	// Performance Counter gives high-resolution time stamps
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);
	currentTime = startTime;
	previousTime = startTime;

	// Windows message loop (and our game loop)
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		// Determine if there is a message from the operating system
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// Translate and dispatch the message
			// to our custom WindowProc function
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			// Calculate up-to-date timing info
			QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);
			float deltaTime = max((float)((currentTime - previousTime) * perfSeconds), 0.0f);
			float totalTime = (float)((currentTime - startTime) * perfSeconds);
			previousTime = currentTime;

			// Calculate basic fps
			Window::UpdateStats(totalTime);

			// Input updating
			Input::Update();

			// Update and draw
			game.Update(deltaTime, totalTime);
			game.Draw(deltaTime, totalTime);

			// Notify Input system about end of frame
			Input::EndOfFrame();

#if defined(DEBUG) || defined(_DEBUG)
			// Print any graphics debug messages that occurred this frame
			Graphics::PrintDebugMessages();
#endif
		}
	}

	// Clean up
	game.ShutDown();
	Input::ShutDown();
	return (HRESULT)msg.wParam;
}
