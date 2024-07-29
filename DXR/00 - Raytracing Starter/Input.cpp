#include "Input.h"
#include <hidusage.h>

// --------------- Basic usage -----------------
// 
// All input-related functions are part of the
// "Input" namespace, and can be accessed like so:
// 
//   if (Input::KeyDown('W')) { }
//   if (Input::KeyDown('A')) { }
//   if (Input::KeyDown('S')) { }
//   if (Input::KeyDown('D')) { }
// 
// 
// The keyboard functions all take a single character
// like 'W', ' ' or '8' (which will implicitly cast 
// to an int) or a pre-defined virtual key code like
// VK_SHIFT, VK_ESCAPE or VK_TAB. These virtual key
// codes are are accessible through the Windows.h 
// file (already included in Input.h). See the 
// following for a complete list of virtual key codes:
// https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
// 
// Checking if various keys are down or up:
// 
//   if (Input::KeyDown('W')) { }
//   if (Input::KeyUp('2')) { }
//   if (Input::KeyDown(VK_SHIFT)) { }
//
// 
// Checking if a key was initially pressed or released 
// this frame:  
// 
//   if (Input::KeyPressed('Q')) { }
//   if (Input::KeyReleased(' ')) { }
// 
// (Note that these functions will only return true on 
// the FIRST frame that a key is pressed or released.)
// 
// 
// Checking for mouse button input is similar:
// 
//   if (Input::MouseLeftDown()) { }
//   if (Input::MouseRightDown()) { }
//   if (Input::MouseMiddleUp()) { }
//   if (Input::MouseLeftPressed()) { }
//   if (Input::MouseRightReleased()) { }
//
// 
// To handle relative mouse movement, you can use either
// "standard" or "raw" mouse input, as shown below:  
// 
//  - *Standard* input simply reads the cursor position on
//    the screen each frame and calculates the delta,
//    which respects pointer acceleration.  Use these
//    functions if you expect the same pointer behavior
//    as your mouse cursor in Windows.
// 
//       int xDelta = Input::GetMouseXDelta();
//       int yDelta = Input::GetMouseYDelta();
// 
//  - *Raw* input is read directly from the device, and is
//    a measure of how far the *mouse* moved, not the *cursor*.
//    Use these functions if you want high-precision movements
//    independent of the operating system or screen pixels.
// 
//       int xRawDelta = Input::GetRawMouseXDelta();
//       int yRawDelta = Input::GetRawMouseYDelta();
//                                 ^^^
//  
// ---------------------------------------------

namespace Input
{
	// Annonymous namespace to hold variables only accessible in this file
	namespace 
	{
		// Arrays for the current and previous key states
		unsigned char* kbState = 0;
		unsigned char* prevKbState = 0;

		// Mouse position and wheel data
		int mouseX = 0;
		int mouseY = 0;
		int prevMouseX = 0;
		int prevMouseY = 0;
		int mouseXDelta = 0;
		int mouseYDelta = 0;
		int rawMouseXDelta = 0;
		int rawMouseYDelta = 0;
		float wheelDelta = 0;

		// Support for capturing input outside the input manager
		bool keyboardCaptured = false;
		bool mouseCaptured = false;

		// The window's handle (id) from the OS, so
		// we can get the cursor's position
		HWND hWnd = 0;
	}
}


// ---------------------------------------------------
//  Initializes the input variables and sets up the
//  initial arrays of key states
//
//  windowHandle - the handle (id) of the window,
//                 which is necessary for mouse input
// ---------------------------------------------------
void Input::Initialize(HWND windowHandle)
{
	kbState = new unsigned char[256];
	prevKbState = new unsigned char[256];

	memset(kbState, 0, sizeof(unsigned char) * 256);
	memset(prevKbState, 0, sizeof(unsigned char) * 256);

	wheelDelta = 0.0f;
	mouseX = 0; mouseY = 0;
	prevMouseX = 0; prevMouseY = 0;
	mouseXDelta = 0; mouseYDelta = 0;
	keyboardCaptured = false; mouseCaptured = false;

	hWnd = windowHandle;

	// Register for raw input from the mouse
	RAWINPUTDEVICE mouse = {};
	mouse.usUsagePage = HID_USAGE_PAGE_GENERIC;
	mouse.usUsage = HID_USAGE_GENERIC_MOUSE;
	mouse.dwFlags = RIDEV_INPUTSINK;
	mouse.hwndTarget = windowHandle;
	RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
}

// ---------------------------------------------------
//  Shuts down the input system, freeing any
//  allocated memory
// ---------------------------------------------------
void Input::ShutDown()
{
	delete[] kbState;
	delete[] prevKbState;
}

// ----------------------------------------------------------
//  Updates the input manager for this frame.  This should
//  be called at the beginning of every Game::Update(), 
//  before anything that might need input
// ----------------------------------------------------------
void Input::Update()
{
	// Copy the old keys so we have last frame's data
	memcpy(prevKbState, kbState, sizeof(unsigned char) * 256);

	// Get the latest keys (from Windows)
	// Note the use of (void), which denotes to the compiler
	// that we're intentionally ignoring the return value
	(void)GetKeyboardState(kbState);

	// Get the current mouse position then make it relative to the window
	POINT mousePos = {};
	GetCursorPos(&mousePos);
	ScreenToClient(hWnd, &mousePos);

	// Save the previous mouse position, then the current mouse 
	// position and finally calculate the change from the previous frame
	prevMouseX = mouseX;
	prevMouseY = mouseY;
	mouseX = mousePos.x;
	mouseY = mousePos.y;
	mouseXDelta = mouseX - prevMouseX;
	mouseYDelta = mouseY - prevMouseY;
}

// ----------------------------------------------------------
//  Resets the mouse wheel value and raw mouse delta at the 
//  end of the frame. This cannot occur earlier in the frame, 
//  since these come from Win32 windowing messages, which are
//  handled between frames.
// ----------------------------------------------------------
void Input::EndOfFrame()
{
	// Reset wheel value
	wheelDelta = 0;
	rawMouseXDelta = 0;
	rawMouseYDelta = 0;
}

// ----------------------------------------------------------
//  Get the mouse's current position in pixels relative
//  to the top left corner of the window.
// ----------------------------------------------------------
int Input::GetMouseX() { return mouseX; }
int Input::GetMouseY() { return mouseY; }


// ---------------------------------------------------------------
//  Get the mouse's change (delta) in position since last
//  frame in pixels relative to the top left corner of the window.
// ---------------------------------------------------------------
int Input::GetMouseXDelta() { return mouseXDelta; }
int Input::GetMouseYDelta() { return mouseYDelta; }


// ---------------------------------------------------------------
//  Passes raw mouse input data to the input manager to be
//  processed.  This input is the lParam of the WM_INPUT
//  windows message, captured from (presumably) DXCore.
// 
//  See the following article for a discussion on different
//  types of mouse input, not including GetCursorPos():
//  https://learn.microsoft.com/en-us/windows/win32/dxtecharts/taking-advantage-of-high-dpi-mouse-movement
// ---------------------------------------------------------------
void Input::ProcessRawMouseInput(LPARAM lParam)
{
	// Variables for the raw data and its size
	unsigned char rawInputBytes[sizeof(RAWINPUT)] = {};
	unsigned int sizeOfData = sizeof(RAWINPUT);

	// Get raw input data from the lowest possible level and verify
	if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, rawInputBytes, &sizeOfData, sizeof(RAWINPUTHEADER)) == -1)
		return;

	// Got data, so cast to the proper type and check the results
	RAWINPUT* raw = (RAWINPUT*)rawInputBytes;
	if (raw->header.dwType == RIM_TYPEMOUSE)
	{
		// This is mouse data, so grab the movement values
		rawMouseXDelta = raw->data.mouse.lLastX;
		rawMouseYDelta = raw->data.mouse.lLastY;
	}
}

// ---------------------------------------------------------------
//  Get the mouse's change (delta) in position since last
//  frame based on raw mouse data (no pointer acceleration)
// ---------------------------------------------------------------
int Input::GetRawMouseXDelta() { return rawMouseXDelta; }
int Input::GetRawMouseYDelta() { return rawMouseYDelta; }


// ---------------------------------------------------------------
//  Get the mouse wheel delta for this frame.  Note that there is 
//  no absolute position for the mouse wheel; this is either a
//  positive number, a negative number or zero.
// ---------------------------------------------------------------
float Input::GetMouseWheel() { return wheelDelta; }


// ---------------------------------------------------------------
//  Sets the mouse wheel delta for this frame.  This is called
//  by DXCore whenever an OS-level mouse wheel message is sent
//  to the application.  You'll never need to call this yourself.
// ---------------------------------------------------------------
void Input::SetWheelDelta(float delta)
{
	wheelDelta = delta;
}


// ---------------------------------------------------------------
//  Sets whether or not keyboard input is "captured" elsewhere.
//  If the keyboard is "captured", the input manager will report 
//  false on all keyboard input.
// ---------------------------------------------------------------
void Input::SetKeyboardCapture(bool captured)
{
	keyboardCaptured = captured;
}


// ---------------------------------------------------------------
//  Sets whether or not mouse input is "captured" elsewhere.
//  If the mouse is "captured", the input manager will report 
//  false on all mouse input.
// ---------------------------------------------------------------
void Input::SetMouseCapture(bool captured)
{
	mouseCaptured = captured;
}


// ----------------------------------------------------------
//  Is the given key down this frame?
//  
//  key - The key to check, which could be a single character
//        like 'W' or '3', or a virtual key code like VK_TAB,
//        VK_ESCAPE or VK_SHIFT.
// ----------------------------------------------------------
bool Input::KeyDown(int key)
{
	if (key < 0 || key > 255) return false;

	return (kbState[key] & 0x80) != 0 && !keyboardCaptured;
}

// ----------------------------------------------------------
//  Is the given key up this frame?
//  
//  key - The key to check, which could be a single character
//        like 'W' or '3', or a virtual key code like VK_TAB,
//        VK_ESCAPE or VK_SHIFT.
// ----------------------------------------------------------
bool Input::KeyUp(int key)
{
	if (key < 0 || key > 255) return false;

	return !(kbState[key] & 0x80) && !keyboardCaptured;
}

// ----------------------------------------------------------
//  Was the given key initially pressed this frame?
//  
//  key - The key to check, which could be a single character
//        like 'W' or '3', or a virtual key code like VK_TAB,
//        VK_ESCAPE or VK_SHIFT.
// ----------------------------------------------------------
bool Input::KeyPress(int key)
{
	if (key < 0 || key > 255) return false;

	return
		kbState[key] & 0x80 &&			// Down now
		!(prevKbState[key] & 0x80) &&	// Up last frame
		!keyboardCaptured;
}

// ----------------------------------------------------------
//  Was the given key initially released this frame?
//  
//  key - The key to check, which could be a single character
//        like 'W' or '3', or a virtual key code like VK_TAB,
//        VK_ESCAPE or VK_SHIFT.
// ----------------------------------------------------------
bool Input::KeyRelease(int key)
{
	if (key < 0 || key > 255) return false;

	return
		!(kbState[key] & 0x80) &&	// Up now
		prevKbState[key] & 0x80 &&	// Down last frame
		!keyboardCaptured;
}


// ----------------------------------------------------------
//  A utility function to fill a given array of booleans 
//  with the current state of the keyboard.  This is most
//  useful when hooking the engine's input up to another
//  system, such as a user interface library.  (You probably 
//  won't use this very much, if at all!)
// 
//  keyArray - pointer to a boolean array which will be
//             filled with the current keyboard state
//  size - the size of the boolean array (up to 256)
// 
//  Returns true if the size parameter was valid and false
//  if it was <= 0 or > 256
// ----------------------------------------------------------
bool Input::GetKeyArray(bool* keyArray, int size)
{
	if (size <= 0 || size > 256) return false;

	// Loop through the given size and fill the
	// boolean array.  Note that the double exclamation
	// point is on purpose; it's a quick way to
	// convert any number to a boolean.
	for (int i = 0; i < size; i++)
		keyArray[i] = !!(kbState[i] & 0x80);

	return true;
}


// ----------------------------------------------------------
//  Is the specific mouse button down this frame?
// ----------------------------------------------------------
bool Input::MouseLeftDown() { return (kbState[VK_LBUTTON] & 0x80) != 0 && !mouseCaptured; }
bool Input::MouseRightDown() { return (kbState[VK_RBUTTON] & 0x80) != 0 && !mouseCaptured; }
bool Input::MouseMiddleDown() { return (kbState[VK_MBUTTON] & 0x80) != 0 && !mouseCaptured; }


// ----------------------------------------------------------
//  Is the specific mouse button up this frame?
// ----------------------------------------------------------
bool Input::MouseLeftUp() { return !(kbState[VK_LBUTTON] & 0x80) && !mouseCaptured; }
bool Input::MouseRightUp() { return !(kbState[VK_RBUTTON] & 0x80) && !mouseCaptured; }
bool Input::MouseMiddleUp() { return !(kbState[VK_MBUTTON] & 0x80) && !mouseCaptured; }


// ----------------------------------------------------------
//  Was the specific mouse button initially 
// pressed or released this frame?
// ----------------------------------------------------------
bool Input::MouseLeftPress() { return kbState[VK_LBUTTON] & 0x80 && !(prevKbState[VK_LBUTTON] & 0x80) && !mouseCaptured; }
bool Input::MouseLeftRelease() { return !(kbState[VK_LBUTTON] & 0x80) && prevKbState[VK_LBUTTON] & 0x80 && !mouseCaptured; }

bool Input::MouseRightPress() { return kbState[VK_RBUTTON] & 0x80 && !(prevKbState[VK_RBUTTON] & 0x80) && !mouseCaptured; }
bool Input::MouseRightRelease() { return !(kbState[VK_RBUTTON] & 0x80) && prevKbState[VK_RBUTTON] & 0x80 && !mouseCaptured; }

bool Input::MouseMiddlePress() { return kbState[VK_MBUTTON] & 0x80 && !(prevKbState[VK_MBUTTON] & 0x80) && !mouseCaptured; }
bool Input::MouseMiddleRelease() { return !(kbState[VK_MBUTTON] & 0x80) && prevKbState[VK_MBUTTON] & 0x80 && !mouseCaptured; }
