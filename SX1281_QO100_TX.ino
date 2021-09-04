//
// 2021-08  QO-100 SAT CW Transmitter with OLED Display
// by Jan Uhrin, OM2JU and Ondrej Kolonicny, OK1CDJ
//
//
// States of program with explanation:
//
//  RUN     - main state, displays freq. and status, responds to keyer or received UDP packets
//  SET_PWM
//  SET_KEYER
//  SET_OFFSET
//  SET_SSID
//  SET_PWD
//  WIFI_RECONNECT
// ...
//
//
// Power level settings - from documentation of LoRa128xF27 module
// LEV dBm	mA		Reg value
// 9  26.4  520 	13
// 8  25.5  426 	10
// 7  23.4  343 	7
// 6  20.85 268 	4
// 5  18.26 229 	1
// 4  15.2 	182 	-2
// 3  12.3 	155 	-5
// 2  9.3 	138 	-8
// 1  6.0 	130 	-12
// 0  3.0 	125 	-15
//
// SX128x datasheet p. 73:   Reg vale 0 = -18dBm, Reg value 31 = 13dBm  ==> PA of the module has gain 32.2 dB
//
//

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

//#include <WiFi.h>
#include "time.h"
#include <SPI.h>                                               //the lora device is SPI based so load the SPI library                                         
#include <SX128XLT.h>                                          //include the appropriate library  
#include "Settings.h"                                          //include the setiings file, frequencies, LoRa settings etc   


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);



#define CW_SIDETONE_FREQ  600  // Hz

#define PUSH_BTN_PRESSED 0
#define WAIT_Push_Btn_Release(milisec)  delay(milisec);while(digitalRead(ROTARY_ENC_PUSH)==0){}


// Program state
enum state_t {
   S_RUN = 0,
   S_RUN_RUN,
   S_TOP_MENU_ITEMS,
   S_RUN_CQ,
   S_SET_SPEED_WPM,
   S_SET_OUTPUT_POWER,
   S_SET_KEYER_TYPE,
   S_SET_FREQ_OFFSET,
   S_SET_TEXT_GENERIC,
   S_WIFI_RECONNECT,
   S_RUN_BEACON
} program_state;

enum set_text_state_t {
   S_SET_MY_CALL = 0,
   S_SET_WIFI_SSID,
   S_SET_WIFI_PWD
} set_text_state;


//
// for($i=-1;$i<13;$i++) { print $i+1 . " "; print 18.26+0.7*$i . " " . 10**((18.26+0.7*$i)/10). "\n"; }
// 0 17.56 57.0164272280748
// 1 18.26 66.9884609416526
// 2 18.96 78.7045789695099
// 3 19.66 92.4698173938223
// 4 20.36 108.642562361707
// 5 21.06 127.643880881134
// 6 21.76 149.968483550237
// 7 22.46 176.197604641163
// 8 23.16 207.014134879104
// 9 23.86 243.220400907382
// 10 24.56 285.759054337495
// 11 25.26 335.737614242955
// 12 25.96 394.457302075279
// 13 26.66 463.446919736288
//
#define PowerArrayMiliWatt_Size 5
//
const uint32_t PowerArrayMiliWatt [][2] = {
  { 50,  0 },   // cca 50 mW
  { 100, 4 },   // cca 100 mW
  { 200, 8 },   // cca 200 mW
  { 330, 11 },  // cca 300 mW
  { 450, 13 }   // cca 450 mW
};
//

const char * TopMenuArray[] = { 
  "1. Main LoRaCW    ",
  "2. CQ...          ",
  "3. Set WPM        ",
  "4. Set Out Power  ",
  "5. Set Keyer Typ  ",
  "6. Set Offset Hz  ",
  "7. Set My Call    ",
  "8. Set WiFi SSID  ",
  "9. Set WiFi PWD   ",
  "10. WiFi Reconn.  ",
  "11. Beacon (vvv)  "
 };

struct RotaryEncounters
{
   int32_t cntVal;
   int32_t cntMin;
   int32_t cntMax;
   int32_t cntIncr;
   int32_t cntValOld;
};

hw_timer_t * timer = NULL;
//TFT_eSPI     tft   = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
SX128XLT LT;                      // Create a library class instance called LT

uint8_t  rotaryA_Val = 0xFF;
uint8_t  rotaryB_Val = 0xFF;
uint8_t  ISR_cnt = 0;
uint8_t  cntIncrISR;
uint32_t loopCnt = 0;
uint32_t menuIndex;
uint8_t  keyerVal      = 1;
uint8_t  pushBtnVal    = 1;
uint8_t  keyerDotPressed = 0;
uint8_t  keyerDashPressed = 0;
uint8_t  keyerCWstarted = 0;
uint8_t  pushBtnPressed = 0;
uint8_t stop = 0;

uint32_t FreqWord = 0;
uint32_t FreqWordNoOffset = 0;
uint32_t WPM_dot_delay;

char   freq_ascii_buf[20];   // Buffer for formatting of FREQ value
char   general_ascii_buf[40];
uint8_t general_ascii_buf_index = 0;
char   mycall_ascii_buf[40];
char   wifi_ssid_ascii_buf[40];
char   wifi_pwd_ascii_buf[40];

String s_mycall_ascii_buf;
String s_wifi_ssid_ascii_buf;
String s_wifi_pwd_ascii_buf;
String s_general_ascii_buf;


char   cq_message_buf[] = " CQ CQ DE ";
char   cq_message_end_buf[] = " +K";
char   beacon_message_buf[] = "VVV  VVV  VVV  TEST  ";

RotaryEncounters RotaryEnc_FreqWord;
RotaryEncounters RotaryEnc_MenuSelection;
RotaryEncounters RotaryEnc_KeyerSpeedWPM;
RotaryEncounters RotaryEnc_KeyerType;
RotaryEncounters RotaryEnc_OffsetHz;
RotaryEncounters RotaryEnc_OutPowerMiliWatt;
RotaryEncounters RotaryEnc_TextInput_Char_Index;
RotaryEncounters RotaryEncISR;

Preferences preferences;

// Copy values of variable pointed by r1 to RotaryEncISR variable
void RotaryEncPush(RotaryEncounters *r1) {
   RotaryEncISR = *r1;
   RotaryEncISR.cntValOld = RotaryEncISR.cntVal + 1;  // Make Old value different from actual in order to e.g. force initial update of display
}

// Copy values of RotaryEncISR variable to variable pointed by r1
void RotaryEncPop(RotaryEncounters *r1) {
   *r1 = RotaryEncISR;
}


// Display - Prepare box and cursor for main field
void display_mainfield_begin() {
  display.setTextColor(WHITE);  
  //display.clearDisplay();
  display.fillRect(0,10,SCREEN_WIDTH,35,BLACK);  // x, y, width, height
  display.setTextSize(1);
  display.setCursor(10, 25);
}

// Display - Prepare box and cursor for value field
void display_valuefield_begin () {
  display.fillRect(5,43-2,SCREEN_WIDTH-10,12,WHITE);  // x, y, width, height
  display.fillRect(6,43-2+1,SCREEN_WIDTH-10-2,12-2,BLACK);  // x, y, width, height
  display.setTextColor(WHITE);  
  display.setTextSize(1);
  display.setCursor(10, 43);
  //display.fillRect(10,30,90,50,BLACK);
}

// Display - Clear the valuefield box
void display_valuefield_clear () {
  display.fillRect(5,43-2,SCREEN_WIDTH-10,12,BLACK);  // x, y, width, height
}

// Display - show values on status bar
void display_status_bar () {
  display.fillRect(0,SCREEN_HEIGHT-9-9,SCREEN_WIDTH,9+9,WHITE);  // x, y, width, height
  display.setTextColor(BLACK);  
  display.setTextSize(1);
  display.setCursor(1, SCREEN_HEIGHT-8);  // 7 is the font height
  display.print(s_mycall_ascii_buf);
  display.setCursor(52, SCREEN_HEIGHT-8);  // 7 is the font height
  display.print(RotaryEnc_KeyerSpeedWPM.cntVal);
  display.print("wpm, ");
  //display.setCursor(40, SCREEN_HEIGHT-7); // 7 is the font height
  display.print(PowerArrayMiliWatt[RotaryEnc_OutPowerMiliWatt.cntVal][0]);
  display.print("mW");

  display.setCursor(1, SCREEN_HEIGHT-8-8);  // 7 is the font height
  display.print("WiFi: ");
  display.print(s_wifi_ssid_ascii_buf);

  display.display();
  //... tbd
}


// Limit values of RotaryEncISR.cntVal
void limitRotaryEncISR_values() {
 if (RotaryEncISR.cntVal >= RotaryEncISR.cntMax) { RotaryEncISR.cntVal = RotaryEncISR.cntMax; }
 if (RotaryEncISR.cntVal <= RotaryEncISR.cntMin) { RotaryEncISR.cntVal = RotaryEncISR.cntMin; }
}

// Calculate duration of DOT from WPM
void Calc_WPM_dot_delay ( uint32_t wpm) {
  WPM_dot_delay = (uint32_t) (double(1200.0) / (double) wpm);
}

// Send CW tone with duration duration_ms 
void sendCW(uint16_t duration_ms) {
  //digitalWrite(LED1, HIGH);
  ledcWriteTone(3, CW_SIDETONE_FREQ);
  LT.writeCommand(RADIO_SET_FS, 0, 0);
  LT.txEnable();
  LT.writeCommand(RADIO_SET_TXCONTINUOUSWAVE, 0, 0);
  delay(duration_ms);
  ledcWriteTone(3, 0);
  digitalWrite(LED1, LOW);
  LT.setMode(MODE_STDBY_RC); // This should terminate TXCONTINUOUSWAVE
  LT.rxEnable();
}


//
// Refer to state diagram in page 57. - Figure 10-1: Transceiver Circuit Modes
//
// 1. Command SET_TXCONTINUOUSWAVE will get the TRX state machine to TX mode
// 2. Command SET_FS will get the TRX state machine to FS mode
//
// We should never go to STDBY mode since we want that PLL always runs
//
// Start CW - from FS to TX mode
void startCW() {
  //digitalWrite(LED1, HIGH);
  ledcWriteTone(3, CW_SIDETONE_FREQ);
//  LT.writeCommand(RADIO_SET_FS, 0, 0);
//  LT.txEnable();
  LT.writeCommand(RADIO_SET_TXCONTINUOUSWAVE, 0, 0);
}

// Stop CW - from TX to FS mode
void stopCW() {
  ledcWriteTone(3, 0);
  digitalWrite(LED1, LOW);
  LT.writeCommand(RADIO_SET_FS, 0, 0);
  //LT.setMode(MODE_STDBY_RC); // This should terminate TXCONTINUOUSWAVE
  //LT.rxEnable();
}

// Flash the LED
void led_Flash(uint16_t flashes, uint16_t delaymS)
{
  uint16_t index;
  for (index = 1; index <= flashes; index++)
  {
    //digitalWrite(LED1, HIGH);
    ledcWriteTone(3, CW_SIDETONE_FREQ);
    delay(delaymS);
    ledcWriteTone(3, 0);
    digitalWrite(LED1, LOW);
    delay(delaymS);
  }
}

//-----------------------------------------------------------------------------
// Encoding a character to Morse code and playing it
void morseEncode ( unsigned char rxd ) {
	uint8_t i, j, m, mask, morse_len;

	if (rxd >= 97 && rxd < 123) {		// > 'a' && < 'z'
	  rxd = rxd - 32;					// make the character uppercase
	}
	//
	if ((rxd < 97) && (rxd > 12)) {		// above 96 no valid Morse characters
	  m   = Morse_Coding_table[rxd-32];
    morse_len = (m >> 5) & 0x07;
    mask = 0x10;
	  if (morse_len >= 6) { 
		  morse_len = 6;
      mask = 0x20;
	  }
	  //
	  for (i=0;i<morse_len;i++) {
    startCW();
		if ((m & mask) > 0x00) { // Dash
      delay(WPM_dot_delay);
      delay(WPM_dot_delay);
      delay(WPM_dot_delay);
		} else { // Dot
      delay(WPM_dot_delay);
		}
    stopCW();
		// Dot-wait between played dot/dash 
    delay(WPM_dot_delay);
		mask = mask >> 1;
	  } //end for(i=0...
	  // Dash-wait between characters
    delay(WPM_dot_delay);
    delay(WPM_dot_delay);
    delay(WPM_dot_delay);
	  //
	} //if (rxd < 97...
}


void loop()
{
   // Some debug
   /*
   Serial.print("-- ");
   Serial.print(program_state);
   Serial.print(" ");
   Serial.print(RotaryEncISR.cntVal);
   Serial.print(" ");
   //Serial.print(cntValOld);
   Serial.println("");
   */
   //delay(150);

   //-----------------------------------------
   // -- Straight keyer
   if (RotaryEnc_KeyerType.cntVal & 0x00000001) {
     // Keyer pressed
     if ((keyerVal&0x01) == 0)  { 
       if (keyerCWstarted == 0) {
         startCW();
       }
       keyerCWstarted = 1;         
       delay(10);
     }
     // Keyer released
     if ((keyerVal&0x01) == 1) { 
       keyerCWstarted = 0;         
       stopCW();
       delay(10);
     }
   } else {
     // -- Iambic keywer
     // Keyer pressed DOT
     if (((keyerVal&0x01) == 0) || (keyerDotPressed == 1)) {
       startCW();
       delay(WPM_dot_delay);
       ((keyerVal&0x01) == 0) ? keyerDotPressed  = 1 : keyerDotPressed = 0;
       ((keyerVal&0x02) == 0) ? keyerDashPressed = 1 : keyerDashPressed = 0;
       stopCW();
       delay(WPM_dot_delay);
     }
     // Keyer pressed DASH
     if (((keyerVal&0x02) == 0) || (keyerDashPressed == 1)) {
       startCW();
       delay(WPM_dot_delay);
       delay(WPM_dot_delay);
       ((keyerVal&0x01) == 0) ? keyerDotPressed  = 1 : keyerDotPressed = 0;
       ((keyerVal&0x02) == 0) ? keyerDashPressed = 1 : keyerDashPressed = 0;
       delay(WPM_dot_delay);
       stopCW();
       delay(WPM_dot_delay);
     }
   }
   //-----------------------------------------
   switch (program_state) {
      //--------------------------------
      case S_RUN:
        display_valuefield_clear();
        program_state = S_RUN_RUN;
      break;
      //--------------------------------
      case S_RUN_RUN:
        // Update display with Frequency info when it has changed
        if (RotaryEncISR.cntVal != RotaryEncISR.cntValOld) {
          limitRotaryEncISR_values();
          display_mainfield_begin();
          format_freq(RegWordToFreq(RotaryEncISR.cntVal), freq_ascii_buf);
          //format_freq(RegWordToFreq(RotaryEncISR.cntVal) - 2400000000, freq_ascii_buf);
          //display.println(RegWordToFreq(RotaryEncISR.cntVal),DEC);      
          display.print(freq_ascii_buf);
          display.display();      
          JUsetRfFrequency(RegWordToFreq(RotaryEncISR.cntVal), RotaryEnc_OffsetHz.cntVal);  // Offset     
          display_status_bar();
        }
        //
        RotaryEncISR.cntValOld = RotaryEncISR.cntVal;
        //
        // This has to be at very end since with RotaryEncPush we are making RotaryEncISR.cntValOld different from RotaryEncISR.cntVal
        if (pushBtnVal == PUSH_BTN_PRESSED) {
          WAIT_Push_Btn_Release(200);
          program_state = S_TOP_MENU_ITEMS;
          RotaryEncPop(&RotaryEnc_FreqWord);
          RotaryEncPush(&RotaryEnc_MenuSelection);
        }
        //
      break;
      //--------------------------------
      case S_TOP_MENU_ITEMS:
        // Wrap aroud
        menuIndex = RotaryEncISR.cntVal % (sizeof(TopMenuArray) / sizeof(TopMenuArray[0]));
        // Update display if there is change
        if (RotaryEncISR.cntVal != RotaryEncISR.cntValOld) {
          display_mainfield_begin();
          display.print(TopMenuArray[menuIndex]);
          display.display();
        }
        RotaryEncISR.cntValOld = RotaryEncISR.cntVal;
        //
        if (pushBtnVal == PUSH_BTN_PRESSED) {
          WAIT_Push_Btn_Release(200);
          RotaryEncPop(&RotaryEnc_MenuSelection);
          switch (menuIndex) {
          // -----------------------------
          case 0:
            program_state = S_RUN;
            RotaryEncPush(&RotaryEnc_FreqWord); // makes RotaryEncISR.cntValOld different from RotaryEncISR.cntVal  ==> forces display update
          break;
          // -----------------------------
          case 1:
            program_state = S_RUN_CQ;
            display_valuefield_begin();
            display.print("CQ...");
            display.display();
          break;
          // -----------------------------
          case 2:
            program_state = S_SET_SPEED_WPM;
            RotaryEncPush(&RotaryEnc_KeyerSpeedWPM); // makes RotaryEncISR.cntValOld different from RotaryEncISR.cntVal  ==> forces display update
          break;
          // -----------------------------
          case 3:
            program_state = S_SET_OUTPUT_POWER;
            RotaryEncPush(&RotaryEnc_OutPowerMiliWatt); // makes RotaryEncISR.cntValOld different from RotaryEncISR.cntVal  ==> forces display update
          break;
          // -----------------------------
          case 4:
            program_state = S_SET_KEYER_TYPE;
            RotaryEncPush(&RotaryEnc_KeyerType); // makes RotaryEncISR.cntValOld different from RotaryEncISR.cntVal  ==> forces display update
          break;
          // -----------------------------
          case 5:
            program_state = S_SET_FREQ_OFFSET;
            RotaryEncPush(&RotaryEnc_OffsetHz); // makes RotaryEncISR.cntValOld different from RotaryEncISR.cntVal  ==> forces display update
          break;
          // -----------------------------
          case 6:
            program_state  = S_SET_TEXT_GENERIC;
            set_text_state = S_SET_MY_CALL;
            s_general_ascii_buf = s_mycall_ascii_buf.substring(0);
            RotaryEncPush(&RotaryEnc_TextInput_Char_Index);
            RotaryEncISR.cntVal = s_general_ascii_buf[0];
            general_ascii_buf_index = 0;
          break;
          // -----------------------------
          case 7:
            program_state  = S_SET_TEXT_GENERIC;
            set_text_state = S_SET_WIFI_SSID;
            s_general_ascii_buf = s_wifi_ssid_ascii_buf.substring(0);
            RotaryEncPush(&RotaryEnc_TextInput_Char_Index);
            RotaryEncISR.cntVal = s_general_ascii_buf[0];
            general_ascii_buf_index = 0;
          break;
          // -----------------------------
          case 8:
            program_state  = S_SET_TEXT_GENERIC;
            set_text_state = S_SET_WIFI_PWD;
            s_general_ascii_buf = s_wifi_pwd_ascii_buf.substring(0);
            RotaryEncPush(&RotaryEnc_TextInput_Char_Index);
            RotaryEncISR.cntVal = s_general_ascii_buf[0];
            general_ascii_buf_index = 0;
          break;
          // -----------------------------
          case 9:
            program_state = S_WIFI_RECONNECT;
          break;
          // -----------------------------
          case 10:
            program_state = S_RUN_BEACON;
            display_valuefield_begin();
            display.print("BEACON...");
            display.display();
          break;
          // -----------------------------          
          default:
            program_state = S_RUN;
          break;
          }
        }        
      break;
      //--------------------------------
      case S_RUN_CQ:
        stop = 0;
        for(int j=0;(j<3) && !stop;j++) {
          // CQ
          for(int i=0;(i<sizeof(cq_message_buf)) && !stop;i++) {
            morseEncode(cq_message_buf[i]);
            if (pushBtnVal == PUSH_BTN_PRESSED) { stop++; }
          }
          // My Call
          for(int i=0;(i<s_mycall_ascii_buf.length()) && !stop;i++) {
            morseEncode(s_mycall_ascii_buf[i]);
            if (pushBtnVal == PUSH_BTN_PRESSED) { stop++; }
          }
          //
          morseEncode(' ');
          // My Call
          for(int i=0;(i<s_mycall_ascii_buf.length()) && !stop;i++) {
            morseEncode(s_mycall_ascii_buf[i]);
            if (pushBtnVal == PUSH_BTN_PRESSED) { stop++; }
          }
        } // j
        // +K
        for(int i=0;(i<sizeof(cq_message_end_buf)) && !stop;i++) {
          morseEncode(cq_message_end_buf[i]);
          if (pushBtnVal == PUSH_BTN_PRESSED) { stop++; }
        }
        WAIT_Push_Btn_Release(200);
        RotaryEncPush(&RotaryEnc_FreqWord);
        //display_valuefield_begin();
        //display.print("         ");
        program_state = S_RUN;
        display.display();
      break;
      //--------------------------------
      case S_SET_SPEED_WPM:
        if (RotaryEncISR.cntVal != RotaryEncISR.cntValOld) {
          limitRotaryEncISR_values();
          display_valuefield_begin();
          display.print(RotaryEncISR.cntVal);
          //display.print("   ");
          display.display();
          Calc_WPM_dot_delay(RotaryEncISR.cntVal);  // this will set the WPM_dot_delay variable
        }
        RotaryEncISR.cntValOld = RotaryEncISR.cntVal;
        if (pushBtnVal == PUSH_BTN_PRESSED) {
          WAIT_Push_Btn_Release(200);
          preferences.putInt("KeyerWPM", RotaryEncISR.cntVal);
          RotaryEncPop(&RotaryEnc_KeyerSpeedWPM);
          RotaryEncPush(&RotaryEnc_FreqWord);
          //display_valuefield_begin();
          //display.print("         ");
          //display.display();
          program_state = S_RUN;
        }        
      break;
      //--------------------------------
      case S_SET_OUTPUT_POWER:
        if (RotaryEncISR.cntVal != RotaryEncISR.cntValOld) {
          limitRotaryEncISR_values();
          display_valuefield_begin();
          //RotaryEncISR.cntVal = RotaryEncISR.cntVal % (sizeof(PowerArrayMiliWatt) / sizeof(uint32_t)); // safe
          RotaryEncISR.cntVal = RotaryEncISR.cntVal % PowerArrayMiliWatt_Size;
          display.print(PowerArrayMiliWatt[RotaryEncISR.cntVal][0]);
          display.print(" mW    ");
          display.display();
          LT.setTxParams(PowerArrayMiliWatt[RotaryEncISR.cntVal][1], RADIO_RAMP_10_US);
        }
        RotaryEncISR.cntValOld = RotaryEncISR.cntVal;
        if (pushBtnVal == PUSH_BTN_PRESSED) {
          WAIT_Push_Btn_Release(200);
          preferences.putInt("OutPower", RotaryEncISR.cntVal);
          RotaryEncPop(&RotaryEnc_OutPowerMiliWatt);
          RotaryEncPush(&RotaryEnc_FreqWord);
          program_state = S_RUN;
        }        
      break;
      //--------------------------------
      case S_SET_KEYER_TYPE:
        if (RotaryEncISR.cntVal != RotaryEncISR.cntValOld) {
          display_valuefield_begin();
          display.print(RotaryEncISR.cntVal%2 ?  "Straight  " : "Iambic    ");
          display.display();
        }
        RotaryEncISR.cntValOld = RotaryEncISR.cntVal;
        //
        if (pushBtnVal == PUSH_BTN_PRESSED) {
          WAIT_Push_Btn_Release(200);
          preferences.putInt("KeyerType", RotaryEncISR.cntVal);
          RotaryEncPop(&RotaryEnc_KeyerType);
          RotaryEncPush(&RotaryEnc_FreqWord);
          program_state = S_RUN;
        }        
      break;
      //--------------------------------
      case S_SET_FREQ_OFFSET:
        if (RotaryEncISR.cntVal != RotaryEncISR.cntValOld) {
          limitRotaryEncISR_values();
          JUsetRfFrequency(RegWordToFreq(RotaryEncISR.cntVal), RotaryEncISR.cntVal);  // Offset     
          display_valuefield_begin();
          display.print(RotaryEncISR.cntVal);
          display.print("   ");
          display.display();
        }
        RotaryEncISR.cntValOld = RotaryEncISR.cntVal;
        //
        if (pushBtnVal == PUSH_BTN_PRESSED) {
          WAIT_Push_Btn_Release(200);
          preferences.putInt("OffsetHz", RotaryEncISR.cntVal);
          RotaryEncPop(&RotaryEnc_OffsetHz);
          RotaryEncPush(&RotaryEnc_FreqWord);
          program_state = S_RUN;
        }        
      break;
      //--------------------------------
      case S_SET_TEXT_GENERIC:
        //if (RotaryEncISR.cntVal != RotaryEncISR.cntValOld) {
          limitRotaryEncISR_values();
          delay(150);
          display_valuefield_begin();
          if (loopCnt % 2) {
            s_general_ascii_buf[general_ascii_buf_index] = RotaryEncISR.cntVal;
          } else {
            s_general_ascii_buf[general_ascii_buf_index] = ' ';
          }
          display.print(s_general_ascii_buf);    
          display.display();
          // Set it back to valid character after displaying     
          s_general_ascii_buf[general_ascii_buf_index] = RotaryEncISR.cntVal;
        //}
        RotaryEncISR.cntValOld = RotaryEncISR.cntVal;
        //
        if (pushBtnVal == PUSH_BTN_PRESSED) {
          WAIT_Push_Btn_Release(200);
          // 127 = End of string
          if (RotaryEncISR.cntVal == 127) {
            s_general_ascii_buf[general_ascii_buf_index] = 0;  // Terminate string with null
            switch (set_text_state) {
              //---------------------
              case S_SET_MY_CALL:
                preferences.putString("MyCall", s_general_ascii_buf);          
                s_mycall_ascii_buf = s_general_ascii_buf.substring(0); 
              break;
              //---------------------
              case S_SET_WIFI_SSID:
                preferences.putString("MySSID", s_general_ascii_buf);          
                s_wifi_ssid_ascii_buf = s_general_ascii_buf.substring(0); 
              break;
              //---------------------
              case S_SET_WIFI_PWD:
                preferences.putString("MyPWD", s_general_ascii_buf);          
                s_wifi_pwd_ascii_buf = s_general_ascii_buf.substring(0); 
              break;
              //---------------------
              default:
              break;
            }
            RotaryEncPush(&RotaryEnc_FreqWord);
            program_state = S_RUN;
          } else {
            general_ascii_buf_index++;
            if (general_ascii_buf_index >= s_general_ascii_buf.length()) {
              s_general_ascii_buf+=RotaryEncISR.cntVal;;
              RotaryEncISR.cntVal = s_general_ascii_buf[general_ascii_buf_index];
            } else {
              RotaryEncISR.cntVal = s_general_ascii_buf[general_ascii_buf_index];
            }
            RotaryEncISR.cntValOld = RotaryEncISR.cntVal + 1;   // Force display update in next cycle
          }
        }
      break;
      //--------------------------------
      case S_WIFI_RECONNECT:
      break;
      //--------------------------------
      case S_RUN_BEACON:
        //
        //
        for(int j=0;j<10;j++) {
          pushBtnPressed=0;
          for(int i=0;i<sizeof(beacon_message_buf);i++) {
            morseEncode(beacon_message_buf[i]);
            if (pushBtnVal == PUSH_BTN_PRESSED) { pushBtnPressed=1; break; }
          }
          if (pushBtnPressed != 0) break;
        }
        //if (pushBtnPressed) {
          WAIT_Push_Btn_Release(200);
          RotaryEncPush(&RotaryEnc_FreqWord);
          display_valuefield_begin();
          display.print("         ");
          display.display();  
          program_state = S_RUN;
        //}        
      break;
      //--------------------------------
      default:
      break;
   } // switch program_state

   loopCnt++;

}  // loop


void IRAM_ATTR onTimer() {
  //portENTER_CRITICAL_ISR(&timerMux);
  //interruptCounter++;
  //digitalWrite(LED_PIN,LEDtoggle);
  //LEDtoggle = !LEDtoggle;  
  //portEXIT_CRITICAL_ISR(&timerMux);


   uint32_t incr_val;
   //
   if (ISR_cnt != 255) ISR_cnt++;
   //
   keyerVal = digitalRead(KEYER_DASH)<<1 | digitalRead(KEYER_DOT);
   //
   pushBtnVal = digitalRead(ROTARY_ENC_PUSH);
   // SW debounce
   rotaryA_Val = (rotaryA_Val<<1 | (uint8_t)digitalRead(ROTARY_ENC_A)) & 0x0F;
   //
   // Rising edge --> 0001
   if (rotaryA_Val == 0x01) {
      //timeout_cnt = 0;
      rotaryB_Val = digitalRead(ROTARY_ENC_B);
      // Rotation speedup
      (ISR_cnt <= 20) ? cntIncrISR = RotaryEncISR.cntIncr<<4 : cntIncrISR = RotaryEncISR.cntIncr;
      ISR_cnt = 0;
      //
      if (rotaryB_Val == 0) {
          RotaryEncISR.cntVal += cntIncrISR;
         //RotaryEncISR.cntVal = RotaryEncISR.cntVal + cntIncrISR;
      } else {
          RotaryEncISR.cntVal -= cntIncrISR;
         //RotaryEncISR.cntVal = RotaryEncISR.cntVal - cntIncrISR;
      }
   }
  
 
}




void setup() {
  pinMode(ROTARY_ENC_A,    INPUT_PULLUP);   
  pinMode(ROTARY_ENC_B,    INPUT_PULLUP);   
  pinMode(ROTARY_ENC_PUSH, INPUT_PULLUP);  
  pinMode(KEYER_DOT,       INPUT_PULLUP);  
  pinMode(KEYER_DASH,      INPUT_PULLUP); 
  pinMode(TCXO_EN, OUTPUT); 
  digitalWrite(TCXO_EN,1);
  // pinMode(LED1, OUTPUT);                                   //setup pin as output for indicator LED
  //
  // Configure BUZZER functionalities.
  ledcSetup(3, 8000, 8);   //PWM Channel, Freq, Resolution
  /// Attach BUZZER pin.
  ledcAttachPin(BUZZER, 3);  // Pin, Channel
 
  // Timer for ISR which is processing rotary encoder events
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 3500, true);  // 2500 = 2.5 msec
  timerAlarmEnable(timer);

  RotaryEnc_FreqWord.cntVal  = FreqToRegWord(Frequency);
  RotaryEnc_FreqWord.cntMin  = FreqToRegWord(2400000000);
  RotaryEnc_FreqWord.cntMax  = FreqToRegWord(2400500000);
  RotaryEnc_FreqWord.cntIncr = 1;

  RotaryEnc_MenuSelection    = {1000000, 0, 2000000, 1, 0};   // We will implement modulo to wrap around menu items
  RotaryEnc_KeyerSpeedWPM    = {20, 10, 30, 1, 0};
  RotaryEnc_KeyerType        = {1000000, 0, 2000000, 1, 0};   // We will implement modulo 
  RotaryEnc_OffsetHz         = {Offset, -100000, 100000, 100, 0};
  RotaryEnc_OutPowerMiliWatt = {PowerArrayMiliWatt_Size-1, 0, PowerArrayMiliWatt_Size-1, 1, 0};
  RotaryEnc_TextInput_Char_Index  = {65, 33, 127, 1, 66};


  // Get configuration values stored in EEPROM/FLASH
  preferences.begin("my-app", false);   // false = RW mode
  // Get the counter value, if the key does not exist, return a default value of XY
  // Note: Key name is limited to 15 chars.
  RotaryEnc_KeyerSpeedWPM.cntVal    = preferences.getInt("KeyerWPM", 20);
  RotaryEnc_KeyerType.cntVal        = preferences.getInt("KeyerType", 0);
  RotaryEnc_OffsetHz.cntVal         = preferences.getInt("OffsetHz", 0);
  RotaryEnc_OutPowerMiliWatt.cntVal = preferences.getInt("OutPower", PowerArrayMiliWatt_Size-1);  // Max output power

  //mycall_ascii_buf                  = preferences.getString("MyCall", "CALL???", 40);
  //wifi_ssid_ascii_buf               = preferences.getString("MySSID", "SSID???", 40);
  //wifi_pwd_ascii_buf                = preferences.getString("MyPWD",  "PWD???",  40);

  s_mycall_ascii_buf                  = preferences.getString("MyCall", "CALL???");
  s_wifi_ssid_ascii_buf               = preferences.getString("MySSID", "??");
  s_wifi_pwd_ascii_buf                = preferences.getString("MyPWD",  "PWD???");


  Calc_WPM_dot_delay(RotaryEnc_KeyerSpeedWPM.cntVal);

  RotaryEncPush(&RotaryEnc_FreqWord);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    //for(;;); // Don't proceed, loop forever
  }

  
  display.clearDisplay();
  display.fillRect(0,0,SCREEN_WIDTH,9,WHITE);  // x, y, width, height
  display.setTextSize(1);
  display.setTextColor(BLACK); 
  display.setCursor(1, 1);
  display.println(" QO-100 CW TX 2.4GHz");
  display.display();
  delay(1000);
 
  //led_Flash(2, 125);                                       //two quick LED flashes to indicate program start

  Serial.begin(115200);
  Serial.println();
  Serial.print(F(__TIME__));
  Serial.print(F(" "));
  Serial.println(F(__DATE__));
  Serial.println();
  Serial.println(F(Program_Version));
  Serial.println();
  Serial.println();

  //SPI.begin();
  SPI.begin(SCK, MISO, MOSI, NSS);
  Serial.println(F("SPI OK..."));

  //SPI beginTranscation is normally part of library routines, but if it is disabled in library
  //a single instance is needed here, so uncomment the program line below
  //SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  //setup hardware pins used by device, then check if device is found
   if (LT.begin(NSS, NRESET, RFBUSY, DIO1, DIO2, DIO3, RX_EN, TX_EN, LORA_DEVICE))  {
    Serial.println(F("LoRa Device found"));
    led_Flash(1, 50);
    delay(30);
    led_Flash(1, 50*3);
    //delay(125);
    led_Flash(1, 50);
  } else {
    Serial.println(F("No device responding"));
    while (1) { led_Flash(50, 50); } //long fast speed LED flash indicates device error 
  }

 //The function call list below shows the complete setup for the LoRa device using the information defined in the
  //Settings.h file.
  //The 'Setup LoRa device' list below can be replaced with a single function call;
  //LT.setupLoRa(Frequency, Offset, SpreadingFactor, Bandwidth, CodeRate);

  
  //***************************************************************************************************
  //Setup LoRa device
  //***************************************************************************************************
  LT.setMode(MODE_STDBY_RC);
  LT.setRegulatorMode(USE_LDO);
  LT.setPacketType(PACKET_TYPE_GFSK);
  JUsetRfFrequency(Frequency, Offset);
  LT.setBufferBaseAddress(0, 0);
  LT.setModulationParams(SpreadingFactor, Bandwidth, CodeRate);
  LT.setPacketParams(12, LORA_PACKET_VARIABLE_LENGTH, 255, LORA_CRC_ON, LORA_IQ_NORMAL, 0, 0);
  //LT.setModulationParams(GFS_BLE_BR_0_125_BW_0_3, GFS_BLE_MOD_IND_0_35, BT_0_5);
  LT.setTxParams(TXpower, RADIO_RAMP_10_US);
  //LT.setDioIrqParams(IRQ_RADIO_ALL, (IRQ_TX_DONE + IRQ_RX_TX_TIMEOUT), 0, 0);
  LT.setDioIrqParams(IRQ_RADIO_NONE, 0, 0, 0);
  //***************************************************************************************************

  LT.txEnable();  //This will stay alway ON as we transmit only
  

  Serial.println();
  LT.printModemSettings();                               //reads and prints the configured LoRa settings, useful check
  Serial.println();
  LT.printOperatingSettings();                           //reads and prints the configured operating settings, useful check
  Serial.println();
  Serial.println();
  LT.printRegisters(0x900, 0x9FF);                       //print contents of device registers, normally 0x900 to 0x9FF
  Serial.println();
  Serial.println();

  Serial.print(F("Transmitter ready"));
  Serial.println();

  LT.setTxParams(PowerArrayMiliWatt[RotaryEnc_OutPowerMiliWatt.cntVal][1], RADIO_RAMP_10_US);


}


//
// JU: Modified function to set frequency since definition of FREQ_STEP was not precise enough (198.364 - should be 50e6/2**18=198.3642578125)
#define FREQ_STEP_JU 198.3642578125

void JUsetRfFrequency(uint32_t frequency, int32_t offset)
{

  // first we call original function so that private variables savedFrequency and savedOffset are updated
  //LT.setRfFrequency(frequency, offset);

  FreqWordNoOffset = FreqToRegWord(frequency);
  frequency = frequency + offset;
  uint8_t buffer[3];
  FreqWord = FreqToRegWord(frequency);
  buffer[0] = ( uint8_t )( ( FreqWord >> 16 ) & 0xFF );
  buffer[1] = ( uint8_t )( ( FreqWord >> 8 ) & 0xFF );
  buffer[2] = ( uint8_t )( FreqWord & 0xFF );
  LT.writeCommand(RADIO_SET_RFFREQUENCY, buffer, 3);
}

// Get RegWord from frequency
uint32_t FreqToRegWord(uint32_t frequency) {
  return ( uint32_t )( (double) frequency / (double)FREQ_STEP_JU);
}

// Get frequency from RegWord
uint32_t RegWordToFreq(uint32_t freqword) {
  return (uint32_t)((double)freqword * (double)FREQ_STEP_JU);
}

// Will add decimal point as separator between thousands, milions...
void format_freq(uint32_t n, char *out)
{
    int c;
    char buf[20];
    char *p;

    sprintf(buf, "%u", n);
    c = 2 - strlen(buf) % 3;
    for (p = buf; *p != 0; p++) {
       *out++ = *p;
       if (c == 1) {
           *out++ = '.';
       }
       c = (c + 1) % 3;
    }
    *--out = 0;
}