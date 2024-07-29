
#include <Windows.h>
#include <codecvt>
#include <locale>

#include "PathHelpers.h"

// --------------------------------------------------------------------------
// Gets the actual path to this executable
//
// - As it turns out, the relative path for a program is different when 
//    running through VS and when running the .exe directly, which makes 
//    it a pain to properly load external files (like textures & shaders)
//    - Running through VS: Current Dir is the *project folder*
//    - Running from .exe:  Current Dir is the .exe's folder
// - This has nothing to do with DEBUG and RELEASE modes - it's purely a 
//    Visual Studio "thing", and isn't obvious unless you know to look 
//    for it.  In fact, it could be fixed by changing a setting in VS, but
//    that option is stored in a user file (.suo), which is ignored by most
//    version control packages by default.  Meaning: the option must be
//    changed on every PC.  Ugh.  So instead, here's a helper.
// --------------------------------------------------------------------------
std::string GetExePath()
{
	// Assume the path is just the "current directory" for now
	std::string path = ".\\";

	// Get the real, full path to this executable
	char currentDir[1024] = {};
	GetModuleFileNameA(0, currentDir, 1024);

	// Find the location of the last slash charaacter
	char* lastSlash = strrchr(currentDir, '\\');
	if (lastSlash)
	{
		// End the string at the last slash character, essentially
		// chopping off the exe's file name.  Remember, c-strings
		// are null-terminated, so putting a "zero" character in 
		// there simply denotes the end of the string.
		*lastSlash = 0;

		// Set the remainder as the path
		path = currentDir;
	}

	// Toss back whatever we've found
	return path;
}


// ----------------------------------------------------
//  Fixes a relative path so that it is consistently
//  evaluated from the executable's actual directory
//  instead of the app's current working directory.
// 
//  See the comments of GetExePath() for more details.
// ----------------------------------------------------
std::string FixPath(const std::string& relativeFilePath)
{
	return GetExePath() + "\\" + relativeFilePath;
}


// ----------------------------------------------------
//  Fixes a relative path so that it is consistently
//  evaluated from the executable's actual directory
//  instead of the app's current working directory.
// 
//  See the comments of GetExePath() for more details.
// 
//  Note that this overload uses wide character strings
//  (wstring) instead of standard strings, as most windows
//  API calls require wide character strings.
// ---------------------------------------------------- 
std::wstring FixPath(const std::wstring& relativeFilePath)
{
	return NarrowToWide(GetExePath()) + L"\\" + relativeFilePath;
}


// ----------------------------------------------------
//  Helper function for converting a wide character 
//  string to a standard ("narrow") character string
// ----------------------------------------------------
std::string WideToNarrow(const std::wstring& str)
{
	int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.length(), 0, 0, 0, 0);
	std::string result(size, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &result[0], size, 0, 0);
	return result;
}


// ----------------------------------------------------
//  Helper function for converting a standard ("narrow") 
//  string to a wide character string
// ----------------------------------------------------
std::wstring NarrowToWide(const std::string& str)
{
	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), 0, 0);
	std::wstring result(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
	return result;
}