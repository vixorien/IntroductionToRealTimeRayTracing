#pragma once

#include <string>
#include <d3d11.h>

// Helpers for determining the actual path to the executable
std::string GetExePath();
std::string FixPath(const std::string& relativeFilePath);
std::wstring FixPath(const std::wstring& relativeFilePath);
std::string WideToNarrow(const std::wstring& str);
std::wstring NarrowToWide(const std::string& str);