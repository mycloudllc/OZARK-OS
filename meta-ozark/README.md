# meta-ozark

Ozark OS distro, images, branding and applications.

- **Depends on:** openembedded-core (`core`), meta-oe (`openembedded-layer`), meta-qt5 (`qt5-layer`)
- **Compatible with:** Yocto Project 5.0 "Scarthgap"
- **Distro:** `DISTRO = "ozark"`
- **Images:** `ozark-image-base` (splash + console), `ozark-image` (splash + Qt UI)

The layer is board-agnostic. Anything tied to one BSP lives in
`dynamic-layers/<bsp-collection>/` and is only parsed when that BSP layer is in
`bblayers.conf` (see `BBFILES_DYNAMIC` in `conf/layer.conf`). Moving to another
board means adding its BSP layer and, if needed, a matching `dynamic-layers/`
folder — the recipes in `recipes-*` stay as they are.

| Path | Purpose |
|---|---|
| `conf/distro/ozark.conf` | systemd, no X11/Wayland, Qt 5 on EGLFS |
| `recipes-core/images/` | Image recipes |
| `recipes-core/psplash/` | Ozark splash image and colours |
| `recipes-graphics/fonts/` | Barlow Condensed brand font |
| `recipes-graphics/ozark-display/` | `/etc/default/ozark-display` env + EGLFS KMS config for UI services |
| `recipes-qt/hello-ozark/` | Hello-world Qt app and its systemd unit |
| `dynamic-layers/raspberrypi/` | Quiet `cmdline.txt`, stable DRM card name, splash override for Pi machines |
