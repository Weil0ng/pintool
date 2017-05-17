#include "ramulator_wrapper.H"

using namespace RAMULATOR;

VOID Ramulator::InitKnobs() {
    _ramulator_knob_config = new KNOB<string>(KNOB_MODE_WRITEONCE,
            _ramulator_family,
            "-config",
            "",
            "Configuration file to ramulator",
            _prefix);
}

Ramulator::Ramulator(const string prefix)
    : _ramulator_family("ramulator") {
    _prefix = prefix;

    InitKnobs();
}

VOID Ramulator::Access(ADDRINT addr, REQUEST_TYPE type, UINT32 read_complete) {
}
