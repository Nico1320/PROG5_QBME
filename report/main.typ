#let ink = rgb("#19344a")
#let accent = rgb("#187c80")
#let pale = rgb("#edf4f5")
#let muted = rgb("#536475")
#set document(title: "Prog5 — UNO Q Climate Station", author: "Name / student number")
#set page(
  paper: "a4",
  margin: (x: 23mm, top: 20mm, bottom: 20mm),
  numbering: "1",
  header: context if counter(page).get().first() > 1 {
    text(font: "DejaVu Sans", size: 7.5pt, fill: muted)[
      PROG5 / UNO Q CLIMATE STATION
    ]
  },
)
#set text(font: "Libertinus Serif", size: 10.5pt, lang: "en")
#set par(justify: true, leading: 0.6em)
#set heading(numbering: "1.1")
#show heading: set text(font: "DejaVu Sans", fill: ink)
#show heading.where(level: 1): set text(size: 17pt)
#show heading.where(level: 2): set text(size: 11pt)
#show link: set text(fill: accent)
#show raw: set text(font: "DejaVu Sans Mono", size: 8pt)
#show raw.where(block: true): it => block(
  width: 100%, fill: rgb("#f4f6f8"), inset: 8pt, radius: 3pt,
)[#it]
#set table(
  inset: (x: 6pt, y: 5pt),
  stroke: none,
  fill: (x, y) => if y == 0 { pale } else if calc.odd(y) { rgb("#f8fafb") } else { white },
)
#show table.cell: set par(justify: false)
#let note(body) = block(
  width: 100%, fill: pale, inset: 10pt, stroke: (left: 2pt + accent),
)[#body]
#let detail(body) = text(size: 9pt, fill: muted)[#body]

#v(16mm)
#text(font: "DejaVu Sans", size: 10pt, fill: accent, tracking: 1.3pt)[
  EMBEDDED SYSTEMS ENGINEERING / PROGRAMMING 5
]
#v(8mm)
#text(font: "DejaVu Sans", size: 29pt, weight: "bold", fill: ink)[
  UNO Q\
  Climate Station
]
#v(5mm)
#text(size: 15pt, fill: muted)[Portable C++ sensor library and MQTT application]
#v(10mm)
#table(
  columns: (34mm, 1fr),
  [*Student*], [Name / student number],
  [*Course*], [Programming 5 — P1 lab assignments],
  [*Platform*], [Arduino UNO Q · STM32 + onboard Debian],
  [*Sensor*], [Bosch BMP280 · temperature and pressure],
)
#v(6mm)
#text(font: "DejaVu Sans", weight: "bold", fill: ink)[Abstract]

This project implements a reusable C++ library for BMP280 acquisition and a
station application for periodic MQTT publication. The sensor core is independent
of Arduino and Linux APIs. Platform adapters provide I²C and timing. The same
project includes a basic stepper library, UML models, unit tests and a matrix
display. Verification covers native execution on UNO Q, MCU compilation, MQTT
receipt with synthetic inputs and observed matrix output.

#v(5mm)
#outline(title: [Contents], depth: 1, indent: auto)

#pagebreak()
= Scope and requirements <sec-scope>

The station is designed to acquire temperature and atmospheric pressure, display
the readings and publish them over MQTT. The programming objective is a portable,
testable C++ design rather than a platform-specific sketch. A BMP280 was selected
because it is an I²C sensor supported by the P1 assignment scope. It measures
temperature and pressure. It does not measure humidity. @course @bmp280

The UNO Q provides both execution environments: Arduino C++ on its STM32 and a
native C++ application on onboard Debian. The separate STEP/DIR library addresses
the first P1 assignment. @sec-submission maps the eight assignments to the
submitted files.

== Requirements

Priorities follow MoSCoW: *M* = Must, *S* = Should and *C* = Could.

#table(
  columns: (9mm, 1fr, 11mm),
  table.header([*ID*], [*Requirement and acceptance criterion*], [*Prio.*]),
  [R1], [Acquire BMP280 temperature in °C and pressure in Pa. Check compensation against known raw inputs, then test a connected sensor.], [M],
  [R2], [Build the same sensor core unchanged for STM32 and onboard Linux, without platform headers in the core.], [M],
  [R3], [At x1/x1, request a 7 ms conversion delay and allow at most 100 ms of extra polling delays before returning a timeout.], [M],
  [R4], [Return named errors for unsupported chips, bus faults, invalid calibration and timeouts. Leave the caller's reading unchanged on failure.], [M],
  [R5], [Sample at the configured nominal period and skip overdue slots instead of creating catch-up bursts.], [S],
  [R6], [Publish finite temperature/pressure JSON to a configurable MQTT topic and demonstrate receipt by an independent subscriber.], [M],
  [R7], [Continue sampling after a failed publication and report the dropped sample.], [S],
  [R8], [Wake and join the sampler during shutdown, then release its referenced resources in a valid order.], [M],
  [R9], [Show temperature and pressure on the UNO Q matrix with explicit units and input-mode identification.], [C],
  [R10], [Provide UML, basic C++ tests and a review against DRY, KISS, SOLID, coupling and cohesion.], [M],
)

The delay limits in R3 exclude bus/RPC execution time. Periodic sampling is not a
hard-real-time guarantee. Verification results are in @sec-results.

#note[
  *Platform scope.* The assignment names Raspberry Pi for its Linux component.
  This implementation uses UNO Q onboard Debian. Acceptance of that substitution
  remains to be confirmed with the lecturer.
]

#pagebreak()
= Use cases <sec-use-cases>

#figure(
  image("../docs/diagrams/use-cases.svg", width: 100%, height: 92mm, fit: "contain"),
  caption: [Climate-station boundary and external actors.],
)

The operator configures and runs the station. The MQTT broker is an external
service. Subscribers receive messages through it. The sensor and the two UNO Q
processors are implementation components inside the system.

#table(
  columns: (14mm, 1fr, 1fr),
  table.header([*Case*], [*Normal operation*], [*Alternative / failure flow*]),
  [UC-01], [Initialize BMP280, trigger conversion, compensate and inspect the reading.], [Initialization or sampling failure produces a named error rather than a successful new reading.],
  [UC-02], [Serialize a valid reading, publish to a topic and receive it through the broker.], [A failed publication is reported as dropped. Sampling continues.],
  [UC-03], [Inspect the error and correct firmware, address, wiring or device selection.], [An unsupported chip requires a matching driver, not a changed ID check.],
  [UC-04], [Request stop, wake and join the worker, disconnect MQTT and release resources.], [The current operation finishes before shutdown. This is not a motor emergency stop.],
)

UC-01 starts when the operator runs the application with the sensor connected
and the correct firmware selected. The result is a valid reading or a named error.
Serial, the Linux console and the matrix are alternate output paths.

UC-02 also requires an MQTT-enabled build and a reachable broker. An independent
subscriber confirms receipt. UC-03 is triggered by an error and ends when the
cause is corrected or identified.

UC-04 applies to the Linux station. Ctrl-C, SIGTERM or the end of `--seconds`
requests shutdown. The worker is joined before its dependencies are destroyed.

#pagebreak()
= Hardware and implementation <sec-hardware>

== Platform and wiring

UNO Q combines an STM32U585 microcontroller with a Qualcomm processor running
Debian Linux. The STM32 owns the sensor's physical I²C bus. In the Linux path,
`RouterI2cBus` accesses it through Arduino Router MessagePack RPC. @unoq @router

#table(
  columns: (1fr, 1fr),
  table.header([*UNO Q connection*], [*BMP280 breakout*]),
  [3V3], [Compatible 3.3 V supply input],
  [GND], [GND],
  [Dedicated SDA · D20/PB11], [SDA / SDI],
  [Dedicated SCL · D21/PB10], [SCL / SCK],
)

The sketches use `Wire` for the dedicated headers. Qwiic uses `Wire1`
(PD13/PD12). A4/A5 are a different pin group. I²C pull-ups must
use 3.3 V. CSB selects I²C mode, and SDO selects `0x76` or `0x77`. The production
BMP280 chip ID is `0x58`, a different value from its I²C address. @bmp280

== Sensor algorithm

Construction stores the bus, clock, address and configuration without touching
hardware. `init()` validates the device, writes the reset command, waits for NVM
copy completion, loads 24 calibration bytes and configures sleep mode.

`readForced()` starts a conversion, requests the conversion delay, polls status
with a finite limit and reads six bytes from `0xF7`. The burst keeps pressure
and temperature bytes together. Calibration is little-endian and raw values are
20-bit. Compensation implements the datasheet's floating-point equations and
returns °C and Pa. `tFine` is local, avoiding shared state between sensors.
@bmp280

== Firmware roles

#table(
  columns: (41mm, 1fr),
  table.header([*Sketch*], [*Responsibility*]),
  [`ArduinoClimate`], [Sensor acquisition on STM32, Serial output],
  [`UnoQBridge`], [Raw I²C RPC for the onboard-Linux sensor library],
  [`UnoQMatrixClimate`], [Standalone MCU acquisition/display and status RPC],
  [`ArduinoStepper`], [Separate STEP/DIR pulse-control exercise],
)

Only one MCU image runs at a time. The matrix image is not the raw-I²C bridge.
The full Linux application requires `UnoQBridge`. The matrix is an optional
standalone output path. Its pressure display uses hPa.

== Stepper library

`Stepper` borrows a `StepperIo` adapter and emits blocking STEP/DIR pulses.
Signed moves set direction. Position records commanded pulses, not measured
motion. The Arduino example uses D2 for STEP and D3 for DIR. An external,
3.3 V-compatible driver and a suitable motor supply are required. Coils must
never be connected directly to GPIO.

#pagebreak()
= Software architecture <sec-design>

#figure(
  image("../docs/diagrams/classes.svg", width: 100%, height: 87mm, fit: "contain"),
  caption: [Sensor abstractions and platform implementations.],
)

#figure(
  image("../docs/diagrams/app-classes.svg", width: 100%, height: 78mm, fit: "contain"),
  caption: [Application dependencies and resource ownership.],
)

`Bmp280` borrows `I2cBus` and `Clock`. It owns its calibration state.
`Sampler` borrows an `EnvironmentSensor` and owns its thread. `main()` composes
the sensor, publisher and sampler, keeping concrete choices outside the core.

The bus and clock interfaces separate transfer from timing. Console and MQTT
publishers share one output contract. This limits coupling: changes to a platform
or publisher do not require a second sensor algorithm. References must outlive
their users. Destruction joins the worker before destroying callback targets.

#pagebreak()
= Sampling and publication

This section describes the implemented acquisition design. The measured paths
and their input sources are identified in the results section.

#figure(
  image("../docs/diagrams/sample-sequence.svg", width: 100%, height: 164mm, fit: "contain"),
  caption: [Designed acquisition and publication sequence using the raw-I²C firmware.],
)

The sampler is the only sensor caller after initialization. Its callback
serializes the measurement and invokes the publisher on the same worker thread.
The period wait uses a condition variable. Stop requests wake the wait and join
the thread after any operation in progress. Shared MQTT connection state is atomic.

The STM32 executes `Wire` requests in loop context through `provide_safe`.
The socket client handles partial messages and bounds each RPC request, but a
timeout does not cancel MCU work already queued. @bridge

MQTT uses QoS 0 without retention or offline storage. Successful publication
means queued, not confirmed delivered. Initial setup failure switches to console
output with a failure exit status. Established clients use libmosquitto
reconnection. The supplied configuration is local-only. @mosquitto

#pagebreak()
= Verification and results <sec-results>

== Environment

#table(
  columns: (38mm, 1fr),
  table.header([*Component*], [*Recorded version*]),
  [Native execution], [UNO Q · Debian 13 ARM64 · GCC 14.2],
  [Build and messaging], [CMake 3.31.6 · Mosquitto 2.0.21],
  [Arduino toolchain], [Arduino CLI 1.5.1 · UNO Q Zephyr core 0.90.0],
  [MCU communication], [RouterBridge 0.4.4 · RPClite 0.3.1 · MsgPack 0.4.2],
)

== Results

#table(
  columns: (40mm, 1fr),
  table.header([*Check*], [*Result and interpretation*]),
  [Native build], [Full router/MQTT application builds on UNO Q.],
  [Basic C++ tests], [All 3 CTest entries pass, including grouped sensor, stepper, application, matrix and router checks (simulated peer).],
  [Sensor compensation], [Known raw inputs produce approximately 25.08248 °C and 100653.26 Pa. Tests cover byte order, errors and polling limits.],
  [MQTT integration], [An independent subscriber received two valid JSON payloads through the onboard broker. One sample was dropped before the connection was ready.],
  [MCU build], [All four Arduino sketches compile and link for `arduino:zephyr:unoq`.],
  [Matrix operation], [Upload and status RPC pass. Visible scrolling was confirmed with synthetic inputs.],
  [Additional checking], [Host build/tests pass with address and undefined-behavior sanitizers.],
)

Physical BMP280 and motor hardware were not connected. Software and MQTT tests
used synthetic inputs. The raw-I²C sensor path, sensor-derived display/MQTT,
physical timing and motor motion remain to be verified. Broker-loss recovery
was not established by the recorded MQTT capture.

== MCU resource use

#table(
  columns: (1fr, 29mm, 27mm),
  table.header([*Sketch*], [*Program (B)*], [*RAM (B)*]),
  [`ArduinoClimate`], [83,772], [31,832],
  [`ArduinoStepper`], [80,012], [29,924],
  [`UnoQBridge`], [84,716], [31,442],
  [`UnoQMatrixClimate`], [86,912], [33,014],
)

#detail[
  Build-region limits: 786,432 program bytes and 262,144 RAM bytes. These are
  whole-program figures. RAM is global/static usage, not runtime stack/heap.
  They do not isolate interface overhead.
  Raw captures are listed in @sec-evidence.
]

#pagebreak()
= Design review and refactoring <sec-review>

The review separates architecture decisions from changes made after reviewing
the first implementation. It covers DRY, KISS, SOLID, coupling and cohesion.

== Removing repeated register encoding

Both initialization and forced acquisition originally encoded the same control
register fields independently. The expressions below show the duplication.

```cpp
// Initialization: oversampling fields, sleep mode
(code(config_.temperature) << 5) |
(code(config_.pressure) << 2)

// Forced acquisition: repeated fields plus a literal mode
(code(config_.temperature) << 5) |
(code(config_.pressure) << 2) | 1
```

The encoding now has one implementation, with named modes:

```cpp
std::uint8_t controlByte(const BmpConfig& config,
                         std::uint8_t mode) {
    return static_cast<std::uint8_t>(
        (code(config.temperature) << 5) |
        (code(config.pressure) << 2) | mode);
}
```

This improves *DRY*: a layout change has one source. Named `sleepMode` and
`forcedMode` improve readability. Regression tests still check `0x24`/`0x25`
for the default configuration.

#table(
  columns: (35mm, 1fr, 1fr),
  table.header([*Other refactor*], [*Before*], [*After*]),
  [Chip constants], [The constructor and `init()` repeated `0x76`, `0x77` and `0x58`.], [Named address and chip-ID constants in `Bmp280.hpp`.],
  [Sketch bus selection], [The adapter and bus startup each named `Wire`.], [Both use a single `sensorWire` reference.],
)

== Principle review

#table(
  columns: (28mm, 1fr),
  table.header([*Principle*], [*Application and trade-off*]),
  [SRP / cohesion], [Sensor interpretation, transfer, scheduling, publication and display rendering have focused responsibilities.],
  [OCP / DIP], [Core users depend on injected interfaces. Concrete platform and publisher types are selected outside the core.],
  [LSP], [Substitutes must honor full-transfer success and unchanged output on acquisition failure.],
  [ISP], [Timing is separate from byte transfer. Motor I/O exposes only its required operations.],
  [DRY / KISS], [One compensation core serves both processors. Forced-mode acquisition and synchronous RPC keep the scope small.],
  [Lifecycle], [The sampler joins its thread. Socket and MQTT resources have owners. Borrowed references are not deleted.],
)

== Error handling changes

These changes affect behavior rather than just structure:

#table(
  columns: (1fr, 1fr),
  table.header([*Change*], [*Check*]),
  [Sensor exceptions become `BusFailure` callbacks rather than escaping the worker.], [Application test with a throwing sensor.],
  [RPC parser sizes and nesting are limited.], [Simulated peer sends a malformed array header.],
  [MQTT setup failure keeps console sampling active and returns failure on exit.], [Code review of `main()`.],
  [Moves that would overflow accumulated stepper position are rejected before pulses.], [Code review. Tests cover signed step-count boundaries, not the full accumulated range.],
  [Embedded NUL socket paths and empty CLI fields are rejected.], [Code review.],
)

Two small DRY issues remain: calibration sanity checks appear in two functions,
and CLI address validation still repeats the numeric addresses. Other limits
are blocking motor moves, the fixed temperature/pressure contract and the
unmeasured cost of `double` on the STM32.

#pagebreak()
= Build and run <sec-running>

Run commands from the project root. The Arduino sketches target the STM32.
The Linux station must be compiled on the UNO Q or with an appropriate ARM
toolchain.

== Arduino setup

Install the Arduino UNO Q Zephyr core, version 0.90.0, through Boards Manager.
The recorded CLI version is 1.5.1. Core 0.90.0 also needs RouterBridge for Serial.
Keep the library dependencies in a project-local sketchbook:

```sh
export ARDUINO_DIRECTORIES_USER="$PWD/.arduino-user"
arduino-cli lib update-index
arduino-cli lib install --no-deps \
  Arduino_RouterBridge@0.4.4 Arduino_RPClite@0.3.1 \
  MsgPack@0.4.2 ArxContainer@0.7.0 \
  ArxTypeTraits@0.3.2 DebugLog@0.8.4
```

Keep the environment variable set when compiling. For example:

```sh
arduino-cli compile --fqbn arduino:zephyr:unoq \
  --library "$PWD" examples/ArduinoClimate
```

Use the same command with `examples/UnoQBridge`, `examples/ArduinoStepper` or
`examples/UnoQMatrixClimate` for the other sketches. Upload the selected sketch
with the UNO Q tooling. Only one sketch runs at a time. Open Serial Monitor at
115200 baud for the sensor example.

Select `sensorWire = Wire` for the dedicated headers or `Wire1` for Qwiic in the
sensor and matrix sketches. In `UnoQBridge`, change both the bus constructor and
`Wire.begin()` when selecting a different bus. Use address `0x77` if SDO is high.

For the matrix, `PROG5_MATRIX_DEMO=0` enables BMP280 acquisition. The source
default is `1`, which uses labelled synthetic values. Hardware errors display
`BMP280 ERROR` rather than falling back to generated readings. The matrix
alternates temperature in C and pressure in hPa, with a scroll step of about
80 ms. Query the displayed snapshot with:

```sh
arduino-router-cli prog5_matrix_status
```

The separate stepper example uses a 2000 µs half-period, or nominally 250 pulses
per second before software overhead. Identify the driver before wiring it.
Check 3.3 V input compatibility, pulse timing, current limit, enable wiring and
the motor supply. Do not power the motor from a GPIO or the 3V3 pin.

== Native Linux build

Install these packages on the UNO Q from a root terminal:

```sh
apt-get update
apt-get install -y build-essential cmake pkg-config \
  libmsgpack-cxx-dev libmosquitto-dev \
  mosquitto mosquitto-clients
```

Build and test as the normal board user:

```sh
cmake -S . -B build-uno-q -DCMAKE_BUILD_TYPE=Debug \
  -DPROG5_WITH_UNO_Q=ON -DPROG5_WITH_MQTT=ON
cmake --build build-uno-q --parallel 2
ctest --test-dir build-uno-q --output-on-failure
```

For sensor acquisition, upload `UnoQBridge` and connect the BMP280. The Linux
user needs read/write access to `/var/run/arduino-router.sock`, normally through
the `arduino-router` group. `/dev/ttyHS1` is reserved for the router. Do not run
competing programs that reconfigure the same sensor.

```sh
./build-uno-q/station --seconds 10 --address 0x76
```

== MQTT

Use the local Mosquitto broker. Replace `YOUR-NAME` in both commands. Start the
subscriber in one terminal:

```sh
mosquitto_sub -h localhost -v \
  -t 'han/ese/YOUR-NAME/bmp280/state'
```

Run the station in another:

```sh
./build-uno-q/station --mqtt --host localhost \
  --topic 'han/ese/YOUR-NAME/bmp280/state'
```

Payload fields are `temperature_c` and `pressure_pa`. Pressure remains in Pa in
the JSON, even though the matrix displays hPa:

```json
{"temperature_c":25.08,"pressure_pa":100653.26}
```

Stop with Ctrl-C or add `--seconds 10`. Set the period with `--period-ms`.
Use `--fake` for software validation without a sensor. It bypasses the hardware
transport and generates input for the same sampling and publication path.

#pagebreak()
= Submission and evidence <sec-submission>

== P1 assignment mapping

#table(
  columns: (9mm, 1fr),
  table.header([*Lab*], [*Submitted work*]),
  [1], [`Stepper`, its Arduino adapter, motor example and pulse tests. Physical motor movement is still to be checked.],
  [2], [Use-case diagram and descriptions in @sec-use-cases.],
  [3], [`Bmp280`, `ArduinoI2cBus` and the Arduino sensor example. The sketch compiles. Physical acquisition is still to be checked.],
  [4], [The same sensor core with `RouterI2cBus` on onboard Debian. Native build and simulated-peer tests pass. Physical I²C is still to be checked.],
  [5], [Sampling callback and C++ MQTT publisher. Independent receipt was checked using synthetic input.],
  [6], [Class, use-case and sequence diagrams, with editable PlantUML sources.],
  [7], [Basic C++ tests run through CTest.],
  [8], [Design review, before/after register encoding and regression checks in @sec-review.],
)

Before submission, enter the student details, confirm the platform substitution,
add the wiring photograph and physical sensor/motor results, then rebuild the
report. Submit through Classroom or the agreed Git repository and give
`jakorten` access. Include the source, tests, UML, report and relevant captures.
Leave out build directories, downloaded toolchains, SSH material and MCU backups.

== Evidence files <sec-evidence>

The raw UNO Q captures are in `docs/evidence/uno-q/`.

#table(
  columns: (36mm, 1fr),
  table.header([*Check*], [*Capture file*]),
  [Native build], [`configure.txt`, `build.txt`],
  [Native tests], [`ctest.txt`, `unit-tests.txt`, `matrix-ctest.txt`],
  [MQTT receipt], [`mqtt-simulation.txt`],
  [Arduino sensor build], [`arduino-ArduinoClimate.txt`],
  [Stepper build], [`arduino-ArduinoStepper.txt`],
  [I²C bridge build], [`arduino-UnoQBridge.txt`],
  [Matrix build], [`arduino-UnoQMatrixClimate.txt`],
  [Matrix runtime], [`matrix-upload.txt`, `matrix-status.txt`],
)

The host sanitizer result is `docs/evidence/local-full-ctest.txt`. Captures retain
their original input labels. Visible matrix operation was also confirmed directly.

= Conclusion

The project demonstrates the P1 software progression: a basic motor library,
sensor use cases, portable Arduino/Linux C++, MQTT publication, UML, tests and
a traceable design review. Reusing one sensor core through small interfaces is
the central result. Native execution and MQTT receipt were verified with
synthetic inputs, and the student observed matrix output. The raw-I²C firmware
was compiled, but its physical sensor path remains untested.

The remaining engineering work is physical validation: connect the selected
BMP280 and motor driver, verify acquisition and motion, and capture sensor-derived
messages. The remaining submission items are listed in @sec-submission, and @sec-running
contains the commands to reproduce the results.

#bibliography("references.bib", style: "ieee")
