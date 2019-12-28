/*

MQTT-SN 802.15.4 Net
End device Type 1 THR RLP
An MQTT-SN Client implementation based on MRF24J40 and Arduino

Visit project website at https://sites.google.com/view/hobbyiot/projects/mqtt-sn-802-15-4

Twitter: @sivanovbg

This is the End device Type 1 THR Real Low power Arduino code and shcematics. When sleeps it only consumes 4 uA from the power source (measured at VCC = 3.3V). Both DHT11 and MRF24J40MA are powered by Arduino I/Os and are being switched on only when needed.

Type 1 stands for Sensor node and THR means Temperature, Humidity and Reed sensors within.

The implementation is based ot MQTT-SN Specification Version 1.2. Arduino board used is Arduino Pro Mini @ 3.3 V. The 3.3V regulator has to be removed or disconnected as shown on the schematic.

Topics should be predefined on the client and gateway sides and are fixed to 2 positions alpha-numeric string.

Messages supported:

CONNECT

CONNACK

PINGREQ

PINGRESP

PUBLISH

*/

//#define poweronoff

#define command_sleep

#include <SPI.h>
#include <mrf24j.h>   // *** Please use the modified library found within the same repo on GitHub ***
#include "DHT.h"
#include "LowPower.h"

const int pin_reset = 6;
const int pin_cs = 10;
const int pin_interrupt = 2;

#define DHTPWR 8

#define DHTPIN 4
#define DHTTYPE DHT11

Mrf24j mrf(pin_reset, pin_cs, pin_interrupt);

long sleep_timer = 0;
long last_time, last_ping, last_pingresp, last_pub;
long tx_interval = 5000; //5000; // 100 with resp. to sleep interval
long ping_interval = 6000; // 1000
long pub_interval = 18000; // 2000 // 60000
long timeout_interval = 60000; // 10000

boolean message_received = false;
boolean node_connected = false;
boolean connection_timeout = true;
boolean node_subscribed = false;
char rx_buffer[127];
char tx_buffer[127];
uint8_t rx_len;

unsigned long current_time;

float temperature;
float humidity;
long batt_value;

uint8_t temperature_string[10] = "24.00";
uint8_t humidity_string[10];
uint8_t batt_value_string[10];

// Change 0x44 and 0x96 with the 802.15.4 short address bytes of your Client (this node)

uint8_t CONNECT_MSG[] = { 0x0A, 0x04, 0x00, 0x01, 0x03, 0x84, 0xFF, 0xFF, 0x88, 0x96 }; // 0x012C = 5 min; 0x0258 = 10 min; 0x0384 = 15 min; 0x0834 = 35 min duration

uint8_t PINGREQ_MSG[] = { 0x0A, 0x16, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x88, 0x96 };

// Several sample messages used within the current implementation set up

uint8_t PUBLISH_MSGON[] = { 0x0B, 0x0C, 0x00, 'o', '6', 0x88, 0x96, ' ', 'R', 'E', 'D' };
uint8_t PUBLISH_MSGOFF[] = { 0x0B, 0x0C, 0x00, 'o', '6', 0x88, 0x96, 'D', 'a', 'r', 'k' };
uint8_t PUBLISH_MSGTMP[] = { 0x0C, 0x0C, 0x00, 't', '6', 0x88, 0x96, '-', '-', '-', '-', '-' };
uint8_t PUBLISH_MSGHUM[] = { 0x0C, 0x0C, 0x00, 'h', '6', 0x88, 0x96, '-', '-', '-', '-', '-' };

uint8_t PUBLISH_MSGALR[] = { 0x0C, 0x0C, 0x00, 'r', '6', 0x88, 0x96, 'A', 'L', 'E', 'R', 'T' };
uint8_t PUBLISH_MSGOPN[] = { 0x0C, 0x0C, 0x00, 'r', '6', 0x88, 0x96, 'O', 'P', 'E', 'N', ' ' };
uint8_t PUBLISH_MSGCLS[] = { 0x0C, 0x0C, 0x00, 'r', '6', 0x88, 0x96, 'C', 'L', 'O', 'S', 'E' };

uint8_t PUBLISH_MSGBAT[] = { 0x0C, 0x0C, 0x00, 'b', '6', 0x88, 0x96, 'b', 'a', 't', '0', '1' };

uint8_t SUBSCRIBE_MSG[] = { 0x07, 0x12, 0x00, 0x88, 0x96, 's', '6' };

typedef struct
{
  uint8_t Length;
  uint8_t MsgType;
  uint8_t Var[127];
} Message;

Message Msg;

#define OWN_ADDRESS 0x8896    // 802.15.4 short address of the Client (this node)
#define GW_ADDRESS 0x8266    // 802.15.4 short address of the Gateway

#define CONNACK   0x05
#define PINGRESP  0x17
#define PUBLISH   0x0C
#define PUBACK    0x0D
#define SUBACK    0x13

DHT dht(DHTPIN, DHTTYPE);

void setup() { 
  
  Serial.begin(115200);

  Serial.println();
  Serial.println("|MQTT-SN 802.15.4 Net End device Type 1 THR RLP|");
  Serial.println("|----------------------------------------------|");
  Serial.print("|Own address: 0x"); Serial.println(OWN_ADDRESS,HEX);
  Serial.print("|Connection timeout, minutes: "); Serial.println(15);
  Serial.println("|----------------------------------------------|");
  Serial.println();

  attachInterrupt(0, interrupt_routine, FALLING);

  attachInterrupt(1, reed_routine, CHANGE);

  pinMode(2,INPUT_PULLUP); // define input for MRF24J40 interrupt, PU is internal
//  pinMode(2,INPUT); // define input for MRF24J40 interrupt, PU is external
  pinMode(3,INPUT); // define input for reed sensor, PU is external

  pinMode(8, OUTPUT); // power for DHT11

  digitalWrite(DHTPWR, HIGH);  // DHT11 Power on
  
  pinMode(9, OUTPUT); // power for MRF24J40MA

  pinMode(8, OUTPUT); // Test LED
  digitalWrite(9, HIGH); // Test LED ON
  
  last_time = millis();
  interrupts();

  current_time = millis();

  last_time = current_time + tx_interval + 1;
  last_ping = current_time + ping_interval + 1;
  last_pingresp = current_time + timeout_interval + 1;
  last_pub = current_time + pub_interval + 1;

  dht.begin();

  pinMode(DHTPIN, INPUT); // remove DHTPIN internal pull up to save power

  dht_off(); // switch off DHT11

  mrf.reset();
  mrf.init();
  
  mrf.set_pan(0xcfce); // pan ID = 0xABCD // Enter your 802.15.4 PAN ID here
  mrf.address16_write(OWN_ADDRESS);

  // MRF24J40 Inits for timed sleep mode ***************************************************

  mrf.write_short(MRF_INTCON,mrf.read_short(MRF_INTCON)&0xBF); // Enable wake interrupt (bit 6)

  // Must be initial settings...
  
  // 1. Select 100 KHz internal oscillator (default setting) by clearing SLPCLKEN bit
  //  mrf.write_long(MRF_SLPCON0,mrf.read_long(MRF_SLPCON0)&0xFE);
  //  delay(10);
  
  // 2. Initialize SLPCLKSEL = 0x2 (100 kHz Internal oscillator)
  //    mrf.write_long(MRF_RFCON7,0x80);
  
  // 3. Set CLKOUTEN = 1 and SLPCLKDIV = 0x01 done during MRF init phase

  // 3.5 Calibrate
  //    mrf.write_long(MRF_SLPCAL2,mrf.read_long(MRF_SLPCAL2)&0x10);
  //    delay(1000);

  // 4. Set the WAKETIME to 0x0D2 -> 2.1 ms
    mrf.write_long(MRF_WAKETIMEL,0xD2);
    mrf.write_long(MRF_WAKETIMEH,0x00);

}

void interrupt_routine() {

    delay(200);
    mrf.interrupt_handler(); // mrf24 object interrupt routine

//    pinMode(pin_led,OUTPUT);
}

void reed_routine()
{
  mrf_wake();

  if(digitalRead(3) == 1)
  {
    Serial.println("Door/window just OPENED!");
    mrf.send16(GW_ADDRESS,PUBLISH_MSGOPN,sizeof(PUBLISH_MSGOPN));
  }
  else
  {
    Serial.println("Door/window just CLOSED!");
    mrf.send16(GW_ADDRESS,PUBLISH_MSGCLS,sizeof(PUBLISH_MSGCLS));
  }

//  Serial.println("INT1 Change, .............sending alert!");
  
//  mrf.send16(GW_ADDRESS,PUBLISH_MSGALR,sizeof(PUBLISH_MSGALR));

  delay(100);  // ************************************************

//  mrf_sleep();
}

void loop()
{  
  int i;

//  while(1) timed_sleep();  // Test!

//  mrf_wake();

  mrf.check_flags(&handle_rx, &handle_tx);

//  mrf_sleep();

  current_time = millis();
  
  if(current_time - last_time > tx_interval)
    if (!node_connected)
    {
      Serial.println("Initializing wireless module...");
      mrf_init();
      
      last_time = current_time;
      
//      mrf_sleep();
//      delay(1000);
      mrf_wake(); // Check >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

      Serial.println("Sending CONNECT to the Gateway");
//      mrf.rx_flush(); // ***
//      Serial.println(mrf.get_pan(),HEX);
//      Serial.print("Own address is 0x");
//      Serial.println(mrf.address16_read(),HEX);
  
      mrf.send16(GW_ADDRESS,CONNECT_MSG,sizeof(CONNECT_MSG));

//      mrf_sleep(); cpu_sleep(); // Old fasion - replaced with the next line

    }
    else
    {
      mrf_sleep();
    }

    if(message_received)
    {
      Msg.Length = rx_buffer[0];  // Fill in the message fields
      Msg.MsgType = rx_buffer[1];
      for(i=2;i<Msg.Length;i++)
      {
        Msg.Var[i-2] = rx_buffer[i];
      }

      switch (Msg.MsgType)        // Check message type
      {
        case(CONNACK):
        {
          Serial.println("CONNACK received");
          message_received = false;
          node_connected = true;
          connection_timeout = false;

//          mrf_wake(); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

          timed_sleep();
          
        } break;
        
        case(PINGRESP):
        {
          Serial.println("PINGRESP received");
          last_pingresp = current_time;
          message_received = false;
          connection_timeout = false;

          delay(1);

//          mrf_sleep(); cpu_sleep();  // TBD // Old fasion - replaced with the next line

          mrf_wake(); // Fix, do not remove!
          
          timed_sleep();
          
        } break;

        case(PUBACK):
        {
          
//          mrf_sleep();
          
        } break;

        case(SUBACK):
        {
          Serial.println("SUBACK Received.");  
          node_subscribed = true;
          message_received = false;

//          mrf_sleep();
          
        } break;

        case(PUBLISH):
        {
          Serial.println("Publish message received");
          
          if(node_subscribed)
          {
            Serial.print("Processing publish message: ");
//          pubMsg.Flags = rx_buffer[2];
//          pubMsg.topic_id[0] = rx_buffer[3];
//          pubMsg.topic_id[1] = rx_buffer[4];
//          pubMsg.msg_id[0] = rx_buffer[5];
//          pubMsg.msg_id[1] = rx_buffer[6];

            for(i=6;i<Msg.Length;i++)
            {
                Serial.print(rx_buffer[i]);
            }
            Serial.println();
  
            if(rx_buffer[6] == '1')
            {
//              digitalWrite(pin_led,HIGH);
//              delay(100);
//              mrf.send16(GW_ADDRESS,PUBLISH_MSGON,sizeof(PUBLISH_MSGON));

//                temperature = dht.readTemperature();
//                delay(1000);
//                dtostrf(temperature,0,2,temperature_string);
//                uint8_t tmp_len = strlen(temperature_string);
//                Serial.print((char)temperature_string);
//                Serial.print(", len=");
//                Serial.println(tmp_len);
//        
//                for(i=0;i<tmp_len;i++)
//                {
//                  PUBLISH_MSGTMP[i+7] = temperature_string[i];
//                  Serial.print((char)PUBLISH_MSGTMP[i+7]);
//                }
//                PUBLISH_MSGTMP[i+7] = ' ';
//                PUBLISH_MSGTMP[i+8] = 'C';
//                PUBLISH_MSGTMP[i+9] = 0x00;
//                Serial.println();
//                PUBLISH_MSGTMP[i+7+2] = 0x00;
//                PUBLISH_MSGTMP[0] = 7+i+2;
//                delay(1000);        
//                mrf.send16(GW_ADDRESS,PUBLISH_MSGTMP,sizeof(PUBLISH_MSGTMP));
            }
            if(rx_buffer[6] == '0')
            {
//              digitalWrite(pin_led,LOW);
//              delay(100);
//              mrf.send16(GW_ADDRESS,PUBLISH_MSGOFF,sizeof(PUBLISH_MSGOFF));
            }
          }
          else
          {
            Serial.println("Client is not subscribed");
          }
          message_received = false;
//          mrf_sleep();          
        } break;
      }
    }

    current_time = millis();
    
    if(node_connected)
    {
      if (current_time - last_ping > ping_interval)
      {
        mrf_wake();
               
        last_ping = current_time;
        Serial.println("Sending PINGREQ...");
        mrf.send16(GW_ADDRESS,PINGREQ_MSG,sizeof(PINGREQ_MSG));  // Send PING request

        delay(100);

        mrf_sleep();

//        cpu_sleep();

      }
    }

// *** end of loop() routine here *** //    

  current_time = millis();
  
  if(current_time - last_pingresp > timeout_interval)
  {
    last_pingresp = current_time;
//    Serial.print(millis()); Serial.print(": ");
    Serial.println("Connection timeout...");
    connection_timeout = true;
    node_connected = false;
    message_received = false;
    node_subscribed = false;
  }  

  if(node_connected)
    if(current_time - last_pub > pub_interval)
    {
      current_time = millis();
      last_pub = current_time;

      dht_on();
      
      delay(1000);
      Serial.println("Reading temperature...");
      temperature = dht.readTemperature(); // get the current temperature
                                           // and prepare the MQTT-SN message
      dtostrf(temperature,0,0,temperature_string);
      uint8_t tmp_len = strlen(temperature_string);

      for(i=0;i<tmp_len;i++)
      {
        PUBLISH_MSGTMP[i+7] = temperature_string[i];
      }
//      PUBLISH_MSGTMP[i+7] = ' ';
//      PUBLISH_MSGTMP[i+8] = 'C';
      PUBLISH_MSGTMP[i+7] = 0x00;
//      PUBLISH_MSGTMP[i+7+2] = 0x00;
      PUBLISH_MSGTMP[0] = 7+i; // +2 deleted, MQTT-SN message prepared

      Serial.println("Reading humidity...");
      humidity = dht.readHumidity(); // get the current humidity
                                           // and prepare the MQTT-SN message
      dtostrf(humidity,0,0,humidity_string);
      uint8_t hum_len = strlen(humidity_string);

      for(i=0;i<tmp_len;i++)
      {
        PUBLISH_MSGHUM[i+7] = humidity_string[i];
      }
//      PUBLISH_MSGTMP[i+7] = ' ';
//      PUBLISH_MSGTMP[i+8] = 'C';
      PUBLISH_MSGHUM[i+7] = 0x00;
//      PUBLISH_MSGTMP[i+7+2] = 0x00;
      PUBLISH_MSGHUM[0] = 7+i; // +2 deleted, MQTT-SN message prepared      

      batt_value = readVcc();

//      Serial.println( batt_value);

      dtostrf(batt_value,0,0,batt_value_string);

      uint8_t batt_len = strlen(batt_value_string);

      for(i=0;i<batt_len;i++)
      {
        PUBLISH_MSGBAT[i+7] = batt_value_string[i];
      }
//      PUBLISH_MSGBAT[i+7] = ' ';
//      PUBLISH_MSGBAT[i+8] = ' ';
      PUBLISH_MSGBAT[i+7] = 0x00;
//      PUBLISH_MSGBAT[i+7+2] = 0x00;
      PUBLISH_MSGBAT[0] = 7+i; // +2 deleted, MQTT-SN message prepared

      dht_off();
      
      mrf_wake();               // Ready for sending, powering the MRF on.

      Serial.println("Sending temperature...");
              
      mrf.send16(GW_ADDRESS,PUBLISH_MSGTMP,sizeof(PUBLISH_MSGTMP)); // send the temperature value

      delay(500);

      Serial.println("Sending humidity...");
              
      mrf.send16(GW_ADDRESS,PUBLISH_MSGHUM,sizeof(PUBLISH_MSGHUM)); // send the humidity value

      delay(500);
     
      Serial.println("Sending battery voltage...");

      mrf.send16(GW_ADDRESS,PUBLISH_MSGBAT,sizeof(PUBLISH_MSGBAT)); // send the battery value

      delay(500);

      mrf_sleep();
      
//      if(node_subscribed==false)
//      {
//        Serial.println("Sending SUBSCRIBE...");
//        mrf.send16(GW_ADDRESS,SUBSCRIBE_MSG,sizeof(SUBSCRIBE_MSG));
//      }
      
    }
//  cpu_sleep();   
}

void handle_rx() {
    uint8_t i;
    
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
//    Serial.println("Packet received, waiting 100 ms");
    rx_len = mrf.rx_datalength();
    for (i = 0; i < rx_len; i++)
    {
          rx_buffer[i] = mrf.get_rxinfo()->rx_data[i];
    }
//    client_address = rx_buffer[7];
    message_received = true;
    digitalWrite(LED_BUILTIN, HIGH);
}

void handle_tx() {
//    if (mrf.get_txinfo()->tx_ok) {
//        Serial.println("802.15.4 TX went ok, got ack");
//    } else {
//        Serial.print("802.15.4 TX failed after ");Serial.print(mrf.get_txinfo()->retries);Serial.println(" retries\n");
//    }
}

// -------------------------------------------------------------------------------

//#ifdef poweronoff

void spi_on()                                             // using power ON/OFF
{
//  interrupts();
  
//  Serial.print(millis()); Serial.print(": ");
//  Serial.println("Wireless wakes up...");

//  pinMode(11, OUTPUT);  // restore MOSI to Output
  
//  digitalWrite(10, HIGH);  // SS connection = HIGH


//  pinMode(2,INPUT_PULLUP); // enable INT0 pull up

//  digitalWrite(9, HIGH);  // MRF Power on
//  delay(1000);

//  digitalWrite(6,LOW); // MRF Reset release
  
//  mrf.reset();
//  mrf.init();
  
//  mrf.set_pan(0xcfce); // pan ID = 0xABCD // Enter your 802.15.4 PAN ID here
//  mrf.address16_write(OWN_ADDRESS);
  
  Serial.println("SPI Interface is now ON");

  delay(100); // 20 ms shoud be enough...
}

void spi_off()                                             // using power ON/OFF
{
  delay(100); // 1000
  
//  digitalWrite(6, LOW);  // MRF24 Reset

//  digitalWrite(9, LOW); // MRF24 power off

//  pinMode(2,INPUT); // disable INT0 pull up

//  pinMode(11, INPUT); // switch MOSI to Input to reduce current from VCC

//  digitalWrite(11, LOW);
  
//  digitalWrite(2, LOW);
//  digitalWrite(10, LOW);  // SS connection = LOW

//  Serial.print(millis()); Serial.print(": ");
  Serial.println("SPI interface is now OFF");

  delay(100);

}

//#endif // poweronoff

// -------------------------------------------------------------------------------
#ifdef command_sleep

void mrf_wake()
{
//  interrupts();
  

  // Perform register wake-up
  mrf.write_short(MRF_WAKECON,mrf.read_short(MRF_WAKECON)|0x40); // Wake up sequence start
  delay(10);
  mrf.write_short(MRF_WAKECON,mrf.read_short(MRF_WAKECON)&0xBF); // Wake up sequence end

  delay(10);
  // RF State mashine reset
  mrf.write_short(MRF_RFCTL,mrf.read_short(MRF_RFCTL)|0x04); // RF State mashine reset
  delay(10);
  mrf.write_short(MRF_RFCTL,mrf.read_short(MRF_RFCTL)&0xFB); // RF State mashine release

  delay(100); // 20 ms shoud be enough...

  Serial.println("Wireless module is ON");

}

void mrf_sleep()
{

  Serial.println("Wireless module IMMEDIATE SLEEP");

  delay(50);

//  noInterrupts();
//
//  delay(50);
  
  mrf.write_short(MRF_SOFTRST,mrf.read_short(MRF_SOFTRST)|0x04); // LP - Power management reset
  delay(10);
//  mrf.write_short(MRF_SLPACK,0xC8); // LP - Go to Sleep together with the required WAKECNT bits.
  mrf.write_short(MRF_SLPACK,mrf.read_short(MRF_SLPACK)|0x80); // LP - Go to Sleep

  delay(50);

//  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
//  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
//  LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
//
//  delay(50);  // for any reason...
//
//  interrupts();
//
//  delay(50);
//  
//  Serial.print(millis()); Serial.print(": ");
//  Serial.println("Arduino wakes up...Good morning!");
}

#endif // command_sleep
// -------------------------------------------------------------------------------

void cpu_sleep()
{
  uint8_t i;
  
//  Serial.print(millis()); Serial.print(": ");
  Serial.println("Arduino goes to sleep for 64 s"); 

  delay(50);

    // go to sleep for 64 seconds

    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    
  delay(100);  // for any reason...
  
//  Serial.print(millis()); Serial.print(": ");
  Serial.println("Arduino wakes up...Good morning!");  
}
// Timed sleep ****************************************************************************

void timed_sleep()
{

  Serial.println("Wireless module TIMED SLEEP"); 

  delay(10);

  // Place here the MRF set up lines for wake up count down

    // MRF24J40 Inits for timed sleep mode ***************************************************

 // mrf.write_short(MRF_INTCON,mrf.read_short(MRF_INTCON)&0xBF); // Enable wake interrupt (bit 6)

  // Must be initial settings...
  
  // 1. Select 100 KHz internal oscillator (default setting) by clearing SLPCLKEN bit
  //  mrf.write_long(MRF_SLPCON0,mrf.read_long(MRF_SLPCON0)&0xFE);
  //  delay(10);
  
  // 2. Initialize SLPCLKSEL = 0x2 (100 kHz Internal oscillator)
  //    mrf.write_long(MRF_RFCON7,0x80);
  
  // 3. Set CLKOUTEN = 1 and SLPCLKDIV = 0x01 done during MRF init phase

  // 3.5 Calibrate
  //    mrf.write_long(MRF_SLPCAL2,mrf.read_long(MRF_SLPCAL2)&0x10);
  //    delay(1000);

  // 4. Set the WAKETIME to 0x0D2 -> 2.1 ms
  //  mrf.write_long(MRF_WAKETIMEL,0xD2);
  //  mrf.write_long(MRF_WAKETIMEH,0x00);

  // *******************************************************************************************

  // 5. Set the WAKECNT to 0x0C8 -> 2.0 ms
  // using WAKECNT6..0 in SLPACK register and WAKECNT8..7 in RFCTL

    mrf.write_short(MRF_SLPACK,0x48); // bits 0..6 of WAKECNT
    mrf.write_short(MRF_RFCTL,mrf.read_short(MRF_RFCTL)|0x08); // bit 7 of WAKECNT = 1
    mrf.write_short(MRF_RFCTL,mrf.read_short(MRF_RFCTL)&0xEF); // bit 8 of WAKECNT = 0

  // 6. Set MAINCNT value and put MRF into sleep mode
  // Will sleep for 60 s: MAINCNT = 0x005B8D80
    mrf.write_long(MRF_MAINCNT0,0x80);
    mrf.write_long(MRF_MAINCNT1,0x8D);
    mrf.write_long(MRF_MAINCNT2,0xC8); // 0x05 = 15 s; 0x17 = 60 s; 0x64 = 5 min; 0xA5 = 7 min; 0xC8 = 10 min?
    mrf.write_long(MRF_MAINCNT3,0x00);

    mrf.write_long(MRF_MAINCNT3,mrf.read_long(MRF_MAINCNT3)|0x80); // Put MRF into timed sleep!

//    delay(10);

    spi_off();

    Serial.println("Arduino is going to SLEEP");

    digitalWrite(9, LOW); // Test LED OFF

    delay(5);

    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);

    // Sistem will sleep until MRF counts down the desired sleep period
    // and wakes the Arduino MCU module after that

//    noInterrupts();

    mrf.read_short(MRF_INTSTAT); // Read INTSTAT register to clear the interrupt

    Serial.println("Wireless module wakes the Arduino and Arduino is ON");

    digitalWrite(9, HIGH); // Test LED ON
    
    spi_on();

//    interrupts();

    
}

void dht_off()
{
      digitalWrite(DHTPWR, LOW);  // DHT11 Power off
      pinMode(DHTPIN, INPUT); // set to only Imput to remove the internal pull up
      Serial.println("DHT11 Power OFF");
}

void dht_on()
 
{
      digitalWrite(DHTPWR, HIGH);  // DHT11 Power on
      delay(2000);
      
      Serial.println("DHT11 Power ON");
      delay(2000);
}

long readVcc()
{
  long result; // Read 1.1V reference against AVcc
  
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV
  return result;
}

void mrf_init()
{
    mrf.reset();
    mrf.init();
  
    mrf.set_pan(0xcfce); // pan ID = 0xABCD // Enter your 802.15.4 PAN ID here
    mrf.address16_write(OWN_ADDRESS);

    mrf.write_short(MRF_INTCON,mrf.read_short(MRF_INTCON)&0xBF); // Enable wake interrupt (bit 6)

    mrf.write_long(MRF_WAKETIMEL,0xD2);
    mrf.write_long(MRF_WAKETIMEH,0x00);
}
