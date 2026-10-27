#ifndef SUBTITLE_UI_LAYOUT_H
#define SUBTITLE_UI_LAYOUT_H
#include "ui_model.h"   /* Z7_HISTORY_UI selects the picker rows */
/* Shared by the language picker renderer and its touch hit regions. */
#define UI_LANGUAGE_X 156
#define UI_LANGUAGE_Y 12
#define UI_LANGUAGE_WIDTH 312
#define UI_LANGUAGE_HEIGHT 42
#define UI_LANGUAGE_MENU_Y 68
#define UI_LANGUAGE_ROW_HEIGHT 52
#ifdef Z7_HISTORY_UI
#define UI_LANGUAGE_ROWS 3u   /* EN->ZH, ZH->EN, face-to-face conversation */
#else
#define UI_LANGUAGE_ROWS 2u
#endif
/* History geometry is shared by rendering and touch, independent of subtitle size. */
#define UI_HISTORY_BODY_Y 140
#define UI_HISTORY_BODY_HEIGHT 348
#define UI_HISTORY_LEFT_X 76
#define UI_HISTORY_RIGHT_X 548
#define UI_HISTORY_TEXT_WIDTH 408
#define UI_HISTORY_BACK_X 24
#define UI_HISTORY_BACK_WIDTH 152
#define UI_HISTORY_CLEAR_X 788
#define UI_HISTORY_CLEAR_WIDTH 212
#define UI_HISTORY_OLDER_X 546
#define UI_HISTORY_NEWER_X 658
#define UI_HISTORY_PAGE_WIDTH 96
#define UI_HISTORY_CONFIRM_Y 322
#define UI_HISTORY_CANCEL_X 332
#define UI_HISTORY_CONFIRM_X 524
#define UI_HISTORY_CONFIRM_WIDTH 168
#endif
