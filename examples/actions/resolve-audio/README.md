# Convert Audio for Resolve — example omanta action

DaVinci Resolve on Linux, Studio included, can't decode AAC audio, so phone
clips and screen recordings import with no sound. This action adds
**Convert Audio for Resolve** to the right-click menu for videos. It writes
`<name>.resolve.mov` beside the original, with the video copied untouched and
the audio re-encoded in a codec Resolve can read. The original is never
modified, an existing `.resolve.mov` is never overwritten, and audio above
48 kHz is resampled to 48 kHz.

It is also an example of an action with a setting. The `option` lines in
`resolve-audio.toml` add an **Audio Codec** dropdown under
**Preferences → Custom Actions**, and omanta hands the choice to the script as
`OMANTA_OPTION_CODEC`.

## Install

Requires `ffmpeg`.

```sh
cp resolve-audio.toml ~/.config/omanta/actions/
install -m755 omanta-resolve-audio ~/.local/bin/
```

The menu item appears straight away. To remove it, delete both files, or switch
it off in Preferences.

## Codecs

| Choice | Size per minute (stereo, 48 kHz) | Notes |
|---|---|---|
| PCM 16-bit (default) | about 11 MB | Uncompressed; works everywhere |
| PCM 24-bit | about 16.5 MB | Uncompressed; more headroom for audio work |
| ALAC | similar to PCM on most real audio | Lossless and compressed; check that your Resolve imports it |
| MP3 320 kbps | about 2.3 MB | Lossy; check that your Resolve imports it |

On an omanta without action options, the `option` lines are ignored and the
script uses PCM 16-bit. To pick a codec there, run the script with the
variable set, e.g. `OMANTA_OPTION_CODEC=mp3 omanta-resolve-audio clip.mp4`.

## Test

```sh
sh test.sh ./omanta-resolve-audio
```
