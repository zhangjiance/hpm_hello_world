/*
 * Copyright (c) 2021 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>
#include "board.h"
#include "hpm_debug_console.h"
#include "hpm_gpio_drv.h"
#include "hpm_soc.h"
#include "hpm_usb_drv.h"
#include "hpm_interrupt.h"
#include "usb_config.h"
#include "hpm_dfu_trigger.h"

#define LED_FLASH_PERIOD_IN_MS 100

#if defined(BOARD_HAS_CHAN_LEDS) && BOARD_HAS_CHAN_LEDS
#define LED_STARTUP_STEPS   8U
#define LED_STARTUP_STEP_MS 30U

static void led_startup_anim(void)
{
    for (uint8_t i = 0U; i < LED_STARTUP_STEPS; i++) {
        board_led_toggle();
        board_delay_ms(LED_STARTUP_STEP_MS);
    }
    board_led_write(BOARD_LED_OFF_LEVEL);
}
#endif

int main(void)
{
    board_init();
    board_init_led_pins();

#if defined(BOARD_HAS_CHAN_LEDS) && BOARD_HAS_CHAN_LEDS
    led_startup_anim();
#endif

    board_timer_create(LED_FLASH_PERIOD_IN_MS, board_led_toggle);

    /* USB DFU runtime — responds to dfu-util -e */
    extern void app_usb_init(uint8_t busid, uintptr_t reg_base);
    board_init_usb((USB_Type *)CONFIG_HPM_USBD_BASE);
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);
    app_usb_init(0, CONFIG_HPM_USBD_BASE);

    int key_press_count = 0;

    printf("hello world, USB: DFU runtime + CDC ACM VCOM (loopback enabled)\r\n");

    while (1)
    {
        /* Key detection: check every 100ms, 5 consecutive presses -> boot */
#ifdef BOARD_APP_GPIO_CTRL
        uint8_t key = gpio_read_pin(BOARD_APP_GPIO_CTRL,
                                     BOARD_APP_GPIO_INDEX,
                                     BOARD_APP_GPIO_PIN);
        if (key == BOARD_BUTTON_PRESSED_VALUE) {
            key_press_count++;
            if (key_press_count >= 5) {
                printf("\n[KEY] Held 500ms, entering bootloader...\n");
                board_delay_ms(50);
                hpm_reboot_to_boot();
            }
        } else {
            key_press_count = 0;
        }
#endif
        board_delay_ms(100);
    }
    return 0;
}
