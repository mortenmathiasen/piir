// Record the IR codes arduino-heatpumpir sends, as captures for ir2irlingo.py.
//
// usage: heatpumpir_capture list
//        heatpumpir_capture MODEL [RANDOM [SEED]]
//        heatpumpir_capture MODEL all
//
// Prints JSON Lines on standard output: a base setting, every value of every
// feature changed alone, and RANDOM (default 300) random combinations, or
// with "all" every combination (for ir2irlingo --fallback).
// Features: power, mode, fan, temperature, vertical, horizontal. The
// temperature range and the fan speeds come from the model's info().
//
// Models that put the time of day into their codes get 00:00: time() is
// replaced below, as irlingo cannot send a clock.
#include <HeatpumpIRFactory.h>
#include <IRSender.h>

#include <ctime>
#include <random>
#include <string>
#include <vector>

// ------------------------------------------------------------- recording

class RecordingSender : public IRSender {
public:
  RecordingSender() : IRSender(0) {}
  void setFrequency(int frequency) override {
    this->frequency = frequency < 1000 ? frequency * 1000 : frequency;
  }
  void mark(int length) override { add(true, length); }
  void space(int length) override { add(false, length); }

  void add(bool isMark, long length) {
    if (length <= 0 || (durations.empty() && !isMark))
      return;
    if (!durations.empty() && (durations.size() % 2 == 1) == isMark)
      durations.back() += length;
    else
      durations.push_back(length);
  }
  void clear() { durations.clear(); }

  std::vector<long> durations;   // mark, space, mark, ...
  int frequency = 38000;
};

static RecordingSender *recorder = nullptr;

void delay(unsigned long ms) { if (recorder) recorder->space(ms * 1000); }
void delayMicroseconds(unsigned int us) { if (recorder) recorder->space(us); }
unsigned long millis() { return 0; }
unsigned long micros() { return 0; }

// The time of day some models send: always 00:00
extern "C" time_t time(time_t *t) {
  if (t)
    *t = 0;
  return 0;
}
extern "C" struct tm *localtime(const time_t *) {
  static struct tm midnight = {};
  midnight.tm_mday = 1;
  midnight.tm_year = 70;
  return &midnight;
}

// --------------------------------------------------------------- features

static const char *const MODELS[] = {
  "AUX", "ballu", "carrier_mca", "carrier_nqv", "daikin_arc417", "daikin_arc480", "daikin",
  "electroluxyal", "fuego", "fujitsu_awyz", "gree", "greeyaa", "greeyan", "greeyac", "greeyt",
  "hisense_aud", "hitachi", "hyundai", "ivt", "nibe", "midea", "mitsubishi_fa", "mitsubishi_fd",
  "mitsubishi_fe", "mitsubishi_heavy_fdtc", "mitsubishi_heavy_zj", "mitsubishi_heavy_zm",
  "mitsubishi_kj", "mitsubishi_msc", "mitsubishi_msy", "mitsubishi_sez", "olimpia",
  "panasonic_ckp", "panasonic_dke", "panasonic_eke", "panasonic_jke", "panasonic_lke",
  "panasonic_nke", "philco_phs32", "samsung_aqv", "samsung_fjm", "sharp", "toshiba_daiseikai",
  "toshiba", "ZHJG01", "ZHLT01",
};

struct Value {
  std::string name;
  uint8_t code;
};

struct Feature {
  std::string name;
  std::vector<Value> values;
  size_t base;
};

// A number from the model's info JSON, e.g. "mT":16
static int infoNumber(const char *info, const char *key, int fallback) {
  std::string pattern = std::string("\"") + key + "\":";
  const char *p = info ? strstr(info, pattern.c_str()) : nullptr;
  return p ? atoi(p + pattern.size()) : fallback;
}

static std::vector<Feature> features(HeatpumpIR *heatpump) {
  const char *info = heatpump->info();
  int minTemp = infoNumber(info, "mT", 16);
  int maxTemp = infoNumber(info, "xT", 30);
  int fanSpeeds = infoNumber(info, "fs", 5);

  Feature temperature{"temperature", {}, 0};
  for (int t = minTemp; t <= maxTemp; t++) {
    if (t == 22 || (t == maxTemp && temperature.base == 0 && maxTemp < 22))
      temperature.base = temperature.values.size();
    temperature.values.push_back({std::to_string(t), (uint8_t)t});
  }
  Feature fan{"fan", {{"AUTO", FAN_AUTO}}, 0};
  for (int f = 1; f <= fanSpeeds && f <= 5; f++)
    fan.values.push_back({std::to_string(f), (uint8_t)f});

  return {
    {"power", {{"ON", POWER_ON}, {"OFF", POWER_OFF}}, 0},
    {"mode", {{"AUTO", MODE_AUTO}, {"HEAT", MODE_HEAT}, {"COOL", MODE_COOL},
              {"DRY", MODE_DRY}, {"FAN", MODE_FAN}}, 2},
    fan,
    temperature,
    {"vertical", {{"AUTO", VDIR_AUTO}, {"SWING", VDIR_SWING}, {"UP", VDIR_UP},
                  {"MUP", VDIR_MUP}, {"MIDDLE", VDIR_MIDDLE}, {"MDOWN", VDIR_MDOWN},
                  {"DOWN", VDIR_DOWN}}, 0},
    {"horizontal", {{"AUTO", HDIR_AUTO}, {"SWING", HDIR_SWING}, {"MIDDLE", HDIR_MIDDLE},
                    {"LEFT", HDIR_LEFT}, {"MLEFT", HDIR_MLEFT}, {"MRIGHT", HDIR_MRIGHT},
                    {"RIGHT", HDIR_RIGHT}}, 0},
  };
}

static void capture(HeatpumpIR *heatpump, const std::vector<Feature> &fs,
                    const std::vector<size_t> &chosen) {
  RecordingSender sender;
  recorder = &sender;
  uint8_t c[6];
  for (size_t i = 0; i < 6; i++)
    c[i] = fs[i].values[chosen[i]].code;
  heatpump->send(sender, c[0], c[1], c[2], c[3], c[4], c[5]);
  recorder = nullptr;
  if (sender.durations.size() % 2 == 0 && !sender.durations.empty())
    sender.durations.pop_back();   // the final space is not recorded

  printf("{\"features\": {");
  for (size_t i = 0; i < fs.size(); i++)
    printf("%s\"%s\": \"%s\"", i ? ", " : "", fs[i].name.c_str(),
           fs[i].values[chosen[i]].name.c_str());
  printf("}, \"frequency\": %d, \"durations\": [", sender.frequency);
  for (size_t i = 0; i < sender.durations.size(); i++)
    printf("%s%ld", i ? ", " : "", sender.durations[i]);
  printf("]}\n");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s list | MODEL [RANDOM [SEED]] | MODEL all\n", argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "list") == 0) {
    for (const char *model : MODELS)
      printf("%s\n", model);
    return 0;
  }
  HeatpumpIR *heatpump = HeatpumpIRFactory::create(argv[1]);
  if (!heatpump) {
    fprintf(stderr, "Unknown model %s, see: %s list\n", argv[1], argv[0]);
    return 2;
  }
  bool all = argc > 2 && strcmp(argv[2], "all") == 0;
  int randomCount = argc > 2 && !all ? atoi(argv[2]) : 300;
  unsigned seed = argc > 3 ? atoi(argv[3]) : 1;

  std::vector<Feature> fs = features(heatpump);
  std::vector<size_t> base;
  for (const Feature &f : fs)
    base.push_back(f.base);

  capture(heatpump, fs, base);
  for (size_t i = 0; i < fs.size(); i++)
    for (size_t v = 0; v < fs[i].values.size(); v++)
      if (v != fs[i].base) {
        std::vector<size_t> chosen = base;
        chosen[i] = v;
        capture(heatpump, fs, chosen);
      }
  if (all) {
    std::vector<size_t> chosen(fs.size(), 0);
    while (true) {
      capture(heatpump, fs, chosen);
      size_t i = 0;
      while (i < fs.size() && ++chosen[i] == fs[i].values.size())
        chosen[i++] = 0;
      if (i == fs.size())
        break;
    }
    delete heatpump;
    return 0;
  }
  std::mt19937 rng(seed);
  for (int n = 0; n < randomCount; n++) {
    std::vector<size_t> chosen;
    for (const Feature &f : fs)
      chosen.push_back(std::uniform_int_distribution<size_t>(0, f.values.size() - 1)(rng));
    capture(heatpump, fs, chosen);
  }
  delete heatpump;
  return 0;
}
