/* USER CODE BEGIN Header */
/**
 * STM32G474RE 密碼鎖 + 防盜噴霧
 * LPUART1 (PA2 TX / PA3 RX) 115200 8N1
 * PC8 = 綠燈(密碼正確)
 * PC9 = 紅燈(錯 3 次閃 3 秒)
 * PC5 = 噴霧輸出(按 PC6 後持續 3 秒)
 * PC6 = 按鈕(EXTI 下降緣,最高優先權)
 */
/* USER CODE END Header */
#include "main.h"

/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

UART_HandleTypeDef hlpuart1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);

/* USER CODE BEGIN PV */
char password[16] = "1234";      // 預設密碼(可被重設)
char buf[16];                    // 收到的字元暫存
uint8_t idx = 0;                 // 目前收到幾個字元
uint8_t err_count = 0;           // 錯誤次數
uint8_t mode = 0;                // 0=一般輸入  1=等舊密碼  2=等新密碼

uint8_t rx_byte;                 // UART 單字元接收
volatile uint8_t rx_data = 0;
volatile uint8_t rx_flag = 0;

uint8_t  green_on = 0;           // 綠燈計時
uint32_t green_start = 0;

uint8_t  red_on = 0;             // 紅燈閃爍計時
uint32_t red_start = 0;
uint32_t red_toggle = 0;

volatile uint8_t  spray_on = 0;  // 噴霧計時
volatile uint32_t spray_start = 0;
volatile uint32_t last_press = 0;   // 防彈跳用
/* USER CODE END PV */

/* USER CODE BEGIN 0 */
// 傳字串(一定要放在 wrong 前面)
void send(char *s)
{
    HAL_UART_Transmit(&hlpuart1, (uint8_t*)s, strlen(s), 100);
}

// 密碼錯誤處理(累計 3 次觸發紅燈)
void wrong(void)
{
    err_count++;
    send("Wrong\r\n");

    if (err_count >= 3)
    {
        err_count = 0;
        red_on = 1;
        red_start = HAL_GetTick();
        red_toggle = red_start;
        send("Alarm!\r\n");
    }
}

// UART 收到 1 個字元
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LPUART1)
    {
        rx_data = rx_byte;
        rx_flag = 1;
        HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);   // 繼續收下一個
    }
}

// 按鈕動作(兩種回呼名稱共用)
void spray_start_fn(void)
{
    uint32_t now = HAL_GetTick();
    if (now - last_press > 200 && spray_on == 0)       // 防彈跳 200ms
    {
        last_press = now;
        spray_start = now;
        spray_on = 1;
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET);   // 噴霧開始
    }
}

// 新版 HAL 的回呼名稱
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_6) spray_start_fn();
}

// 舊版 HAL 的回呼名稱
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_6) spray_start_fn();
}
/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_LPUART1_UART_Init();

    /* USER CODE BEGIN 2 */
    HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
    HAL_Delay(500);   // 開機等一下,讓終端機有時間連上
    send("\r\n=== Password Lock Ready ===\r\n");
    send("Enter password (or RESET to change):\r\n");
    /* USER CODE END 2 */

    while (1)
    {
        uint32_t now = HAL_GetTick();

        /* ---- 處理收到的字元 ---- */
        if (rx_flag == 1)
        {
            rx_flag = 0;
            char c = rx_data;

            if (c == '\r' || c == '\n')          // 按下 Enter
            {
                if (idx > 0)
                {
                    buf[idx] = 0;
                    idx = 0;

                    if (mode == 0)                           // 一般模式
                    {
                        if (strcmp(buf, "RESET") == 0)       // 要求重設密碼
                        {
                            mode = 1;
                            send("Enter old password:\r\n");
                        }
                        else if (strcmp(buf, password) == 0) // 密碼正確
                        {
                            err_count = 0;
                            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);
                            green_on = 1;
                            green_start = now;
                            send("OK\r\n");
                        }
                        else
                        {
                            wrong();
                        }
                    }
                    else if (mode == 1)                      // 驗證舊密碼
                    {
                        if (strcmp(buf, password) == 0)
                        {
                            err_count = 0;
                            mode = 2;
                            send("Enter new password:\r\n");
                        }
                        else
                        {
                            mode = 0;
                            wrong();
                        }
                    }
                    else if (mode == 2)                      // 設定新密碼
                    {
                        if (strcmp(buf, "RESET") == 0)
                        {
                            send("Cannot use RESET\r\n");
                        }
                        else
                        {
                            strcpy(password, buf);
                            send("Password changed\r\n");
                        }
                        mode = 0;
                    }
                }
                else
                {
                    send("Enter password (or RESET to change):\r\n");
                }
            }
            else if (idx < 15)
            {
                buf[idx] = c;
                idx++;
            }
        }

        /* ---- 綠燈亮 2 秒後關閉 ---- */
        if (green_on == 1 && now - green_start >= 2000)
        {
            green_on = 0;
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
        }

        /* ---- 紅燈閃 3 秒 ---- */
        if (red_on == 1)
        {
            if (now - red_start >= 3000)
            {
                red_on = 0;
                HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);
            }
            else if (now - red_toggle >= 250)    // 每 250ms 切換一次
            {
                red_toggle = now;
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_9);
            }
        }

        /* ---- 噴霧 3 秒後關閉 ---- */
        if (spray_on == 1 && now - spray_start >= 3000)
        {
            spray_on = 0;
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET);
        }
    }
}

/* ===== 以下為 CubeMX 產生的初始化(通常不用手改) ===== */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_LPUART1_UART_Init(void)
{
    hlpuart1.Instance = LPUART1;
    hlpuart1.Init.BaudRate = 115200;
    hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
    hlpuart1.Init.StopBits = UART_STOPBITS_1;
    hlpuart1.Init.Parity = UART_PARITY_NONE;
    hlpuart1.Init.Mode = UART_MODE_TX_RX;
    hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&hlpuart1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 輸出腳先拉低 */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);

    /* PC5 / PC8 / PC9 輸出 */
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PC6 按鈕:下降緣中斷 + 內部上拉 */
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* 按鈕中斷優先權最高 */
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}
