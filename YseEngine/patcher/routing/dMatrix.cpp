#include "dMatrix.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"

#include <cmath>
#include <limits>

using namespace YSE::PATCHER;

#define className dMatrix

CONSTRUCT_DSP() {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(arguments);

  // Room for one dump line: two port numbers and a gain.
  line.reserve(3 * kExprValueTextMax);

  ShapePorts();

  ADD_DESCRIPTION("Signal routing and mixing matrix: every input reaches every output at a gain "
                  "of its own, set by messages, and each output is the sum of its connected "
                  "inputs. Gain changes ramp, so switching does not click.");
  ADD_CATEGORY(pCategory::ROUTING);
  PARAM_DOC("arguments", "1 1 1",
            "The number of inputs and of outputs (1-128 each) and the gain 'connect' uses, "
            "with 'ramp <ms>' (default 10) and 'exclusive <0-3>' among them ('~matrix 4 2 1 "
            "ramp 50'). Later messages and the connections are not saved.",
            "inputs, outputs, gain, then the keywords");
}

void dMatrix::ShapePorts() {
  // Rebuilt rather than resized: the pin counts come from the arguments.
  // Every caller runs before the object is wired or published.
  const matrixControl::shape shape = control.Parse(arguments);
  fn = DSP::mixMatrix(shape.inputs, shape.outputs, shape.defaultGain);
  snapshot.assign(static_cast<std::size_t>(shape.inputs) * shape.outputs, 0.f);
  dumpAt = 0;
  dumping = false;
  pendingDumps = matrixControl::DUMP_NONE;

  inputs.clear();
  outputs.clear();

  // in 0: input 0, and the connection messages
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_LIST_IN(SetList);
  INLET_DOC(0, "in 0",
            "Input 0 (unconnected reads silence), and the connection messages, which number "
            "inputs and outputs from 0: '<in> <out> <gain> [<ramp ms>]' sets a crosspoint (gain 0 "
            "disconnects); 'connect <in> <out> [<out> ...] [<gain>]' connects at the gain, "
            "given as a float, or at the default gain; 'disconnect <in> <out> [<out> ...]'; "
            "'clear'; 'ramp <ms>' (alias 'ramptime') for later changes; 'exclusive <0-3>' (1: "
            "an input reaches one output, 2: an output hears one input, 3: both; the new "
            "connection wins); 'dump', 'dumptarget' and 'dumpconnections' send the crosspoints "
            "out the rightmost outlet.",
            "");

  // in 1 to N-1: the other inputs
  for (UInt i = 1; i < fn.inputs(); ++i) {
    inputs.emplace_back(this, false, static_cast<int>(i));
    REG_SIGNAL_IN;
    const std::string n = std::to_string(i);
    inputs.back().SetDoc("in " + n,
                         "Input " + n +
                             ", added to every output it is connected to at that "
                             "crosspoint's gain (unconnected reads silence).",
                         "");
  }

  // out 0 to M-1: the outputs
  for (UInt o = 0; o < fn.outputs(); ++o) {
    ADD_OUT_BUFFER;
    const std::string n = std::to_string(o);
    outputs.back().SetDoc("out " + n,
                          "Output " + n +
                              ": the sum of the inputs connected to it, each times "
                              "its gain.",
                          "");
  }

  // out M: the dumps
  ADD_OUT_LIST;
  outputs.back().SetDoc("dump",
                        "One '<in> <out> <gain>' list per crosspoint, by input and then by "
                        "output, after 'dump' (the current gain of every crosspoint connected "
                        "or still fading out) or 'dumptarget' / 'dumpconnections' (the gain "
                        "every connection is heading for). Fed back in after 'clear', the "
                        "lines restore the routing.",
                        "");
}

PARM_CLEAR() {
  arguments.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  control.List(value);
}

MESSAGES() {
  (void)value;
  control.Message(message);
}

CALC() {
  // Unconnected inputs are silent, passed as null so they cost nothing.
  control.Apply(fn);
  for (UInt i = 0; i < fn.inputs(); ++i) {
    inlet& in = inputs[i];
    sources[i] = in.IsConstant() ? nullptr : &in.Signal(0.f);
  }
  fn(sources.data());
  for (UInt o = 0; o < fn.outputs(); ++o)
    outputs[o].SendBuffer(&fn.output(o), thread);

  // A dump takes the matrix as it is at the end of this block; one asked
  // for during a dump waits for it to end.
  pendingDumps |= control.TakeDumps();
  if (!dumping && pendingDumps != matrixControl::DUMP_NONE) {
    const bool current = (pendingDumps & matrixControl::DUMP_CURRENT) != 0;
    pendingDumps &= current ? ~matrixControl::DUMP_CURRENT : ~matrixControl::DUMP_TARGET;
    const UInt outs = fn.outputs();
    for (UInt in = 0; in < fn.inputs(); ++in) {
      for (UInt out = 0; out < outs; ++out) {
        const Flt target = fn.target(in, out);
        const Flt now = fn.gain(in, out);
        Flt& entry = snapshot[(static_cast<std::size_t>(in) * outs) + out];
        if (current)
          entry = target != 0.f || now != 0.f ? now : std::numeric_limits<Flt>::quiet_NaN();
        else
          entry = target != 0.f ? target : std::numeric_limits<Flt>::quiet_NaN();
      }
    }
    dumpAt = 0;
    dumping = true;
  }
  // Never Send* a control message from CALC(): the patcher sends the dump
  // after the traversal, through FlushArmedMessages.
  if (dumping) MarkOutletArmed();
}

// After the block's traversal, on the audio thread: this block's share of the
// dump.
void dMatrix::FlushArmedMessages(YSE::THREAD thread) {
  if (!dumping) return;
  const UInt outs = fn.outputs();
  outlet& out = outputs[outs];
  UInt sent = 0;
  while (dumpAt < snapshot.size() && sent < DUMP_LINES_PER_BLOCK) {
    const std::size_t at = dumpAt++;
    const Flt gain = snapshot[at];
    if (std::isnan(gain)) continue;
    char number[kExprValueTextMax];
    line.clear();
    std::size_t size = WriteInt(static_cast<int>(at / outs), number);
    line.append(number, size);
    line.push_back(' ');
    size = WriteInt(static_cast<int>(at % outs), number);
    line.append(number, size);
    line.push_back(' ');
    const int written = ExprFormatValue(ExprValue::Float(gain), number, kExprValueTextMax);
    if (written > 0) line.append(number, static_cast<std::size_t>(written));
    out.SendList(line, thread);
    ++sent;
  }
  // With more to send, the next block's CALC() arms the rest.
  if (dumpAt >= snapshot.size()) dumping = false;
}

#undef className
