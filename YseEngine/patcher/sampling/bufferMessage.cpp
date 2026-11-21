#include "bufferMessage.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include <cstring>
#include <memory>
#include <type_traits>

using namespace YSE::PATCHER;

static_assert(std::is_trivially_copyable<bufferMessage>::value,
              "a bufferMessage is copied into a scheduler slot with memcpy");

namespace {

  // A word of a message, in place: no copy, no allocation.
  struct token {
    const char* text = nullptr;
    std::size_t length = 0;
  };

  constexpr int MAX_TOKENS = 8;

  int Tokenize(const char* text, std::size_t length, token* out, int max) {
    std::size_t at = 0;
    int count = 0;
    while (count < max) {
      while (at < length && IsSelectorSeparator(text[at]))
        at++;
      if (at >= length) break;
      const std::size_t begin = at;
      while (at < length && !IsSelectorSeparator(text[at]))
        at++;
      out[count].text = text + begin;
      out[count].length = at - begin;
      count++;
    }
    return count;
  }

  bool Is(const token& t, const char* word) {
    const std::size_t n = std::strlen(word);
    return t.length == n && std::memcmp(t.text, word, n) == 0;
  }

  bool Number(const token& t, float& out) {
    return t.length > 0 && ReadNumericToken(t.text, t.length, out);
  }

  // The window names `apply` takes.
  bool WindowNamed(const token& t, YSE::DSP::SAMPLE_WINDOW& out) {
    using YSE::DSP::SAMPLE_WINDOW;
    if (Is(t, "triangle"))
      out = SAMPLE_WINDOW::TRIANGLE;
    else if (Is(t, "hamming"))
      out = SAMPLE_WINDOW::HAMMING;
    else if (Is(t, "hanning") || Is(t, "hann"))
      out = SAMPLE_WINDOW::HANNING;
    else if (Is(t, "blackman"))
      out = SAMPLE_WINDOW::BLACKMAN;
    else if (Is(t, "welch"))
      out = SAMPLE_WINDOW::WELCH;
    else if (Is(t, "kaiser"))
      out = SAMPLE_WINDOW::KAISER;
    else
      return false;
    return true;
  }

} // namespace

namespace YSE {
  namespace PATCHER {

    bool ParseBufferMessage(const char* text, std::size_t length, bufferMessage& task) {
      token words[MAX_TOKENS];
      const int count = Tokenize(text, length, words, MAX_TOKENS);
      if (count == 0) return false;
      const token& word = words[0];

      float x = 0.f;
      float y = 0.f;

      // A path or a name into task.text; false when there is none or it is too
      // long to carry.
      auto takeText = [&](int index) {
        if (index >= count || words[index].length >= sizeof(task.text)) return false;
        std::memcpy(task.text, words[index].text, words[index].length);
        task.text[words[index].length] = '\0';
        return true;
      };

      if (Is(word, "read") || Is(word, "replace")) {
        if (!takeText(1)) return false;
        task.op = BUFFER_OP::READ;
        task.flag = Is(word, "replace") ? 1 : 0;
        task.a = count > 2 && Number(words[2], x) ? x : 0.f;
        task.b = count > 3 && Number(words[3], x) ? x : -1.f;
        task.channels = count > 4 && Number(words[4], x) ? static_cast<std::int32_t>(x) : -1;
      } else if (Is(word, "write") || Is(word, "writewave") || Is(word, "writeaiff") ||
                 Is(word, "writeflac")) {
        if (!takeText(1)) return false;
        task.op = BUFFER_OP::WRITE;
        DSP::SAMPLE_FILE format = DSP::SAMPLE_FILE::AUTO;
        if (Is(word, "writewave")) format = DSP::SAMPLE_FILE::WAV;
        if (Is(word, "writeaiff")) format = DSP::SAMPLE_FILE::AIFF;
        if (Is(word, "writeflac")) format = DSP::SAMPLE_FILE::FLAC;
        task.which = static_cast<std::uint8_t>(format);
      } else if (Is(word, "clear")) {
        task.op = BUFFER_OP::FILL;
        task.a = 0.f;
      } else if (Is(word, "fill")) {
        if (count < 2) return false;
        if (Number(words[1], x)) {
          task.op = BUFFER_OP::FILL;
          task.a = x;
        } else if (Is(words[1], "sin") || Is(words[1], "cos")) {
          task.op = BUFFER_OP::FILL_SINE;
          task.flag = Is(words[1], "cos") ? 1 : 0;
          task.a = count > 2 && Number(words[2], x) ? x : 1.f;
          task.b = count > 3 && Number(words[3], y) && y != 0.f ? y : 1.f;
        } else {
          return false;
        }
      } else if (Is(word, "apply")) {
        if (count < 2) return false;
        DSP::SAMPLE_WINDOW window = DSP::SAMPLE_WINDOW::HANNING;
        if (Is(words[1], "gain") || Is(words[1], "offset")) {
          if (count < 3 || !Number(words[2], x)) return false;
          task.op = Is(words[1], "gain") ? BUFFER_OP::GAIN : BUFFER_OP::OFFSET;
          task.a = x;
        } else if (Is(words[1], "getdeltas")) {
          task.op = BUFFER_OP::DELTAS;
        } else if (WindowNamed(words[1], window)) {
          task.op = BUFFER_OP::WINDOW;
          task.which = static_cast<std::uint8_t>(window);
          task.a = 6.f;
          int next = 2;
          if (window == DSP::SAMPLE_WINDOW::KAISER && next < count && Number(words[next], x)) {
            task.a = x;
            next++;
          }
          task.flag = next < count && Is(words[next], "half") ? 1 : 0;
        } else {
          return false;
        }
      } else if (Is(word, "normalize")) {
        task.op = BUFFER_OP::NORMALIZE;
        task.a = count > 1 && Number(words[1], x) ? x : 1.f;
      } else if (Is(word, "setsize") || Is(word, "sizeinsamps")) {
        if (count < 2 || !Number(words[1], x) || x < 0.f) return false;
        const bool samples = Is(word, "sizeinsamps");
        task.op = samples ? BUFFER_OP::SIZE_SAMPLES : BUFFER_OP::SIZE_MS;
        task.a = x;
        task.n = samples ? static_cast<std::uint32_t>(x) : 0;
        task.channels =
            count > 2 && Number(words[2], y) && y >= 1.f ? static_cast<std::int32_t>(y) : -1;
      } else if (Is(word, "crop") || Is(word, "crop_samples")) {
        if (count < 3 || !Number(words[1], x) || !Number(words[2], y) || x < 0.f || y < 0.f)
          return false;
        const bool samples = Is(word, "crop_samples");
        task.op = samples ? BUFFER_OP::CROP_SAMPLES : BUFFER_OP::CROP_MS;
        task.a = x;
        task.b = y;
        task.n = static_cast<std::uint32_t>(x);
        task.m = static_cast<std::uint32_t>(y);
      } else if (Is(word, "duplicate")) {
        if (!takeText(1)) return false;
        task.op = BUFFER_OP::DUPLICATE;
      } else if (Is(word, "sr")) {
        if (count < 2 || !Number(words[1], x) || x < 0.f) return false;
        task.op = BUFFER_OP::RATE;
        task.a = x;
      } else if (Is(word, "getsamps") || Is(word, "getsize") || Is(word, "getsr") ||
                 Is(word, "getchans")) {
        task.op = BUFFER_OP::QUERY;
        BUFFER_QUERY query = BUFFER_QUERY::SAMPS;
        if (Is(word, "getsize")) query = BUFFER_QUERY::SIZE;
        if (Is(word, "getsr")) query = BUFFER_QUERY::SR;
        if (Is(word, "getchans")) query = BUFFER_QUERY::CHANS;
        task.which = static_cast<std::uint8_t>(query);
      } else {
        return false;
      }
      return true;
    }

    bool ApplyBufferMessage(DSP::sampleBuffer& buf, bufferMessage& task, const char* path) {
      // The file to open: the host resolver's answer, or the name as sent.
      const char* file = path != nullptr ? path : task.text;

      bool ok = true;
      switch (task.op) {
      case BUFFER_OP::READ:
        ok = task.flag != 0 ? buf.replace(file) : buf.read(file, task.a, task.b, task.channels);
        break;
      case BUFFER_OP::WRITE:
        ok = buf.write(file, static_cast<DSP::SAMPLE_FILE>(task.which));
        break;
      case BUFFER_OP::FILL:
        buf.fill(task.a);
        break;
      case BUFFER_OP::FILL_SINE:
        buf.fillSine(task.a, task.b, task.flag != 0);
        break;
      case BUFFER_OP::WINDOW:
        buf.applyWindow(static_cast<DSP::SAMPLE_WINDOW>(task.which), task.flag != 0, task.a);
        break;
      case BUFFER_OP::GAIN:
        buf.applyGain(task.a);
        break;
      case BUFFER_OP::OFFSET:
        buf.applyOffset(task.a);
        break;
      case BUFFER_OP::DELTAS:
        buf.applyDeltas();
        break;
      case BUFFER_OP::NORMALIZE:
        buf.normalize(task.a);
        break;
      case BUFFER_OP::SIZE_MS:
        buf.setSizeMs(task.a, task.channels);
        break;
      case BUFFER_OP::SIZE_SAMPLES:
        buf.setSize(task.n, task.channels);
        break;
      case BUFFER_OP::CROP_MS:
        ok = buf.cropMs(task.a, task.b);
        break;
      case BUFFER_OP::CROP_SAMPLES:
        ok = buf.crop(task.n, task.m);
        break;
      case BUFFER_OP::DUPLICATE: {
        std::shared_ptr<DSP::sampleBuffer> source = DSP::sampleBufferRegistry::find(task.text);
        ok = source != nullptr;
        if (ok) buf.copyFrom(*source);
        break;
      }
      case BUFFER_OP::RATE:
        buf.setSampleRate(task.a);
        break;
      case BUFFER_OP::QUERY:
        break;
      }

      task.replyFrames = buf.frames();
      task.replyChannels = buf.channels();
      task.replyRate = buf.sampleRate();
      task.replyMs = buf.lengthMs();
      return ok;
    }

  } // namespace PATCHER
} // namespace YSE
