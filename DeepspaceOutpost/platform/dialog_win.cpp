/*
 * DeepspaceOutpost - DirectX 11 / XAudio2 port of Elite: The New Kind.
 *
 * dialog_win.cpp  (M4)
 *
 * gfx_request_file: the commander save/load file picker, on the Win32 common
 * dialog (replaces Allegro's file_select). The title text distinguishes a save
 * ("Save ...") from a load, choosing GetSaveFileName vs GetOpenFileName.
 */

#include <windows.h>
#include <commdlg.h>

#include <cstring>
#include <cwchar>

#include "gfx.h"
#include "platform_win.h"

int gfx_request_file(const char* title, char* path, const char* ext)
{
	wchar_t wpath[MAX_PATH] = {};
	if (path)
		MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH);

	wchar_t wtitle[128] = {};
	if (title)
		MultiByteToWideChar(CP_ACP, 0, title, -1, wtitle, 128);

	/* Build a "Commander (*.ext)\0*.ext\0All Files\0*.*\0\0" filter. */
	wchar_t we[16] = L"nkc";
	if (ext)
		MultiByteToWideChar(CP_ACP, 0, ext, -1, we, 16);
	wchar_t filter[64];
	int n = 0;
	n += swprintf(filter + n, 32, L"Commander (*.%ls)", we) + 1;
	n += swprintf(filter + n, 16, L"*.%ls", we) + 1;
	wcscpy(filter + n, L"All Files"); n += 10;
	wcscpy(filter + n, L"*.*");       n += 4;
	filter[n] = L'\0';

	OPENFILENAMEW ofn{};
	ofn.lStructSize  = sizeof(ofn);
	ofn.hwndOwner    = platform_window();
	ofn.lpstrFile    = wpath;
	ofn.nMaxFile     = MAX_PATH;
	ofn.lpstrFilter  = filter;
	ofn.lpstrDefExt  = we;
	ofn.lpstrTitle   = wtitle[0] ? wtitle : nullptr;

	bool isSave = title && (wcsstr(wtitle, L"Save") != nullptr);

	BOOL ok;
	if (isSave)
	{
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
		ok = GetSaveFileNameW(&ofn);
	}
	else
	{
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
		ok = GetOpenFileNameW(&ofn);
	}

	if (!ok)
		return 0;

	if (path)
		WideCharToMultiByte(CP_ACP, 0, wpath, -1, path, 255, nullptr, nullptr);
	return 1;
}
