#include "dOverdrive.h"

using namespace YSE::PATCHER;

#define className dOverdrive

CONSTRUCT_DSP() {
  drive = DSP::overdrive::DEFAULT_DRIVE;

  // in 0: the signal to distort
  ADD_IN_0;
  REG_SIGNAL_IN;

  // in 1: the drive, a number or a signal
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetDrive);
  REG_INT_IN(SetDriveInt);

  // out 0: the distorted signal
  ADD_OUT_BUFFER;

  ADD_PARAM(drive);

  ADD_DESCRIPTION("A soft clipper: amplifies the signal by a drive factor and rounds its peaks "
                  "off into -1 to 1, for warm saturation and overdrive.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in", "The signal to distort (unconnected reads 0).", "any float");
  INLET_DOC(1, "drive",
            "The drive factor: 1 is linear, higher values amplify and round off more, values "
            "below 1 distort heavily. A number or a signal read per sample (unconnected, the "
            "last number sent here or the creation argument).",
            "any float, usually 1 to 10");
  OUTLET_DOC(0, "out",
             "The distorted signal: for an input of magnitude a below 1, a magnitude of 1 - (1 - "
             "a) to the power of the drive with the sign of the input; -1 or 1 at or beyond "
             "full scale.",
             "-1 to 1");
  PARAM_DOC("drive", "1",
            "The drive factor ('~overdrive 4'). A number in the right inlet changes it, but "
            "what is saved stays the argument.",
            "any float, usually 1 to 10");
}

// The DSP class ignores a drive that is not finite.
FLOAT_IN(SetDrive) {
  (void)inlet;
  (void)thread;
  drive = value;
}

INT_IN(SetDriveInt) {
  (void)inlet;
  (void)thread;
  drive = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets (#1036): the input reads 0, the drive its number.
  DSP::buffer& out = fn(inputs[0].Signal(0.f), inputs[1].Signal(drive.load()));
  outputs[0].SendBuffer(&out, thread);
}

#undef className
