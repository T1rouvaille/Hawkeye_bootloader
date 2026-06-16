################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ra/fsp/src/r_adc/r_adc.c 

C_DEPS += \
./ra/fsp/src/r_adc/r_adc.d 

OBJS += \
./ra/fsp/src/r_adc/r_adc.o 

SREC += \
ra2e1_boot.srec 

MAP += \
ra2e1_boot.map 


# Each subdirectory must supply rules for building sources it contributes
ra/fsp/src/r_adc/%.o: ../ra/fsp/src/r_adc/%.c
	$(file > $@.in,-mcpu=cortex-m23 -mthumb -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -Wunused -Wuninitialized -Wall -Wextra -Wmissing-declarations -Wconversion -Wpointer-arith -Wshadow -Wlogical-op -Waggregate-return -Wfloat-equal -g -D_RENESAS_RA_ -D_RA_CORE=CM23 -D_RA_ORDINAL=1 -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_boot/src" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_boot/ra/fsp/inc" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_boot/ra/fsp/inc/api" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_boot/ra/fsp/inc/instances" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_boot/ra_gen" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_boot/ra_cfg/fsp_cfg/bsp" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_boot/ra_cfg/fsp_cfg" -I"." -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_boot/ra/arm/CMSIS_6/CMSIS/Core/Include" -std=c99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" -x c "$<")
	@echo Building file: $< && arm-none-eabi-gcc @"$@.in"

