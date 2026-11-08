**English** / [日本語](./README.ja.md)

# MikuMikuWorld
A chart editor and viewer for the mobile rhythm game Project Sekai Colorful Stage feat. Hatsune Miku.

## Features:
- Import and export Sliding Universal Score (\*.sus) files.
- BPM, time signature, and hi-speed adjustment.
- Custom timeline divisions up to 1920.
- Create and use custom note presets.
- Customizable keyboard shortcuts.

## Added Features:
- **MV support**: load a music video (MP4, MKV, WebM, MOV, AVI and other common formats) with its own offset. The MV plays without audio and replaces the background in both the timeline and the preview, following the playback position while scrubbing and playing. Outside the length of the video, the normal background is shown. Videos are decoded with the bundled FFmpeg libraries.
- **Subtitles**: load a subtitle file (\*.srt, \*.vtt, \*.ass, \*.ssa) to show lyrics over the timeline and the preview, timed to the music offset. Subtitles can be placed at the bottom or top (horizontal), or on the left or right (vertical), from the settings.
- **Credits accurate to the game**: the single artist field is replaced by separate Lyrics, Music, Arrangement and Vocals fields. They are exported to the SUS `#ARTIST` line in the game's format (`作詞：… 作曲：… 編曲：… Vo. …`) and read back on import. Charts saved by older versions have their artist text split into these fields where possible.
- **More statistics**: the statistics table also counts skill triggers and FEVER CHANCE.
- The MV file, MV offset and subtitle file are saved in the \*.mmws score file.

## Requirements:
- 64-bit Windows 10 or higher*.
- [Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe).
- GPU supporting OpenGL 3.3 with latest drivers installed.

*Earlier versions of Windows may work but not tested.

## Download:
The latest version can be downloaded [here](https://github.com/crash5band/MikuMikuWorld/releases/latest/download/MikuMikuWorld.zip).

## Screenshot:
![MikuMikuWorld](https://github.com/crash5band/MikuMikuWorld/assets/44091782/ba9dbcdf-fa77-4b44-b5e3-2858a283fce0)
