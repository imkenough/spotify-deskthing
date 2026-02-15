# Spotify Desk Thing

A DIY Spotify controller using an ESP32 and an ILI9341 TFT display. This project allows you to view current track info, album art, and control playback (play/pause, next, previous, volume) directly from your desk.

For more detailed information and setup guides, visit the [Wiki](https://github.com/imkenough/spotify-deskthing/wiki).

## Features

- Real-time Spotify playback status.
- Album art display with dynamic color extraction for the UI theme.
- Playback controls: Play/Pause, Next, Previous.
- Volume control.
- Web-based setup for Spotify authentication.

## Hardware Requirements

- ESP32 Development Board.
- ILI9341 TFT Display (320x240).
- Push buttons for controls.

## Setup Instructions

1.  **Clone the repository.**
2.  **Configure `config.h`**:
    - Rename `config.h.example` to `config.h`.
    - Enter your WiFi SSID and Password.
    - Create a Spotify Application on the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard/).
    - Set the Redirect URI to `http://127.0.0.1:8888/callback`.
    - Copy the `Client ID` and `Client Secret` into `config.h`.
3.  **Install Libraries**:
    - `TFT_eSPI`
    - `TJpg_Decoder`
    - `ArduinoJson`
4.  **Configure TFT_eSPI**:
    - Ensure your `User_Setup.h` in the `TFT_eSPI` library folder matches the pins defined in `User_Setup.h.example` (or copy `User_Setup.h.example` to your library configuration).
5.  **Upload the code** to your ESP32.
6.  **Authentication**:
    - On first boot, the screen will show an IP address.
    - Visit that IP in your browser.
    - Follow the link to log in to Spotify.
    - After being redirected to a broken page (127.0.0.1), copy the entire URL and paste it back into the setup page on the ESP32.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
