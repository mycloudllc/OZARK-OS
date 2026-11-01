# Don't load the Ilitek ILI210x/ILI251x touch driver.
#
# In this kernel, the vc4-kms-dsi-waveshare-panel overlay always declares two
# touch controllers on the panel's I2C bus, a Goodix at 0x14 and an Ilitek at
# 0x41, with no parameter to turn the Ilitek one off. On panels that only have
# the Goodix chip, ili210x fails to read 0x41 and retries ~200 times a second
# for as long as the Pi is on, flooding the kernel log (tens of MB per boot).
# Goodix touch is unaffected. Remove this if a panel with Ilitek touch is used.
KERNEL_MODULE_PROBECONF += "ili210x"
module_conf_ili210x = "blacklist ili210x"
