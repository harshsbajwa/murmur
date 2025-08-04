# Test Assets

This directory contains small, license-free sample media files for testing purposes.

## Files

- **test_tone.wav**: 1-second 440Hz sine wave tone (16kHz, mono, ~32KB)
- **test_speech.wav**: Short "Hello world test audio" sample (16kHz, mono, ~48KB)  
- **test_video.mp4**: 2-second test pattern video (320x240, 1fps, ~6KB)

## Usage

These files are designed to be:
- Small in size for fast CI builds
- License-free (generated programmatically)
- Suitable for basic media processing tests
- Compatible with QFINDTESTDATA() paths

## Generation

The files were generated using:

```bash
# Audio tone
ffmpeg -f lavfi -i "sine=frequency=440:duration=1" -ar 16000 -ac 1 test_tone.wav

# Speech sample  
say -v Alex "Hello world test audio" -o temp.aiff
ffmpeg -i temp.aiff -ar 16000 -ac 1 test_speech.wav

# Test video
ffmpeg -f lavfi -i "testsrc=duration=2:size=320x240:rate=1" -c:v libx264 -pix_fmt yuv420p -crf 28 test_video.mp4
```
