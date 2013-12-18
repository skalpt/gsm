/*
Arduino 1.0.1
 This sketch reads TC35 SMS to control items
 connected to the Arduino.
 
 The readTC35 and readkeyboard functions are from
 Process incoming serial data without blocking by Nick Gammon
 http://gammon.com.au/serial
 The rest of the sketch is by me
 
 Run this sketch and enter the folowing AT commands
 in the Serial Monitor to set user profile
 you only need to do this once.
 
 AT+CMGF=1          for txt mode
 AT+CNMI=2,1,0,0,1  message indication
 AT^SMGO=1          SMS full indication
 AT&W               store to memory
 
 To check what you have entered
 AT&V               display current configuration 
 
 */



#include <SoftwareSerial.h>
SoftwareSerial gsmSerial(2,3);


//-------- TC35 GSM ---------------
int SMS_location_number;
const unsigned int MAX_INPUT = 165; // 160 characters for SMS plus a few extra 
static unsigned int input_pos = 0;


void setup() {

  Serial.begin(9600);
  gsmSerial.begin(9600);


  //--- turn on TC35 ---
  // wire pin 8 Arduino to IGT pin on TC35
  // it grounds IGN pin for 100 ms
  // this is the same as pressing the button
  // on the TC35 to start it up

  pinMode(8, INPUT);
  digitalWrite(8, LOW);
  pinMode(8, OUTPUT);
  delay(100);
  pinMode(8, INPUT);

}//------ End setup -------

void loop() {

  readTC35();
  readKeyboard();

}//------ End loop --------

//---------------------------- Read TC35 ------------------------------------

// Read data from the TC35, When a linefeed is read the data is processed

void readTC35(){

  static char input_line [MAX_INPUT];
  //static unsigned int input_pos = 0;

  if (gsmSerial.available () > 0) 
  {
    while (gsmSerial.available () > 0) {
      char inByte = gsmSerial.read ();

      switch (inByte)
      {

      case '\n':   // end of text
        input_line [input_pos] = 0;  // terminating null byte

        // terminator reached! process input_line here ...
        process_data (input_line);

        // reset buffer for next time
        input_pos = 0;  
        break;

      case '\r':   // discard carriage return
        break;

      default:
        // keep adding if not full ... allow for terminating null byte
        if (input_pos < (MAX_INPUT - 1))
          input_line [input_pos++] = inByte;
        break;

      }  // end of switch
    }  // end of while incoming data
  }  // end of if incoming data
}  // end of readTC35

//---------------------------- Read Keyboard --------------------------------

void readKeyboard(){

  static char input_line [MAX_INPUT];
  //static unsigned int input_pos = 0;

  if (Serial.available () > 0) 
  {
    while (Serial.available () > 0) {
      char inByte = Serial.read ();

      switch (inByte)
      {

      case '\n':   // end of text
        input_line [input_pos] = 0;  // terminating null byte

        // terminator reached! process input_line here ...
        // if its an AT command, send it to TC35 without processing
        // ---------------- a --------------------- A -------------------- t -------------------- T --
        if(input_line[0] == 97 || input_line[0] == 65 && input_line[1] == 116 || input_line[1] == 84){
          gsmSerial.println(input_line);
        }
        else{
          process_data (input_line);
        }
        // reset buffer for next time
        input_pos = 0;  
        break;

      case '\r':   // discard carriage return
        break;

      default:
        // keep adding if not full ... allow for terminating null byte
        if (input_pos < (MAX_INPUT - 1))
          input_line [input_pos++] = inByte;
        break;

      }  // end of switch
    }  // end of while incoming data
  }  // end of if incoming data
}  // end of readKeyboard

//---------------------------- process_data --------------------------------

void process_data (char * data){

  // display the data

    Serial.println (data);

  if(strstr(data, "??")){    // If data contains ?? display the menu
    Serial.print("\r\n Keyboard Help Menu\r\n ??         This Menu\r\n readsms    List the SMS messages\r\n smsgone    Delete all SMS messages\r\n");
  }

  if(strstr(data, "+CMGR:") && strstr(data, "+448080808080")){  
    // Reads the +CMGR line to check if SMS is from a known Phone number
    // This if statement could cover the whole of the process_data function
    // then only known a phone number could control the Arduoino
  }

  if(strstr(data, "readsms")){    // If data contains readsms
    gsmSerial.println("AT+CMGL=\"ALL\"");  // Read all SMS on the SIM
  }

  if(strstr(data, "smsgone")){
    delete_All_SMS();
  }

  if(strstr(data, "^SMGO: 2")){ // SIM card FULL
    delete_All_SMS();           // delete all SMS
  }

  if(strstr(data, "+CMTI:")){    // An SMS has arrived
    char* copy = data + 12;      // Read from position 12 until a non ASCII number to get the SMS location
    SMS_location_number = (byte) atoi(copy);  // Convert the ASCII number to an int
    gsmSerial.print("AT+CMGR=");
    gsmSerial.println(SMS_location_number);  // Print the SMS in Serial Monitor
  }                                          // this SMS data will go through this process_data function again 
                                             // any true if statements will execute

  if(strstr(data, "Heating on")){              // If data contains Heating on
    Serial.println("Heating is switched ON");  // Control your syuff here
    //delete_one_SMS();  // delete the SMS if you want
  }

  if(strstr(data, "Heating off")){
    Serial.println("Heating is switched OFF");
    //delete_one_SMS();
  }

  if(strstr(data, "Lights on")){
    Serial.println("Lights are ON");
    //delete_one_SMS();
  }

  if(strstr(data, "Lights off")){
    Serial.println("Lights are OFF");
    //delete_one_SMS();
  }
}  //--------------------------- end of process_data ---------------------------

void delete_one_SMS(){
  Serial.print("deleting SMS ");
  Serial.println(SMS_location_number);
  gsmSerial.print("AT+CMGD=");
  gsmSerial.println(SMS_location_number);
}

void delete_All_SMS(){
  for(int i = 1; i <= 20; i++) {
    gsmSerial.print("AT+CMGD=");
    gsmSerial.println(i);
    Serial.print("deleting SMS ");
    Serial.println(i);
    delay(500);
  }
}