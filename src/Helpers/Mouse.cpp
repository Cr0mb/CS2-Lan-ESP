#include "Mouse.h"
#include <windows.h>

typedef UINT (WINAPI *pNtUserSendInput)(UINT, LPINPUT, int);
static pNtUserSendInput fnNtUserSendInput = NULL;
static BOOL g_initialized = FALSE;

BOOL mouse_open(void)
{
	if (!g_initialized)
	{
		HMODULE hWin32u = GetModuleHandleA("win32u.dll");
		if (!hWin32u)
			hWin32u = LoadLibraryA("win32u.dll");
		if (hWin32u)
			fnNtUserSendInput = (pNtUserSendInput)GetProcAddress(hWin32u, "NtUserSendInput");
		g_initialized = TRUE;
	}
	return TRUE;
}

void mouse_close(void)
{
}

static void send_input_internal(INPUT* input, int count)
{
	if (fnNtUserSendInput)
		fnNtUserSendInput((UINT)count, input, sizeof(INPUT));
	else
		SendInput((UINT)count, input, sizeof(INPUT));
}

void mouse_move(char button, char x, char y, char wheel)
{
	if (!g_initialized)
		mouse_open();

	INPUT input = {0};
	input.type = INPUT_MOUSE;

	if (x != 0 || y != 0) {
		input.mi.dx = x;
		input.mi.dy = y;
		input.mi.dwFlags |= MOUSEEVENTF_MOVE;
	}

	if (wheel != 0) {
		input.mi.mouseData = wheel;
		input.mi.dwFlags |= MOUSEEVENTF_WHEEL;
	}

	if (input.mi.dwFlags != 0) {
		send_input_internal(&input, 1);
	}
}

void mouse_down(void)
{
	if (!g_initialized)
		mouse_open();

	INPUT input = {0};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
	send_input_internal(&input, 1);
}

void mouse_up(void)
{
	if (!g_initialized)
		mouse_open();

	INPUT input = {0};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
	send_input_internal(&input, 1);
}

void mouse_right_down(void)
{
	if (!g_initialized)
		mouse_open();

	INPUT input = {0};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
	send_input_internal(&input, 1);
}

void mouse_right_up(void)
{
	if (!g_initialized)
		mouse_open();

	INPUT input = {0};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
	send_input_internal(&input, 1);
}
