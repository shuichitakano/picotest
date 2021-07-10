#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/divider.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/interp.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <memory>
#include <math.h>

#include "dvi/dvi.h"

const uint LED_PIN = PICO_DEFAULT_LED_PIN;

int64_t alarm_callback(alarm_id_t id, void *user_data)
{
    //    printf("alarm called%d.\n", id);
    // Put your timeout handler code in here
    gpio_put(LED_PIN, 0);
    return 0;
}

// (16 + 96 + 48 + 640) * (10 + 2 + 33 + 480) * 59.94 * 10 = 251748

namespace
{
    constexpr dvi::Config dviConfig_ = {
        .pinTMDS = {10, 12, 14},
        .pinClock = 8,
        .invert = true,
    };

    std::unique_ptr<dvi::DVI> dvi_;
}

void __not_in_flash_func(core1_main)()
{
    dvi_->registerIRQThisCore();
    dvi_->waitForValidLine();

    dvi_->start();
    dvi_->loopScanBuffer16bpp();
}

int main()
{
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    //    set_sys_clock_khz(251750, true);
    set_sys_clock_khz(252000, true);

    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_put(LED_PIN, 1);

    //
    dvi_ = std::make_unique<dvi::DVI>(pio0, &dviConfig_, dvi::getTiming640x480p60Hz());
    dvi_->setAudioFreq(48000, 25200, 6144);
    dvi_->allocateAudioBuffer(1024);
    // multicore_launch_core1(core1_main);

#if 0
    int32_t dividend = 123456;
    int32_t divisor = -321;
    // This is the recommended signed fast divider for general use.
    divmod_result_t result = hw_divider_divmod_s32(dividend, divisor);
    printf("%d/%d = %d remainder %d\n", dividend, divisor, to_quotient_s32(result), to_remainder_s32(result));
    // This is the recommended unsigned fast divider for general use.
    int32_t udividend = 123456;
    int32_t udivisor = 321;
    divmod_result_t uresult = hw_divider_divmod_u32(udividend, udivisor);
    printf("%d/%d = %d remainder %d\n", udividend, udivisor, to_quotient_u32(uresult), to_remainder_u32(uresult));

    // Interpolator example code
    interp_config cfg = interp_default_config();
    // Now use the various interpolator library functions for your use case
    // e.g. interp_config_clamp(&cfg, true);
    //      interp_config_shift(&cfg, 2);
    // Then set the config
    interp_set_config(interp0, 0, &cfg);
#endif

    // Timer example code - This example fires off the callback after 2000ms
    // add_alarm_in_ms(2000, alarm_callback, NULL, false);

    //   puts("Hello, world!");

    std::vector<int16_t> sinTable(1024);
    for (int i = 0; i < 1024; ++i)
    {
        float t = i * 3.14159f * 2 / 1024;
        sinTable[i] = int(sin(t) * 1024);
    }

    int phase = 0;
    auto updateSample = [&] {
        auto &ring = dvi_->getAudioRingBuffer();
        auto n = ring.getWritableSize();
        //        printf("ws %d\n", n);
        auto p = ring.getWritePointer();
        auto ct = n;
        while (ct--)
        {
#if 0
            float t = (float)phase * (3.1415927f * 2 / 65536.0f);
            auto v = static_cast<int16_t>(sin(t) * 1024);
#else
            //            int16_t v = phase < 32768 ? 1024 : -1024;
            auto v = sinTable[phase >> 6];
#endif
            *p++ = {v, v};
            //            printf("%d %d %d\n", phase, v, phase >> 5);

            constexpr int step = 440 * 65536 / 48000;
            phase = (phase + step) & 65535;
        }
        //        phase &= 65535;
        ring.advanceWritePointer(n);
        //        printf("advance %d, ws %d\n", n, ring.getWritableSize());
    };

    updateSample();
    updateSample();

    multicore_launch_core1(core1_main);
    while (true)
    {
        gpio_put(LED_PIN, (dvi_->getFrameCounter() / 60) & 1);

        uint16_t c0 = dvi_->getFrameCounter();
        for (int y = 0; y < 240; ++y)
        {
            updateSample();

            auto c = c0;
            auto line = dvi_->getLineBuffer();
            for (auto &v : *line)
            {
                v = c++;
            }
            dvi_->setLineBuffer(line);
            ++c0;
        }
    }
    return 0;
}
