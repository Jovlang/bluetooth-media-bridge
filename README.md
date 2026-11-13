# Bluetooth Media Bridge

An experimental Windows bridge that exposes VLC and MPC-HC through Windows System Media Transport Controls (SMTC). Bluetooth headset Play and Pause commands are forwarded to the selected player as play/pause toggles.

## Status

The bridge builds and has been verified to register a Windows media session. **The original headset two-tap problem has not yet been confirmed fixed.**

The motivating issue was that four headset taps produced only two legacy media-key events, while YouTube responded to single taps. Keyboard remapping could not recover those missing events. This implementation receives SMTC callbacks instead of intercepting keyboard input.

## Requirements

- Windows 10 or Windows 11, x64.
- VLC (`vlc.exe`) or MPC-HC (`mpc-hc.exe` / `mpc-hc64.exe`).
- Visual Studio 2022 Build Tools with C++ desktop tools and a Windows SDK including C++/WinRT, to build from source.

## Build

Open **x64 Native Tools Command Prompt for VS 2022**, change to this repository, and run:

```bat
build.cmd
```

The executable is written to `bin\media-bridge.exe`. No third-party runtime is required; the Microsoft C++ runtime is linked statically.

## Run

Open VLC or MPC-HC, then double-click `Start.cmd`. The bridge runs in the background and appears in Windows media controls as **VLC / MPC-HC headset controls**. A notification-area icon shows whether the bridge is connected to a player, waiting for a player, or disabled. Windows may place it in the tray overflow menu.

Click or right-click the tray icon to enable/disable forwarding, open the diagnostic log, or exit. Disabling leaves the tray icon running so you can enable it again. **Exit** or `Stop.cmd` stops the process and removes the icon and media session. The icon is restored automatically if Windows Explorer restarts.

```bat
bin\media-bridge.exe --inspect
bin\media-bridge.exe --stop
```

`--inspect` checks whether Windows can see the bridge's media session. The bridge writes received media-button events and command-delivery results to `bin\media-bridge.log`.

No startup entry, player setting, or driver is installed. Start the bridge again after signing in or restarting Windows. Run it at the same privilege level as the player.

## Behavior and limitations

- The foreground supported player is preferred, followed by the last foreground supported player, then the sole open supported player window. With several players open, focus the intended one first.
- Both Play and Pause callbacks toggle playback. Next and Previous callbacks forward the corresponding media commands.
- MPC-HC receives its direct play/pause command (889). VLC receives `WM_APPCOMMAND` Play/Pause (14).
- Another app starting playback may become Windows' selected media session.
- The bridge's reported playback state follows commands it forwards. Mouse and keyboard changes inside the player are **not** synchronized back to the bridge, and successful message delivery does not prove playback changed.
- No audio playback, keyboard recording, global keyboard hook, or DLL injection is used.

## Implementation

A hidden desktop window owns an SMTC instance obtained through `ISystemMediaTransportControlsInterop`. Button callbacks are posted to its message loop and forwarded to a selected player window. A timer refreshes the target window every 500 ms.

References: [Microsoft SMTC documentation](https://learn.microsoft.com/en-us/windows/apps/develop/media-playback/system-media-transport-controls), [Bluetooth playback-state guidance](https://learn.microsoft.com/en-us/windows-hardware/design/accessory-guidelines/bluetooth-accessory-guidelines/bluetooth-accessory-guidelines-classic-audio#avrcp--play-and-pause).
