# Luckfox 3.5-RPi-LCD-CTP panel firmware

`st7796s.bin` is the MIPI-DBI command sequence supplied by Luckfox for the
3.5-RPi-LCD-CTP's ST7796S display controller. It is installed only by the
`misc-tools --board=micropanel-touch --variant=luckfox-ctp` image hook as
`/lib/firmware/st7796s.bin`. The matching variant also writes
`/etc/modules-load.d/micropanel-touch-luckfox-ctp.conf` to load the in-tree
`panel_mipi_dbi` driver. This is required on modular kernels: `st7796s` must
remain the first compatible string because the driver derives this firmware
filename from it, but that string alone does not trigger the driver's module
alias.

Source: https://files.luckfox.com/wiki/Luckfox/Display/3inch5-RPi-LCD-CTP/St7796s.zip

Archive SHA-256: `50a7b5db7b6583a809a2f93b4017cc006690e0711196b5afcc701cd5e13a690f`

`st7796s.bin` SHA-256: `17204e39cce35fba857ad2dff14243e1d3a958c4dac00283f8df9b7ad5147cc7`
