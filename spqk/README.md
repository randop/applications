# spqk

`spqk` is a small text-to-speech demo built on a speech synthesis library. Use
`--run-demo` to play a sequence demonstrating voices and speech parameters.
The `-d` / `--data` option synthesizes a JSON-configured utterance or batch.

## Dependencies

- Meson and a C++ compiler
- Ninja (the default Meson backend used below)
- Speech synthesis library development headers, library, and voice data
- Boost.Program_options
- Boost.JSON
- spdlog (provided through the Meson wrap, using the standard library formatter)

Meson locates the speech synthesis library and both Boost components through
its dependency mechanism. Install the development packages for these
libraries before configuring the build.

## Build

Run these commands from the project root:

```sh
meson setup build
meson compile -C build
```

If `build` is already configured, run `meson compile -C build` to rebuild. The
executable is `build/spqk`.

## Usage

```sh
./build/spqk --help
./build/spqk --version
./build/spqk --list-voices
./build/spqk --run-demo
```

`--list-voices` prints a JSON array containing the voices available to the
installed speech data set.

Pass a JSON object to synthesize one utterance. Without `output_file`, the
audio plays through the system audio output:

```sh
./build/spqk --data '{"text":"Hello from spqk.","voice":"en-us","rate":175,"pitch":50,"volume":100}'
./build/spqk -d '{"text":"Save this as speech.","voice":"en-us","output_file":"speech.wav"}'
```

`--data` also accepts an array. Each item with its own `output_file` is written
to a separate WAV; items without one are merged into a single WAV written to
`spqk-output.wav` in the current directory:

```sh
./build/spqk --data '[{"text":"First.","output_file":"first.wav"},{"text":"Second.","output_file":"second.wav"}]'
./build/spqk --data '[{"text":"First."},{"text":"Second."}]'
```

The second form combines all items without an `output_file` into one WAV. To
use a custom output path for the combined file, use the batch envelope form:

```sh
./build/spqk --data '{"output_file":"batch.wav","items":[{"text":"First."},{"text":"Second."}]}'
```

Per-item `output_file` paths override the shared batch output path in either
form.

## JSON payload

The payload accepts a single speech item object, an array of speech item
objects, or an object with `items` and an optional shared `output_file`.
Each speech item requires a non-empty `text` string; the other fields are
optional.

| Field | Type | Default | Meaning and validation |
| --- | --- | --- | --- |
| `text` | string | Required | UTF-8 text to synthesize; must not be empty. |
| `voice` | string or object | Default voice | Voice name (for example `en-us`) or voice criteria below. |
| `rate` | integer | `175` | Words per minute; valid range 80–450. |
| `pitch` | integer | `50` | Base pitch; valid range 0–100. |
| `volume` | integer | `100` | Nonnegative signed integer; values above 200 can distort. |
| `range` | integer | Default (`50`) | Pitch range; valid range 0–100. |
| `punctuation` | string or integer | Default | `none`, `some`, or `all`, or enum values 0, 2, or 1 respectively. |
| `punctuation_list` | string | Default list | Unicode punctuation characters to announce when `punctuation` is `some`. |
| `capitals` | integer | Default (`0`) | 0 none, 1 sound icon, 2 spelling, 3 or higher pitch raise. |
| `word_gap` | integer | Default (`0`) | Pause between words in 10 ms units; nonnegative signed integer. |
| `intonation` | integer | Default (`0`) | Passed directly to the synthesis parameter API. |
| `ssml_break_mul` | integer | Default (`100`) | SSML break multiplier, passed directly to the synthesis API. |
| `output_file` | string | Play audio | Non-empty path writes mono 16-bit PCM WAV. Empty string is treated as omitted. |

The `voice` object supports the selection criteria exposed by the synthesis
library:

```json
{
  "text": "Hello from spqk.",
  "voice": {
    "language": "en-uk",
    "gender": "female",
    "age": 25,
    "variant": 0
  },
  "rate": 180,
  "pitch": 55,
  "volume": 100,
  "range": 50,
  "punctuation": "some",
  "capitals": 0,
  "word_gap": 0,
  "output_file": "speech.wav"
}
```

Voice criteria may contain `name`, `language`, `gender`, `age`, and `variant`.
Gender accepts `male`, `female`, or `unspecified` (also integers 1, 2, or 0).
Age and variant must be integers from 0 to 255. Unknown names or criteria
that do not select a voice cause an error.

The parser rejects malformed JSON, non-object items, empty arrays, missing or
empty `text`, fields with the wrong type, invalid enum strings, out-of-range
values, and unknown fields. Boost.JSON parses the payload, and
Boost.Program_options handles command-line options.

## Speech synthesis integration

The demo initializes the speech synthesis library and applies voice selection
and speech parameters through its C API before synthesizing UTF-8 text. Voice
names and voice selection criteria use the library's voice selection API.
`--list-voices` enumerates installed voices and serializes names, identifiers,
gender, age, variant, and language codes with priorities as JSON. Playback mode
sends audio to the system output. WAV mode uses the library's synchronous audio
callback to collect samples and writes them at the sample rate reported by the
library.

## Structured logs

Compact `key=value` event records go to stderr. Command results remain on
stdout, so version text and voice-list JSON can be captured without log lines.
Event names and fields are stable for agent and script consumers. Example
records (the listed voice count varies by installation):

```text
spqk.version status=ok code=0 version=0.1.2 next=stdout
spqk.help status=ok code=0 next=stdout
spqk.demo status=start code=0 next=spqk.synth.start
spqk.synth.start status=start code=0 id=1 voice=default output=playback next=initialize
spqk.synth.done status=ok code=0 id=1 next=ready
spqk.synth.error status=error code=-1 id=2 reason=initialize_failed next=exit
spqk.voice.list status=ok code=0 count=128 next=stdout
```
