#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "hal/hal.h"

TaskHandle_t xTaskINA = NULL;
TaskHandle_t xTaskUART = NULL;
TaskHandle_t xTaskAPP = NULL;
TaskHandle_t xTaskButton = NULL;

void Task_INA22x(void *pvParameters);
void Task_UART_Command(void *pvParameters);
void Task_APP_Run(void *pvParameters);
void Task_Button_Click(void *pvParameters);

void setup() {
  // put your setup code here, to run once:
  HAL::Sys_Init();
  HAL::Button_Init();
  HAL::INA22x_Init();
  HAL::LCD_Init();
  disableLoopWDT(); // Disable the loop watchdog timer to prevent resets during long operations
  xTaskCreatePinnedToCore(Task_INA22x, "Task_INA22x", 2048, NULL, 2, &xTaskINA, 0); //函数名，任务名，堆栈大小，参数，优先级，任务句柄，核心ID
  xTaskCreatePinnedToCore(Task_UART_Command, "Task_UART_Command", 4096, NULL, 1, &xTaskUART, 0);
  xTaskCreatePinnedToCore(Task_APP_Run, "Task_APP_Run", 4096, NULL, 1, &xTaskAPP, 0);
  xTaskCreatePinnedToCore(Task_Button_Click, "Task_Button_Click", 2048, NULL, 1, &xTaskButton, 0);
  vTaskDelete(NULL); // Delete the default loop task since we will be using our own tasks for handling different functionalities
  // HAL::LCD_FillWindow(0, 0, 240, 240, 0x0000);
}

void loop() {
  // put your main code here, to run repeatedly:
  // HAL::INA22x_GetData(&INA);
  // HAL::UART_Command();
  // HAL::APP_Run();
  // HAL::Button_Click();
  // HAL::Get_FPS();
  // HAL::LOG_INFO("The Loop is running.");
}

void Task_INA22x(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1)
  {
    HAL::INA22x_GetData(&INA);
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10)); // delay for 10ms
  }
}

void Task_UART_Command(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1)
  {
    HAL::UART_Command();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10)); // delay for 10ms
  }
}

void Task_APP_Run(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1)
  {
    HAL::APP_Run();
    // HAL::Get_FPS();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(40)); // delay for 40ms, target 25 FPS for UI updates
  }
}

void Task_Button_Click(void *pvParameters)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1)
  {
    HAL::Button_Click();
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10)); // delay for 10ms
  }
}