/**
 * LVGL configuration for the first supported display class: a 16-bit RGB565
 * Linux framebuffer on the PiScreen-compatible 3.5 inch SPI panel.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/*
 * This is a Linux userspace application, not an MCU target. The built-in
 * LVGL allocator defaults to 64 KiB, which is too small for the partial
 * framebuffer plus UI objects, so use the process allocator instead.
 */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

/* The three shipped skins use this small built-in Montserrat set. */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* The panel's SPI bandwidth makes partial rendering mandatory. */
#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT 0
#define LV_LINUX_FBDEV_BUFFER_SIZE 60
#define LV_LINUX_FBDEV_MMAP 1

#endif
