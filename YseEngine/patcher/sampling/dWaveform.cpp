#include "dWaveform.h"
#include "../analysis/meterCommon.h"
#include <cmath>
#include <cstring>
#include <string_view>
#include <type_traits>

using namespace YSE::PATCHER;
using namespace YSE::PATCHER::meterCommon;
using YSE::DSP::waveformView;

#define className dWaveform

namespace {

  enum class EDIT_OP : std::uint8_t {
    CROP,
    NORMALIZE,
    REPLACE,
  };

  // A setting given as a number: its whole part, held within 0 to `top`.
  int Choice(float value, int top) {
    if (!(value > 0.f)) return 0;
    const int whole = value >= static_cast<float>(top) ? top : static_cast<int>(value);
    return whole;
  }

  unsigned int Count(float value) {
    if (!(value > 0.f)) return 0;
    return value >= 4294967040.f ? 4294967040u : static_cast<unsigned int>(value);
  }

  waveformView::MODE ModeOf(float value) {
    return static_cast<waveformView::MODE>(Choice(value, waveformView::DRAW));
  }

  waveformView::OUTPUT OutputOf(float value) {
    return static_cast<waveformView::OUTPUT>(Choice(value, waveformView::OUTPUT_CONTINUOUS));
  }

  // Max's snapto: 0 nothing, 1 the grid, 2 zero crossings.
  waveformView::SNAP SnapToOf(float value) {
    switch (Choice(value, 2)) {
    case 1:
      return waveformView::SNAP_GRID;
    case 2:
      return waveformView::SNAP_ZERO;
    default:
      return waveformView::SNAP_NONE;
    }
  }

  waveformView::UNIT UnitOf(float value) {
    return static_cast<waveformView::UNIT>(Choice(value, waveformView::BPM));
  }

  // The channel, from 1, as the view's index from 0.
  UInt ChannelOf(float value) {
    const unsigned int c = Count(value);
    return c > 1 ? c - 1 : 0;
  }

  // The index of `word` in `names`, or -1.
  template <std::size_t N> int Named(std::string_view word, const char* const (&names)[N]) {
    for (std::size_t i = 0; i < N; ++i)
      if (word == names[i]) return static_cast<int>(i);
    return -1;
  }

  const char* const MODES[] = {"none", "select", "loop", "move", "draw"};
  const char* const OUTPUTS[] = {"none", "down", "up", "downup", "continuous"};
  const char* const SNAPS[] = {"none", "grid", "tick", "zero"};
  const char* const UNITS[] = {"ms", "samples", "phase", "bpm"};

  // The number in the token from `from`, NaN when there is none.
  float NumberAt(const std::string& text, std::size_t from, std::size_t& next) {
    const std::string_view token = Token(text, from, next);
    float number = 0.f;
    if (token.empty() || !ReadNumericToken(token.data(), token.size(), number))
      return std::nanf("");
    return number;
  }

  // Appends one number to a reserved string: no allocation within the
  // capacity.
  void Append(std::string& out, const YSE::PATCHER::ExprValue& value) {
    char text[kExprValueTextMax];
    const int written = ExprFormatValue(value, text, kExprValueTextMax);
    if (!out.empty()) out.push_back(' ');
    if (written > 0)
      out.append(text, static_cast<std::size_t>(written));
    else
      out.append("0.");
  }

  void Append(std::string& out, float value) {
    Append(out, YSE::PATCHER::ExprValue::Float(value));
  }

  constexpr std::size_t TEXT_CAPACITY = 256;

} // namespace

// The record a buffer edit becomes. Copied whole into a scheduler slot, so it
// stays trivially copyable.
struct dWaveform::editTask {
  std::uint64_t id = 0;
  EDIT_OP op = EDIT_OP::CROP;
  float a = 0.f;
  float b = 0.f;
  char text[fileScheduler::PATH_CAPACITY] = {}; // a file
};

CONSTRUCT_DSP() {
  chanoffset = 1.f;
  setmode = static_cast<float>(waveformView::SELECT);
  outmode = static_cast<float>(waveformView::OUTPUT_CONTINUOUS);
  snapto = 0.f;
  grid = static_cast<float>(waveformView::DEFAULT_GRID);
  ticks = 0.f;
  offset = 0.f;
  setunit = 0.f;
  bpm = static_cast<float>(waveformView::DEFAULT_TEMPO);
  beats = static_cast<float>(waveformView::DEFAULT_BEATS);
  vzoom = 1.f;
  voffset = 0.f;
  clipdraw = 0.f;
  quiet = 0.f;
  zoomOrientation = 0.f;
  for (std::atomic<float>& cell : cells)
    cell.store(0.f, std::memory_order_relaxed);
  mouseText.reserve(TEXT_CAPACITY);
  linkText.reserve(TEXT_CAPACITY);

  // in 0-3: display start, display length, selection start, selection end
  for (int i = 0; i < 4; ++i) {
    inputs.emplace_back(this, i == 0, i);
    REG_FLOAT_IN(SetFloat);
    REG_INT_IN(SetInt);
    REG_BANG_IN(Bang);
    REG_LIST_IN(SetList);
  }
  // in 4: the link
  ADD_IN_4;
  REG_LIST_IN(SetList);

  ADD_OUT_FLOAT; // display start
  ADD_OUT_FLOAT; // display length
  ADD_OUT_FLOAT; // selection start
  ADD_OUT_FLOAT; // selection end
  ADD_OUT_LIST; // mouse
  ADD_OUT_LIST; // link

  REG_PARM_PARSE;
  ADD_PARAM(bufferName);
  ADD_PARAM(chanoffset);
  ADD_PARAM(setmode);
  ADD_PARAM(outmode);
  ADD_PARAM(snapto);
  ADD_PARAM(grid);
  ADD_PARAM(ticks);
  ADD_PARAM(offset);
  ADD_PARAM(setunit);
  ADD_PARAM(bpm);
  ADD_PARAM(beats);
  ADD_PARAM(vzoom);
  ADD_PARAM(voffset);
  ADD_PARAM(clipdraw);
  ADD_PARAM(quiet);
  ADD_PARAM(zoomOrientation);

  ADD_DESCRIPTION("The view of a waveform display of a ~buffer, for the host to draw: the part "
                  "shown, the selection, and mouse gestures that select, scroll, zoom or draw "
                  "into the samples. Sends the selection and links to other views.");
  ADD_CATEGORY(pCategory::SAMPLING);
  const char* const messages =
      " Also takes the messages: 'mouse <x> <y> <state>', 'drag <x> <y>', 'mode', "
      "'mouseoutput', 'snap', 'unit', 'bpm', 'setbpm', 'line', 'undo', 'name' / 'set', "
      "'crop', 'normalize', 'replacebuffercontents' and every parameter by name.";
  INLET_DOC(0, "display start", std::string("The display start in ms; a bang sends it.") + messages,
            "0 or more ms");
  INLET_DOC(1, "display length",
            "The display length in ms; 0 or less shows the whole buffer. A bang sends it.",
            "any ms");
  INLET_DOC(2, "selection start", "The selection start in ms; a bang sends it.", "0 or more ms");
  INLET_DOC(3, "selection end", "The selection end in ms; a bang sends it.", "0 or more ms");
  INLET_DOC(4, "link",
            "A list 'start length selection-start selection-end' from another ~waveform's "
            "link outlet sets all four.",
            "list of 4 floats");
  OUTLET_DOC(0, "display start", "The display start in ms, when the view moves.", "0 or more ms");
  OUTLET_DOC(1, "display length", "The display length in ms, when the view moves.", "ms");
  OUTLET_DOC(2, "selection start", "The selection start in ms, as outmode says.", "0 or more ms");
  OUTLET_DOC(3, "selection end", "The selection end in ms, as outmode says.", "0 or more ms");
  OUTLET_DOC(4, "mouse",
             "Every mouse event: 'x amplitude state', x 0 to 1 across the display, state 1 "
             "down, 2 drag, 0 up.",
             "list");
  OUTLET_DOC(5, "link",
             "'start length selection-start selection-end' whenever one changes, for another "
             "~waveform's link inlet; 'bpm <tempo> <beats>' after a setbpm.",
             "list");
  PARAM_DOC("name", "", "The buffer shown. A name or set message does not change what is saved.",
            "buffer name");
  PARAM_DOC("chanoffset", "1", "The buffer channel shown and drawn, from 1.", "1 or higher");
  PARAM_DOC("setmode", "1",
            "What the mouse does: 0 nothing, 1 select, 2 move the nearer selection edge, 3 "
            "scroll and zoom, 4 draw into the buffer.",
            "0-4");
  PARAM_DOC("outmode", "4",
            "When a mouse gesture sends the selection: 0 never, 1 on the click, 2 on the release, "
            "3 both, 4 continuously.",
            "0-4");
  PARAM_DOC("snapto", "0", "What a selection snaps to: 0 nothing, 1 the grid, 2 zero crossings.",
            "0-2");
  PARAM_DOC("grid", "1000", "The grid step, in the unit setunit gives.", "any float");
  PARAM_DOC("ticks", "0", "How many ticks divide a grid step; 0 is none.", "0 or more");
  PARAM_DOC("offset", "0", "Where the grid starts, in ms.", "any ms");
  PARAM_DOC("setunit", "0",
            "What the grid counts in: 0 ms, 1 samples, 2 a phase of the buffer, 3 bars at bpm.",
            "0-3");
  PARAM_DOC("bpm", "120", "The tempo in beats a minute, for the bpm unit.", "above 0");
  PARAM_DOC("beats", "4", "The beats in a bar, for the bpm unit and setbpm.", "1 or more");
  PARAM_DOC("vzoom", "1", "The vertical zoom: the amplitude at y is (y - voffset) / vzoom.",
            "above 0");
  PARAM_DOC("voffset", "0", "Where the zero line is drawn, -1 (bottom) to 1 (top).", "any float");
  PARAM_DOC("clipdraw", "0",
            "1 holds drawn values within the amplitudes the display shows; 0 does not.", "0 or 1");
  PARAM_DOC("quiet", "0",
            "1 sends nothing but the mouse while dragging, and what changed on release.", "0 or 1");
  PARAM_DOC("zoom_orientation", "0", "0 zooms in when dragging up, 1 when dragging down.",
            "0 or 1");

  // The GUI value of an object made without arguments, which never parses.
  Publish();
}

void dWaveform::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // The scheduler `name`, `set` and the edits go through, built here so a
  // message on the audio thread finds it.
  EnableFileIO();
}

// Control thread: a change of argument rebuilds the object, so this runs on
// one no other thread has seen.
PARM_PARSE() {
  fn.channel(ChannelOf(chanoffset.load()))
      .mode(ModeOf(setmode.load()))
      .output(OutputOf(outmode.load()))
      .snap(SnapToOf(snapto.load()))
      .grid(grid.load())
      .ticks(Count(ticks.load()))
      .offset(offset.load())
      .unit(UnitOf(setunit.load()))
      .tempo(bpm.load(), Count(beats.load()))
      .verticalZoom(vzoom.load())
      .verticalOffset(voffset.load())
      .clipDraw(clipdraw.load() != 0.f)
      .quiet(quiet.load() != 0.f)
      .zoomDown(zoomOrientation.load() != 0.f);
  binding.Bind(bufferName);
  {
    DSP::sampleWriteView view = binding.Current().pin();
    fn.shape(view.frames(), view.sampleRate());
  }
  Publish();
}

// ─── the guard ────────────────────────────────────────────────────────────────

template <class Body> void dWaveform::Handle(YSE::THREAD thread, Body&& body) {
  // Another thread is in, or this is a send coming back: drop it rather than
  // wait (the writer is one claim, the view one state).
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  binding.Take(*this); // a name or set waiting since the last message
  waveformView::event e;
  {
    DSP::sampleWriteView view = binding.Current().pin();
    fn.shape(view.frames(), view.sampleRate());
    e = body(view);
  } // the view goes before anything is sent
  Publish();
  Send(e, thread);
  busy.store(false, std::memory_order_release);
}

// Right to left.
void dWaveform::Send(const waveformView::event& e, YSE::THREAD thread) {
  if (e.tempo) {
    linkText.clear();
    linkText.append("bpm");
    Append(linkText, static_cast<float>(fn.tempo()));
    Append(linkText, ExprValue::Int(static_cast<int>(fn.beats())));
    outputs[5].SendList(linkText, thread);
  }
  if (e.link) {
    linkText.clear();
    Append(linkText, static_cast<float>(fn.displayStart()));
    Append(linkText, static_cast<float>(fn.displayLength()));
    Append(linkText, static_cast<float>(fn.selectionStart()));
    Append(linkText, static_cast<float>(fn.selectionEnd()));
    outputs[5].SendList(linkText, thread);
  }
  if (e.mouse) {
    mouseText.clear();
    Append(mouseText, e.x);
    Append(mouseText, e.y);
    Append(mouseText, ExprValue::Int(static_cast<int>(e.state)));
    outputs[4].SendList(mouseText, thread);
  }
  if (e.selection) {
    outputs[3].SendFloat(static_cast<float>(fn.selectionEnd()), thread);
    outputs[2].SendFloat(static_cast<float>(fn.selectionStart()), thread);
  }
  if (e.display) {
    outputs[1].SendFloat(static_cast<float>(fn.displayLength()), thread);
    outputs[0].SendFloat(static_cast<float>(fn.displayStart()), thread);
  }
}

// The guard holder (or the control thread before publication): the view
// into the GUI value's atomics.
void dWaveform::Publish() {
  const std::shared_ptr<DSP::sampleBuffer>& shown = binding.Current().buffer();
  bufferId.store(shown != nullptr ? shown->id() : 0, std::memory_order_relaxed);
  const float values[CELLS] = {0.f,
                               0.f,
                               static_cast<float>(fn.channel() + 1),
                               static_cast<float>(fn.displayStart()),
                               fn.wholeBuffer() ? 0.f : static_cast<float>(fn.displayLength()),
                               static_cast<float>(fn.selectionStart()),
                               static_cast<float>(fn.selectionEnd()),
                               static_cast<float>(fn.line()),
                               static_cast<float>(fn.mode()),
                               static_cast<float>(fn.unit()),
                               static_cast<float>(fn.snap()),
                               static_cast<float>(fn.gridSpacing()),
                               static_cast<float>(fn.ticks()),
                               static_cast<float>(fn.offset()),
                               static_cast<float>(fn.tempo()),
                               static_cast<float>(fn.beats()),
                               fn.verticalZoom(),
                               fn.verticalOffset()};
  for (unsigned int i = 2; i < CELLS; ++i)
    cells[i].store(values[i], std::memory_order_relaxed);
  serial.fetch_add(1, std::memory_order_release);
}

// ─── inlets ───────────────────────────────────────────────────────────────────

FLOAT_IN(SetFloat) {
  Handle(thread, [&](DSP::sampleWriteView&) {
    switch (inlet) {
    case 0:
      return fn.displayStart(value);
    case 1:
      return fn.displayLength(value);
    case 2:
      return fn.selectionStart(value);
    case 3:
      return fn.selectionEnd(value);
    default:
      return waveformView::event();
    }
  });
}

INT_IN(SetInt) {
  SetFloat(static_cast<float>(value), inlet, thread);
}

BANG_IN(Bang) {
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  binding.Take(*this);
  {
    DSP::sampleWriteView view = binding.Current().pin();
    fn.shape(view.frames(), view.sampleRate());
  }
  Publish();
  const double values[] = {fn.displayStart(), fn.displayLength(), fn.selectionStart(),
                           fn.selectionEnd()};
  if (inlet >= 0 && inlet < 4) outputs[inlet].SendFloat(static_cast<float>(values[inlet]), thread);
  busy.store(false, std::memory_order_release);
}

LIST_IN(SetList) {
  std::size_t next = 0;
  const std::string_view first = Token(value, 0, next);
  float number = 0.f;
  if (first.empty()) return;
  if (!ReadNumericToken(first.data(), first.size(), number)) {
    // No command channel: words reach this handler, from any inlet.
    Message(value, thread);
    return;
  }
  if (inlet != 4) return;
  float link[4] = {number, 0.f, 0.f, 0.f};
  for (int i = 1; i < 4; ++i)
    link[i] = NumberAt(value, next, next);
  Handle(thread,
         [&](DSP::sampleWriteView&) { return fn.link(link[0], link[1], link[2], link[3]); });
}

// ─── messages ─────────────────────────────────────────────────────────────────

void dWaveform::Message(const std::string& text, YSE::THREAD thread) {
  std::size_t next = 0;
  const std::string_view word = Token(text, 0, next);
  std::size_t after = next;
  const std::string_view arg = Token(text, after, after);
  std::size_t cursor = next;
  const float a = NumberAt(text, cursor, cursor);
  const float b = NumberAt(text, cursor, cursor);
  const float c = NumberAt(text, cursor, cursor);
  const bool hasA = !std::isnan(a);

  Handle(thread, [&](DSP::sampleWriteView& view) {
    waveformView::event none;
    // The mouse.
    if (word == "mouse" || word == "drag") {
      const float state = word == "drag" ? 2.f : c;
      if (std::isnan(a) || std::isnan(b) || !(state == 0.f || state == 1.f || state == 2.f))
        return none;
      return fn.mouse(a, b, static_cast<waveformView::MOUSE>(static_cast<int>(state)), &view);
    }
    // Settings by name.
    if (word == "mode") {
      const int which = Named(arg, MODES);
      if (which >= 0) fn.mode(static_cast<waveformView::MODE>(which));
    } else if (word == "mouseoutput") {
      const int which = Named(arg, OUTPUTS);
      if (which >= 0) fn.output(static_cast<waveformView::OUTPUT>(which));
    } else if (word == "snap") {
      const int which = Named(arg, SNAPS);
      if (which >= 0) fn.snap(static_cast<waveformView::SNAP>(which));
    } else if (word == "unit") {
      const int which = Named(arg, UNITS);
      if (which >= 0) fn.unit(static_cast<waveformView::UNIT>(which));
    } else if (word == "line") {
      const int which = Named(arg, UNITS);
      if (which >= 0)
        fn.lineUnit(static_cast<waveformView::UNIT>(which));
      else if (hasA)
        fn.line(a);
    } else if (word == "bpm") {
      if (hasA) fn.tempo(a, std::isnan(b) ? 0 : Count(b));
    } else if (word == "setbpm") {
      return fn.fitTempo();
    } else if (word == "undo") {
      return fn.undo();
    }
    // Settings by parameter name, as Max sets an attribute.
    else if (word == "setmode") {
      if (hasA) fn.mode(ModeOf(a));
    } else if (word == "outmode") {
      if (hasA) fn.output(OutputOf(a));
    } else if (word == "snapto") {
      if (hasA) fn.snap(SnapToOf(a));
    } else if (word == "grid") {
      if (hasA) fn.grid(a);
    } else if (word == "ticks") {
      if (hasA) fn.ticks(Count(a));
    } else if (word == "offset") {
      if (hasA) fn.offset(a);
    } else if (word == "setunit") {
      if (hasA) fn.unit(UnitOf(a));
    } else if (word == "beats") {
      if (hasA) fn.tempo(0.0, Count(a));
    } else if (word == "vzoom") {
      if (hasA) fn.verticalZoom(a);
    } else if (word == "voffset") {
      if (hasA) fn.verticalOffset(a);
    } else if (word == "clipdraw") {
      if (hasA) fn.clipDraw(a != 0.f);
    } else if (word == "quiet") {
      if (hasA) fn.quiet(a != 0.f);
    } else if (word == "zoom_orientation") {
      if (hasA) fn.zoomDown(a != 0.f);
    } else if (word == "chanoffset") {
      if (hasA) fn.channel(ChannelOf(a));
    }
    // The buffer.
    else if (word == "name" || word == "set") {
      // Bound in the background; the next message or block takes it.
      if (!binding.Request(*this, arg.data(), arg.size())) return none;
      if (!std::isnan(b)) fn.channel(ChannelOf(b));
      return fn.reset();
    } else if (word == "crop") {
      const double lo = std::fmin(fn.selectionStart(), fn.selectionEnd());
      const double hi = std::fmax(fn.selectionStart(), fn.selectionEnd());
      if (!(hi > lo)) return none;
      editTask task;
      task.op = EDIT_OP::CROP;
      task.a = static_cast<float>(lo);
      task.b = static_cast<float>(hi);
      if (!Submit(task)) return none;
      return fn.reset();
    } else if (word == "normalize") {
      editTask task;
      task.op = EDIT_OP::NORMALIZE;
      task.a = hasA ? a : 1.f;
      Submit(task);
    } else if (word == "replacebuffercontents") {
      editTask task;
      if (arg.empty() || arg.size() >= sizeof(task.text)) return none;
      std::memcpy(task.text, arg.data(), arg.size());
      task.text[arg.size()] = '\0';
      task.op = EDIT_OP::REPLACE;
      if (!Submit(task)) return none;
      return fn.reset();
    }
    return none;
  });
}

// ─── edits, in the background ─────────────────────────────────────────────────

bool dWaveform::Submit(editTask& task) {
  fileScheduler* io = FileIO();
  const std::shared_ptr<DSP::sampleBuffer>& shown = binding.Current().buffer();
  if (io == nullptr || shown == nullptr) return false;
  task.id = shown->id();
  if (task.op == EDIT_OP::REPLACE) {
    return io->RequestPathTask(this, 0, PA_READ, task.text, std::strlen(task.text),
                               &dWaveform::RunPathTask, &task, sizeof(task));
  }
  return io->RequestTask(this, 0, &dWaveform::RunTask, &task, sizeof(task));
}

bool dWaveform::RunTask(char* args, std::size_t argBytes) {
  return RunPathTask(args, argBytes, nullptr);
}

// The background pool; never touches the object, which may be gone.
bool dWaveform::RunPathTask(char* args, std::size_t argBytes, const char* path) {
  static_assert(std::is_trivially_copyable<editTask>::value,
                "an editTask is copied into a scheduler slot with memcpy");
  if (argBytes != sizeof(editTask)) return false;
  editTask task;
  std::memcpy(&task, args, sizeof(task));
  task.text[sizeof(task.text) - 1] = '\0';
  std::shared_ptr<DSP::sampleBuffer> buf = DSP::sampleBufferRegistry::findById(task.id);
  if (buf == nullptr) return false;
  switch (task.op) {
  case EDIT_OP::CROP:
    return buf->cropMs(task.a, task.b);
  case EDIT_OP::NORMALIZE:
    buf->normalize(task.a);
    return true;
  case EDIT_OP::REPLACE:
    return buf->replace(path != nullptr ? path : task.text);
  }
  return false;
}

// The top of a block, on the audio thread: an edit has run, or a rebind is
// waiting. Take it and show the buffer as it is now; when a message is being
// handled, it does that itself.
void dWaveform::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  (void)thread;
  if (result.tag != BUFFER_BINDING_TAG && result.op == FILE_OP::TASK && !result.ok)
    failed.fetch_add(1, std::memory_order_relaxed);
  if (busy.exchange(true, std::memory_order_acquire)) return;
  binding.Take(*this);
  {
    DSP::sampleWriteView view = binding.Current().pin();
    fn.shape(view.frames(), view.sampleRate());
  }
  Publish();
  busy.store(false, std::memory_order_release);
}

// ─── the GUI value ────────────────────────────────────────────────────────────

unsigned int dWaveform::GetGuiValueCount() const {
  return CELLS;
}

std::string dWaveform::GetGuiValueAt(unsigned int index) {
  if (index == 0) return std::to_string(serial.load(std::memory_order_acquire));
  if (index == 1) return std::to_string(bufferId.load(std::memory_order_relaxed));
  if (index >= CELLS) return std::string();
  return Cell(cells[index].load(std::memory_order_relaxed));
}

std::string dWaveform::GetGuiValue() {
  std::string out;
  for (unsigned int i = 0; i < CELLS; ++i) {
    if (i > 0) out.push_back(' ');
    out += GetGuiValueAt(i);
  }
  return out;
}

#undef className
