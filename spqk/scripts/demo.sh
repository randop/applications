#!/usr/bin/env bash
set -euo pipefail
MESSAGE="Hello from espeak-ng! This is a demo of text to speech on Linux."
OUTPUT_FILE="/tmp/espeak_demo.wav"
echo "=== espeak-ng demo ==="
if ! command -v espeak-ng >/dev/null 2>&1; then echo 'espeak-ng not installed'; exit 1; fi
echo "1. Default:"; espeak-ng "$MESSAGE"
echo "2. Voice en-us:"; espeak-ng -v en-us "$MESSAGE"
echo "3. Faster 200 wpm:"; espeak-ng -s 200 "$MESSAGE"
echo "4. Slower 120 wpm pitch 70:"; espeak-ng -s 120 -p 70 "$MESSAGE"
echo "5. Save WAV:"; espeak-ng -w "$OUTPUT_FILE" "$MESSAGE"; ls -l "$OUTPUT_FILE"
echo "6. Punctuation handling (-punct):"; espeak-ng --punct "$MESSAGE"
echo "7. Vocal dynamics amplitude (80):"; espeak-ng -a 80 "$MESSAGE"
echo "8. Vocal dynamics amplitude (200):"; espeak-ng -a 200 "$MESSAGE"
echo "9. Word gap 50 (pause between words):"; espeak-ng -g 50 "$MESSAGE"
echo "10. Mixed punctuation for natural prosody:"; espeak-ng --punct "Wait... what? Really! OK, let's continue. Hmm..."
echo "Done"
