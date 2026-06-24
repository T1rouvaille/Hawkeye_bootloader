/**
 * @file    hawkeye_config.h
 * @brief   Hawkeye 激光控制系统 — 统一配置文件
 *
 * 所有可调参数集中管理，方便维护和审查。
 * 硬件引脚级宏（LED1_ON 等）仍保留在 bsp_led.h，此处只放数值型配置。
 *
 * @version 0.0.0.1
 * @date    2026-04-15
 */

#ifndef HAWKEYE_CONFIG_H_
#define HAWKEYE_CONFIG_H_

/* ======================================================================
 *  项目信息
 * ====================================================================== */
#define HAWKEYE_PROJECT_NAME    "Hawkeye"           /* 项目名称 */
#define HAWKEYE_VERSION_MAJOR   0                   /* 主版本号 */
#define HAWKEYE_VERSION_MINOR   0                   /* 次版本号 */
#define HAWKEYE_VERSION_PATCH   0                   /* 修订号 */
#define HAWKEYE_VERSION_BUILD   1                   /* 构建号 */
#define HAWKEYE_VERSION_STR     "0.0.0.1"           /* 版本字符串 */

/* ======================================================================
 *  MCU / 硬件基础参数
 * ====================================================================== */
#define MCU_CLOCK_HZ            (48000000UL)        /* 主频 48 MHz (HOCO) */
#define ADC_RESOLUTION_BITS     (12U)               /* ADC 分辨率 12 位 */
#define ADC_FULL_SCALE          (4096U)             /* 2^12 = 4096 */
#define ADC_VREF_MV             (3300U)             /* ADC 参考电压 3300 mV */

/* ======================================================================
 *  安全保护 — 超时参数（CRITICAL 级保护）
 * ====================================================================== */


/* ======================================================================
 *  电池管理参数
 * ====================================================================== */

/* 电池电压阈值 (ADC 原始值，非 mV)
 * 换算公式: mV = adc * 3300 * 48 / 5 / 4095 */
#define BAT_HIGH_MV             (2492U)     /* 19.2V: FULL→HIGH 边界 */
#define BAT_MID_MV              (2324U)     /* 17.9V: HIGH→MID  边界 */
#define BAT_LOW_MV              (1834U)     /* 14.5V: MID →关断  边界 */

/** 开机最低电压阈值 (ADC 原始值) */
#define BAT_BOOT_MIN_ADC        (1830U)


/** 电池状态迟滞量 (ADC 原始值) */
#define BAT_HYST                (50U)

/** 激光IR压降补偿 (ADC 原始值)
 *  激光开启时电池电压会跌落, 补偿后状态机判断更准确
 *  H 电流最大, V1/V2 电流相近
 *  测量方法: 对比激光全关和全开时的电池ADC差值 */
#define BAT_IR_DROP_H_ADC       40       /* H 激光压降补偿 (TODO: 实测后确定) */
#define BAT_IR_DROP_V1_ADC      30       /* V1激光压降补偿 (TODO: 实测后确定) */
#define BAT_IR_DROP_V2_ADC      30       /* V2激光压降补偿 (TODO: 实测后确定) */

/** 低压关机确认次数 — battery_voltage_task() 每 50ms 调用一次
 *  5 次 × 50ms = 250ms 连续确认，防止瞬时掉压误关机 */
#define BAT_CRITICAL_CONFIRM_COUNT  (1U)

/* ======================================================================
 *  按键参数
 * ====================================================================== */
#define KEY_SCAN_PERIOD_MS      (10U)       /* 按键扫描周期 (ms) */
#define KEY_DEBOUNCE_MS         (50U)       /* 消抖时间 (ms) */
#define KEY_LONG_PRESS_MS       (1000U)     /* 长按判定阈值 (ms) */
#define KEY_ACTIVE_LEVEL        (0U)        /* 按下时电平: 0=低电平有效 */
#define MOS_DELAY_MS            (10U)       /* MOS 驱动延迟 (ms)，PWM 稳定后再开 EN */
#define KEY_STARTUP_IGNORE_MS   (300U)      /* 上电后按键屏蔽时间 (ms) */

/** 锁定开关运行时关机确认次数，lock_sw_read() 每 10ms 调用一次
 *  5 次 × 10ms = 50ms，避免接触抖动或干扰误关机 */
#define LOCK_SW_RUNTIME_CONFIRM_COUNT  (5U)

/* ======================================================================
 *  激光 PWM 参数
 * ====================================================================== */
#define TIMER_PIN               GPT_IO_PIN_GTIOCB  /* 默认 PWM 输出引脚 */
#define GPT_PIN                 BSP_IO_PORT_02_PIN_12
#define MAX_INTENSITY           (100U)      /* 最大亮度百分比 */
#define MAX_DUTY_CYCLE          (1000U)     /* PWM 最大占空比 raw count */
#define LASER_DEFAULT_DUTY      (60U)       /* 激光默认 duty (开启时) */
#define LASER_IDLE_DUTY         (20U)       /* 激光空闲 duty (关闭时载波维持) */
#define LASER_CHANNEL_COUNT     (3U)        /* 激光通道数 */

/** 低电平阶段 duty 比例 (%)，用于 0-4° 和 10-90° 模式 */
#define LOW_PHASE_DUTY_PERCENT  (66U)

/* ======================================================================
 *  ADC 采样参数
 * ====================================================================== */
#define TOTAL_SAMPLES           (64U)       /* 单次采样总数 */
#define TRIM_SIDES              (16U)       /* 去掉两端各 16 个异常值 */
#define VALID_COUNT             (32U)       /* 取中间 32 个求均值 */

#define LD_TOTAL_SAMPLES        (64U)       /* LD 采样总数 */
#define LD_TRIM_SIDES           (16U)       /* LD 去掉两端各 16 */
#define LD_VALID_COUNT          (32U)       /* LD 有效采样数 */

#define HIGH_THRESHOLD          (1000U)     /* 方波高电平判定阈值 */

/** PWM 周期内 ADC 采样组数 */
#define SAMPLE_COUNT            (5U)

/* ======================================================================
 *  PID 及激光调光参数
 * ====================================================================== */
#define PD_FAULT_THRESHOLD      (40)       /* PD 低于此值判定激光管故障 */
#define PD_ABNORMAL_THRESHOLD   (3200)      /* PD 误差超过此值跳过本次调节 */
#define PD_NO_RISE_MAX          (5)         /* duty 涨但 PD 不涨的最大连续次数 → 饱和 */
#define PD_RISE_MIN             (5)         /* PD 变化量低于此值视为未响应 */
#define PD_REVERSE_CONFIRM      (3)         /* PD 反向下降确认次数 → 进入低温模式 */
#define PD_RECOVER_CONFIRM      (5)         /* PD 正常响应确认次数 → 退出低温模式 */
#define PD_REVERSE_DROP         (20)        /* duty 涨 PD 降超过此值判定反向 */
#define CURRENT_LIMIT_FREEZE_COUNT (5000)   /* 电流限制后 PID 冻结计数 */

/* ======================================================================
 *  UART 串口参数
 * ====================================================================== */
#define RX_BUFFER_SIZE          (32U)       /* UART 接收缓冲区大小 (字节) */
/* [FIX #3] WARNING: 明文密码，量产前务必更换或改为从 Flash 加载 */
#define CFG_PASSWORD            "123456"    /* AT 指令配置解锁密码 */
#define UART_MSG_BUF_SIZE       (64U)       /* UART 消息格式化缓冲区 */

/* ======================================================================
 *  IMU (IIM42351) 参数
 * ====================================================================== */
#define CALIBRATION_TRIALS      (32)       /* 校准采样次数 */
#define SAMPLE_PERIOD_MS        (10U)       /* 校准采样间隔 (ms) */

/** 角度阈值 (加速度计原始值) */
#define LIMIT_3_8_DEG           (1085)      /* 3.8° 阈值 */
#define LIMIT_4_DEG             (1143)      /* 4.0° 阈值 */
#define LIMIT_7_5_DEG           (2139)      /* 7.5° 阈值 */
#define LIMIT_10_DEG            (2846)      /* 10.0° 阈值 */

#define Y_AXIS_OFFSET_COMP      (140)
/** 姿态模式切换防抖阈值 — 需连续 N 次一致才切换 */
#define MODE_CHANGE_THRESHOLD   (2U)

/* ======================================================================
 *  Flash 存储参数
 * ====================================================================== */
#define FLASH_DF_BASE_ADDR      (0x40100000U)   /* RA2E1 Data Flash 基地址 */
#define FLASH_DF_BLOCK_SIZE     (64U)           /* Data Flash 块大小 (字节) */
#define FLASH_DF_WRITE_SIZE     (4U)            /* Data Flash 最小写入粒度 */
#define FLASH_BLOCK_CALIBRATION (FLASH_DF_BASE_ADDR) /* 校准数据存储块 */
#define FLASH_CALIB_MAGIC       (0xA5A5A5A5U)   /* 校准数据有效标识 */
#define FLASH_REF_MAGIC         (0x5A5A5A5AU)   /* 参考电压有效标识 */
#define FLASH_OFFSET_CALIB      (0U)            /* 校准数据块内偏移 */
#define FLASH_OFFSET_REF        (16U)           /* 参考电压块内偏移 (4 字节对齐) */


/* ======================================================================
 *  WDT 看门狗参数
 * ====================================================================== */
#define RSTSR0_PORF_Msk         (1U << 0)   /* 上电复位标志 */
#define RSTSR0_IWDTRF_Msk       (1U << 4)   /* 独立看门狗复位标志 */
#define RSTSR0_WDTRF_Msk        (1U << 5)   /* WDT 复位标志 */
#define RSTSR0_SWRF_Msk         (1U << 6)   /* 软件复位标志 */

/* ======================================================================
 *  高温保护参数
 * ====================================================================== */
#define OVERHEAT_BLINK_COUNT        (5U)    /* 闪烁次数 */
#define OVERHEAT_ON_MS              (125U)  /* ON 半周期 ms */
#define OVERHEAT_OFF_MS             (125U)  /* OFF 半周期 ms */
#define OVERHEAT_CYCLE_MS           (OVERHEAT_ON_MS + OVERHEAT_OFF_MS)  /* 4Hz */

/* ======================================================================
 *  NTC 控制板温度保护 (ADC CH10)
 * ====================================================================== */
#define NTC_OVERHEAT_THRESHOLD      450     /* NTC ADC < 此值 → >100°C, 触发高温保护 */
#define NTC_LOW_TEMP_THRESHOLD      3150    /* NTC ADC > 此值 → <-7°C, 标记低温 */
#define NTC_LOW_TEMP_HYST           100     /* 低温恢复滞回 (ADC值), 需低于(阈值-回差)才退出 */
#define NTC_TEMP_CONFIRM_COUNT      5       /* 连续确认次数 (5×20ms=100ms消抖) */
#define NTC_LOW_TEMP_DUTY_PERCENT   85      /* 低温时激光占空比百分比 (85%) */
#define NTC_LOW_TEMP_REF_PERCENT    75      /* 低温时参考电压百分比 (75%), 独立于占空比 */

/* ======================================================================
 *  光功率自动校准参数
 *  协议: PC上位机读取光功率计 → 发AT+POW=当前值 → APP小步调电压 → 循环至目标
 *  通道: H(SIDE) / V1(HORIZ) / V2(FRONT)
 * ====================================================================== */
/* 目标光功率 (x100, 单位 0.01mW) */
#define POWCAL_TARGET_H         200     /* H  目标 2.00mW */
#define POWCAL_TARGET_V1        100     /* V1 目标 1.00mW */
#define POWCAL_TARGET_V2        100     /* V2 目标 1.00mW */

/* 步进阈值 (x100, 单位 0.01mW) */
#define POWCAL_THRESH_LARGE     50      /* |差值| > 0.50mW → 大步进 */
#define POWCAL_THRESH_MED       20      /* |差值| > 0.20mW → 中步进 */
#define POWCAL_THRESH_SMALL     10      /* 正误差阈值: 0.10mW, power>=target 且 power-target<=此值 → 到位 */

/* 步进电压值 (mV) */
#define POWCAL_STEP_LARGE_MV    200     /* 大步进电压 */
#define POWCAL_STEP_MED_MV      100     /* 中步进电压 */
#define POWCAL_STEP_SMALL_MV    50      /* 小步进电压 */

/* 参考电压限幅 */
#define POWCAL_VOLT_MAX_MV      3300    /* 参考电压上限 */
#define POWCAL_VOLT_MIN_MV      0       /* 参考电压下限 */

/* 保护阈值 */
#define POWCAL_PD_MIN_MV        50      /* PD 初始值低于此值禁止校准 */

/* ======================================================================
 *  sleep mode time
 * ====================================================================== */
#define NO_LASER_POWEROFF_S         (15U * 60U)   /* 900秒 15min */

#endif /* HAWKEYE_CONFIG_H_ */
