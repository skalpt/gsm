#include <SoftwareSerial.h>  
#include <OneWire.h>

static byte queueStatus = 1;
static byte lastStatus = 0;
static unsigned long lastIdleTime = 0;
const byte SETUP_TC35_FIND = 1;
const byte SETUP_TC35_SET_TEXT_MODE = 2;
const byte SETUP_TC35_WAIT_FOR_SIM = 3;
const byte SETUP_TC35_SET_NEW_MSG_IND = 4;
const byte SETUP_TC35_WAIT_FOR_SIM_DATA = 5;
const byte SETUP_TC35_SET_MSG_FULL_IND = 6;
const byte SETUP_TC35_CLEAR_MSGS = 7;
const byte SETUP_DS_FIND = 8;
const byte MAX_SETUP_STEP = 8;
const byte IDLE_AND_READY = 9;
const byte WAITING_FOR_DS = 10;

static boolean queueBusy = false;
static unsigned long timeoutLength;
static unsigned long timeoutTime;
static int timeoutRetries;
static int timeoutRetryCount;
static byte timeoutErrHandleType;
static boolean errorThrown = false;
const byte FAIL_AND_RESUME = 0;
const byte FAIL_AND_RESUME_NEXT = 1;
const byte FAIL_AND_HALT = 2;

SoftwareSerial gsmSerial(2, 3); // Creates a software serial port (rx, tx)
const unsigned int MAXINPUT = 165; // 160 characters for SMS plus a few extra 
static char inputLine[MAXINPUT];
static unsigned int inputPos = 0;
static char incomingNumber[20];
const byte MAX_MSG_LOCATION = 20;
const byte CHECK_MSG_FREQ = 5000;

OneWire ds(A0); // DS18B20 data bus is on pin A0
static byte addr[8]; // Stores the DS18B20 address (ROM code)
static String tempReading = "";

void setup() {
  // Initialize serial ports for communication
  Serial.begin(9600);
  gsmSerial.begin(9600);
  Serial.println("Program started.");
  Serial.println();
  
  initTC35();
}

void initTC35() {
  Serial.println("Powering up peripherals...");
  pinMode(8, INPUT);
  digitalWrite(8, LOW);
  pinMode(8, OUTPUT);
  delay(100);
  pinMode(8, INPUT);
  // We could put a delay here while the TC35 initialises,
  // but instead we will sent ATs until it responds during
  // the setup steps.
  Serial.println("Done.");
  Serial.println();
}

void loop() {
  if (!queueBusy) {
    processQueue();
  } else {
    manageTimeouts();
  }
  readTC35();
}

void processQueue() {
  switch (queueStatus) {
    case SETUP_TC35_FIND:
      Serial.println("Searching for TC35...");
      setTimeoutMgr(1000, 20, FAIL_AND_HALT);
      gsmSerial.print("AT\r"); // Send test command to TC35
      break;
    case SETUP_TC35_SET_TEXT_MODE:
      Serial.println("Enabling text mode...");
      setTimeoutMgr(1000, 3, FAIL_AND_RESUME_NEXT);
      gsmSerial.print("AT+CMGF=1\r"); // Set text mode
      break;
    case SETUP_TC35_WAIT_FOR_SIM:
      Serial.println("Waiting for SIM to initialise...");
      setTimeoutMgr(1000, 20, FAIL_AND_HALT);
      gsmSerial.print("AT+CPIN?\r"); // Send SIM PIN status request to TC35
      break;
    case SETUP_TC35_SET_NEW_MSG_IND:
      Serial.println("Disabling new message indicator...");
      setTimeoutMgr(1000, 3, FAIL_AND_RESUME_NEXT);
      gsmSerial.print("AT+CNMI=0,1,0,0,1\r"); // Set message indicator
      break;
    case SETUP_TC35_WAIT_FOR_SIM_DATA:
      Serial.println("Waiting for TC35 to read SIM data...");
      setTimeoutMgr(1000, 20, FAIL_AND_HALT);
      gsmSerial.print("AT+SSET?\r"); // Send SIM data ready request to TC35
      break;
    case SETUP_TC35_SET_MSG_FULL_IND:
      Serial.println("Disabling \"SMS full\" indicator...");
      setTimeoutMgr(1000, 3, FAIL_AND_RESUME_NEXT);
      gsmSerial.print("AT^SMGO=0\r"); // Set SMS full indicator
      break;
    case SETUP_TC35_CLEAR_MSGS:
      deleteAllMsgs();
      break;
    case SETUP_DS_FIND:
      setTimeoutMgr(1000, 3, FAIL_AND_RESUME_NEXT);
      findDS();
      break;
    case IDLE_AND_READY:
      if (lastIdleTime > CHECK_MSG_FREQ) {
        lastIdleTime = millis();
        checkMsgs();
      }
      break;
    case WAITING_FOR_DS:
      // still needs to be written
      break;
  }
}

void deleteAllMsgs() {
  static int deleteMsgCounter;
  
  if (deleteMsgCounter == 0) {
    Serial.println("Deleting all messages from SIM card...");
    deleteMsgCounter++;
  }
  if (errorThrown) deleteMsgCounter++;

  if (deleteMsgCounter > MAX_MSG_LOCATION) {
    Serial.println("Success!");
    Serial.println();
    deleteMsgCounter = 0;
    queueStatus++;
    clearTimeoutMgr();
  } else {
    setTimeoutMgr(1000, 3, FAIL_AND_RESUME);
    deleteMsg(deleteMsgCounter);
  }
}

void deleteMsg(int locationId) {
  gsmSerial.print("AT+CMGD=");
  gsmSerial.print(locationId);
  gsmSerial.print("\r");
}

void findDS() {
  Serial.println("Searching for DS18B20...");
  ds.reset_search();
  while (ds.search(addr)) {
    if (addr[0] == 0x28 && ds.crc8(addr, 7) == addr[7]) {
      Serial.println("Success!");
      Serial.println();
      queueStatus++;
      clearTimeoutMgr();
      break;
    }
  }
}

void checkMsgs() {
  
}

void setTimeoutMgr(long timeout, int retries, byte errorHandleType) {
  queueBusy = true;
  timeoutLength = timeout;
  timeoutTime = millis() + timeoutLength;
  timeoutRetries = retries;
  timeoutErrHandleType = errorHandleType;
  errorThrown = false;
}

void clearTimeoutMgr() {
  queueBusy = false;
  timeoutRetryCount = 0;
  errorThrown = false;
}

void manageTimeouts() {
  if (millis() > timeoutTime) {
    timeoutRetryCount++;
    timeoutTime = millis() + timeoutLength;
    if (timeoutRetries == -1 || timeoutRetryCount < timeoutRetries) {
      Serial.println("Failed, trying again.");
      queueBusy = false;
    } else if (timeoutRetryCount == timeoutRetries) {
      errorThrown = true;
      switch (timeoutErrHandleType) {
        case FAIL_AND_RESUME:
          Serial.println("Failed, moving on to next item.");
          clearTimeoutMgr();
          break;
        case FAIL_AND_RESUME_NEXT:
          Serial.println("Failed, giving up and moving on.");
          Serial.println();
          queueStatus++;
          clearTimeoutMgr();
          break;
        case FAIL_AND_HALT:
          Serial.println("Failed, giving up and shutting down the queue manager.");
          while(1) { }
          break;
      }
    }
  }
}

void readTC35() {
  if (gsmSerial.available () > 0) {
    while (gsmSerial.available () > 0) {
      char inByte = gsmSerial.read();

      switch (inByte) {
        case '\n':   // end of text
          inputLine[inputPos] = 0;  // Terminating null byte
          processData(inputLine); // Terminator reached! process input_line here...
          inputPos = 0; // Reset buffer for next time
          break;
  
        case '\r':   // discard carriage return
          break;

        default:
          // keep adding if not full ... allow for terminating null byte
          if (inputPos < (MAXINPUT - 1))
            inputLine[inputPos++] = inByte;
          break;
      }
    }
  }
}

void processData(char* data) {
  Serial.println(data);
  
  if (strcmp(data, "OK") == 0) {
    clearTimeoutMgr();
    
    if (queueStatus <= MAX_SETUP_STEP) {
      Serial.println("Success!");
      Serial.println();
      queueStatus++;
    } else if (queueStatus == IDLE_AND_READY) {
      if (incomingNumber[0] == 0) {
        Serial.println("Failed to retrieve number from message.");
        Serial.println();
      } else {
        Serial.println("Success!");
        Serial.println();

        Serial.println("Reading temperature...");
        requestTemp();
      }
    }
  }
}

void requestTemp() {
  if (!ds.search(addr)) {
    ds.reset_search();
    return;
  }
  if (ds.crc8(addr, 7) != addr[7]) {
    return;
  }
  if (addr[0] != 0x28) {
    return;
  }
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);

  //Set a timeout timer
  queueStatus = WAITING_FOR_DS;
}

void readTemp() {
  ds.reset();
  ds.select(addr);
  ds.write(0xBE);
  
  byte readingData[9];
  for (int i = 0; i < 9; i++)
  {
    readingData[i] = ds.read();
  }
  
  if (ds.crc8(readingData, 8) != readingData[8])
  {
    return;
  }
  
  ds.reset_search();

  int TReading = (readingData[1] << 8) + readingData[0];

  byte SignBit = 0;
  if (TReading < 0) SignBit = 1;
  TReading = abs(TReading);

  int Tc_100 = (6 * TReading) + TReading / 4;
  int Whole = Tc_100 / 100;
  int Fract = Tc_100 % 100;

  tempReading = "";
  if (SignBit) tempReading += "-";
  tempReading += Whole;
  tempReading += ".";
  if (Fract < 10) tempReading += "0";
  tempReading += Fract;

  sendSMS();
}

void sendSMS() {

}

void readTC35y() {

  
  //If a character comes in from the cellular module...
  if(gsmSerial.available() > 0){
//    gsm_char=gsmSerial.read();    //Store the char in gsm_char.
//    Serial.print(gsm_char);  //Print it to debug serial
  }
  //Read serial input
  if(Serial.available() > 0){
//    gsm_char=Serial.read();  //Store char in gsm_char (Not really from the gsm, just saving some memory)
    //Evaluate input.
//    if(gsm_char=='s'){
      //Send sms!
      gsmSerial.print("AT+CMGS=+61400509514\r"); //AT command to send SMS
      delay(100);
      gsmSerial.print("Hello Henrik"); //Print the message
      delay(10);
      gsmSerial.print("\x1A"); //Send it ascii SUB
      Serial.println("Message sent");
    }
//  }
}
