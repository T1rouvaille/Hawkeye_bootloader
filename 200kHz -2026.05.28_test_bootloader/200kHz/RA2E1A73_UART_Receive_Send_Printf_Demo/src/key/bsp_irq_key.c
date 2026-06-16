/*
 * bsp_key.c
 */
#include <key/bsp_irq_key.h>
#include <SysTick/bsp_SysTick.h>
#include "debug_uart/bsp_debug_uart.h"
#include <agt/bsp_agt_timing.h>
#include "common_utils.h"
#include <led/bsp_led.h>
#include "stdbool.h"
#include "stdio.h"
#include "stdint.h"
#include "timer_pwm.h"
#include "hawkeye_config.h"
#include "stats/bsp_stats.h"
#include "flash/flash.h"
/* 低温标记 (定义在 bsp_adc.c, 声明在 bsp_adc.h) */
extern volatile bool g_ntc_low_temp;

typedef enum
{
    MOS_IDLE = 0,
    MOS_WAIT_PWM_THEN_ON,
    MOS_WAIT_PWM_THEN_OFF,
} mos_state_t;

static mos_state_t mos_state      = MOS_IDLE;
static uint16_t    mos_delay_ms   = 0;
static int8_t      mos_pending_ch = -1;
static uint16_t    key_startup_ignore_ms = 0;
static uint16_t    key_release_confirm_ms = 0;
static bool        key_input_armed = false;
static uint8_t     last_laser_mask = 0;  /* 上次激光状态掩码，用于去重保存 */

key_t keys[] = {
    { .pin = BSP_IO_PORT_02_PIN_08, .name = "KEY1" },
    { .pin = BSP_IO_PORT_02_PIN_14, .name = "KEY2" },
    { .pin = BSP_IO_PORT_02_PIN_15, .name = "KEY3" },
};

volatile uint8_t gLaserOn[3] = {0, 0, 0};

const uint8_t key_count = sizeof(keys) / sizeof(keys[0]);
/* ===== duty ===== */
extern uint32_t duty00;
extern uint32_t duty11;
extern uint32_t duty22;
/* ------------------------------------------------------------------ */

static uint8_t key_read(bsp_io_port_pin_t pin)
{
    bsp_io_level_t level;
    R_IOPORT_PinRead(&g_ioport_ctrl, pin, &level);
    return (uint8_t)level;
}

static uint8_t laser_on_count(void)
{
    return (uint8_t)((gLaserOn[0] != 0) + (gLaserOn[1] != 0) + (gLaserOn[2] != 0));
}

static void key_resync_state(key_t *k, uint8_t level)
{
    k->stable_level  = level;
    k->last_level    = level;
    k->debounce_time = 0;
    k->press_time    = 0;
    k->long_sent     = 0;
    k->short_sent    = 0;
    k->event         = KEY_NONE;
}

static bool key_all_released(void)
{
    for (uint8_t i = 0; i < key_count; i++)
    {
        if (key_read(keys[i].pin) == KEY_ACTIVE_LEVEL)
        {
            return false;
        }
    }

    return true;
}

/*
 * 只在第一路开启时调用（全灭→有路），三路200kHz全部设初始值
 * 激活路=60，其余=4
 */
static void all_carrier_on(void)
{
    uint32_t d = g_ntc_low_temp ? (60 * NTC_LOW_TEMP_DUTY_PERCENT / 100) : 60;
    set_laser1_200k_intensity   (gLaserOn[2] ? d : 20, TIMER_PIN);
    set_laser2_200k_intensity (gLaserOn[0] ? d : 20, TIMER_PIN);
    set_laser3_200k_intensity (gLaserOn[1] ? d : 20, GPT_IO_PIN_GTIOCA);
}

/*
 * 最后一路关闭时调用，三路200kHz和8.47kHz全部清零
 */
static void all_carrier_off(void)
{
    /* 200kHz */
    set_laser1_200k_intensity  (0, TIMER_PIN);
    set_laser2_200k_intensity(0, TIMER_PIN);
    set_laser3_200k_intensity(0, GPT_IO_PIN_GTIOCA);

    /* 8.47kHz */
    set_laser1_8470_intensity(0, GPT_IO_PIN_GTIOCA);
    set_laser2_8470_intensity  (0, TIMER_PIN);
    set_laser3_8470_intensity(0, GPT_IO_PIN_GTIOCA);

}

/* ------------------------------------------------------------------ */

static void key_process(key_t *k)
{
    uint8_t sample = key_read(k->pin);
    k->event = KEY_NONE;

    if (sample == k->last_level) {
        if (k->debounce_time < KEY_DEBOUNCE_MS)
            k->debounce_time += KEY_SCAN_PERIOD_MS;
        else
            k->stable_level = sample;
    } else {
        k->debounce_time = 0;
        k->last_level = sample;

        /* 仅按下需要消抖，释放立即生效，缩短按键周期 */
        if (sample != KEY_ACTIVE_LEVEL) {
            k->stable_level = sample;
        }
    }

    if (k->stable_level == KEY_ACTIVE_LEVEL) {
        if (k->press_time < 60000) k->press_time += KEY_SCAN_PERIOD_MS;

        /* 消抖完成后立即触发短按，不等释放 */
        if (k->short_sent == 0) {
            k->event      = KEY_SHORT_PRESS;
            k->short_sent = 1;
        }

        if ((k->press_time >= KEY_LONG_PRESS_MS) && (k->long_sent == 0)) {
            k->event     = KEY_LONG_PRESS;
            k->long_sent = 1;
        }
    } else {
        k->press_time = 0;
        k->long_sent  = 0;
        k->short_sent = 0;
    }
}

void key_init(void)
{
    R_IOPORT_Open(&g_ioport_ctrl, g_ioport.p_cfg);
    key_startup_ignore_ms  = KEY_STARTUP_IGNORE_MS;
    key_release_confirm_ms = 0;
    key_input_armed        = false;

    for (uint8_t i = 0; i < key_count; i++) {
        uint8_t lv = key_read(keys[i].pin);
        key_resync_state(&keys[i], lv);
    }
}

void key_scan_task(void)
{
    if (key_startup_ignore_ms > 0U)
    {
        if (key_startup_ignore_ms > KEY_SCAN_PERIOD_MS)
        {
            key_startup_ignore_ms -= KEY_SCAN_PERIOD_MS;
        }
        else
        {
            key_startup_ignore_ms = 0U;
        }

        for (uint8_t i = 0; i < key_count; i++)
        {
            key_resync_state(&keys[i], key_read(keys[i].pin));
        }

        key_release_confirm_ms = 0U;
        return;
    }

    if (!key_input_armed)
    {
        bool all_released = key_all_released();

        for (uint8_t i = 0; i < key_count; i++)
        {
            key_resync_state(&keys[i], key_read(keys[i].pin));
        }

        if (all_released)
        {
            if (key_release_confirm_ms < KEY_DEBOUNCE_MS)
            {
                key_release_confirm_ms += KEY_SCAN_PERIOD_MS;
            }

            if (key_release_confirm_ms >= KEY_DEBOUNCE_MS)
            {
                key_input_armed = true;
            }
        }
        else
        {
            key_release_confirm_ms = 0U;
        }

        return;
    }

    for (uint8_t i = 0; i < key_count; i++)
        key_process(&keys[i]);
}

key_event_type_t key_get_event(uint8_t idx)
{
    if (idx >= key_count) return KEY_NONE;
    return keys[idx].event;
}

/* ------------------------------------------------------------------ */

void key_handle_events(void)
{
    for (uint8_t i = 0; i < key_count; i++)
    {
        key_event_type_t evt = keys[i].event;

        if (evt == KEY_SHORT_PRESS)
        {
            if (mos_state != MOS_IDLE)
            {
                keys[i].event = KEY_NONE;
                continue;
            }

            if (gLaserOn[i] == 0)
            {
                /* ===== 开启该路 ===== */
                uint8_t was_all_off = (laser_on_count() == 0);

                gLaserOn[i] = 1;
                stats_laser_key_press(i);

                pwm_cycle_counter  = 0;
                pwm_in_high_phase  = 0;
                sampled_this_cycle = 0;
                clear_flag();
                laser_mode_4_10deg_reset();
                laser_mode_10_90deg_reset();

                if (was_all_off)
                {
                    all_carrier_on();
                    mos_delay_ms = 0;
                    mos_state    = MOS_WAIT_PWM_THEN_ON;
                }
                else
                {
                    if (i == 2)      { set_laser1_200k_intensity(60, TIMER_PIN);          }
                    else if (i == 0) { set_laser2_200k_intensity(60, TIMER_PIN);          }
                    else if (i == 1) { set_laser3_200k_intensity(60, GPT_IO_PIN_GTIOCA);  }
                }
                
                /* 保存激光状态 */
                uint8_t current_mask = (gLaserOn[0] << 0) | (gLaserOn[1] << 1) | (gLaserOn[2] << 2);
                if (current_mask != last_laser_mask) {
                    last_laser_mask = current_mask;
                    flash_save_laser_state(current_mask);
                }
            }
            else
            {
                /* ===== 关闭该路 ===== */
                uint8_t count_before = laser_on_count();

                if (count_before > 1)
                {
                    if (i == 2)
                    {
                        set_laser1_200k_intensity(20, TIMER_PIN);
                        duty00 = 60;
                        gLaserOn[i] = 0;
                    }
                    else if (i == 0)
                    {
                        set_laser2_200k_intensity(20, TIMER_PIN);
                        duty11 = 60;
                        gLaserOn[i] = 0;
                    }
                    else if (i == 1)
                    {
                        set_laser3_200k_intensity(20, GPT_IO_PIN_GTIOCA);
                        duty22 = 60;
                        gLaserOn[i] = 0;
                    }
                    
                    /* 保存激光状态 */
                    uint8_t current_mask = (gLaserOn[0] << 0) | (gLaserOn[1] << 1) | (gLaserOn[2] << 2);
                    if (current_mask != last_laser_mask) {
                        last_laser_mask = current_mask;
                        flash_save_laser_state(current_mask);
                    }
                }
                else
                {
                    EN1_OFF;

                    if (i == 2)      { duty00 = 60; }
                    else if (i == 0) { duty11 = 60; }
                    else if (i == 1) { duty22 = 60; }

                    mos_pending_ch = (int8_t)i;
                    mos_delay_ms   = 0;
                    mos_state      = MOS_WAIT_PWM_THEN_OFF;
                    
                    /* 保存激光状态（全关）*/
                    flash_save_laser_state(0);
                    last_laser_mask = 0;
                }
            }
        }

        keys[i].event = KEY_NONE;
    }
}

/* ------------------------------------------------------------------ */

void key_en_delay_task_1ms(void)
{
    if (mos_state == MOS_IDLE)
        return;

    mos_delay_ms++;
    if (mos_delay_ms < MOS_DELAY_MS)
        return;

    mos_delay_ms = 0;

    if (mos_state == MOS_WAIT_PWM_THEN_ON)
    {
        /* 第一路开启：PWM已稳定10ms，开EN1 */
        EN1_ON;
    }
    else if (mos_state == MOS_WAIT_PWM_THEN_OFF)
    {
        /* 最后一路关闭：EN1已关10ms，关所有PWM */
        gLaserOn[mos_pending_ch] = 0;
        all_carrier_off();
        mos_pending_ch = -1;
    }

    mos_state = MOS_IDLE;
}

/* ------------------------------------------------------------------ */

void laser1_set(uint8_t on)
{
    if (on)
    {
        set_laser1_200k_intensity(60, TIMER_PIN);
    }
    else
    {
        set_laser1_200k_intensity(0, TIMER_PIN);
        set_laser1_8470_intensity(0, GPT_IO_PIN_GTIOCA);
    }
}

void laser2_set(uint8_t on)
{
    if (on)
    {
        set_laser2_200k_intensity(60, TIMER_PIN);
    }
    else
    {
        set_laser2_200k_intensity(0, TIMER_PIN);
        set_laser2_8470_intensity(0, TIMER_PIN);
    }
}

void laser3_set(uint8_t on)
{
    if (on)
    {
        set_laser3_200k_intensity(60, GPT_IO_PIN_GTIOCA);
    }
    else
    {
        set_laser3_200k_intensity(0, GPT_IO_PIN_GTIOCA);
        set_laser3_8470_intensity(0, GPT_IO_PIN_GTIOCA);
    }
}

uint8_t laser_any_on(void)
{
    return (gLaserOn[0] || gLaserOn[1] || gLaserOn[2]);
}

/* ======================================================================
 *  串口控制激光开关 — 复用按键处理中的 MOS 状态机逻辑
 *  idx: 0=H(SIDE) / 1=V1(HORIZ) / 2=V2(FRONT)
 *  on:  true=开启, false=关闭
 *  返回 true=成功, false=忙或参数错误
 * ====================================================================== */
bool laser_serial_ctrl(uint8_t idx, bool on)
{
    if (idx >= 3) return false;
    if (mos_state != MOS_IDLE) return false;

    if (on)
    {
        /* ===== 开启 ===== */
        if (gLaserOn[idx] != 0U) return true;  /* 已开启，无需操作 */

        uint8_t was_all_off = (laser_on_count() == 0);

        gLaserOn[idx] = 1;
        stats_laser_key_press(idx);

        pwm_cycle_counter  = 0;
        pwm_in_high_phase  = 0;
        sampled_this_cycle = 0;
        clear_flag();
        laser_mode_4_10deg_reset();
        laser_mode_10_90deg_reset();

        if (was_all_off)
        {
            all_carrier_on();
            mos_delay_ms = 0;
            mos_state    = MOS_WAIT_PWM_THEN_ON;
        }
        else
        {
            if (idx == 2)      { set_laser1_200k_intensity(60, TIMER_PIN);          }
            else if (idx == 0) { set_laser2_200k_intensity(60, TIMER_PIN);          }
            else if (idx == 1) { set_laser3_200k_intensity(60, GPT_IO_PIN_GTIOCA);  }
        }

        /* 保存激光状态 */
        uint8_t current_mask = (uint8_t)((gLaserOn[0] << 0) | (gLaserOn[1] << 1) | (gLaserOn[2] << 2));
        if (current_mask != last_laser_mask)
        {
            last_laser_mask = current_mask;
            flash_save_laser_state(current_mask);
        }
    }
    else
    {
        /* ===== 关闭 ===== */
        if (gLaserOn[idx] == 0U) return true;  /* 已关闭，无需操作 */

        uint8_t count_before = laser_on_count();

        if (count_before > 1)
        {
            /* 还有其它路亮着，只关该路 PWM 到 idle */
            if (idx == 2)
            {
                set_laser1_200k_intensity(20, TIMER_PIN);
                duty00 = 60;
            }
            else if (idx == 0)
            {
                set_laser2_200k_intensity(20, TIMER_PIN);
                duty11 = 60;
            }
            else /* idx == 1 */
            {
                set_laser3_200k_intensity(20, GPT_IO_PIN_GTIOCA);
                duty22 = 60;
            }
            gLaserOn[idx] = 0;

            uint8_t current_mask = (uint8_t)((gLaserOn[0] << 0) | (gLaserOn[1] << 1) | (gLaserOn[2] << 2));
            if (current_mask != last_laser_mask)
            {
                last_laser_mask = current_mask;
                flash_save_laser_state(current_mask);
            }
        }
        else
        {
            /* 最后一路关闭：走 MOS 时序 */
            EN1_OFF;

            if (idx == 2)      { duty00 = 60; }
            else if (idx == 0) { duty11 = 60; }
            else               { duty22 = 60; }

            mos_pending_ch = (int8_t)idx;
            mos_delay_ms   = 0;
            mos_state      = MOS_WAIT_PWM_THEN_OFF;

            flash_save_laser_state(0);
            last_laser_mask = 0;
        }
    }

    return true;
}

/* ======================================================================
 *  串口控制激光全开/全关 — 原子操作，避免逐路调用时 MOS 状态机冲突
 *  on:  true=全开, false=全关
 *  返回 true=成功, false=MOS 忙
 * ====================================================================== */
bool laser_serial_ctrl_all(bool on)
{
    if (mos_state != MOS_IDLE) return false;

    if (on)
    {
        /* ===== 全开 ===== */
        if (gLaserOn[0] && gLaserOn[1] && gLaserOn[2]) return true;

        uint8_t was_all_off = (laser_on_count() == 0);

        /* 先设三路状态，再一次配置 PWM，避免 MOS 忙阻塞 */
        gLaserOn[0] = 1;
        gLaserOn[1] = 1;
        gLaserOn[2] = 1;

        for (int i = 0; i < 3; i++) stats_laser_key_press(i);

        pwm_cycle_counter  = 0;
        pwm_in_high_phase  = 0;
        sampled_this_cycle = 0;
        clear_flag();
        laser_mode_4_10deg_reset();
        laser_mode_10_90deg_reset();

        /* all_carrier_on() 读取 gLaserOn 状态，三路全=1 则全设 60 */
        all_carrier_on();

        if (was_all_off)
        {
            mos_delay_ms = 0;
            mos_state    = MOS_WAIT_PWM_THEN_ON;
        }

        flash_save_laser_state(0x07);
        last_laser_mask = 0x07;
    }
    else
    {
        /* ===== 全关 ===== */
        if (laser_on_count() == 0) return true;

        EN1_OFF;

        if (gLaserOn[2]) duty00 = 60;
        if (gLaserOn[0]) duty11 = 60;
        if (gLaserOn[1]) duty22 = 60;

        gLaserOn[0] = 0;
        gLaserOn[1] = 0;
        gLaserOn[2] = 0;

        mos_pending_ch = 0;  /* 占位，key_en_delay_task_1ms 会冗余清零 */
        mos_delay_ms   = 0;
        mos_state      = MOS_WAIT_PWM_THEN_OFF;

        flash_save_laser_state(0);
        last_laser_mask = 0;
    }

    return true;
}

/* ===== 开机前锁定开关检测，不通过直接死循环 ===== */
void lock_sw_boot_check(void)
{
    bsp_io_level_t boot_lock_level;
    R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_07, &boot_lock_level);
    if (boot_lock_level == BSP_IO_LEVEL_HIGH)
    {
        //system_power_off_request(POWER_OFF_REASON_LOCK_SW_BOOT);
        Power_En_OFF;
        Batt_OFF;
    }
    else if (boot_lock_level == BSP_IO_LEVEL_LOW)
    {
        system_power_on();
    }
}

/* ===== 运行中锁定开关检测，松开立即关机 ===== */
void lock_sw_read(void)
{
    static uint8_t lock_high_confirm_count = 0;
    bsp_io_level_t lock_level;

    R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_07, &lock_level);

    if (lock_level == BSP_IO_LEVEL_HIGH)
    {
        if (lock_high_confirm_count < LOCK_SW_RUNTIME_CONFIRM_COUNT)
        {
            lock_high_confirm_count++;
        }

        if (lock_high_confirm_count >= LOCK_SW_RUNTIME_CONFIRM_COUNT)
        {
            uart9_send_blocking("+RESP:SW FAIL\r\n");
            system_power_off_request(POWER_OFF_REASON_LOCK_SW_RUNTIME);
        }
    }
    else
    {
        lock_high_confirm_count = 0;
    }
}

/**
 * @brief 开机时恢复激光状态
 */
void key_restore_on_boot(void)
{
    uint8_t laser_mask = flash_load_laser_state();
    
    // 如果全关状态，默认开三路
    if (laser_mask == 0) {
        laser_mask = 0x01;  // 0b001，只开启 gLaserOn[0]
    }
    
    // 设置激光状态
    gLaserOn[0] = (laser_mask & 0x01) ? 1 : 0;
    gLaserOn[1] = (laser_mask & 0x02) ? 1 : 0;
    gLaserOn[2] = (laser_mask & 0x04) ? 1 : 0;
    
    // 如果有激光需要开启
    if (laser_mask != 0) {
        // 设置PWM初始值（激活路=60，其余=20）
        set_laser1_200k_intensity(gLaserOn[2] ? 60 : 20, TIMER_PIN);
        set_laser2_200k_intensity(gLaserOn[0] ? 60 : 20, TIMER_PIN);
        set_laser3_200k_intensity(gLaserOn[1] ? 60 : 20, GPT_IO_PIN_GTIOCA);
        
        // 设置MOS状态，等待10ms后开EN1
        mos_delay_ms = 0;
        mos_state = MOS_WAIT_PWM_THEN_ON;
    }
}


