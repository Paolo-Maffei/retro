// Show a running light across the three main connectors, in proper order.
// This needs something like the Digilent 8-LED PMOD to see that it works.

#include <jee.h>

template< class T >
void blink (T pin) {
    pin = 1;
    wait_ms(100);
    pin = 0;
}

int main() {
    enableSysTick();

    Port<'A'>::modeMap(0b1000000011111111, Pinmode::out);
    Port<'B'>::modeMap(0b1111111111111011, Pinmode::out);

    // disable JTAG in AFIO-MAPR to release PB3, PB4, and PA15
    // (looks like this has to be done *after* the modeMap GPIO inits!)
    constexpr uint32_t afio = 0x40010000;
    MMIO32(afio+0x04) |= (2<<24); // disable JTAG, keep SWD enabled

    while (true) {
        { PinB<12> pin; blink(pin); }
        { PinB<15> pin; blink(pin); }
        { PinB<14> pin; blink(pin); }
        { PinB<13> pin; blink(pin); }

        { PinB<9>  pin; blink(pin); }
        { PinB<11> pin; blink(pin); }
        { PinB<10> pin; blink(pin); }
        { PinB<8>  pin; blink(pin); }

        { PinA<15> pin; blink(pin); }
        { PinB<5>  pin; blink(pin); }
        { PinB<4>  pin; blink(pin); }
        { PinB<3>  pin; blink(pin); }

        { PinB<1>  pin; blink(pin); }
        { PinB<7>  pin; blink(pin); }
        { PinB<6>  pin; blink(pin); }
        { PinB<0>  pin; blink(pin); }

        { PinA<4>  pin; blink(pin); }
        { PinA<7>  pin; blink(pin); }
        { PinA<6>  pin; blink(pin); }
        { PinA<5>  pin; blink(pin); }

        { PinA<1>  pin; blink(pin); }
        { PinA<3>  pin; blink(pin); }
        { PinA<2>  pin; blink(pin); }
        { PinA<0>  pin; blink(pin); }
    }
}
