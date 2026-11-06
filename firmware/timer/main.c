// Four-channel timer for the muon detector (Raspberry Pi Pico 2 / Pico 2 W).
//
// Each input (GP2-GP5) has a PIO state machine that records the value of a counter at every rising
// edge. The counters decrease by 1 every 2 system-clock cycles (13.3 ns at 150 MHz) and start together,
// so the times of the four channels can be compared with each other.
// One DMA channel per input empties the PIO FIFO into a ring buffer; the CPU turns the values into
// 64-bit times and sends them to the PC over USB serial, one line per event:
//     channel,time_in_steps
// Lines starting with # are header and status lines (every 10 s): they are not events.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "timer.pio.h"

#define VERSION "timer 0.1 (13 October 2026)"
#define N_CHANNELS 4
#define PIN_BASE 2                 // GP2..GP5, as on the base board PCB-402
#define RING_BITS 12               // ring buffer of 2^12 words (16 KB) per channel
#define RING_SIZE (1u << RING_BITS)
#define TRANSFERS 0x0FFFFFFFu      // DMA transfers before it has to be re-armed (268 million events)
#define WRAP_MARKER 0xFFFFFFFFu

static uint32_t ring[N_CHANNELS][RING_SIZE] __attribute__((aligned(RING_SIZE * 4)));
static int dma_ch[N_CHANNELS];
static uint32_t read_count[N_CHANNELS];   // words read from each ring
static uint32_t last[N_CHANNELS];         // last counter value, to count wraps
static uint64_t wraps[N_CHANNELS];
static uint64_t events[N_CHANNELS];
static uint64_t lost;                     // words overwritten in the ring before being read

static void start_dma(PIO pio, uint sm, int c) {
    dma_ch[c] = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_ch[c]);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_ring(&cfg, true, RING_BITS + 2);   // the write address wraps every 16 KB
    channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
    dma_channel_configure(dma_ch[c], &cfg, ring[c], &pio->rxf[sm], TRANSFERS, true);
}

static uint32_t written(int c) {
    return TRANSFERS - dma_channel_hw_addr(dma_ch[c])->transfer_count;
}

int main(void) {
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    PIO pio = pio0;
    uint offset = pio_add_program(pio, &timer_program);
    for (int c = 0; c < N_CHANNELS; c++) {
        uint pin = PIN_BASE + c;
        pio_gpio_init(pio, pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);            // unconnected input = low
        pio_sm_set_consecutive_pindirs(pio, c, pin, 1, false);
        pio_sm_config cfg = timer_program_config(offset, pin);
        pio_sm_init(pio, c, offset + timer_offset_idle, &cfg);
        pio_sm_exec(pio, c, pio_encode_mov_not(pio_x, pio_null));   // X = 0xFFFFFFFF
        last[c] = WRAP_MARKER;
        start_dma(pio, c, c);
    }
    pio->fdebug = 0xFFFFFFFFu;          // clear the FIFO-full flags
    pio_enable_sm_mask_in_sync(pio, (1u << N_CHANNELS) - 1);
    absolute_time_t start = get_absolute_time();

    uint32_t f = clock_get_hz(clk_sys);
    absolute_time_t next_status = make_timeout_time_ms(1000);
    absolute_time_t led_off = nil_time;
    bool header_sent = false;

    while (true) {
        // header as soon as the PC opens the port (and again every time it reopens it)
        if (stdio_usb_connected() && !header_sent) {
            printf("# %s\n# clock %lu Hz, step 2 cycles = %.4f ns, channels GP%d-GP%d\n# channel,time_in_steps\n",
                   VERSION, (unsigned long)f, 2e9 / f, PIN_BASE, PIN_BASE + N_CHANNELS - 1);
            header_sent = true;
        }
        if (!stdio_usb_connected()) header_sent = false;

        for (int c = 0; c < N_CHANNELS; c++) {
            uint32_t w = written(c);
            if (w - read_count[c] > RING_SIZE) {      // the reader fell behind: data overwritten
                lost += (w - read_count[c]) - RING_SIZE;
                read_count[c] = w - RING_SIZE;
            }
            while (read_count[c] != w) {
                uint32_t v = ring[c][read_count[c] & (RING_SIZE - 1)];
                read_count[c]++;
                if (v > last[c]) wraps[c]++;           // the counter goes down: an increase is a wrap
                last[c] = v;
                if (v == WRAP_MARKER) continue;        // only the wrap signal, not an event
                uint64_t t = (wraps[c] << 32) | (uint64_t)(WRAP_MARKER - v);
                events[c]++;
                printf("%d,%llu\n", c, (unsigned long long)t);
                gpio_put(PICO_DEFAULT_LED_PIN, 1);
                led_off = make_timeout_time_ms(30);
            }
        }
        if (!is_nil_time(led_off) && absolute_time_diff_us(led_off, get_absolute_time()) > 0) {
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            led_off = nil_time;
        }
        if (absolute_time_diff_us(next_status, get_absolute_time()) > 0) {
            uint32_t stall = (pio->fdebug >> PIO_FDEBUG_RXSTALL_LSB) & 0xF;
            printf("# status,%llu s,events %llu %llu %llu %llu,lost %llu,pio_fifo_full %lx\n",
                   (unsigned long long)(absolute_time_diff_us(start, get_absolute_time()) / 1000000),
                   (unsigned long long)events[0], (unsigned long long)events[1],
                   (unsigned long long)events[2], (unsigned long long)events[3],
                   (unsigned long long)lost, (unsigned long)stall);
            next_status = make_timeout_time_ms(10000);
        }
    }
}
