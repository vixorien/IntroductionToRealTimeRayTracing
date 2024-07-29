#pragma once

#include <Windows.h>

// See Input.cpp for usage details

namespace Input
{
	void Initialize(HWND windowHandle);
	void ShutDown();
	void Update();
	void EndOfFrame();

	int GetMouseX();
	int GetMouseY();
	int GetMouseXDelta();
	int GetMouseYDelta();

	void ProcessRawMouseInput(LPARAM input);
	int GetRawMouseXDelta();
	int GetRawMouseYDelta();

	float GetMouseWheel();
	void SetWheelDelta(float delta);

	void SetKeyboardCapture(bool captured);
	void SetMouseCapture(bool captured);

	bool KeyDown(int key);
	bool KeyUp(int key);

	bool KeyPress(int key);
	bool KeyRelease(int key);

	bool GetKeyArray(bool* keyArray, int size = 256);

	bool MouseLeftDown();
	bool MouseRightDown();
	bool MouseMiddleDown();

	bool MouseLeftUp();
	bool MouseRightUp();
	bool MouseMiddleUp();

	bool MouseLeftPress();
	bool MouseLeftRelease();

	bool MouseRightPress();
	bool MouseRightRelease();

	bool MouseMiddlePress();
	bool MouseMiddleRelease();
}