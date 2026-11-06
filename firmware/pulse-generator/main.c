// Test pulse generator for the timer (Raspberry Pi Pico 2).
// Outputs: GP2 and GP3 together (connect GP2 of the generator to GP2 of the timer, and ground).
// Commands on the USB serial port, one letter:
//   d  pairs at known distances: 1, 2, 5, 10, 20 us, 1000 pairs each
//   e  10000 pairs with exponentially distributed distance, mean 2.197 us, up to 20 us
//   1, 2, 3  20000 random pulses at 1, 10, 100 kHz on average (load test)
//   h  help
// Without a PC (for example powered by the timer, with a single USB cable): if no computer enumerates the
// USB port within 3 s of power-up, it repeats one sequence by itself every 5 s. The sequence is chosen
// with a jumper:
//   none: d (known distances);  GP14 to ground: e (exponential);  GP15 to ground: 1 (1 kHz load).
// Before sending the pulses it prints the exact list of rising edges:
//     g,index,time_in_cycles      (time since the first edge, in system-clock cycles)
// so that the same list can be compared with the timer output.

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "pulse_generator.pio.h"
#include "tusb.h"

#define VERSION "pulse-generator 0.2 (13 October 2026)"
#define PIN_OUT 2
#define MAX_PULSES 40000
#define SEED 20261012u
#define PIN_SELECT_E 14
#define PIN_SELECT_LOAD 15

static uint32_t delays[MAX_PULSES];   // words for the PIO: interval - fixed cycles
static uint32_t n_pulses;
static uint64_t rng_state;
static double f_clk;

static double uniform(void) {         // xorshift64*, number in (0, 1)
    rng_state ^= rng_state >> 12; rng_state ^= rng_state << 25; rng_state ^= rng_state >> 27;
    uint64_t r = rng_state * 2685821657736338717ull;
    return ((r >> 11) + 0.5) * (1.0 / 9007199254740992.0);
}

static void add(double interval_s) {  // interval since the previous edge
    uint32_t cycles = (uint32_t)llround(interval_s * f_clk);
    if (cycles < PULSE_GENERATOR_FIXED_CYCLES + 8) cycles = PULSE_GENERATOR_FIXED_CYCLES + 8;  // at least 34 cycles
    if (n_pulses < MAX_PULSES) delays[n_pulses++] = cycles - PULSE_GENERATOR_FIXED_CYCLES;
}

static void prepare(char mode) {
    n_pulses = 0;
    rng_state = SEED;
    if (mode == 'd') {
        const double distances_us[5] = {1, 2, 5, 10, 20};
        for (int k = 0; k < 5; k++)
            for (int i = 0; i < 1000; i++) {
                add(100e-6 + 10e-6 * uniform());   // pause between pairs, random phase
                add(distances_us[k] * 1e-6);
            }
    } else if (mode == 'e') {
        for (int i = 0; i < 10000; i++) {
            add(200e-6 + 200e-6 * uniform());
            double d;
            do d = -2.197e-6 * log(uniform()); while (d > 20e-6);
            add(d);
        }
    } else {
        double r = mode == '1' ? 1e3 : mode == '2' ? 1e4 : 1e5;
        for (int i = 0; i < 20000; i++) add(-log(uniform()) / r);
    }
}

static void print_list(char mode) {
    printf("# %s, mode %c, seed %u, clock %.0f Hz, %lu pulses %d cycles high\n",
           VERSION, mode, SEED, f_clk, (unsigned long)n_pulses, 22);
    printf("# g,index,time_in_cycles\n");
    uint64_t t = 0;
    for (uint32_t i = 0; i < n_pulses; i++) {
        if (i > 0) t += delays[i] + PULSE_GENERATOR_FIXED_CYCLES;
        printf("g,%lu,%llu\n", (unsigned long)i, (unsigned long long)t);
    }
}

int main(void) {
    stdio_init_all();
    f_clk = clock_get_hz(clk_sys);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &pulse_generator_program);
    pio_gpio_init(pio, PIN_OUT);
    pio_gpio_init(pio, PIN_OUT + 1);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_OUT, 2, true);
    pio_sm_config cfg = pulse_generator_program_config(offset, PIN_OUT);
    pio_sm_init(pio, sm, offset, &cfg);
    pio_sm_set_enabled(pio, sm, true);
    int dma = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));

    gpio_init(PIN_SELECT_E); gpio_pull_up(PIN_SELECT_E);
    gpio_init(PIN_SELECT_LOAD); gpio_pull_up(PIN_SELECT_LOAD);
    absolute_time_t deadline = make_timeout_time_ms(3000);
    // "connected to a PC" = the USB port has been enumerated by a computer (even with no terminal open)
    while (!tud_mounted() && absolute_time_diff_us(get_absolute_time(), deadline) > 0) sleep_ms(10);
    if (!tud_mounted()) {
        // standalone: same sequence every 5 s, LED on while sending
        char mode = !gpio_get(PIN_SELECT_E) ? 'e' : !gpio_get(PIN_SELECT_LOAD) ? '1' : 'd';
        prepare(mode);
        while (true) {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            dma_channel_configure(dma, &dc, &pio->txf[sm], delays, n_pulses, true);
            dma_channel_wait_for_finish_blocking(dma);
            while (!pio_sm_is_tx_fifo_empty(pio, sm)) tight_loop_contents();
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            sleep_ms(5000);
        }
    }

    while (true) {
        int ch = getchar_timeout_us(100000);
        if (ch == PICO_ERROR_TIMEOUT) continue;
        if (ch == 'h' || ch == '?') {
            printf("# %s. Commands: d (known distances), e (exponential), 1/2/3 (load 1/10/100 kHz)\n", VERSION);
            continue;
        }
        if (ch != 'd' && ch != 'e' && ch != '1' && ch != '2' && ch != '3') continue;
        prepare((char)ch);
        print_list((char)ch);
        printf("# start\n");
        sleep_ms(100);
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        dma_channel_configure(dma, &dc, &pio->txf[sm], delays, n_pulses, true);
        dma_channel_wait_for_finish_blocking(dma);
        while (!pio_sm_is_tx_fifo_empty(pio, sm)) tight_loop_contents();
        sleep_ms(1);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        printf("# end\n");
    }
}
