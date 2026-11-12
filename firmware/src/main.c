/*
 * Blipper - ANT+ Controls page-turn remote for Garmin Edge
 *
 * Target : nice!nano v2 (nRF52840) + S340 SoftDevice (ANT + BLE)
 * Profile: ANT+ Controls. The Edge is the channel master (device type 16);
 *          this remote is the slave and sends "Generic Command" data page 73
 *          as acknowledged messages.
 *
 * Bare metal: only the SoftDevice API and nRF MDK register headers are used.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "nrf.h"
#include "nrf_sdm.h"
#include "nrf_soc.h"
#include "nrf_nvic.h"
#include "ant_interface.h"
#include "ant_parameters.h"

#include "config.h"

#define MS_TO_TICKS(ms)     (((ms) * TICK_HZ + 999) / 1000)
#define LONG_PRESS_TICKS    MS_TO_TICKS(LONG_PRESS_MS)
#define CMD_TIMEOUT_TICKS   MS_TO_TICKS(CMD_QUEUE_TIMEOUT_MS)
#define SLEEP_TICKS         ((uint32_t)SLEEP_WHEN_UNPAIRED_S * TICK_HZ)

#define PAGE_REQUEST        0x46    /* common page 70 */
#define PAGE_GENERIC_CMD    0x49    /* page 73        */
#define PAGE_MANUFACTURER   0x50    /* common page 80 */
#define PAGE_PRODUCT        0x51    /* common page 81 */

/* Required by the nrf_nvic.h inline helpers. */
nrf_nvic_state_t nrf_nvic_state = {0};

/* ------------------------------------------------------------------------- */
/* GPIO helpers                                                              */
/* ------------------------------------------------------------------------- */
static NRF_GPIO_Type *gpio_port(uint32_t pin)
{
    return (pin < 32) ? NRF_P0 : NRF_P1;
}

static void pin_cfg_input_pullup(uint32_t pin, bool sense_low)
{
    gpio_port(pin)->PIN_CNF[pin & 31] =
        (GPIO_PIN_CNF_DIR_Input       << GPIO_PIN_CNF_DIR_Pos)   |
        (GPIO_PIN_CNF_INPUT_Connect   << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Pullup     << GPIO_PIN_CNF_PULL_Pos)  |
        (GPIO_PIN_CNF_DRIVE_S0S1      << GPIO_PIN_CNF_DRIVE_Pos) |
        ((sense_low ? GPIO_PIN_CNF_SENSE_Low : GPIO_PIN_CNF_SENSE_Disabled)
                                      << GPIO_PIN_CNF_SENSE_Pos);
}

static void pin_cfg_output(uint32_t pin)
{
    gpio_port(pin)->PIN_CNF[pin & 31] =
        (GPIO_PIN_CNF_DIR_Output       << GPIO_PIN_CNF_DIR_Pos)   |
        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Disabled    << GPIO_PIN_CNF_PULL_Pos)  |
        (GPIO_PIN_CNF_DRIVE_S0S1       << GPIO_PIN_CNF_DRIVE_Pos);
}

static bool pin_is_low(uint32_t pin)
{
    return ((gpio_port(pin)->IN >> (pin & 31)) & 1u) == 0;
}

static void led_set(bool on)
{
    if (on) gpio_port(PIN_LED)->OUTSET = 1u << (PIN_LED & 31);
    else    gpio_port(PIN_LED)->OUTCLR = 1u << (PIN_LED & 31);
}

/* ------------------------------------------------------------------------- */
/* nice!nano: run the 3.3 V rail from REG0 (UICR defaults to 1.8 V after a   */
/* full chip erase, which is too low to light the blue LED).                 */
/* ------------------------------------------------------------------------- */
static void regout0_ensure_3v3(void)
{
    /* Only relevant in high-voltage mode (board powered through VDDH). */
    if ((NRF_POWER->MAINREGSTATUS & POWER_MAINREGSTATUS_MAINREGSTATUS_Msk) == 0)
        return;

    if ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk) ==
        (UICR_REGOUT0_VOUT_DEFAULT << UICR_REGOUT0_VOUT_Pos))
    {
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
        NRF_UICR->REGOUT0 = (NRF_UICR->REGOUT0 & ~UICR_REGOUT0_VOUT_Msk) |
                            (UICR_REGOUT0_VOUT_3V3 << UICR_REGOUT0_VOUT_Pos);
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
        NVIC_SystemReset();   /* UICR changes take effect after reset */
    }
}

/* ------------------------------------------------------------------------- */
/* LED patterns                                                              */
/* ------------------------------------------------------------------------- */
static uint8_t  m_led_blinks;       /* blinks still to show */
static uint8_t  m_led_on_ticks;
static uint8_t  m_led_phase;        /* ticks left in current on/off phase */
static bool     m_led_is_on;

static void led_blink(uint8_t count, uint8_t on_ticks)
{
    m_led_blinks   = count;
    m_led_on_ticks = on_ticks;
    m_led_phase    = 0;
    m_led_is_on    = false;
}

static void led_tick(void)
{
    if (m_led_phase) { m_led_phase--; return; }
    if (m_led_is_on) {
        led_set(false);
        m_led_is_on = false;
        m_led_phase = 5;              /* 100 ms off between blinks */
    } else if (m_led_blinks) {
        m_led_blinks--;
        led_set(true);
        m_led_is_on = true;
        m_led_phase = m_led_on_ticks;
    }
}

/* ------------------------------------------------------------------------- */
/* ANT channel                                                               */
/* ------------------------------------------------------------------------- */
typedef enum { CH_CLOSED, CH_SEARCHING, CH_TRACKING } ch_state_t;

#define ANT_BURST_BUF   64
#define ANT_EVT_QUEUE   16
static uint8_t m_ant_mem[ANT_ENABLE_GET_REQUIRED_SPACE(1, 0, ANT_BURST_BUF, ANT_EVT_QUEUE)]
    __attribute__((aligned(4)));

static ch_state_t m_ch_state = CH_CLOSED;
static uint16_t   m_serial16;

static void ant_channel_open(void)
{
    if (m_ch_state != CH_CLOSED) return;
    if (sd_ant_channel_open(ANT_CHANNEL) == NRF_SUCCESS)
        m_ch_state = CH_SEARCHING;
}

static void ant_setup(void)
{
    static const uint8_t netkey[8] = ANTPLUS_NETWORK_KEY;

    ANT_ENABLE cfg = {
        .ucTotalNumberOfChannels     = 1,
        .ucNumberOfEncryptedChannels = 0,
        .usNumberOfEvents            = ANT_EVT_QUEUE,
        .pucMemoryBlockStartLocation = m_ant_mem,
        .usMemoryBlockByteSize       = sizeof(m_ant_mem),
    };
    if (sd_ant_enable(&cfg) != NRF_SUCCESS) NVIC_SystemReset();

    sd_ant_network_address_set(ANT_NETWORK, netkey);
    sd_ant_channel_assign(ANT_CHANNEL, CHANNEL_TYPE_SLAVE, ANT_NETWORK, 0);
    /* Wildcard device number / transmission type: pair with any Edge. */
    sd_ant_channel_id_set(ANT_CHANNEL, 0, CONTROLS_DEVICE_TYPE, 0);
    sd_ant_channel_radio_freq_set(ANT_CHANNEL, CONTROLS_RF_FREQ);
    sd_ant_channel_period_set(ANT_CHANNEL, CONTROLS_PERIOD);
    sd_ant_channel_rx_search_timeout_set(ANT_CHANNEL, CONTROLS_SEARCH_TIMEOUT);
}

/* ------------------------------------------------------------------------- */
/* Command queue (page 73, acknowledged)                                     */
/* ------------------------------------------------------------------------- */
#define CMDQ_LEN 4
static uint16_t m_cmdq[CMDQ_LEN];
static uint8_t  m_cmdq_head, m_cmdq_count;
static uint32_t m_cmd_age;          /* ticks the head command has waited */
static uint8_t  m_cmd_retries;
static bool     m_cmd_in_flight;
static uint8_t  m_cmd_seq;          /* sequence number of head command   */
static bool     m_cmd_seq_valid;

static void cmd_pop(void)
{
    m_cmdq_head = (m_cmdq_head + 1) % CMDQ_LEN;
    m_cmdq_count--;
    m_cmd_age = 0;
    m_cmd_retries = 0;
    m_cmd_seq_valid = false;
    m_cmd_in_flight = false;
}

static void cmd_push(int32_t cmd)
{
    if (cmd < 0) return;
    if (m_cmdq_count == CMDQ_LEN) return;            /* full: drop */
    m_cmdq[(m_cmdq_head + m_cmdq_count) % CMDQ_LEN] = (uint16_t)cmd;
    if (m_cmdq_count++ == 0) m_cmd_age = 0;
    ant_channel_open();                              /* wake the search */
}

static void cmd_try_send(void)
{
    if (!m_cmdq_count || m_cmd_in_flight || m_ch_state != CH_TRACKING) return;

    /* Same sequence number on retries so the Edge can drop duplicates. */
    if (!m_cmd_seq_valid) { m_cmd_seq++; m_cmd_seq_valid = true; }

    uint16_t cmd = m_cmdq[m_cmdq_head];
    uint8_t p[ANT_STANDARD_DATA_PAYLOAD_SIZE] = {
        PAGE_GENERIC_CMD,
        (uint8_t)(m_serial16 & 0xFF), (uint8_t)(m_serial16 >> 8),
        (uint8_t)(ANT_MANUFACTURER_ID & 0xFF), (uint8_t)(ANT_MANUFACTURER_ID >> 8),
        m_cmd_seq,
        (uint8_t)(cmd & 0xFF), (uint8_t)(cmd >> 8),
    };
    if (sd_ant_acknowledge_message_tx(ANT_CHANNEL, sizeof(p), p) == NRF_SUCCESS)
        m_cmd_in_flight = true;
}

static void cmd_tick(void)
{
    if (!m_cmdq_count) return;
    if (!m_cmd_in_flight && ++m_cmd_age > CMD_TIMEOUT_TICKS) {
        cmd_pop();                    /* Edge never showed up */
        led_blink(3, 2);
        return;
    }
    cmd_try_send();
}

/* Reply to common page 70 "request data page" for pages 80 / 81. */
static void handle_page_request(const uint8_t *p)
{
    if (p[0] != PAGE_REQUEST || p[7] != 0x01 || m_cmd_in_flight) return;

    uint32_t sn = NRF_FICR->DEVICEID[0];
    uint8_t r[ANT_STANDARD_DATA_PAYLOAD_SIZE];

    if (p[6] == PAGE_MANUFACTURER) {
        uint8_t t[] = { PAGE_MANUFACTURER, 0xFF, 0xFF, ANT_HW_REVISION,
                        ANT_MANUFACTURER_ID & 0xFF, ANT_MANUFACTURER_ID >> 8,
                        ANT_MODEL_NUMBER & 0xFF, ANT_MODEL_NUMBER >> 8 };
        memcpy(r, t, sizeof(r));
    } else if (p[6] == PAGE_PRODUCT) {
        uint8_t t[] = { PAGE_PRODUCT, 0xFF, 0xFF, ANT_SW_REVISION,
                        (uint8_t)sn, (uint8_t)(sn >> 8), (uint8_t)(sn >> 16), (uint8_t)(sn >> 24) };
        memcpy(r, t, sizeof(r));
    } else {
        return;
    }
    if (p[5] & 0x80) sd_ant_acknowledge_message_tx(ANT_CHANNEL, sizeof(r), r);
    else             sd_ant_broadcast_message_tx(ANT_CHANNEL, sizeof(r), r);
}

static void ant_events_drain(void)
{
    uint8_t     ch, evt;
    ANT_MESSAGE msg;

    while (sd_ant_event_get(&ch, &evt, msg.aucMessage) == NRF_SUCCESS) {
        if (ch != ANT_CHANNEL) continue;
        switch (evt) {
        case EVENT_RX:
            if (m_ch_state != CH_TRACKING) led_blink(2, 4);   /* found the Edge */
            m_ch_state = CH_TRACKING;
            if (msg.ANT_MESSAGE_ucMesgID == MESG_BROADCAST_DATA_ID ||
                msg.ANT_MESSAGE_ucMesgID == MESG_ACKNOWLEDGED_DATA_ID)
                handle_page_request(msg.ANT_MESSAGE_aucPayload);
            break;

        case EVENT_TRANSFER_TX_COMPLETED:
            if (m_cmd_in_flight) { cmd_pop(); led_blink(1, 2); }
            break;

        case EVENT_TRANSFER_TX_FAILED:
            if (m_cmd_in_flight) {
                m_cmd_in_flight = false;
                if (++m_cmd_retries > CMD_MAX_RETRIES) { cmd_pop(); led_blink(3, 2); }
            }
            break;

        case EVENT_RX_FAIL_GO_TO_SEARCH:
            m_ch_state = CH_SEARCHING;
            break;

        case EVENT_CHANNEL_CLOSED:      /* search timed out */
            m_ch_state = CH_CLOSED;
            m_cmd_in_flight = false;
            if (m_cmdq_count) ant_channel_open();   /* still have work: keep looking */
            break;

        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Buttons                                                                   */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint32_t pin;
    int32_t  short_cmd;
    int32_t  long_cmd;
    bool     pressed;       /* debounced */
    uint8_t  bounce;
    uint16_t held;
    bool     long_fired;
} button_t;

static button_t m_buttons[] = {
    { .pin = PIN_BTN_A, .short_cmd = BTN_A_SHORT, .long_cmd = BTN_A_LONG },
    { .pin = PIN_BTN_B, .short_cmd = BTN_B_SHORT, .long_cmd = BTN_B_LONG },
};
#define NUM_BUTTONS (sizeof(m_buttons) / sizeof(m_buttons[0]))

static uint32_t m_idle_ticks;

static void buttons_tick(void)
{
    for (unsigned i = 0; i < NUM_BUTTONS; i++) {
        button_t *b = &m_buttons[i];
        bool raw = pin_is_low(b->pin);

        if (raw != b->pressed) {
            if (++b->bounce < 2) continue;    /* 2 stable samples = 40 ms */
            b->bounce  = 0;
            b->pressed = raw;
            m_idle_ticks = 0;

            if (raw) {                        /* press */
                b->held = 0;
                b->long_fired = false;
                if (b->long_cmd == CMD_NONE) cmd_push(b->short_cmd);
            } else if (b->long_cmd != CMD_NONE && !b->long_fired) {
                cmd_push(b->short_cmd);       /* release before long-press time */
            }
        } else {
            b->bounce = 0;
        }

        if (b->pressed && b->long_cmd != CMD_NONE && !b->long_fired &&
            ++b->held >= LONG_PRESS_TICKS) {
            b->long_fired = true;
            cmd_push(b->long_cmd);
        }
    }
}

static bool any_button_down(void)
{
    for (unsigned i = 0; i < NUM_BUTTONS; i++)
        if (pin_is_low(m_buttons[i].pin)) return true;
    return false;
}

/* ------------------------------------------------------------------------- */
/* Sleep: System OFF, wake on any button (wakes through reset)               */
/* ------------------------------------------------------------------------- */
static void go_to_sleep(void)
{
    sd_ant_channel_close(ANT_CHANNEL);
    led_set(false);
    while (any_button_down()) {}
    for (unsigned i = 0; i < NUM_BUTTONS; i++)
        pin_cfg_input_pullup(m_buttons[i].pin, true);
    sd_power_system_off();
    for (;;) {}
}

/* ------------------------------------------------------------------------- */
/* Interrupts                                                                */
/* ------------------------------------------------------------------------- */
static volatile uint32_t m_ticks;

void RTC1_IRQHandler(void)
{
    NRF_RTC1->EVENTS_TICK = 0;
    (void)NRF_RTC1->EVENTS_TICK;
    m_ticks++;
}

void SD_EVT_IRQHandler(void)
{
    /* Events are drained in the main loop; this IRQ only wakes the CPU. */
}

static void sd_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
    (void)id; (void)pc; (void)info;
    NVIC_SystemReset();
}

static void tick_timer_start(void)
{
    NRF_RTC1->PRESCALER = (32768 / TICK_HZ) - 1;
    NRF_RTC1->INTENSET  = RTC_INTENSET_TICK_Msk;
    sd_nvic_SetPriority(RTC1_IRQn, 6);
    sd_nvic_EnableIRQ(RTC1_IRQn);
    NRF_RTC1->TASKS_START = 1;
}

/* ------------------------------------------------------------------------- */
int main(void)
{
    regout0_ensure_3v3();

    pin_cfg_output(PIN_LED);
    led_set(false);
    for (unsigned i = 0; i < NUM_BUTTONS; i++)
        pin_cfg_input_pullup(m_buttons[i].pin, false);

    m_serial16 = (uint16_t)NRF_FICR->DEVICEID[0];
    if (m_serial16 == 0 || m_serial16 == 0xFFFF) m_serial16 = 1;

#if USE_LFXO
    nrf_clock_lf_cfg_t lf = { .source = NRF_CLOCK_LF_SRC_XTAL, .rc_ctiv = 0,
                              .rc_temp_ctiv = 0, .accuracy = NRF_CLOCK_LF_ACCURACY_20_PPM };
#else
    nrf_clock_lf_cfg_t lf = { .source = NRF_CLOCK_LF_SRC_RC, .rc_ctiv = 16,
                              .rc_temp_ctiv = 2, .accuracy = NRF_CLOCK_LF_ACCURACY_500_PPM };
#endif
    if (sd_softdevice_enable(&lf, sd_fault_handler, ANT_LICENSE_KEY) != NRF_SUCCESS)
        NVIC_SystemReset();

    sd_nvic_SetPriority(SD_EVT_IRQn, 6);
    sd_nvic_EnableIRQ(SD_EVT_IRQn);

    ant_setup();
    tick_timer_start();

    led_blink(1, 10);                 /* boot */
    ant_channel_open();

    uint32_t done = 0;
    for (;;) {
        uint32_t evt_id;
        while (sd_evt_get(&evt_id) == NRF_SUCCESS) {}   /* discard SoC events */

        ant_events_drain();

        while (done != m_ticks) {
            done++;
            buttons_tick();
            cmd_tick();
            led_tick();

            if (m_ch_state == CH_TRACKING) {
                m_idle_ticks = 0;
            } else {
                /* heartbeat while searching */
                if (m_ch_state == CH_SEARCHING && (done % (2 * TICK_HZ)) == 0 && !m_led_blinks)
                    led_blink(1, 1);
                if (++m_idle_ticks > SLEEP_TICKS && !m_cmdq_count) go_to_sleep();
            }
        }

        sd_app_evt_wait();
    }
}
