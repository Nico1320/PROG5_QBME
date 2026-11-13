#include "prog5/Hal.hpp"

namespace prog5 {

const char* errorName(Error error) {
    switch (error) {
    case Error::Ok: return "Ok";
    case Error::InvalidArgument: return "InvalidArgument";
    case Error::BusFailure: return "BusFailure";
    case Error::WrongChipId: return "WrongChipId";
    case Error::InvalidCalibration: return "InvalidCalibration";
    case Error::NotInitialised: return "NotInitialised";
    case Error::Timeout: return "Timeout";
    case Error::InvalidData: return "InvalidData";
    }
    return "Unknown";
}

} // namespace prog5
