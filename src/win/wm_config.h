#pragma once

#define WM_SCREEN_WIDTH 640
#define WM_SCREEN_HEIGHT 480

#define WM_TITLEBAR_HEIGHT 18
#define WM_FRAME_WIDTH 2

#define WM_ICON_SIZE 32
#define WM_ICON_MARGIN 10
#define WM_ICON_LABEL_HEIGHT 24  // Height for text label below icon (2 lines * 11px + spacing)
#define WM_ICON_LABEL_MAX_WIDTH 48
#define WM_ICON_TOTAL_WIDTH 48  // Use label width as total width for consistent centering
#define WM_ICON_TOTAL_HEIGHT (WM_ICON_SIZE + WM_ICON_LABEL_HEIGHT + 2)  // Icon + label + spacing
#define WM_ICON_CENTER_OFFSET ((WM_ICON_TOTAL_WIDTH - WM_ICON_SIZE) / 2)  // Offset to center icon over label

// Icon slot grid spacing (must be >= TOTAL dimensions to prevent overlap)
#define WM_ICON_SLOT_WIDTH  (WM_ICON_TOTAL_WIDTH + 8)   // 56px horizontal spacing
#define WM_ICON_SLOT_HEIGHT (WM_ICON_TOTAL_HEIGHT + 8)  // 66px vertical spacing

#define WM_DOUBLECLICK_TICKS 30  /* 30 ticks at 100Hz = 300ms */

#define WM_MAX_WINDOWS 16
#define WM_MAX_CONTROLS_PER_FORM 64

#define WM_BG_MARGIN 10

