/**
  ******************************************************************************
    @file    edp7in5b_HD.h
    @author  Waveshare Team
    @version V1.0.0
    @date    23-January-2018
    @brief   This file describes initialisation of 7.5 and 7.5b e-Papers

  ******************************************************************************
*/
#include <ESP8266WiFi.h>
static void EPD_7in5B_HD_Readbusy(void)
{
  Serial.print("\r\ne-Paper busy\r\n");
  unsigned char busy;
  WiFiClient client ;
  do {
    busy = digitalRead(BUSY_PIN);
    ESP.wdtFeed();
    server.handleClient();
  } while (busy);
  Serial.print("e-Paper busy release\r\n");
  delay(200);
}

int EPD_7in5B_HD_init()
{
  EPD_Reset();
  Serial.print("EPD_7in5B_HD_init\r\n");
  EPD_SendCommand(0x12);                   //SWRESET
  EPD_7in5B_HD_Readbusy();
  EPD_Send_1(0X46, 0XF7);
  EPD_7in5B_HD_Readbusy();
  EPD_Send_1(0X47, 0XF7);
  EPD_7in5B_HD_Readbusy();
  EPD_Send_5(0X0C, 0XAE, 0XC7, 0XC3, 0XC0, 0X40);
  EPD_Send_3(0X01, 0XAF, 0X02, 0X01);
  EPD_Send_1(0X11, 0X01);
  EPD_Send_4(0X44, 0X00, 0X00, 0X6F, 0X03);
  EPD_Send_4(0X45, 0XAF, 0X02, 0X00, 0X00);
  EPD_Send_1(0X3C, 0X01);
  EPD_Send_1(0X18, 0X80);
  EPD_Send_1(0X22, 0XB1);
  EPD_SendCommand(0X20);
  EPD_7in5B_HD_Readbusy();
  EPD_Send_2(0X4E, 0X00, 0X00);
  EPD_Send_2(0X4F, 0XAF, 0X02);
  EPD_SendCommand(0X24);

  return 0;
}

void EPD_7IN5B_HD_Show(void)
{
  EPD_Send_1(0X22, 0XC7);
  EPD_SendCommand(0X20);
  delay(10);	        	//!!!The delay here is necessary, 200uS at least!!!
  EPD_7in5B_HD_Readbusy();
  //EPD_Send_1(0X10, 0X01);	//deep sleep
}
