// JeeH-like wrappers

template< int N >
struct Pin {
    static constexpr int pin = N;

    static void mode (int v) { pinMode(N, v); }

    operator int () const { return digitalRead(N); }
    void operator= (int v) const { digitalWrite(N, v); }
};

namespace Pinmode {
    constexpr int in_pullup = INPUT_PULLUP;
    constexpr int out = OUTPUT;
}

void wait_ms (uint32_t ms) {
    delay(ms);
}
