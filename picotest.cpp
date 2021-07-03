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

    multicore_launch_core1(core1_main);

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
    add_alarm_in_ms(2000, alarm_callback, NULL, false);

    //   puts("Hello, world!");

    while (true)
    {
        gpio_put(LED_PIN, (dvi_->getFrameCounter() / 60) & 1);

        uint16_t c0 = dvi_->getFrameCounter();
        for (int y = 0; y < 240; ++y)
        {
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
