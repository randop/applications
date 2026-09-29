#include <cstdio>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"

#include <boost/program_options.hpp>
#include <boost/json.hpp>
#include <espeak-ng/speak_lib.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace {

std::shared_ptr<spdlog::logger> app_logger;
std::uint64_t next_synthesis_id = 1;

void initialize_logger() {
  auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
  app_logger = std::make_shared<spdlog::logger>("spqk", std::move(sink));
  app_logger->set_pattern("%v");
  app_logger->set_level(spdlog::level::info);
}

std::string log_value(std::string value) {
  for (char& ch : value) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    if (!(std::isalnum(byte) || ch == '_' || ch == '-' || ch == '.' || ch == '/')) {
      ch = '_';
    }
  }
  return value.empty() ? "default" : value;
}

void log_synth_error(std::uint64_t id, int code, std::string_view reason) {
  app_logger->error("spqk.synth.error status=error code={} id={} reason={} next=exit",
                    code, id, reason);
}

struct SpeechData {
  std::string text;
  std::string voice_name;
  std::string voice_language;
  bool voice_properties = false;
  int voice_gender = 0;
  int voice_age = 0;
  int voice_variant = 0;
  int rate = 175;
  int pitch = 50;
  int volume = 100;
  std::string punctuation_list;
  bool has_punctuation_list = false;
  std::vector<std::pair<espeak_PARAMETER, int>> extra_parameters;
  std::string output_file;
};

struct SpeechBatch {
  std::vector<SpeechData> items;
  std::string output_file;
};

std::vector<short>* active_audio_samples = nullptr;

int collect_audio(short* samples, int count, espeak_EVENT*) {
  if (active_audio_samples && samples && count > 0) {
    active_audio_samples->insert(active_audio_samples->end(), samples, samples + count);
  }
  return 0;
}

SpeechData parse_speech_item(const boost::json::value& parsed) {
  namespace bj = boost::json;
  if (!parsed.is_object()) {
    throw std::runtime_error("each speech item must be a JSON object");
  }
  const bj::object& object = parsed.as_object();
  SpeechData data;
  for (const auto& field : object) {
    const std::string name = field.key_c_str();
    if (name != "text" && name != "voice" && name != "rate" &&
        name != "pitch" && name != "volume" && name != "range" &&
        name != "punctuation" && name != "capitals" && name != "word_gap" &&
        name != "intonation" && name != "ssml_break_mul" &&
        name != "punctuation_list" &&
        name != "output_file") {
      throw std::runtime_error("unknown field '" + name + "'");
    }
  }

  const auto text = object.find("text");
  if (text == object.end() || !text->value().is_string() ||
      text->value().as_string().empty()) {
    throw std::runtime_error("field 'text' is required and must be a non-empty string");
  }
  data.text = text->value().as_string().c_str();

  const auto read_string = [&object](const char* name, std::string& target) {
    const auto field = object.find(name);
    if (field != object.end()) {
      if (!field->value().is_string()) {
        throw std::runtime_error(std::string("field '") + name + "' must be a string");
      }
      target = field->value().as_string().c_str();
    }
  };
  const auto read_int = [&object](const char* name, int& target, int minimum,
                                  int maximum) {
    const auto field = object.find(name);
    if (field != object.end()) {
      if (!field->value().is_int64()) {
        throw std::runtime_error(std::string("field '") + name + "' must be an integer");
      }
      const std::int64_t value = field->value().as_int64();
      if (value < minimum || value > maximum) {
        throw std::runtime_error(std::string("field '") + name + "' must be between " +
                                 std::to_string(minimum) + " and " +
                                 std::to_string(maximum));
      }
      target = static_cast<int>(value);
    }
  };

  const auto voice = object.find("voice");
  if (voice != object.end()) {
    if (voice->value().is_string()) {
      data.voice_name = voice->value().as_string().c_str();
    } else if (voice->value().is_object()) {
      data.voice_properties = true;
      const bj::object& criteria = voice->value().as_object();
      for (const auto& field : criteria) {
        const std::string name = field.key_c_str();
        if (name != "name" && name != "language" && name != "gender" &&
            name != "age" && name != "variant") {
          throw std::runtime_error("unknown voice field '" + name + "'");
        }
      }
      const auto read_voice_string = [&criteria](const char* name, std::string& target) {
        const auto field = criteria.find(name);
        if (field != criteria.end()) {
          if (!field->value().is_string()) {
            throw std::runtime_error(std::string("voice field '") + name + "' must be a string");
          }
          target = field->value().as_string().c_str();
        }
      };
      read_voice_string("name", data.voice_name);
      read_voice_string("language", data.voice_language);
      const auto read_voice_int = [&criteria](const char* name, int& target, int max) {
        const auto field = criteria.find(name);
        if (field != criteria.end()) {
          if (!field->value().is_int64()) {
            throw std::runtime_error(std::string("voice field '") + name + "' must be an integer");
          }
          const auto value = field->value().as_int64();
          if (value < 0 || value > max) {
            throw std::runtime_error(std::string("voice field '") + name + "' is out of range");
          }
          target = static_cast<int>(value);
        }
      };
      const auto gender = criteria.find("gender");
      if (gender != criteria.end()) {
        if (gender->value().is_string()) {
          const std::string value = gender->value().as_string().c_str();
          if (value == "male") data.voice_gender = 1;
          else if (value == "female") data.voice_gender = 2;
          else if (value != "unspecified") {
            throw std::runtime_error("voice field 'gender' must be male, female, or unspecified");
          }
        } else if (gender->value().is_int64() &&
                   gender->value().as_int64() >= 0 && gender->value().as_int64() <= 2) {
          data.voice_gender = static_cast<int>(gender->value().as_int64());
        } else {
          throw std::runtime_error("voice field 'gender' must be a gender name or integer 0-2");
        }
      }
      read_voice_int("age", data.voice_age, 255);
      read_voice_int("variant", data.voice_variant, 255);
    } else {
      throw std::runtime_error("field 'voice' must be a string or voice criteria object");
    }
  }
  read_int("rate", data.rate, 80, 450);
  read_int("pitch", data.pitch, 0, 100);
  read_int("volume", data.volume, 0, std::numeric_limits<int>::max());
  const auto punctuation_list = object.find("punctuation_list");
  if (punctuation_list != object.end()) {
    if (!punctuation_list->value().is_string()) {
      throw std::runtime_error("field 'punctuation_list' must be a string");
    }
    data.punctuation_list = punctuation_list->value().as_string().c_str();
    data.has_punctuation_list = true;
  }
  int value = 0;
  if (object.contains("range")) {
    read_int("range", value, 0, 100);
    data.extra_parameters.emplace_back(espeakRANGE, value);
  }
  if (object.contains("capitals")) {
    read_int("capitals", value, 0, 1000);
    data.extra_parameters.emplace_back(espeakCAPITALS, value);
  }
  if (object.contains("word_gap")) {
    read_int("word_gap", value, 0, std::numeric_limits<int>::max());
    data.extra_parameters.emplace_back(espeakWORDGAP, value);
  }
  if (object.contains("intonation")) {
    read_int("intonation", value, std::numeric_limits<int>::min(),
             std::numeric_limits<int>::max());
    data.extra_parameters.emplace_back(espeakINTONATION, value);
  }
  if (object.contains("ssml_break_mul")) {
    read_int("ssml_break_mul", value, std::numeric_limits<int>::min(),
             std::numeric_limits<int>::max());
    data.extra_parameters.emplace_back(espeakSSML_BREAK_MUL, value);
  }
  const auto punctuation = object.find("punctuation");
  if (punctuation != object.end()) {
    if (punctuation->value().is_string()) {
      const std::string value = punctuation->value().as_string().c_str();
      if (value == "none") data.extra_parameters.emplace_back(espeakPUNCTUATION, espeakPUNCT_NONE);
      else if (value == "all") data.extra_parameters.emplace_back(espeakPUNCTUATION, espeakPUNCT_ALL);
      else if (value == "some") data.extra_parameters.emplace_back(espeakPUNCTUATION, espeakPUNCT_SOME);
      else throw std::runtime_error("field 'punctuation' must be none, some, or all");
    } else {
      read_int("punctuation", value, 0, 2);
      data.extra_parameters.emplace_back(espeakPUNCTUATION, value);
    }
  }
  read_string("output_file", data.output_file);
  return data;
}

SpeechBatch parse_speech_data(const std::string& payload) {
  namespace bj = boost::json;
  const bj::value parsed = bj::parse(payload);
  SpeechBatch batch;
  if (parsed.is_array()) {
    for (const auto& item : parsed.as_array()) batch.items.push_back(parse_speech_item(item));
    batch.output_file = "spqk-output.wav";
  } else if (parsed.is_object()) {
    const bj::object& object = parsed.as_object();
    if (object.contains("items")) {
      for (const auto& field : object) {
        const std::string name = field.key_c_str();
        if (name != "items" && name != "output_file") {
          throw std::runtime_error("unknown batch field '" + name + "'");
        }
      }
      const auto& items = object.at("items");
      if (!items.is_array() || items.as_array().empty()) {
        throw std::runtime_error("'items' must be a non-empty array");
      }
      if (object.contains("output_file")) {
        if (!object.at("output_file").is_string()) {
          throw std::runtime_error("batch 'output_file' must be a string");
        }
        batch.output_file = object.at("output_file").as_string().c_str();
      }
      for (const auto& item : items.as_array()) batch.items.push_back(parse_speech_item(item));
    } else {
      batch.items.push_back(parse_speech_item(parsed));
    }
  } else {
    throw std::runtime_error("JSON payload must be an object or an array of objects");
  }
  if (batch.items.empty()) throw std::runtime_error("speech item array must not be empty");
  return batch;
}

void write_u32(std::ofstream& output, std::uint32_t value) {
  const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8),
                        static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
  output.write(bytes, sizeof(bytes));
}

std::wstring to_wide(const std::string& text) {
  std::wstring result;
  for (std::size_t index = 0; index < text.size();) {
    const unsigned char first = static_cast<unsigned char>(text[index++]);
    std::uint32_t codepoint = first;
    unsigned int continuation_count = 0;
    if ((first & 0xe0) == 0xc0) { codepoint = first & 0x1f; continuation_count = 1; }
    else if ((first & 0xf0) == 0xe0) { codepoint = first & 0x0f; continuation_count = 2; }
    else if ((first & 0xf8) == 0xf0) { codepoint = first & 0x07; continuation_count = 3; }
    for (unsigned int i = 0; i < continuation_count; ++i) {
      codepoint = (codepoint << 6) |
                  (static_cast<unsigned char>(text[index++]) & 0x3f);
    }
    if (WCHAR_MAX >= 0x10ffff || codepoint <= 0xffff) {
      result.push_back(static_cast<wchar_t>(codepoint));
    } else {
      codepoint -= 0x10000;
      result.push_back(static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
      result.push_back(static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff)));
    }
  }
  return result;
}

bool write_wav(const std::string& path, int sample_rate,
               const std::vector<short>& samples) {
  if (samples.size() > (UINT32_MAX - 36) / sizeof(short)) {
    std::fprintf(stderr, "audio is too large for a WAV file\n");
    return false;
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    std::fprintf(stderr, "cannot open output file '%s'\n", path.c_str());
    return false;
  }
  const auto data_size = static_cast<std::uint32_t>(samples.size() * sizeof(short));
  output.write("RIFF", 4);
  write_u32(output, 36 + data_size);
  output.write("WAVEfmt ", 8);
  write_u32(output, 16);
  output.put(1); output.put(0);  // PCM format
  output.put(1); output.put(0);  // mono
  write_u32(output, static_cast<std::uint32_t>(sample_rate));
  write_u32(output, static_cast<std::uint32_t>(sample_rate * sizeof(short)));
  output.put(2); output.put(0);  // block alignment
  output.put(16); output.put(0); // bits per sample
  output.write("data", 4);
  write_u32(output, data_size);
  output.write(reinterpret_cast<const char*>(samples.data()), data_size);
  if (!output) {
    std::fprintf(stderr, "failed writing output file '%s'\n", path.c_str());
    return false;
  }
  return true;
}

int run_payload(const SpeechData& data, const std::string& output_file,
                std::vector<short>* batch_samples = nullptr,
                int* batch_sample_rate = nullptr) {
  const std::uint64_t id = next_synthesis_id++;
  const std::string voice = data.voice_properties
      ? (!data.voice_name.empty() ? data.voice_name
         : !data.voice_language.empty() ? data.voice_language : "criteria")
      : data.voice_name;
  app_logger->info("spqk.synth.start status=start code=0 id={} voice={} output={} next=initialize",
                   id, log_value(voice), output_file.empty() ? "playback" : "wav");
  const bool to_file = !output_file.empty();
  std::vector<short> item_samples;
  if (to_file) active_audio_samples = batch_samples ? batch_samples : &item_samples;
  const int sample_rate = espeak_Initialize(
      to_file ? AUDIO_OUTPUT_SYNCHRONOUS : AUDIO_OUTPUT_PLAYBACK, 0, nullptr, 0);
  if (sample_rate < 0) {
    active_audio_samples = nullptr;
    std::fprintf(stderr, "espeak_Initialize failed\n");
    log_synth_error(id, sample_rate, "initialize_failed");
    return 1;
  }
  if (to_file) espeak_SetSynthCallback(collect_audio);
  espeak_ERROR voice_result = EE_OK;
  if (data.voice_properties) {
    espeak_VOICE voice_spec{};
    voice_spec.name = data.voice_name.empty() ? nullptr : data.voice_name.c_str();
    voice_spec.languages = data.voice_language.empty() ? nullptr : data.voice_language.c_str();
    voice_spec.gender = static_cast<unsigned char>(data.voice_gender);
    voice_spec.age = static_cast<unsigned char>(data.voice_age);
    voice_spec.variant = static_cast<unsigned char>(data.voice_variant);
    voice_result = espeak_SetVoiceByProperties(&voice_spec);
  } else if (!data.voice_name.empty()) {
    voice_result = espeak_SetVoiceByName(data.voice_name.c_str());
  }
  if (voice_result != EE_OK) {
    std::fprintf(stderr, "failed to select requested voice\n");
    espeak_Terminate();
    active_audio_samples = nullptr;
    log_synth_error(id, static_cast<int>(voice_result), "voice_selection_failed");
    return 1;
  }
  espeak_SetParameter(espeakRATE, data.rate, 0);
  espeak_SetParameter(espeakPITCH, data.pitch, 0);
  espeak_SetParameter(espeakVOLUME, data.volume, 0);
  for (const auto& [parameter, value] : data.extra_parameters) {
    espeak_SetParameter(parameter, value, 0);
  }
  if (data.has_punctuation_list) {
    const std::wstring punctuation = to_wide(data.punctuation_list);
    espeak_SetPunctuationList(punctuation.c_str());
  }
  unsigned int engine_message_id = 0;
  const espeak_ERROR result = espeak_Synth(
      data.text.c_str(), 0, 0, POS_CHARACTER, 0, espeakCHARS_UTF8,
      &engine_message_id, nullptr);
  if (result == EE_OK) espeak_Synchronize();
  espeak_Terminate();
  active_audio_samples = nullptr;
  if (result != EE_OK) {
    std::fprintf(stderr, "espeak_Synth failed (%d)\n", result);
    log_synth_error(id, static_cast<int>(result), "synthesis_failed");
    return 1;
  }
  if (to_file && batch_samples && batch_sample_rate) *batch_sample_rate = sample_rate;
  if (to_file && !batch_samples && !write_wav(output_file, sample_rate, item_samples)) {
    log_synth_error(id, 1, "output_failed");
    return 1;
  }
  app_logger->info("spqk.synth.done status=ok code=0 id={} next=ready", id);
  return 0;
}

int run_batch(const SpeechBatch& batch) {
  std::vector<short> combined_samples;
  int combined_sample_rate = 0;
  bool use_combined_file = false;
  app_logger->info("spqk.batch status=start code=0 items={} next=spqk.synth.start",
                   batch.items.size());
  for (const SpeechData& item : batch.items) {
    const std::string& output_file = item.output_file.empty() ? batch.output_file : item.output_file;
    std::vector<short>* target = item.output_file.empty() && !batch.output_file.empty()
                                     ? &combined_samples : nullptr;
    if (target) use_combined_file = true;
    if (run_payload(item, output_file, target, target ? &combined_sample_rate : nullptr) != 0) {
      app_logger->error("spqk.batch status=error code=1 next=exit");
      return 1;
    }
  }
  if (use_combined_file) {
    if (!write_wav(batch.output_file, combined_sample_rate, combined_samples)) {
      log_synth_error(0, 1, "batch_output_failed");
      app_logger->error("spqk.batch status=error code=1 next=exit");
      return 1;
    }
    app_logger->info("spqk.batch.merge status=ok code=0 items={} output={} next=exit",
                     batch.items.size(), batch.output_file);
  }
  app_logger->info("spqk.batch status=ok code=0 next=exit");
  return 0;
}

int list_voices_json() {
  namespace bj = boost::json;
  if (espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, nullptr, 0) < 0) {
    std::fprintf(stderr, "espeak_Initialize failed\n");
    app_logger->error("spqk.voice.list status=error code=-1 count=0 next=exit");
    return 1;
  }
  bj::array voices_json;
  const espeak_VOICE** voices = espeak_ListVoices(nullptr);
  if (voices) {
    for (const espeak_VOICE** voice = voices; *voice; ++voice) {
      bj::object item;
      item["name"] = (*voice)->name ? (*voice)->name : "";
      item["identifier"] = (*voice)->identifier ? (*voice)->identifier : "";
      item["gender"] = (*voice)->gender == 1 ? "male" :
                       (*voice)->gender == 2 ? "female" : "unspecified";
      item["age"] = (*voice)->age;
      item["variant"] = (*voice)->variant;
      bj::array languages;
      const auto* language = reinterpret_cast<const unsigned char*>((*voice)->languages);
      while (language && *language) {
        const unsigned int priority = *language++;
        const char* name = reinterpret_cast<const char*>(language);
        const std::size_t length = std::strlen(name);
        bj::object entry;
        entry["name"] = std::string(name, length);
        entry["priority"] = priority;
        languages.push_back(std::move(entry));
        language += length + 1;
      }
      item["languages"] = std::move(languages);
      voices_json.push_back(std::move(item));
    }
  }
  espeak_Terminate();
  std::cout << bj::serialize(voices_json) << '\n';
  app_logger->info("spqk.voice.list status=ok code=0 count={} next=stdout", voices_json.size());
  return 0;
}

bool synth(const std::string& text) {
  const std::uint64_t id = next_synthesis_id++;
  app_logger->info("spqk.synth.start status=start code=0 id={} voice=active output=playback next=synthesize",
                   id);
  const std::size_t size = 0;
  const unsigned int position = 0;
  const espeak_POSITION_TYPE position_type = POS_CHARACTER;
  const unsigned int end_position = 0;
  const unsigned int flags = 0;
  void* user_data = nullptr;

  unsigned int message_id = 0;
  const espeak_ERROR result = espeak_Synth(text.data(), size, position, position_type,
                                           end_position, flags, &message_id, user_data);
  if (result != EE_OK) {
    log_synth_error(id, static_cast<int>(result), "synthesis_failed");
    return false;
  }
  const espeak_ERROR sync_result = espeak_Synchronize();
  if (sync_result != EE_OK) {
    log_synth_error(id, static_cast<int>(sync_result), "synchronize_failed");
    return false;
  }
  app_logger->info("spqk.synth.done status=ok code=0 id={} next=ready", id);
  return true;
}

bool say(const std::string& label, const std::string& text) {
  std::printf("=== %s ===\n", label.c_str());
  std::fflush(stdout);
  return synth(text);
}

bool set_voice(const char* name) {
  const espeak_ERROR result = espeak_SetVoiceByName(name);
  if (result != EE_OK) {
    std::fprintf(stderr, "failed to set voice '%s'\n", name);
    return false;
  }
  return true;
}

void set_param(espeak_PARAMETER param, int value) {
  espeak_SetParameter(param, value, 0);
}

int run_demo() {
  app_logger->info("spqk.demo status=start code=0 next=spqk.synth.start");
  if (espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, 0, nullptr, 0) < 0) {
    std::fprintf(stderr, "espeak_Initialize failed\n");
    app_logger->error("spqk.demo status=error code=-1 next=exit");
    return 1;
  }

  bool success = true;
  success &= say("Default voice",
                 "The morning light settles across the harbor, and the city begins its day.");

  success &= set_voice("en-us");
  success &= say("Voice en-us",
                 "Across the wide valley, a train follows the river toward the distant coast.");

  set_param(espeakRATE, 200);
  success &= say("Speed 200 wpm",
                 "A brisk pace can make a short announcement clear, energetic, and easy to follow.");

  set_param(espeakRATE, 120);
  set_param(espeakPITCH, 70);
  success &= say("Speed 120 wpm, pitch 70",
                 "Take a moment to notice the quiet details: steady rain, a warm room, and a cup of tea.");

  set_param(espeakVOLUME, 80);
  success &= say("Amplitude 80", "This sample is spoken at a softer volume.");
  set_param(espeakVOLUME, 200);
  success &= say("Amplitude 200", "This sample is spoken at a stronger volume.");

  set_param(espeakVOLUME, 100);
  set_param(espeakWORDGAP, 50);
  success &= say("Word gap 50",
                 "A measured pause between words can make each phrase easier to distinguish.");

  set_param(espeakWORDGAP, 0);
  set_param(espeakPUNCTUATION, 1);
  success &= say("Punctuation ignored",
                 "The forecast is promising: bright skies, light winds, and a pleasant afternoon ahead.");
  set_param(espeakPUNCTUATION, 2);
  success &= say("Punctuation fully spoken",
                 "The forecast is promising: bright skies, light winds, and a pleasant afternoon ahead.");
  set_param(espeakPUNCTUATION, 0);

  success &= set_voice("default");
  set_param(espeakRATE, 175);
  set_param(espeakPITCH, 50);
  set_param(espeakVOLUME, 100);
  success &= say("Mixed punctuation prosody",
                 "The forecast is promising: bright skies, light winds, and a pleasant afternoon ahead.");

  espeak_Terminate();
  std::puts("Done");
  if (success) {
    app_logger->info("spqk.demo status=ok code=0 next=exit");
    return 0;
  }
  app_logger->error("spqk.demo status=error code=1 next=exit");
  return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
  initialize_logger();
  namespace po = boost::program_options;

  po::options_description options("Options");
  options.add_options()
      ("help,h", "show this help message and exit")
  ("data,d", po::value<std::string>(),
       "JSON object or array; array items without output_file merge into spqk-output.wav, e.g. '[{\"text\":\"Hello\",\"voice\":\"en-us\"}]'")
      ("list-voices", "list available eSpeak NG voices as JSON")
      ("run-demo", "run the built-in speech demonstration")
      ("version", "show program version and exit");

  po::variables_map args;
  try {
    po::store(po::parse_command_line(argc, argv, options), args);
    po::notify(args);
  } catch (const po::error& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }

  if (args.count("help")) {
    app_logger->info("spqk.help status=ok code=0 next=stdout");
    std::cout << "Usage: spqk [options]\n" << options;
    return 0;
  }
  if (args.count("version")) {
    app_logger->info("spqk.version status=ok code=0 version={} next=stdout", SPQK_VERSION);
    std::printf("spqk %s\n", SPQK_VERSION);
    return 0;
  }
  if (args.count("list-voices")) return list_voices_json();
  if (args.count("data")) {
    try {
      return run_batch(parse_speech_data(args["data"].as<std::string>()));
    } catch (const std::exception& error) {
      std::fprintf(stderr, "invalid --data payload: %s\n", error.what());
      log_synth_error(0, 1, "invalid_payload");
      return 1;
    }
  }
  if (args.count("run-demo")) return run_demo();

  app_logger->info("spqk.help status=ok code=0 next=stdout");
  std::cout << "Usage: spqk [options]\n" << options;
  return 0;
}
