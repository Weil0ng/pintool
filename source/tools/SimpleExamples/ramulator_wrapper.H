#include <iostream>
#include "pin.H"

namespace RAMULATOR {

typedef enum {
    REQUEST_TYPE_READ,
    REQUEST_TYPE_WRITE,
    REQUEST_TYPE_NUM
} REQUEST_TYPE;

class Ramulator{
private:
    string _prefix;
    const string _ramulator_family;

    KNOB<string>* _ramulator_knob_config;
public:
    Ramulator(const string prefix = "");
    VOID InitKnobs();
    VOID Access(ADDRINT addr, REQUEST_TYPE type, UINT32 read_complete);
}; // class Ramulator

} // namespace RAMULATOR
