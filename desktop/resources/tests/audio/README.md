**Generate**
```bash
say -v Alex "This is a test audio clip for transcription." -o test.aiff
ffmpeg -i test.aiff -ar 16000 -ac 1 test.wav
rm test.aiff
```