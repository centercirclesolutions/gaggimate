// Host-native mock of the XL9555 I/O-expander lib (lewisxhe/SensorLib) so the
// controller peripheral TUs that #include it compile under `pio test`. The gear
// pump (#427) is the only real user; on a standard controller the chip is absent
// and gearpumpAddon stays null, so these stubs are never exercised on device.
#ifndef MOCK_EXTENSION_IO_XL9555_HPP
#define MOCK_EXTENSION_IO_XL9555_HPP

#include <cstdint>

#ifndef INPUT
#define INPUT 0
#define OUTPUT 1
#endif
#ifndef LOW
#define LOW 0
#define HIGH 1
#endif

static constexpr uint8_t XL9555_SLAVE_ADDRESS0 = 0x20;
static constexpr uint8_t XL9555_SLAVE_ADDRESS1 = 0x21;
static constexpr uint8_t XL9555_SLAVE_ADDRESS2 = 0x22;
static constexpr uint8_t XL9555_SLAVE_ADDRESS3 = 0x23;
static constexpr uint8_t XL9555_SLAVE_ADDRESS4 = 0x24;
static constexpr uint8_t XL9555_SLAVE_ADDRESS5 = 0x25;
static constexpr uint8_t XL9555_SLAVE_ADDRESS6 = 0x26;
static constexpr uint8_t XL9555_SLAVE_ADDRESS7 = 0x27;

class ExtensionIOXL9555 {
  public:
    enum { IO0 = 0, IO1, IO2, IO3, IO4, IO5, IO6, IO7 };
    bool begin(uint8_t = 0, int = -1, int = -1) { return false; }
    void pinMode(uint8_t, uint8_t) {}
    void digitalWrite(uint8_t, uint8_t) {}
    uint8_t digitalRead(uint8_t) { return 0; }
};

#endif // MOCK_EXTENSION_IO_XL9555_HPP
