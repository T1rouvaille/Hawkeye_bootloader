################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/startup.c \
../ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/system.c 

C_DEPS += \
./ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/startup.d \
./ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/system.d 

OBJS += \
./ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/startup.o \
./ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/system.o 

SREC += \
ra2e1_app.srec 

MAP += \
ra2e1_app.map 


# Each subdirectory must supply rules for building sources it contributes
ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/%.o: ../ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/%.c
	$(file > $@.in,-mcpu=cortex-m23 -mthumb -O2 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -Wunused -Wuninitialized -Wall -Wextra -Wmissing-declarations -Wconversion -Wpointer-arith -Wshadow -Wlogical-op -Waggregate-return -Wfloat-equal -g -D_RENESAS_RA_ -D_RA_CORE=CM23 -D_RA_ORDINAL=1 -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_app/src" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_app/ra/fsp/inc" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_app/ra/fsp/inc/api" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_app/ra/fsp/inc/instances" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_app/ra_gen" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_app/ra_cfg/fsp_cfg/bsp" -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_app/ra_cfg/fsp_cfg" -I"." -I"C:/Users/AT403/Desktop/Hawkeye_code/RA2E1_OTA_Boot_App/ra2e1_app/ra/arm/CMSIS_6/CMSIS/Core/Include" -std=c99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" -x c "$<")
	@echo Building file: $< && arm-none-eabi-gcc @"$@.in"

