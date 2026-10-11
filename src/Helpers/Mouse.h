#pragma once
#ifndef MOUSE_H
#define MOUSE_H
#define ZwClose(handle) NtClose(handle)

typedef int BOOL;

#ifdef __cplusplus
extern "C" {
#endif

BOOL mouse_open(void);
void mouse_close(void);
void mouse_move(char button, char x, char y, char wheel);
void mouse_down(void);
void mouse_up(void);
void mouse_right_down(void);
void mouse_right_up(void);

#ifdef __cplusplus
}
#endif

#endif