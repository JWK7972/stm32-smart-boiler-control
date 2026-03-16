#include <includes.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define AUTO_TARGET_TEMP        30

#define Boiler_Time_OK          0x01
#define Boiler_Temp_OK          0x02
#define SW7_Action_OK           0x04

#define T_SETTING               0
#define T_LOCKED                1
#define T_RUNNING               2

#define BUZZ_PORT               GPIOD
#define BUZZ_PIN                GPIO_Pin_15

#define SW6_PORT                GPIOC
#define SW6_PIN                 GPIO_Pin_6
#define SW7_PORT                GPIOC
#define SW7_PIN                 GPIO_Pin_7

#define KEY19_PORT              GPIOD
#define KEY19_PIN               GPIO_Pin_10
#define KEY20_PORT              GPIOD
#define KEY20_PIN               GPIO_Pin_11

#define TASK_STK_SIZE           512
#define N_TASKS                 20

#define TASK_START_PRIO         10
#define TASK_1_PRIO             11
#define TASK_2_PRIO             12
#define TASK_3_PRIO             13
#define TASK_4_PRIO             14

#define VREF                    3.3f
#define R10K                    10000.0f
#define BETA                    3950.0f
#define T0                      298.15f
#define R0                      10000.0f

OS_EVENT    *DispSem;
OS_FLAG_GRP *BoilerStatus;
OS_EVENT    *LCDSem;

OS_STK TaskStartStk[TASK_STK_SIZE];
OS_STK Task1Stk[TASK_STK_SIZE];
OS_STK Task2Stk[TASK_STK_SIZE];
OS_STK Task3Stk[TASK_STK_SIZE];
OS_STK Task4Stk[TASK_STK_SIZE];

extern __IO uint16_t ADCConvertedValue[5];

volatile INT8U gPowerOn    = 0;
volatile INT8U gAutoMode   = 0;
volatile INT8U gTimerState = T_SETTING;
volatile INT8U gRemainMin  = 0;
volatile INT8U gRemainSec  = 0;
volatile INT8U gHeaterOn   = 0;

void TaskStart(void *data);
void Task1_Input(void *data);
void Task2_Control(void *data);
void Task3_StatusLCD(void *data);
void Task4_LED(void *data);

void Delay(void);
void Inputs_Init(void);
float read_NTC(void);

int main(void)
{
    INT8U os_err = 0;

    BSP_Init();
    DelayMS(300);

    GLCD_init();
    GLCD_clear();

    LCD_initialize();
    LCD_string(0x80, (uint8_t *)"   Smart Boiler ");
    LCD_string(0xC0, (uint8_t *)"   Initializing ");

    Inputs_Init();

    OSInit();

    os_err = OSTaskCreate(TaskStart,
                          (void *)0,
                          (void *)&TaskStartStk[TASK_STK_SIZE - 1],
                          0);
    (void)os_err;

    OSStart();
    return 0;
}

void TaskStart(void *data)
{
    INT8U err;
    OS_CPU_SR cpu_sr;

    (void)data;

    OS_ENTER_CRITICAL();
    SysTick_Configuration();
    OS_EXIT_CRITICAL();

    OSStatInit();

    DispSem      = OSSemCreate(1);
    BoilerStatus = OSFlagCreate(0x00, &err);
    LCDSem       = OSSemCreate(1);

    CommInit();

    OSTaskCreate(Task1_Input,     (void *)0, (void *)&Task1Stk[TASK_STK_SIZE - 1], TASK_1_PRIO);
    OSTaskCreate(Task2_Control,   (void *)0, (void *)&Task2Stk[TASK_STK_SIZE - 1], TASK_2_PRIO);
    OSTaskCreate(Task3_StatusLCD, (void *)0, (void *)&Task3Stk[TASK_STK_SIZE - 1], TASK_3_PRIO);
    OSTaskCreate(Task4_LED,       (void *)0, (void *)&Task4Stk[TASK_STK_SIZE - 1], TASK_4_PRIO);

    for (;;) {
        OSCtxSwCtr = 0;
        OSTimeDlyHMSM(0, 0, 1, 0);
    }
}

void Task1_Input(void *data)
{
    static INT8U Prev_SW6 = 1;
    static INT8U Prev_SW7 = 1;

    INT8U Curr_SW6;
    INT8U Curr_SW7;
    INT8U err;

    (void)data;

    Prev_SW6 = GPIO_ReadInputDataBit(SW6_PORT, SW6_PIN);
    Prev_SW7 = GPIO_ReadInputDataBit(SW7_PORT, SW7_PIN);

    if (Prev_SW6 == 0) {
        OSFlagPost(BoilerStatus,
                   Boiler_Time_OK + Boiler_Temp_OK,
                   OS_FLAG_SET,
                   &err);
    } else {
        OSFlagPost(BoilerStatus,
                   Boiler_Time_OK + Boiler_Temp_OK,
                   OS_FLAG_CLR,
                   &err);
    }

    if (Prev_SW7 == 0) {
        OSFlagPost(BoilerStatus,
                   SW7_Action_OK,
                   OS_FLAG_SET,
                   &err);
    } else {
        OSFlagPost(BoilerStatus,
                   SW7_Action_OK,
                   OS_FLAG_CLR,
                   &err);
    }

    for (;;) {
        Curr_SW6 = GPIO_ReadInputDataBit(SW6_PORT, SW6_PIN);
        if (Curr_SW6 != Prev_SW6) {
            OSTimeDlyHMSM(0, 0, 0, 50);
            if (GPIO_ReadInputDataBit(SW6_PORT, SW6_PIN) == Curr_SW6) {
                if (Curr_SW6 == 0) {
                    OSFlagPost(BoilerStatus,
                               Boiler_Time_OK + Boiler_Temp_OK,
                               OS_FLAG_SET,
                               &err);
                } else {
                    OSFlagPost(BoilerStatus,
                               Boiler_Time_OK + Boiler_Temp_OK,
                               OS_FLAG_CLR,
                               &err);
                }
                Prev_SW6 = Curr_SW6;
            }
        }

        Curr_SW7 = GPIO_ReadInputDataBit(SW7_PORT, SW7_PIN);
        if (Curr_SW7 != Prev_SW7) {
            OSTimeDlyHMSM(0, 0, 0, 50);
            if (GPIO_ReadInputDataBit(SW7_PORT, SW7_PIN) == Curr_SW7) {
                if (Curr_SW7 == 0) {
                    OSFlagPost(BoilerStatus,
                               SW7_Action_OK,
                               OS_FLAG_SET,
                               &err);
                } else {
                    OSFlagPost(BoilerStatus,
                               SW7_Action_OK,
                               OS_FLAG_CLR,
                               &err);
                }
                Prev_SW7 = Curr_SW7;
            }
        }

        OSTimeDlyHMSM(0, 0, 0, 50);
    }
}

void Task2_Control(void *data)
{
    INT8U err = 0;
    OS_FLAGS value;

    static INT8U  TimerState  = T_SETTING;
    static INT16U SetMin      = 0;
    static INT16U SetSec      = 0;
    static INT16U CurMin      = 0;
    static INT16U CurSec      = 0;
    static INT16U TickCount   = 0;

    static INT8U TempAlarmTriggered = 0;

    static INT8U Prev_KEY19 = 1;
    static INT8U Prev_KEY20 = 1;

    (void)data;

    for (;;) {
        uint16_t raw_pot = ADCConvertedValue[4];
        float t = ((float)raw_pot * 30.0f / 4095.0f) + 10.0f;
        int t10 = (int)(t * 10.0f);
        if (t10 < 0) t10 = -t10;
        int t_int  = t10 / 10;
        int t_frac = t10 % 10;

        value = OSFlagPend(BoilerStatus,
                           Boiler_Time_OK + Boiler_Temp_OK,
                           OS_FLAG_WAIT_SET_ALL,
                           10,
                           &err);

        if (err == OS_NO_ERR) {
            gPowerOn = 1;

            OSSemPend(DispSem, 0, &err);
            GLCD_xy(0, 0); printf("ON                  ");
            GLCD_xy(2, 0); printf("Temp : %2d.%1d        ", t_int, t_frac);
            OSSemPost(DispSem);

            value = OSFlagPend(BoilerStatus,
                               SW7_Action_OK,
                               OS_FLAG_WAIT_SET_ALL,
                               10,
                               &err);

            INT8U is_auto_mode = (err == OS_NO_ERR) ? 1 : 0;
            gAutoMode = is_auto_mode;

            INT16U vr1_temp        = (ADCConvertedValue[2] * 15) / 4095 + 20;
            INT16U target_temp_val = is_auto_mode ? AUTO_TARGET_TEMP : vr1_temp;

            if (t_int >= target_temp_val) {
                if (TempAlarmTriggered == 0) {
                    for (int i = 0; i < 2; i++) {
                        GPIO_SetBits(BUZZ_PORT, BUZZ_PIN);
                        OSTimeDlyHMSM(0, 0, 0, 200);
                        GPIO_ResetBits(BUZZ_PORT, BUZZ_PIN);
                        OSTimeDlyHMSM(0, 0, 0, 200);
                    }
                    TempAlarmTriggered = 1;
                }
            } else {
                if (t_int < target_temp_val - 1) {
                    TempAlarmTriggered = 0;
                }
            }

            {
                INT8U Curr_KEY19 = GPIO_ReadInputDataBit(KEY19_PORT, KEY19_PIN);
                INT8U Curr_KEY20 = GPIO_ReadInputDataBit(KEY20_PORT, KEY20_PIN);

                if ((Curr_KEY19 == 0) && (Prev_KEY19 == 1)) {
                    if (TimerState == T_SETTING) {
                        TimerState = T_LOCKED;
                        CurMin     = SetMin;
                        CurSec     = SetSec;
                    } else if (TimerState == T_LOCKED) {
                        TimerState = T_SETTING;
                    }
                }
                Prev_KEY19 = Curr_KEY19;

                if ((Curr_KEY20 == 0) && (Prev_KEY20 == 1)) {
                    if (TimerState == T_LOCKED) {
                        TimerState  = T_RUNNING;
                        TickCount   = 0;
                        CurMin      = SetMin;
                        CurSec      = SetSec;
                    } else if (TimerState == T_RUNNING) {
                        TimerState = T_LOCKED;
                        CurMin     = SetMin;
                        CurSec     = SetSec;
                    }
                }
                Prev_KEY20 = Curr_KEY20;
            }

            {
                INT16U total_sec = (ADCConvertedValue[3] * 600) / 4096;

                if (TimerState == T_SETTING) {
                    SetMin = total_sec / 60;
                    SetSec = total_sec % 60;
                } else if (TimerState == T_RUNNING) {
                    TickCount++;
                    if (TickCount >= 10) {
                        TickCount = 0;

                        if (CurSec == 0) {
                            if (CurMin == 0) {
                                GPIO_SetBits(BUZZ_PORT, BUZZ_PIN);
                                OSTimeDlyHMSM(0, 0, 0, 500);
                                GPIO_ResetBits(BUZZ_PORT, BUZZ_PIN);

                                TimerState = T_LOCKED;
                                CurMin     = SetMin;
                                CurSec     = SetSec;
                            } else {
                                CurMin--;
                                CurSec = 59;
                            }
                        } else {
                            CurSec--;
                        }
                    }
                }
            }

            OSSemPend(DispSem, 0, &err);

            GLCD_xy(1, 0);
            if (is_auto_mode) {
                if (t_int > AUTO_TARGET_TEMP) {
                    printf("HEATER:OFF         ");
                    gHeaterOn = 0;
                } else {
                    printf("HEATER:ON          ");
                    gHeaterOn = 1;
                }
            } else {
                printf("                    ");
                gHeaterOn = 1;
            }

            GLCD_xy(6, 0);
            if (is_auto_mode) {
                printf("      AUTO          ");
            } else {
                printf("     MANUAL         ");
            }

            GLCD_xy(3, 0);
            if (is_auto_mode) {
                printf("Temp Set: %2d C (FIX)", AUTO_TARGET_TEMP);
            } else {
                printf("Temp Set: %2d C (VR1)", target_temp_val);
            }

            {
                INT16U d_min = (TimerState == T_RUNNING) ? CurMin : SetMin;
                INT16U d_sec = (TimerState == T_RUNNING) ? CurSec : SetSec;

                GLCD_xy(4, 0);
                if (TimerState == T_SETTING) {
                    printf("Time SET : %02d:%02d    ", d_min, d_sec);
                } else if (TimerState == T_LOCKED) {
                    printf("Time LOCK: %02d:%02d    ", d_min, d_sec);
                } else if (TimerState == T_RUNNING) {
                    printf("Time RUN : %02d:%02d    ", d_min, d_sec);
                }

                GLCD_xy(5, 0);
                printf("S19:Fix S20:Go/Stop ");
            }

            OSSemPost(DispSem);

            gTimerState = TimerState;
            if (TimerState == T_RUNNING) {
                gRemainMin = CurMin;
                gRemainSec = CurSec;
            } else {
                gRemainMin = SetMin;
                gRemainSec = SetSec;
            }

        } else {
            gPowerOn    = 0;
            gAutoMode   = 0;
            gTimerState = T_SETTING;
            gHeaterOn   = 0;

            OSSemPend(DispSem, 0, &err);

            GLCD_xy(0, 0); printf("OFF                 ");
            for (int i = 1; i <= 7; i++) {
                GLCD_xy(i, 0);
                printf("                    ");
            }

            OSSemPost(DispSem);

            TempAlarmTriggered = 0;
        }

        OSTimeDlyHMSM(0, 0, 0, 100);
    }
}

void Task3_StatusLCD(void *data)
{
    INT8U err;

    (void)data;

    for (;;) {
        INT8U power   = gPowerOn;
        INT8U mode    = gAutoMode;
        INT8U tstate  = gTimerState;
        INT8U min     = gRemainMin;
        INT8U sec     = gRemainSec;

        OSSemPend(LCDSem, 0, &err);

        if (power) {
            if (mode) {
                LCD_string(0x80, (uint8_t *)"ON  MODE:AUTO   ");
            } else {
                LCD_string(0x80, (uint8_t *)"ON  MODE:MANUAL ");
            }

            if (tstate == T_SETTING) {
                char buf[17];
                sprintf(buf, "SET  %02d:%02d      ", min, sec);
                LCD_string(0xC0, (uint8_t *)buf);
            } else if (tstate == T_LOCKED) {
                char buf[17];
                sprintf(buf, "LOCK %02d:%02d      ", min, sec);
                LCD_string(0xC0, (uint8_t *)buf);
            } else if (tstate == T_RUNNING) {
                char buf[17];
                sprintf(buf, "RUN  %02d:%02d      ", min, sec);
                LCD_string(0xC0, (uint8_t *)buf);
            } else {
                LCD_string(0xC0, (uint8_t *)"                ");
            }
        } else {
            LCD_string(0x80, (uint8_t *)"POWER: OFF      ");
            LCD_string(0xC0, (uint8_t *)"                ");
        }

        OSSemPost(LCDSem);

        OSTimeDlyHMSM(0, 0, 1, 0);
    }
}

void Task4_LED(void *data)
{
    (void)data;

    for (;;) {
        if (gPowerOn) {
            LED_ON(0);
        } else {
            LED_OFF(0);
        }

        if (gPowerOn && gHeaterOn) {
            LED_ON(1);
        } else {
            LED_OFF(1);
        }

        OSTimeDlyHMSM(0, 0, 0, 50);
    }
}

void Inputs_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = SW6_PIN | SW7_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SW6_PORT, &GPIO_InitStructure);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = KEY19_PIN | KEY20_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEY19_PORT, &GPIO_InitStructure);
}

float read_NTC(void)
{
    float Vntc, Rntc, temp;

    Vntc = ((float)ADCConvertedValue[0] / 4095.0f) * VREF;
    Rntc = Vntc * (R10K / (VREF - Vntc));
    temp = 1.0f / ((1.0f / T0) + (1.0f / BETA) * logf(Rntc / R10K));

    return (temp - 273.15f);
}

void Delay(void)
{
    uint16_t nTime;

    for (nTime = 0; nTime < 0x0FFF; nTime++) {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
    while (1) {
    }
}
#endif