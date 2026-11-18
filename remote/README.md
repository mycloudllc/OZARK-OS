# Ozark Remote

Control the radio from a phone: media, radio, volume, the lights, vehicle data, the network
and updates.

| | |
|---|---|
| [daemon/](daemon/) | `ozark-remoted`, the radio's side (C++/Qt, recipe `ozark-remoted`) |
| [app/](app/) | **Ozark Remote**, the phone app (Flutter: Android and iOS) |
| [PROTOCOL.md](PROTOCOL.md) | What they say to each other |

```
Phone app ──Wi-Fi: WebSocket :54546──┐
          └─Bluetooth LE: GATT───────┴─ ozark-remoted ─┬─ Dash (org.ozark.Dash1): media, radio, volume, theme
                                                       ├─ ozark-lightd (org.ozark.Lighting1)
                                                       ├─ ozark-vehicled (org.ozark.Vehicle1)
                                                       └─ ozark-net, ozark-update, systemctl
```

## Pairing

The first time, the app asks the radio for a code; the radio shows six digits on its screen
for two minutes, and the app sends them back. The app then gets a token, kept in the phone's
Keychain/Keystore, and signs in with it from then on. Only a hash of the token is stored on
the radio, in `/etc/ozark/remote/clients.json` (the data partition, so it survives updates).
Phones are listed, and removed, in **Settings > Bluetooth > Phone app** on the radio.

Wi-Fi is only accepted from local addresses (the hotspot, a wired LAN), never from LTE.
Bluetooth LE is not link-encrypted in version 1: the token is what keeps others out.

## The radio side

`ozark-remoted.service`. Its log says how each transport is doing:

```
[wifi] listening on port 54546
[ble] advertising on hci0
[session] Connected: wifi 10.42.0.17
[pair] Code on screen for Pixel 8 ( wifi 10.42.0.17 )
[pair] Paired Pixel 8 as sJyNGaLr
```

`busctl call org.ozark.Remote1 /org/ozark/Remote1 org.ozark.Remote1 GetStatus` shows the
transports and who is connected.

## The app

Build (no local Flutter needed):

```bash
docker run --rm -v $PWD/app:/app -w /app ghcr.io/cirruslabs/flutter:3.27.1 \
    sh -c 'flutter pub get && flutter build apk --release'
# -> app/build/app/outputs/flutter-apk/app-release.apk
```

The release APK is signed with Flutter's debug key: fine for installing it yourself
(allow "install unknown apps"), not for the Play Store, which needs your own upload key
(`android/key.properties`). The iPhone build needs a Mac with Xcode: `flutter build ios`,
then sign and install from Xcode with your Apple developer account.

Icon: the Ozark Mountain Technologies badge (`branding/ozark-badge.svg`), in
`assets/icon/` as SVG (square for iOS, adaptive foreground and Android 13 themed layer) and
1024 px PNG (`rsvg-convert -w 1024 -h 1024`); `dart run flutter_launcher_icons` makes every
size. Colours follow the phone's accent (Material You, Android 12+); elsewhere the Ozark
orange.

Tests: `flutter test` (protocol framing and client). `flutter test --tags screens
--run-skipped --update-goldens test/screens_test.dart` renders the screens into
`test/screens/`. `dart run tool/smoke.dart <radio> <token>` talks to a real radio with the
app's protocol code.
