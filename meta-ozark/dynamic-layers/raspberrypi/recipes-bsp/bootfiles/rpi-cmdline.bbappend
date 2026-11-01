# Quiet boot so the screen goes black -> Ozark splash -> UI, with no
# kernel text, Tux logos or blinking cursor in between.
# Kernel messages still go to the serial console on dev images.
CMDLINE:append = " quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0"
