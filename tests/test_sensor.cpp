#include "Test.hpp"
#include "prog5/Bmp280.hpp"

#include <cstdint>
#include <cstring>

using namespace prog5;

namespace {

// Project regression vector, cross-checked against the independent integer
// formulae in datasheet section 3.11.3; not a datasheet worked example.
BmpCalibration regressionCalibration() {
    BmpCalibration c;
    c.t1 = 27504; c.t2 = 26435; c.t3 = -1000;
    c.p1 = 36477; c.p2 = -10685; c.p3 = 3024; c.p4 = 2855;
    c.p5 = 140; c.p6 = -7; c.p7 = 15500; c.p8 = -14600; c.p9 = 6000;
    return c;
}
const BmpCalibration kCal = regressionCalibration();
const std::uint32_t kRawT = 519888;   // raw 20-bit temperature reading
const std::uint32_t kRawP = 415148;   // raw 20-bit pressure reading


// Registers 0x88..0x9F are little-endian: 27504 = 0x6B70, -1000 = 0xFC18.
void basicDecodeCalibration() {
    const std::uint8_t bytes[24] = {
        0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC,               // T1, T2, T3
        0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B, 0x27, 0x0B,   // P1..P4
        0x8C, 0x00, 0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6,   // P5..P8
        0x70, 0x17};                                      // P9
    BmpCalibration c;
    CHECK(decodeCalibration(bytes, 24, c) == Error::Ok);
    CHECK(c.t1 == 27504);
    CHECK(c.t3 == -1000);
    CHECK(c.p6 == -7);
    CHECK(c.p9 == 6000);
}

void basicDecodeRejectsWrongLength() {
    const std::uint8_t bytes[10] = {};
    BmpCalibration c;
    CHECK(decodeCalibration(bytes, 10, c) == Error::InvalidArgument);
}

void basicCompensation() {
    Measurement m;
    CHECK(compensate(kCal, kRawT, kRawP, m) == Error::Ok);
    CHECK_NEAR(m.temperatureC, 25.08248, 0.0001);
    CHECK_NEAR(m.pressurePa, 100653.26, 0.01);   // Pa, about 1006.5 hPa
}

// 0x80000 means "measurement skipped"; preserve the caller's previous value.
void basicCompensationRejectsSkipped() {
    Measurement m;
    m.temperatureC = 20.0;
    CHECK(compensate(kCal, 0x80000, kRawP, m) == Error::InvalidData);
    CHECK(m.temperatureC == 20.0);
}

// Default oversampling (x1/x1): 6.4 ms maximum, rounded to a 7 ms wait.
void basicMeasurementTime() {
    CHECK(measurementTimeUs(BmpConfig{}) == 7000);
}

class UnusedBus final : public I2cBus {
public:
    bool read(std::uint8_t, std::uint8_t, std::uint8_t*, std::size_t) override { return false; }
    bool write(std::uint8_t, std::uint8_t, const std::uint8_t*, std::size_t) override { return false; }
};
class UnusedClock final : public Clock {
public:
    void delayUs(std::uint32_t) override {}
};
void basicReadBeforeInit() {
    UnusedBus bus;
    UnusedClock clock;
    Bmp280 sensor(bus, clock);
    Measurement m;
    CHECK(!sensor.isInitialised());
    CHECK(sensor.readForced(m) == Error::NotInitialised);
}


void putLe16(std::uint8_t* out, int value) {
    const auto u = static_cast<std::uint16_t>(value & 0xFFFF);
    out[0] = static_cast<std::uint8_t>(u & 0xFF);
    out[1] = static_cast<std::uint8_t>(u >> 8);
}

void encodeCalibration(const BmpCalibration& c, std::uint8_t* out) {
    const int values[12] = {c.t1, c.t2, c.t3, c.p1, c.p2, c.p3,
                            c.p4, c.p5, c.p6, c.p7, c.p8, c.p9};
    for (int i = 0; i < 12; ++i) putLe16(out + 2 * i, values[i]);
}

void putRaw20(std::uint8_t* out, std::uint32_t raw) {
    out[0] = static_cast<std::uint8_t>((raw >> 12) & 0xFF);
    out[1] = static_cast<std::uint8_t>((raw >> 4) & 0xFF);
    out[2] = static_cast<std::uint8_t>((raw & 0x0F) << 4);
}

// Datasheet section 3.11.3 integer reference (32-bit T, 64-bit P), used only
// as an independent oracle for the floating-point implementation.
void integerReference(const BmpCalibration& c, std::int32_t adcT, std::int32_t adcP,
                      std::int32_t& centiC, std::uint32_t& paQ24_8) {
    std::int32_t var1 = (((adcT >> 3) - (static_cast<std::int32_t>(c.t1) << 1)) *
                         static_cast<std::int32_t>(c.t2)) >> 11;
    const std::int32_t d = (adcT >> 4) - static_cast<std::int32_t>(c.t1);
    std::int32_t var2 = (((d * d) >> 12) * static_cast<std::int32_t>(c.t3)) >> 14;
    const std::int32_t tFine = var1 + var2;
    centiC = (tFine * 5 + 128) >> 8;

    std::int64_t v1 = static_cast<std::int64_t>(tFine) - 128000;
    std::int64_t v2 = v1 * v1 * c.p6;
    v2 = v2 + ((v1 * c.p5) * (static_cast<std::int64_t>(1) << 17));
    v2 = v2 + (static_cast<std::int64_t>(c.p4) * (static_cast<std::int64_t>(1) << 35));
    v1 = ((v1 * v1 * c.p3) >> 8) + ((v1 * c.p2) * (static_cast<std::int64_t>(1) << 12));
    v1 = (((static_cast<std::int64_t>(1) << 47) + v1) * c.p1) >> 33;
    std::int64_t p = 1048576 - adcP;
    p = ((p * (static_cast<std::int64_t>(1) << 31)) - v2) * 3125 / v1;
    v1 = (static_cast<std::int64_t>(c.p9) * (p >> 13) * (p >> 13)) >> 25;
    v2 = (static_cast<std::int64_t>(c.p8) * p) >> 19;
    p = ((p + v1 + v2) >> 8) + (static_cast<std::int64_t>(c.p7) * 16);
    paQ24_8 = static_cast<std::uint32_t>(p);
}


class FakeBus final : public I2cBus {
public:
    std::uint8_t regs[256] = {};
    std::uint8_t presentAddress = bmp280AddressSdoLow;
    int busyStatusReads = 0;   // status reads that report busy before clearing
    bool statusStuck = false;  // status never clears
    int failReadOf = -1;       // register whose read fails (-1: none)
    int failWriteOf = -1;      // register whose write fails (-1: none)
    int reads = 0, statusReads = 0, writes = 0;
    std::uint8_t lastControl = 0, lastConfig = 0xFF;
    bool resetSeen = false;
    bool forcedSeen = false;

    FakeBus() {
        regs[0xD0] = bmp280ChipId;
        encodeCalibration(kCal, regs + 0x88);
        putRaw20(regs + 0xF7, kRawP);
        putRaw20(regs + 0xFA, kRawT);
    }

    bool read(std::uint8_t address, std::uint8_t reg, std::uint8_t* data,
              std::size_t length) override {
        ++reads;
        if (address != presentAddress || !data || length == 0) return false;
        if (reg == failReadOf) return false;
        if (reg + length > 256) return false;
        if (reg == 0xF3 && length == 1) {
            ++statusReads;
            const bool busy = statusStuck || busyStatusReads > 0;
            if (busyStatusReads > 0) --busyStatusReads;
            *data = busy ? 0x09 : 0x00;
            return true;
        }
        std::memcpy(data, regs + reg, length);
        return true;
    }

    bool write(std::uint8_t address, std::uint8_t reg, const std::uint8_t* data,
               std::size_t length) override {
        ++writes;
        if (address != presentAddress || !data || length != 1) return false;
        if (reg == failWriteOf) return false;
        if (reg == 0xE0 && data[0] == 0xB6) resetSeen = true;
        if (reg == 0xF5) lastConfig = data[0];
        if (reg == 0xF4) {
            lastControl = data[0];
            if ((data[0] & 0x03) == 0x01) forcedSeen = true;
        }
        return true;
    }
};

class FakeClock final : public Clock {
public:
    std::uint64_t totalUs = 0;
    int calls = 0;
    void delayUs(std::uint32_t duration) override {
        totalUs += duration;
        ++calls;
    }
};


void testDecodeCalibration() {
    std::uint8_t bytes[24];
    encodeCalibration(kCal, bytes);
    BmpCalibration c;
    CHECK(decodeCalibration(bytes, sizeof bytes, c) == Error::Ok);
    CHECK(c.t1 == 27504 && c.t2 == 26435 && c.t3 == -1000);
    CHECK(c.p1 == 36477 && c.p2 == -10685 && c.p3 == 3024 && c.p4 == 2855);
    CHECK(c.p5 == 140 && c.p6 == -7 && c.p7 == 15500 && c.p8 == -14600 && c.p9 == 6000);

    // Little-endian byte order and sign extension at the extremes.
    std::uint8_t edge[24] = {};
    edge[0] = 0xFE; edge[1] = 0xFF;   // t1 = 65534 (unsigned)
    edge[2] = 0x00; edge[3] = 0x80;   // t2 = -32768
    edge[4] = 0xFF; edge[5] = 0x7F;   // t3 = 32767
    edge[6] = 0x01; edge[7] = 0x00;   // p1 = 1
    edge[8] = 0xFF; edge[9] = 0xFF;   // p2 = -1
    CHECK(decodeCalibration(edge, sizeof edge, c) == Error::Ok);
    CHECK(c.t1 == 65534 && c.t2 == -32768 && c.t3 == 32767 && c.p1 == 1 && c.p2 == -1);

    // Errors leave the output untouched.
    BmpCalibration untouched = kCal;
    CHECK(decodeCalibration(nullptr, 24, untouched) == Error::InvalidArgument);
    CHECK(decodeCalibration(bytes, 23, untouched) == Error::InvalidArgument);
    CHECK(decodeCalibration(bytes, 25, untouched) == Error::InvalidArgument);
    std::uint8_t zeros[24] = {};
    CHECK(decodeCalibration(zeros, 24, untouched) == Error::InvalidCalibration);
    std::uint8_t ones[24];
    std::memset(ones, 0xFF, sizeof ones);
    CHECK(decodeCalibration(ones, 24, untouched) == Error::InvalidCalibration);
    CHECK(untouched.t1 == kCal.t1 && untouched.p9 == kCal.p9);
}

void testCompensationVector() {
    Measurement m;
    CHECK(compensate(kCal, kRawT, kRawP, m) == Error::Ok);
    CHECK_NEAR(m.temperatureC, 25.08248, 0.0001);
    CHECK_NEAR(m.pressurePa, 100653.26, 0.01);

    std::int32_t centiC = 0;
    std::uint32_t paQ = 0;
    integerReference(kCal, static_cast<std::int32_t>(kRawT),
                     static_cast<std::int32_t>(kRawP), centiC, paQ);
    CHECK(centiC == 2508);
    // Float and integer datasheet variants agree within their resolution.
    CHECK_NEAR(m.temperatureC, centiC / 100.0, 0.01);
    CHECK_NEAR(m.pressurePa, paQ / 256.0, 1.0);
}

void testCompensationRejectsBadInput() {
    Measurement m;
    m.temperatureC = -1.5;
    m.pressurePa = 12345.0;
    // 0x80000 is the datasheet's "measurement skipped" value.
    CHECK(compensate(kCal, 0x80000, kRawP, m) == Error::InvalidData);
    CHECK(compensate(kCal, kRawT, 0x80000, m) == Error::InvalidData);
    CHECK(compensate(kCal, 0x100000, kRawP, m) == Error::InvalidData);
    CHECK(compensate(kCal, kRawT, 0x100000, m) == Error::InvalidData);
    CHECK(compensate(BmpCalibration{}, kRawT, kRawP, m) == Error::InvalidCalibration);
    // Physically implausible results (outside -40..85 C / 300..1100 hPa).
    CHECK(compensate(kCal, 0xFFFFF, kRawP, m) == Error::InvalidData);
    CHECK(compensate(kCal, kRawT, 0, m) == Error::InvalidData);
    CHECK(m.temperatureC == -1.5 && m.pressurePa == 12345.0);
}

void testMeasurementTime() {
    CHECK(measurementTimeUs(BmpConfig{}) == 7000);
    // Datasheet rev 1.26 Table 13 "Max" column: {osrs_p, osrs_t, max us}.
    struct Row { Oversampling p, t; std::uint32_t maxUs; };
    const Row rows[] = {
        {Oversampling::X1, Oversampling::X1, 6400},
        {Oversampling::X2, Oversampling::X1, 8700},
        {Oversampling::X4, Oversampling::X1, 13300},
        {Oversampling::X8, Oversampling::X1, 22500},
        {Oversampling::X16, Oversampling::X2, 43200},
    };
    for (const auto& row : rows) {
        BmpConfig config;
        config.pressure = row.p;
        config.temperature = row.t;
        const auto us = measurementTimeUs(config);
        CHECK(us >= row.maxUs && us <= row.maxUs + 1000 && us % 1000 == 0);
    }
    BmpConfig biggest;
    biggest.pressure = biggest.temperature = Oversampling::X16;
    CHECK(measurementTimeUs(biggest) == 76000);
    BmpConfig invalid;
    invalid.pressure = static_cast<Oversampling>(0); // "skipped" is unsupported
    CHECK(measurementTimeUs(invalid) == 0);
    invalid.pressure = static_cast<Oversampling>(6);
    CHECK(measurementTimeUs(invalid) == 0);
}

void testErrorNames() {
    CHECK(std::strcmp(errorName(Error::Ok), "Ok") == 0);
    CHECK(std::strcmp(errorName(Error::NotInitialised), "NotInitialised") == 0);
    CHECK(std::strcmp(errorName(Error::InvalidData), "InvalidData") == 0);
    CHECK(std::strcmp(errorName(static_cast<Error>(99)), "Unknown") == 0);
}


void testConstructionAndBeforeInit() {
    FakeBus bus;
    FakeClock clock;
    Bmp280 sensor(bus, clock);
    CHECK(bus.reads == 0 && bus.writes == 0 && clock.calls == 0); // no hardware in ctor
    CHECK(!sensor.isInitialised());
    Measurement m;
    m.temperatureC = 1.0;
    CHECK(sensor.readForced(m) == Error::NotInitialised);
    CHECK(m.temperatureC == 1.0);
    CHECK(bus.reads == 0 && bus.writes == 0);
}

void testInitAndForcedRead(std::uint8_t address) {
    FakeBus bus;
    bus.presentAddress = address;
    bus.busyStatusReads = 2; // NVM copy still running briefly after reset
    FakeClock clock;
    Bmp280 sensor(bus, clock, address);
    CHECK(sensor.init() == Error::Ok);
    CHECK(sensor.isInitialised());
    CHECK(bus.resetSeen);
    CHECK(bus.lastConfig == 0x00);         // IIR filter off
    CHECK(bus.lastControl == 0x24);        // osrs_t=x1, osrs_p=x1, sleep
    CHECK(!bus.forcedSeen);

    bus.busyStatusReads = 3;               // conversion still running
    Measurement m;
    CHECK(sensor.readForced(m) == Error::Ok);
    CHECK(bus.forcedSeen && bus.lastControl == 0x25);
    CHECK_NEAR(m.temperatureC, 25.08248, 0.0001);
    CHECK_NEAR(m.pressurePa, 100653.26, 0.01);
}

void testOversamplingControlByte() {
    FakeBus bus;
    FakeClock clock;
    BmpConfig config;
    config.temperature = Oversampling::X2;
    config.pressure = Oversampling::X16;
    Bmp280 sensor(bus, clock, bmp280AddressSdoLow, config);
    CHECK(sensor.init() == Error::Ok);
    CHECK(bus.lastControl == ((2 << 5) | (5 << 2)));
    clock.totalUs = 0;
    Measurement m;
    CHECK(sensor.readForced(m) == Error::Ok);
    CHECK(bus.lastControl == ((2 << 5) | (5 << 2) | 1));
    CHECK(clock.totalUs >= 43200); // waited at least Table 13 max before reading
}

void testInitErrors() {
    {   // Invalid address: rejected before any bus traffic.
        FakeBus bus;
        FakeClock clock;
        Bmp280 sensor(bus, clock, 0x60);
        CHECK(sensor.init() == Error::InvalidArgument);
        CHECK(bus.reads == 0 && bus.writes == 0);
    }
    {   // Invalid oversampling code.
        FakeBus bus;
        FakeClock clock;
        BmpConfig config;
        config.pressure = static_cast<Oversampling>(0);
        Bmp280 sensor(bus, clock, bmp280AddressSdoLow, config);
        CHECK(sensor.init() == Error::InvalidArgument);
        CHECK(bus.reads == 0);
    }
    {   // Nothing at the configured address (sensor strapped to 0x77).
        FakeBus bus;
        bus.presentAddress = bmp280AddressSdoHigh;
        FakeClock clock;
        Bmp280 sensor(bus, clock, bmp280AddressSdoLow);
        CHECK(sensor.init() == Error::BusFailure);
        CHECK(!sensor.isInitialised());
    }
    {   // BME280 (0x60) is not a BMP280.
        FakeBus bus;
        bus.regs[0xD0] = 0x60;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::WrongChipId);
        CHECK(!bus.resetSeen);
    }
    {   // Reset write fails.
        FakeBus bus;
        bus.failWriteOf = 0xE0;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::BusFailure);
    }
    {   // NVM copy never finishes: bounded wait, then Timeout.
        FakeBus bus;
        bus.statusStuck = true;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::Timeout);
        CHECK(!sensor.isInitialised());
        CHECK(bus.statusReads == 21);                 // 20000 us / 1000 + 1
        CHECK(clock.totalUs == 3000 + 20000);
    }
    {   // Calibration read fails.
        FakeBus bus;
        bus.failReadOf = 0x88;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::BusFailure);
    }
    {   // Blank calibration (e.g. bus returning zeros).
        FakeBus bus;
        std::memset(bus.regs + 0x88, 0, 24);
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::InvalidCalibration);
        CHECK(!sensor.isInitialised());
    }
    {   // Re-init after a failure clears the initialised flag first.
        FakeBus bus;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::Ok);
        bus.regs[0xD0] = 0x00;
        CHECK(sensor.init() == Error::WrongChipId);
        CHECK(!sensor.isInitialised());
        Measurement m;
        CHECK(sensor.readForced(m) == Error::NotInitialised);
    }
}

void testReadErrors() {
    {   // Conversion never completes: bounded poll, previous value kept.
        FakeBus bus;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::Ok);
        bus.statusStuck = true;
        bus.statusReads = 0;
        clock.totalUs = 0;
        Measurement m;
        m.pressurePa = 99999.0;
        CHECK(sensor.readForced(m) == Error::Timeout);
        CHECK(m.pressurePa == 99999.0);
        CHECK(bus.statusReads == 101);               // 100000 us / 1000 + 1
        CHECK(clock.totalUs == 7000 + 100000);
    }
    {   // Data burst read fails.
        FakeBus bus;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::Ok);
        bus.failReadOf = 0xF7;
        Measurement m;
        CHECK(sensor.readForced(m) == Error::BusFailure);
    }
    {   // Forced-mode trigger write fails.
        FakeBus bus;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::Ok);
        bus.failWriteOf = 0xF4;
        Measurement m;
        CHECK(sensor.readForced(m) == Error::BusFailure);
    }
    {   // Skipped measurement value from the data registers.
        FakeBus bus;
        FakeClock clock;
        Bmp280 sensor(bus, clock);
        CHECK(sensor.init() == Error::Ok);
        putRaw20(bus.regs + 0xF7, 0x80000);
        Measurement m;
        m.temperatureC = 3.0;
        CHECK(sensor.readForced(m) == Error::InvalidData);
        CHECK(m.temperatureC == 3.0);
    }
}

} // namespace

void runSensorTests() {
    basicDecodeCalibration();
    basicDecodeRejectsWrongLength();
    basicCompensation();
    basicCompensationRejectsSkipped();
    basicMeasurementTime();
    basicReadBeforeInit();

    testDecodeCalibration();
    testCompensationVector();
    testCompensationRejectsBadInput();
    testMeasurementTime();
    testErrorNames();
    testConstructionAndBeforeInit();
    testInitAndForcedRead(bmp280AddressSdoLow);
    testInitAndForcedRead(bmp280AddressSdoHigh);
    testOversamplingControlByte();
    testInitErrors();
    testReadErrors();
}
