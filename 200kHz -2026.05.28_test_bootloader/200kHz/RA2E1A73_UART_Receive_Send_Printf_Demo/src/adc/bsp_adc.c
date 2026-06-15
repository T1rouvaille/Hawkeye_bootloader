#include <adc/bsp_adc.h>
#include <stdio.h>
#include <math.h>
#include "common_utils.h"
#include <led/bsp_led.h>
#include "debug_uart/bsp_debug_uart.h"
#include "stats/bsp_stats.h"
#include <string.h>
#include <key/bsp_irq_key.h>

/* ===== 电池电压相关宏定义 ===== */
#define ADC_TO_BAT_MV(adc_val)  ((uint32_t)(adc_val) * 3300UL * 48UL / 5UL / 4095UL)

/* ===== 激光ADC buffer ===== */
volatile uint16_t buf_pd1[ADC_BUF_SIZE]   = {0};
volatile uint16_t buf_ld1_1[ADC_BUF_SIZE] = {0};
volatile uint16_t buf_ld2_1[ADC_BUF_SIZE] = {0};

volatile uint16_t buf_pd2[ADC_BUF_SIZE]   = {0};
volatile uint16_t buf_ld1_2[ADC_BUF_SIZE] = {0};
volatile uint16_t buf_ld2_2[ADC_BUF_SIZE] = {0};

volatile uint16_t buf_pd3[ADC_BUF_SIZE]   = {0};
volatile uint16_t buf_ld1_3[ADC_BUF_SIZE] = {0};
volatile uint16_t buf_ld2_3[ADC_BUF_SIZE] = {0};

volatile uint8_t  g_laser_buf_ready = 0;
volatile uint16_t g_bat_adc_raw     = 0;
static   uint8_t  s_buf_index       = 0;
volatile uint16_t g_bat_battery_temp_raw     = 0;
/* ===== 保留原有变量（开机段还在用） ===== */
volatile bool scan_complete_flag = false;

uint16_t adc_data_BAT;
uint16_t adc_volt_BAT;
uint16_t PD1;
uint16_t PD12;
uint16_t PD13;
uint16_t PD2;
uint16_t PD22;
uint16_t PD23;
uint16_t PD3;
uint16_t PD32;
uint16_t PD33;
int current_read_index = 0;
int read_phase = 0;
uint16_t pd_raw1_bat = 0;
int pd_voltage_bat = 0;

/* ===== 电池状态 ===== */
typedef enum {
    BAT_STATE_FULL = 0,
    BAT_STATE_HIGH,
    BAT_STATE_MID,
    BAT_STATE_LOW,
    BAT_STATE_CRITICAL,
} bat_state_t;

static bat_state_t  g_bat_state          = BAT_STATE_FULL;
static uint8_t      g_bat_critical_count = 0;
static bool         g_bat_shutdown_pending      = false;
static bool         g_system_power_off_pending  = false;
static power_off_reason_t g_system_power_off_reason = POWER_OFF_REASON_UNKNOWN;
static uint16_t     g_bat_adc_filtered   = 0;

/* ===== 外部依赖 ===== */
extern volatile bool pwm_in_high_phase;
extern volatile bool sample_delay_active;

/* ===== ADC 回调 ===== */
void adc_callback(adc_callback_args_t *p_args)
{
    FSP_PARAMETER_NOT_USED(p_args);

    /* 电池通道：任何时候都更新 */
    R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_9, (uint16_t *)&g_bat_adc_raw);

    /* 电池温度通道：任何时候都更新 */
    R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_22, (uint16_t *)&g_bat_battery_temp_raw);

    /* 开机段兼容：保留 scan_complete_flag */
    scan_complete_flag = true;

    /* 激光通道：只在采样窗口内存buffer */
    if (!pwm_in_high_phase || sample_delay_active)
        return;

    /* 上一批buffer还没被主循环消费，丢弃本次 */
    if (g_laser_buf_ready)
        return;

    if (s_buf_index < ADC_BUF_SIZE)
    {
        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_2,  (uint16_t *)&buf_pd1[s_buf_index]);
        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_0,  (uint16_t *)&buf_ld1_1[s_buf_index]);
        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_1,  (uint16_t *)&buf_ld2_1[s_buf_index]);

        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_20, (uint16_t *)&buf_pd2[s_buf_index]);
        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_5,  (uint16_t *)&buf_ld1_2[s_buf_index]);
        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_6,  (uint16_t *)&buf_ld2_2[s_buf_index]);

        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_8,  (uint16_t *)&buf_pd3[s_buf_index]);
        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_19, (uint16_t *)&buf_ld1_3[s_buf_index]);
        R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_7,  (uint16_t *)&buf_ld2_3[s_buf_index]);

        s_buf_index++;
    }

    if (s_buf_index >= ADC_BUF_SIZE)
    {
        s_buf_index       = 0;
        g_laser_buf_ready = 1;
    }
}

void ADC_Init(void)
{
    fsp_err_t err;
    err = R_ADC_Open(&g_adc0_ctrl, &g_adc0_cfg);
    err = R_ADC_ScanCfg(&g_adc0_ctrl, &g_adc0_channel_cfg);
    assert(FSP_SUCCESS == err);
}

void Start_ADC(void)
{
    R_ADC_ScanStart(&g_adc0_ctrl);
    R_ADC0->ADCSR_b.ADST = 1;
}

void Stop_ADC(void)
{
    R_ADC_ScanStop(&g_adc0_ctrl);
}

/* 快速选择优化版本 - O(n) 平均复杂度 */
/* 使用Bentley-McIlroy 3-way partition快速选择算法 */
static uint16_t trimmed_avg(volatile uint16_t *buf, uint16_t size,
                             uint16_t trim, uint16_t valid)
{
    uint16_t tmp[64];
    uint32_t sum = 0;
    uint16_t i, j, k;
    
    /* 简洁拷贝 */
    __disable_irq();
    for (i = 0; i < size; i++) tmp[i] = buf[i];
    __enable_irq();
    
    /* 快速选择：找到第trim+valid小的元素即可 */
    uint16_t left = 0, right = size - 1;
    uint16_t target_end = trim + valid;
    
    while (left < right) {
        uint16_t pivot = tmp[(left + right) >> 1];
        i = left; j = right; k = left;
        
        /* 3-way partition */
        while (k <= j) {
            if (tmp[k] < pivot) {
                uint16_t t = tmp[i]; tmp[i] = tmp[k]; tmp[k] = t;
                i++; k++;
            } else if (tmp[k] > pivot) {
                uint16_t t = tmp[j]; tmp[j] = tmp[k]; tmp[k] = t;
                j--;
            } else {
                k++;
            }
        }
        
        /* 判断目标区域位置 */
        if (target_end <= i) {
            right = i - 1;
        } else if (trim > j) {
            left = j + 1;
        } else {
            left = right;  // 目标区域已就位，退出循环
        }
    }
    
    /* 求和中间valid个值 */
    for (i = trim; i < target_end; i++) sum += tmp[i];
    return (uint16_t)(sum / valid);
}
/* ===== 对外采样接口：直接读buffer，无阻塞 ===== */
uint16_t Read_ADC_SquareWave_WeightedAverage(void)
{
    return trimmed_avg(buf_pd1, ADC_BUF_SIZE, TRIM_SIDES, VALID_COUNT);
}

uint16_t Read_ADC_SquareWave_WeightedAverage1(void)
{
    return trimmed_avg(buf_pd2, ADC_BUF_SIZE, TRIM_SIDES, VALID_COUNT);
}

uint16_t Read_ADC_SquareWave_WeightedAverage2(void)
{
    return trimmed_avg(buf_pd3, ADC_BUF_SIZE, TRIM_SIDES, VALID_COUNT);
}

void Read_ADC_LD1_LD2_Average(uint16_t *ld1_avg, uint16_t *ld2_avg)
{
    *ld1_avg = trimmed_avg(buf_ld1_1, ADC_BUF_SIZE, LD_TRIM_SIDES, LD_VALID_COUNT);
    *ld2_avg = trimmed_avg(buf_ld2_1, ADC_BUF_SIZE, LD_TRIM_SIDES, LD_VALID_COUNT);
}

void Read_ADC_LD1_LD2_Average1(uint16_t *ld1_avg, uint16_t *ld2_avg)
{
    *ld1_avg = trimmed_avg(buf_ld1_2, ADC_BUF_SIZE, LD_TRIM_SIDES, LD_VALID_COUNT);
    *ld2_avg = trimmed_avg(buf_ld2_2, ADC_BUF_SIZE, LD_TRIM_SIDES, LD_VALID_COUNT);
}

void Read_ADC_LD1_LD2_Average2(uint16_t *ld1_avg, uint16_t *ld2_avg)
{
    *ld1_avg = trimmed_avg(buf_ld1_3, ADC_BUF_SIZE, LD_TRIM_SIDES, LD_VALID_COUNT);
    *ld2_avg = trimmed_avg(buf_ld2_3, ADC_BUF_SIZE, LD_TRIM_SIDES, LD_VALID_COUNT);
}

/* ===== 电池采样：直接读回调更新的全局变量 ===== */
uint16_t Read_ADC_Voltage_Value_BAT(void)
{
    return (uint16_t)g_bat_adc_raw;
}

uint16_t Read_ADC_Voltage_Value_Battery_Temp(void)
{
    return (uint16_t)g_bat_battery_temp_raw;
}

uint16_t Read_ADC_Voltage_Value_NTC(void)
{
    uint16_t ntc_raw = 0;
    R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_10, &ntc_raw);
    return ntc_raw;
}

/* NTC 低温标记 (NTC ADC > 3150 → <-7°C) */
volatile bool g_ntc_low_temp = false;

/* ===== 电池包 NTC 温度检测 ===== */
#define BAT_NTC_SAMPLE_CNT  (4U)    /* NTC计算累加次数 (4次=4秒) */

static uint32_t g_bat_ntc_vbatt_acc = 0;   /* vbatt 累加器 */
static uint32_t g_bat_ntc_th_acc    = 0;    /* th 累加器 */
static uint8_t  g_bat_ntc_sample_cnt = 0;   /* 采样计数 */

void battery_temp_check(void)
{
    uint16_t vbatt = (uint16_t)g_bat_adc_raw;
    uint16_t th    = (uint16_t)g_bat_battery_temp_raw;

    /* 累加采样 */
    g_bat_ntc_vbatt_acc += vbatt;
    g_bat_ntc_th_acc    += th;
    g_bat_ntc_sample_cnt++;

    if (g_bat_ntc_sample_cnt < BAT_NTC_SAMPLE_CNT) return;

    /* 多次采完, 求平均 */
    vbatt = (uint16_t)(g_bat_ntc_vbatt_acc / BAT_NTC_SAMPLE_CNT);
    th    = (uint16_t)(g_bat_ntc_th_acc    / BAT_NTC_SAMPLE_CNT);

    /* 重置累加器 */
    g_bat_ntc_vbatt_acc  = 0;
    g_bat_ntc_th_acc     = 0;
    g_bat_ntc_sample_cnt = 0;

    if (th == 0) return;  /* 防止除零 */

    /* NTC 电阻计算 (电池 → NTC → TH → R_fixed=8.25k → GND)
     * R_ntc(kΩ) = 15.49 × VBATT / TH - 8.25
     * 定点数 ×100: R_x100 = 1549 × VBATT / TH - 825 (单位 0.01kΩ) */
    int32_t r_x100 = (int32_t)1549 * (int32_t)vbatt / (int32_t)th - 825;

    /* 电池包过热保护: R_ntc < 0.65kΩ 或 0.75kΩ < R_ntc < 2.30kΩ 触发关机 */
    if ((r_x100 > 75 && r_x100 < 230))          //|| (r_x100 < 65))
    {
        system_overheat_request();
    }
}

/* ===== 以下电池管理代码完全不变 ===== */
void battery_voltage_task(void)
{
    uint16_t bat_adc_raw = Read_ADC_Voltage_Value_BAT();
    uint16_t bat_adc;

    if (g_bat_adc_filtered == 0U)
    {
        g_bat_adc_filtered = bat_adc_raw;
    }
    else
    {
        int32_t delta = (int32_t)bat_adc_raw - (int32_t)g_bat_adc_filtered;
        g_bat_adc_filtered = (uint16_t)((int32_t)g_bat_adc_filtered +
                                        (delta >> BAT_RUNTIME_FILTER_SHIFT));
    }

    bat_adc = g_bat_adc_filtered;

    /* IR压降补偿: 激光开启时电池电压跌落, 补偿后状态机更准确 */
    {
        uint16_t ir_comp = 0;
        if (gLaserOn[0]) ir_comp += BAT_IR_DROP_H_ADC;
        if (gLaserOn[1]) ir_comp += BAT_IR_DROP_V1_ADC;
        if (gLaserOn[2]) ir_comp += BAT_IR_DROP_V2_ADC;
        uint32_t comp = (uint32_t)bat_adc + ir_comp;
        if (comp > 4095U) comp = 4095U;
        bat_adc = (uint16_t)comp;
    }

    bat_state_t new_state = g_bat_state;

    switch (g_bat_state)
    {
        case BAT_STATE_FULL:
            if (bat_adc < (BAT_HIGH_MV - BAT_HYST))
                new_state = BAT_STATE_HIGH;
            break;
        case BAT_STATE_HIGH:
            if      (bat_adc >= (BAT_HIGH_MV + BAT_HYST)) new_state = BAT_STATE_FULL;
            else if (bat_adc <  (BAT_MID_MV  - BAT_HYST)) new_state = BAT_STATE_MID;
            break;
        case BAT_STATE_MID:
            if      (bat_adc >= (BAT_MID_MV + BAT_HYST))  new_state = BAT_STATE_HIGH;
            else if (bat_adc <  (BAT_LOW_MV - BAT_HYST))  new_state = BAT_STATE_LOW;
            break;
        case BAT_STATE_LOW:
            if      (bat_adc >= (BAT_LOW_MV + BAT_HYST))  new_state = BAT_STATE_MID;
            else if (bat_adc <  BAT_VLOW_MV)               new_state = BAT_STATE_CRITICAL;
            break;
        case BAT_STATE_CRITICAL:
            if (bat_adc >= BAT_LOW_MV)
                new_state = BAT_STATE_LOW;
            break;
        default:
            new_state = BAT_STATE_CRITICAL;
            break;
    }

    g_bat_state = new_state;

    if (g_bat_state == BAT_STATE_CRITICAL)
    {
        if (g_bat_critical_count < BAT_CRITICAL_CONFIRM_COUNT)
            g_bat_critical_count++;
    }
    else
    {
        g_bat_critical_count   = 0;
        g_bat_shutdown_pending = false;
    }

    if (g_bat_critical_count >= BAT_CRITICAL_CONFIRM_COUNT)
        g_bat_shutdown_pending = true;
}

void battery_led_task(void)
{
    static bat_state_t last_bat_state = BAT_STATE_FULL;
    static uint8_t blink_step = 0;

    if (last_bat_state != g_bat_state)
        blink_step = 0;
    last_bat_state = g_bat_state;

    switch (g_bat_state)
    {
        case BAT_STATE_FULL:     led_set_pattern(LED_PATTERN_ALL_ON);  break;
        case BAT_STATE_HIGH:     led_set_pattern(LED_PATTERN_12_ON);   break;
        case BAT_STATE_MID:      led_set_pattern(LED_PATTERN_1_ON);    break;
        case BAT_STATE_LOW:      led_set_pattern(LED_PATTERN_1_ON);    break;
        case BAT_STATE_CRITICAL:
            if (g_bat_shutdown_pending)
                system_power_off_request(POWER_OFF_REASON_BATTERY_LOW);
            break;
        default:
            led_set_pattern(LED_PATTERN_DEFAULT);
            break;
    }
}

static const char *power_off_reason_to_str(power_off_reason_t reason)
{
    switch (reason)
    {
        case POWER_OFF_REASON_BATTERY_LOW:      return "BATTERY_LOW";
        case POWER_OFF_REASON_LOCK_SW_RUNTIME:  return "LOCK_SW_RUNTIME";
        case POWER_OFF_REASON_I2C_FAIL:         return "I2C_FAIL";
        case POWER_OFF_REASON_OVERHEAT:         return "OVERHEAT";
        case POWER_OFF_REASON_IDLE_TIMEOUT:     return "IDLE_TIMEOUT";
        case POWER_OFF_REASON_UNKNOWN:
        default:                                return "UNKNOWN";
    }
}
void system_power_off_with_reason(power_off_reason_t reason)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "+RESP:POWEROFF=%s\r\n",
             power_off_reason_to_str(reason));
    uart9_send_blocking(msg);
    wdt_feed();  // ← 加
    flash_save_poweroff_and_stats((uint32_t)reason, stats_get_ptr());
    wdt_feed();  // ← 加
    Batt_OFF;
    Power_En_OFF;
    wdt_feed();  // ← 加
    R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
    wdt_feed();  // ← 加
    set_laser1_200k_intensity(0, TIMER_PIN);
    set_laser2_200k_intensity(0, TIMER_PIN);
    set_laser3_200k_intensity(0, GPT_IO_PIN_GTIOCA);

    set_laser1_8470_intensity(0, GPT_IO_PIN_GTIOCA);
    set_laser2_8470_intensity(0, TIMER_PIN);
    set_laser3_8470_intensity(0, GPT_IO_PIN_GTIOCA);

    LED1_OFF;
    LED2_OFF;
    LED3_OFF;
    R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
    wdt_feed();  // ← 加
    EN1_OFF;

    while (1)
    {
        Power_En_OFF;
        Batt_OFF;
        wdt_feed();
        __WFI();
    }
}

void system_power_off_request(power_off_reason_t reason)
{
    if (!g_system_power_off_pending)
    {
        g_system_power_off_pending  = true;
        g_system_power_off_reason   = reason;
    }
}

bool system_power_off_pending(void)
{
    return g_system_power_off_pending;
}

void system_power_off_process_pending(void)
{
    if (g_system_power_off_pending)
        system_power_off_with_reason(g_system_power_off_reason);
}

void system_power_off(void)
{
    system_power_off_request(POWER_OFF_REASON_UNKNOWN);
}

void system_power_on(void)
{
    Power_En_ON;
    Batt_ON;
}

void battery_state_init(uint16_t bat_adc)
{
    g_bat_adc_filtered = bat_adc;

    if      (bat_adc >= BAT_HIGH_MV) g_bat_state = BAT_STATE_FULL;
    else if (bat_adc >= BAT_MID_MV)  g_bat_state = BAT_STATE_HIGH;
    else if (bat_adc >= BAT_LOW_MV)  g_bat_state = BAT_STATE_MID;
    else if (bat_adc >= BAT_VLOW_MV) g_bat_state = BAT_STATE_LOW;
    else                              g_bat_state = BAT_STATE_CRITICAL;

    if (bat_adc < BAT_VLOW_MV)
        g_bat_critical_count = 1;
    else
        g_bat_critical_count = 0;

    g_bat_shutdown_pending = false;
}

bool battery_is_low_blink(void)
{
    return (g_bat_state == BAT_STATE_LOW);
}



