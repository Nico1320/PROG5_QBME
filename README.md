# PROG5_QBME

Programming 5 assignments using an Arduino UNO Q and a BMP280 temperature/pressure
sensor. Includes the sensor and stepper libraries, MQTT, UML and C++ tests.

The documentation is in [report/main.pdf](report/main.pdf).
Edit [report/main.typ](report/main.typ) and rebuild it with:

```sh
typst compile --root . report/main.typ report/main.pdf
```

Build and run the software tests:

```sh
cmake -S . -B build
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The report contains the UNO Q setup, firmware choices, results and remaining
hardware checks. UML source and test captures are kept under `docs/`.
