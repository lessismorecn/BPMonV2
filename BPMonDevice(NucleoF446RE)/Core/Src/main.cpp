/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
// @suppress("Float formatting support")
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "DspFilters/Butterworth.h"
#include <time.h>
#include "signalDetector.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
using namespace std;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define Pump_Time              25 //How long to pump in seconds (TO DO redesign to work on pressure not pump time)
#define Read_Time              Pump_Time + 40 //How long to read signal including pump time (40 seconds at 50hz = 2000 samples)
#define Sample_Rate            50 //Sample rate in HZ (100hz = 100 samples per sec)
#define Max_Num_Readings       50 //Max Num Readings
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

RTC_HandleTypeDef hrtc;

UART_HandleTypeDef huart4;

/* USER CODE BEGIN PV */

int Timer_Num_Readings = 24; //Number of readings in timer mode
int Timer_Length = 3600; //timer length in seconds
float waitTime = 1.0 / Sample_Rate; //wait time to ensure correct amount of samples taken per second (TO DO - test actual processing time to confirm)

float systolicRatio = 0.85; //Ratios used in calculating sys/dia points (TO DO: Test more with different ratios)
float diastolicRatio = 1.35;

//Init timers
time_t readingStart = NULL;
time_t hourlyStart = NULL;

//Init data arrays/counters
float data[Sample_Rate * (Read_Time - Pump_Time)];
float filteredData[Sample_Rate * (Read_Time - Pump_Time)];
char graphData[7]; //Only used in graph mode
int dataCounter;
long dataCounter2;
int readingCounter = 0;

//Init flags
bool dateHasBeenSet = false;
bool recieveDateFlag = false;
bool graphFlag = false;
bool readFlag = false;

//Init UART buffers/lines
const int buffer_size = 128;
const int line_size = 32;

char rx_buffer[buffer_size + 1];
uint8_t rx_char;
char rx_line[line_size];

//Used for interrupt
volatile int rx_in=0;
volatile int rx_out=0;

//Init Status
enum Status {
	sleeping, idle, reading, calculating, hourly
};

Status status = idle;

struct Reading {
	time_t date;
	float MAP;
	float systolicPressure;
	float diastolicPressure;

};

struct Reading storedReadings[Max_Num_Readings];
int numStoredReadings = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_UART4_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */

void read_line();
void send_line();
void handleSerialMsg(char *msg);
void handleSendMsg(string msg);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void clear_rx_line();
void handleSetDate();
void filterResults();
void displayLastReading();
string convertReadingToString();
void calculatePressures();
void filterResults();

void updateRTC(time_t now);
time_t getUnixTimeRTC();

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {
	/* USER CODE BEGIN 1 */

	/* USER CODE END 1 */

	/* MCU Configuration--------------------------------------------------------*/

	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* USER CODE BEGIN Init */

	/* USER CODE END Init */

	/* Configure the system clock */
	SystemClock_Config();

	/* USER CODE BEGIN SysInit */

	/* USER CODE END SysInit */

	/* Initialize all configured peripherals */
	MX_GPIO_Init();
	MX_ADC1_Init();
	MX_UART4_Init();
	MX_RTC_Init();
	/* USER CODE BEGIN 2 */

    HAL_UART_Receive_IT (&huart4, &rx_char, 1);

	uint32_t adcResult = 0;
	float voltage;
	float pressure;
	/* USER CODE END 2 */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1) {

		/***************************MAIN CODE LOOP SECTION**************************/
		if (status == hourly) { //TO DO - Set up sleep wait time for timer mode

			time_t seconds = getUnixTimeRTC(); //Get current system time

			if (hourlyStart != NULL && (seconds - hourlyStart) < Timer_Length
					&& readingCounter > 0) { //Check current time to timer length

				//Change state to read
				status = reading;
				readingCounter--;
				readingStart = getUnixTimeRTC();

				//Handling sending reading time info to application
				string str1 = "Reading started at\n ";
				string str2 = ctime(&readingStart);
				string str3 = "~";

				string sendMsg = str1 + str2 + str3;

				handleSendMsg(sendMsg);

				//Reset data counters
				dataCounter = 0;
				dataCounter2 = 0;
			}
		} else if (status == idle) {

			HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET); //Motor pin OFF
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET); //Solenoid pin OFF

		} else if (status == reading) {

			time_t seconds = getUnixTimeRTC();//Get current system time

			//Get ADC result
			HAL_ADC_Start(&hadc1);
			HAL_ADC_PollForConversion(&hadc1, 100);
			adcResult = HAL_ADC_GetValue(&hadc1);
			HAL_ADC_Stop(&hadc1);

			//Calculate pressure from adc result
			voltage = adcResult * 0.000244140625;
			pressure = voltage * 3;
			pressure = pressure * 16.66666;
			pressure = pressure * 7.500638;

			if (readingStart == NULL) {
				handleSendMsg("Error, reading start time not recorded~");
				status = idle;
			}

			//Check if we still need to be pumping
			if (readingStart != NULL && (seconds - readingStart) < Pump_Time) {

				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET); //Motor pin ON
				HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET); //Solenoid pin ON

				//If graph mode on (TO DO: redo this completely without datacounter2)
				if ((graphFlag == true) && (dataCounter2 % 30000 == 0)) {

					//Handle send pressure data to application for graphing
					snprintf(graphData, 6, "%.2f", pressure);
					string graphStr = "";
					graphStr += graphData;
					graphStr += "~";
					handleSendMsg(graphStr);
					snprintf(graphData, 6, "");

				}

				dataCounter2++;

			//Check if we need to be reading data
			} else if (readingStart != NULL
					&& (seconds - readingStart < Read_Time)) {

				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET); //Motor pin OFF
				HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET); //Solenoid pin ON

				if (dataCounter < (Sample_Rate * (Read_Time - Pump_Time))) { //Check if counter is less that total amount of samples to be read

					data[dataCounter] = pressure;
					filteredData[dataCounter] = pressure; //Assigning data to be filtered later (TO DO - clean this up)

					if (graphFlag == true) {
						snprintf(graphData, 6, "%.2f", pressure);
						string graphStr = "";
						graphStr += graphData;
						graphStr += "~";
						handleSendMsg(graphStr);
						snprintf(graphData, 6, "");
					}

					dataCounter++;
				} else { //Debugging
					__NOP();
				}

				//Wait for necessary amount of time to record sample rate - (TO DO - check timings)
				HAL_Delay(waitTime);

			//If not pumping or reading then reading should be finished here
			} else {

 				handleSendMsg("Reading finished, calculating results~");
				HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET); //Motor pin OFF
				HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET); //Solenoid pin OFF

				calculatePressures();
				displayLastReading();

				//Check if we have reached the total amount of readings for timer mode
				if (readingCounter > 0) {
					status = hourly;
					hourlyStart = getUnixTimeRTC(); //Set hourly start to current time
				} else {
					status = idle;
					hourlyStart = NULL;
				}
			}
		}

		//Check if message waiting from interrupt
		if (readFlag == true) {
			read_line();
			handleSerialMsg((char*)rx_line);
			readFlag = false;
		}

		/* USER CODE END WHILE */

		/* USER CODE BEGIN 3 */
	}
	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_OFF;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */
  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}


/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void) {

	/* USER CODE BEGIN ADC1_Init 0 */

	/* USER CODE END ADC1_Init 0 */

	ADC_ChannelConfTypeDef sConfig = { 0 };

	/* USER CODE BEGIN ADC1_Init 1 */

	/* USER CODE END ADC1_Init 1 */
	/** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
	 */
	hadc1.Instance = ADC1;
	hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
	hadc1.Init.Resolution = ADC_RESOLUTION_12B;
	hadc1.Init.ScanConvMode = DISABLE;
	hadc1.Init.ContinuousConvMode = DISABLE;
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
	hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	hadc1.Init.NbrOfConversion = 1;
	hadc1.Init.DMAContinuousRequests = DISABLE;
	hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
	if (HAL_ADC_Init(&hadc1) != HAL_OK) {
		Error_Handler();
	}
	/** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
	 */
	sConfig.Channel = ADC_CHANNEL_6;
	sConfig.Rank = 1;
	sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN ADC1_Init 2 */

	/* USER CODE END ADC1_Init 2 */

}

/**
 * @brief UART4 Initialization Function
 * @param None
 * @retval None
 */
static void MX_UART4_Init(void) {

	/* USER CODE BEGIN UART4_Init 0 */

	/* USER CODE END UART4_Init 0 */

	/* USER CODE BEGIN UART4_Init 1 */

	/* USER CODE END UART4_Init 1 */
	huart4.Instance = UART4;
	huart4.Init.BaudRate = 9600;
	huart4.Init.WordLength = UART_WORDLENGTH_8B;
	huart4.Init.StopBits = UART_STOPBITS_1;
	huart4.Init.Parity = UART_PARITY_NONE;
	huart4.Init.Mode = UART_MODE_TX_RX;
	huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart4.Init.OverSampling = UART_OVERSAMPLING_16;
	if (HAL_UART_Init(&huart4) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN UART4_Init 2 */

	/* USER CODE END UART4_Init 2 */

}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_RESET);

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);

	/*Configure GPIO pins : PA5 PA7 */
	GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/*Configure GPIO pin : PB12 */
	GPIO_InitStruct.Pin = GPIO_PIN_12;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/**
 * @brief Handles sending last reading made over UART serial connection
 * @param None
 * @retval None
 */
void displayLastReading() {

	char MAPC[8];
	char systolicC[8];
	char diastolicC[8];

	snprintf(MAPC, 7, "%.2f", storedReadings[numStoredReadings - 1].MAP);
	snprintf(systolicC, 7, "%.2f",
			storedReadings[numStoredReadings - 1].systolicPressure);
	snprintf(diastolicC, 7, "%.2f",
			storedReadings[numStoredReadings - 1].diastolicPressure);

	string MAPstr = "MAP: ";
	MAPstr += MAPC;
	MAPstr += "~";
	string sysStr = "Systolic: ";
	sysStr += systolicC;
	sysStr += "~";
	string diaStr = "Diastolic: ";
	diaStr += diastolicC;
	diaStr += "~";

	handleSendMsg(MAPstr);
	handleSendMsg(sysStr);
	handleSendMsg(diaStr);

}


/**
 * @brief Converts a reading to string (Used in sending all stored readings from device to application)
 * @param struct Reading reading
 * @retval readingString - input reading as a string
 */
string convertReadingToString(struct Reading reading) {
	string readingString = "/";

	char MAPC[8];
	char systolicC[8];
	char diastolicC[8];
	char dateC[13];

	snprintf(MAPC, 7, "%.2f", reading.MAP);
	snprintf(systolicC, 7, "%.2f", reading.systolicPressure);
	snprintf(diastolicC, 7, "%.2f", reading.diastolicPressure);
	snprintf(dateC, 12, "%d", reading.date);

	readingString += dateC;
	readingString += "/";
	readingString += MAPC;
	readingString += "/";
	readingString += systolicC;
	readingString += "/";
	readingString += diastolicC;
	readingString += "/";

	return (readingString);
}

/**
 * @brief Calculates current stored reading by specifier numStoredReadings into MAP/sys/dia pressure values
 * @param None
 * @retval None
 */
void calculatePressures() {

	//Disable interrupts here as we don't want to be reset if reading has been completed
	HAL_NVIC_DisableIRQ(UART4_IRQn);

	//Peak detection needs data to be filtered first
	filterResults();

	//Init variables
	int mapIndex = 0;
	int systolicIndex = 0;
	int diastolicIndex = 0;
	vector<ld> input;
	vector <int> peakIndexes;

	//Variables used for peak detection
	int lag = 25;
	ld threshold = 2.61;
	ld influence = 0.8;


	//Throw away/reset first 300 samples as butterworth filter usually has junk values due to cuff pressure decaying (TO DO: find cleaner way to do this)
	for (int i = 0; i < 300; i++){
		filteredData[i] = 0;
	}

	//Casting floats to LD (TO DO: redo this as LD unnecessary - redo peak detection to floats) SAVE MEMORY HERE!!!
	for (int i = 0; i <Sample_Rate * (Read_Time - Pump_Time); i++ ){
		input.push_back((ld)filteredData[i]);
	}

	//Analyse input for peaks using z-score and assign to output
	vector<int> output = z_score_thresholding(input, lag, threshold, influence);

	//Saving peaks to separate indexes
	int j = 0;
	for (auto it = output.begin(); it != output.end(); it++) {
	            if (*it == 1){
	            	peakIndexes.push_back(j);
	            	}

	            j++;
			}

	//Loop through all peak indexes to find highest/MAP
	for (auto it = peakIndexes.begin(); it != peakIndexes.end(); it++){
		if(filteredData[*it] > filteredData[mapIndex]){
			mapIndex = *it;
		}
	}

	//Assigning sys/dia peak indexes (rough)
	systolicIndex = (int)(mapIndex * systolicRatio);
	diastolicIndex = (int)(mapIndex * diastolicRatio);

	//Basic way of finding nearest true peak to assigned systolic/diastolic peak index (TO DO: redo this to be much cleaner)
	if(output[systolicIndex] != 1 ){
	            for (int i = 1; i < systolicIndex; i++){

	                if (output[systolicIndex + i] == 1){
	                	systolicIndex = systolicIndex + i;
	                    break;
	                }
	                else if (output[systolicIndex - i] == 1){
	                	systolicIndex = systolicIndex - i;
	                    break;
	                }

	            }
	        }

	if(output[diastolicIndex] != 1 ){
		for (int i = 1; i < diastolicIndex; i++){

			if (output[diastolicIndex + i] == 1){
				diastolicIndex = diastolicIndex + i;
				break;
			}
			else if (output[diastolicIndex - i] == 1){
				diastolicIndex = diastolicIndex - i;
				break;
			}

		}
	}

	//Store readings with assigned MAP/sys/dia indexes
	storedReadings[numStoredReadings].date = readingStart;
	storedReadings[numStoredReadings].MAP = data[mapIndex];
	storedReadings[numStoredReadings].systolicPressure = data[systolicIndex];
	storedReadings[numStoredReadings].diastolicPressure = data[diastolicIndex];

	//Increment counter
	numStoredReadings++;

	//Reenable interrupt
	HAL_NVIC_EnableIRQ(UART4_IRQn);

}

/**
 * @brief Filters the results held in filteredData[] - currently using Butterworth filter
 * @param None
 * @retval None
 */
void filterResults() {

	Dsp::SimpleFilter<Dsp::Butterworth::BandPass<2>, 1> f;

	f.setup(2,    // order
			50,   // sample rate
			9.75, // center frequency
			19	  // band width
			);

	float *const*filteredData2;

	float *item = &filteredData[0];

	filteredData2 = &item;

	f.process(Sample_Rate * (Read_Time - Pump_Time), filteredData2);

}

/**
 * @brief Handles setting the current data/time when sent through UART rx_line
 * @param None
 * @retval None
 */
void handleSetDate() {
	//Wait to make sure everything has been sent across
	HAL_Delay(500);
	clear_rx_line();
	read_line();
	time_t now = atoi(rx_line); //Get date from rx_line
	updateRTC(now); //Update device clock
	dateHasBeenSet = true; //Update flag
	clear_rx_line(); //Clear rx_line as date now set
}

/**
 * @brief Handles updating the current data/time from a UNIX time value
 * @param time_t now (Date/time to be set)
 * @retval None
 */
void updateRTC(time_t now)
{

 RTC_TimeTypeDef sTime;
 RTC_DateTypeDef sDate;

 //Convert input to struct tm
 struct tm time_tm;
 time_tm = *(localtime(&now));

 //Set device time
 sTime.Hours = (uint8_t)time_tm.tm_hour;
 sTime.Minutes = (uint8_t)time_tm.tm_min;
 sTime.Seconds = (uint8_t)time_tm.tm_sec;
 if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
 {
	 //Error_Handler(__FILE__, __LINE__);
 }


 //Set device date
 if (time_tm.tm_wday == 0) { time_tm.tm_wday = 7; } // the chip goes mon tue wed thu fri sat sun
 sDate.WeekDay = (uint8_t)time_tm.tm_wday;
 sDate.Month = (uint8_t)time_tm.tm_mon+1;
 sDate.Date = (uint8_t)time_tm.tm_mday;
 sDate.Year = (uint16_t)(time_tm.tm_year+1900-2000); // time.h is years since 1900, chip is years since 2000

 /*
 * update the RTC
 */
 if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
 {
	 //Error_Handler(__FILE__, __LINE__);
 }

 HAL_RTCEx_BKUPWrite(&hrtc,RTC_BKP_DR0,0x32F2); // lock it in with the backup registers

}

/**
 * @brief Gets a UNIX time value from the RTC
 * @param None
 * @retval time_t currentTime
 */
time_t getUnixTimeRTC(){

	 RTC_DateTypeDef rtcDate;
	 RTC_TimeTypeDef rtcTime;
	 //Get time/date from RTC
	 HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN);
	 HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN);
	 uint8_t hh = rtcTime.Hours;
	 uint8_t mm = rtcTime.Minutes;
	 uint8_t ss = rtcTime.Seconds;
	 uint8_t d = rtcDate.Date;
	 uint8_t m = rtcDate.Month;
	 uint16_t y = rtcDate.Year;
	 uint16_t yr = (uint16_t)(y+2000-1900);
	 //Init current time + assign RTC values
	 time_t currentTime = {0};
	 struct tm tim = {0};
	 tim.tm_year = yr;
	 tim.tm_mon = m - 1;
	 tim.tm_mday = d;
	 tim.tm_hour = hh;
	 tim.tm_min = mm;
	 tim.tm_sec = ss;
	 currentTime = mktime(&tim);

	 return (currentTime);‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍‍
}

/**
 * @brief Handles setting the timer length and number of readings from UART data
 * @param None
 * @retval None
 */
void handleSetOptions() {
	//Wait to ensure everything has been sent across
	HAL_Delay(500);

	//Read and set the timer length
	clear_rx_line();
	read_line();
	Timer_Length = atoi((char*)rx_line);

	//Read and set the timer number of readings
	clear_rx_line();
	read_line();
	Timer_Num_Readings = atoi((char*)rx_line);
	clear_rx_line();

}

/**
 * @brief Handles sending the timer length and number of readings across UART serial connection
 * @param None
 * @retval None
 */
void handleSendOptions() {
	char optC[10];
	string optStr = "";
	snprintf(optC, 9, "%d/%d~", Timer_Length, Timer_Num_Readings);
	optStr += optC;

	handleSendMsg(optStr);
}

/**
 * @brief Handles action to be taken on receiving message across UART serial connection
 * @param char *msg - the current message to be actioned
 * @retval None
 */
void handleSerialMsg(char *msg) {

	//If date flag false then we need to set the RTC for any readings date/time to be recorded
	if (!dateHasBeenSet) {
		handleSendMsg("err1~");
		handleSetDate();
	}

	else {
		int msgAsInt = 0;

		//Basic way of converting message to int for use as switch (TO DO: redo this to be cleaner/or just use ints sent over UART)
		for (int i = 0; i <= strlen(msg); i++) {
			msgAsInt = msgAsInt + (int) (msg[i]);
		}

		//Clear rx_line as message should be read now
		clear_rx_line();

		//***********************MAIN MESSAGE RECIEVE SECTION***************/
		switch (msgAsInt) {

		case 538:        //read

			//Make sure we are currently idle
			if (status == idle) {

				//Check we haven't exceeded total amount of readings stored
				if (numStoredReadings + 1 >= Max_Num_Readings - 1) {
					handleSendMsg("err2~");
				} else {

					if (graphFlag == false) {
						handleSendMsg("Switching state to read~");
						HAL_Delay(500);//Delay for message send

						//Switch state to read
						status = reading;
						readingStart = getUnixTimeRTC();
						string str1 = "Reading started at\n ";
						string str2 = ctime(&readingStart);
						string str3 = "~";

						string sendMsg = str1 + str2 + str3;

						handleSendMsg(sendMsg);

						dataCounter = 0;
					} else {
						status = reading;
						readingStart = getUnixTimeRTC();
						dataCounter = 0;
						dataCounter2 = 0;
						HAL_Delay(500);
					}
				}
			} else {
				handleSendMsg("Cannot switch to read as not currently idle~");
			}

			break;

		case 531:        //cancel
			//Switch state to idle if state not currently idle
			if (status != idle) {
				handleSendMsg("Reset, switching state to idle~");
				status = idle;
				readingCounter = 0;
			} else {
				handleSendMsg("Error, system already idle~");
			}
			break;
		case 557:        //24 hour/timer mode
			if (status == idle) {
				//Check if timer amount of readings will exceed maximum
				if (numStoredReadings + Timer_Num_Readings
						>= Max_Num_Readings - 1) {
					handleSendMsg("err2~");
				} else {
					handleSendMsg("Switching state to timed~");
					readingCounter = Timer_Num_Readings;
					hourlyStart = getUnixTimeRTC();
					status = hourly;
				}

				//If already in timer mode send message with nextreading
			} else if (status == hourly) {

				time_t nextReading = hourlyStart + Timer_Length;

				string str1 = "Timer already started, next reading at ";
				string str2 = ctime(&nextReading);
				string str3 = "~";

				string sendMsg = str1 + str2 + str3;
				handleSendMsg(sendMsg);

			} else {
				handleSendMsg("Cannot switch to hourly as not currently idle~");
			}
			break;
		case 560:        //get data
			//Check if we have any readings saved
			if (numStoredReadings == 0) {
				handleSendMsg("err3~");

			} else {
				//Send all stored readings over UART
				for (int i = 0; i < numStoredReadings; i++) {
					handleSendMsg(convertReadingToString(storedReadings[i]));
					HAL_Delay(100);

				}
				//Send end reading message
				handleSendMsg("endr~");
				HAL_Delay(100);
				handleSendMsg("Readings downloaded and wiped from device~");
				//Reset number of stored readings (counter)
				numStoredReadings = 0;
			}

			break;
		case 552:                //graph mode
			graphFlag = !graphFlag;
			break;

		case 579:                //options request

			handleSendOptions();

			break;

		case 580:                //options set
			handleSetOptions();

			break;
		default:                //default msg not recognised
			handleSendMsg("Request not recognised~");
			break;

		}

		return;
	}
}

/**
 * @brief Handles sending message over UART serial connection
 * @param string msg
 * @retval None
 */
void handleSendMsg(string msg) {

	//Copy message to char array
	int n = msg.length();
	char msgUC[n + 1];
	strcpy(msgUC,msg.c_str());

	//Transmit message
	HAL_UART_Transmit(&huart4, (uint8_t*)msgUC, msg.length(), HAL_MAX_DELAY);

	return;

}
/**
 * @brief Clears rx_line of any stored data
 * @param None
 * @retval None
 */
void clear_rx_line(){

    for (int i = 0; i < line_size; i++){

        rx_line[i] = '\0';

        }

}

/**
 * @brief Reads from the rx buffer until end of message char '~'
 * @param None
 * @retval None
 */
void read_line() {


	int i;
	i = 0;

	//Critical Section
	HAL_NVIC_DisableIRQ(UART4_IRQn);
	while ((i==0) || (rx_line[i-1] != '~')) {

		if (rx_in == rx_out) {
			// End Critical Section - need to allow rx interrupt to get new characters for buffer
			        	HAL_NVIC_EnableIRQ(UART4_IRQn);

			            while (rx_in == rx_out) {

			            }
			// Start Critical Section - don't interrupt while changing global buffer variables
			            HAL_NVIC_DisableIRQ(UART4_IRQn);
			        }

		rx_line[i] = rx_buffer[rx_out];
		i++;
		rx_out = (rx_out + 1) % buffer_size;
	}

	//End Critical Section
	HAL_NVIC_EnableIRQ(UART4_IRQn);

}

/**
 * @brief Call when UART rx registers message recieved - reads UART char by char
 * @param None
 * @retval None
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){

	rx_buffer[rx_in] = rx_char;
	rx_in = (rx_in + 1) % buffer_size;

	//If eom character set readflag to true to read line
	if (rx_char == '~'){
		readFlag = true;
	}

    HAL_UART_Receive_IT (&huart4, &rx_char, 1);
	return;
}


/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */

	/* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
