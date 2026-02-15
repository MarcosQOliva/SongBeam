#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

#include <Audio.h>
#include <Wire.h>
#include <TimeLib.h>
#include <TimeAlarms.h>

extern "C" uint32_t set_arm_clock(uint32_t frequency);

char postfix[5]=".wav";
char devname[5]="R01_";

int recordState=0;

//write wav
unsigned long ChunkSize = 0L;
unsigned long Subchunk1Size = 16;
unsigned int AudioFormat = 1;
unsigned int numChannels = 8;

int PIN_BUTTON = 23;

unsigned int PINRED = 14;
unsigned int PINBLUE = 15;
unsigned int PINGREEN = 16;

unsigned int verboseOutput=1;


unsigned long sampleRate = 44100;
unsigned int bitsPerSample = 16;
unsigned long byteRate = sampleRate*numChannels*(bitsPerSample/8);// samplerate x channels x (bitspersample / 8)
unsigned int blockAlign = numChannels*bitsPerSample/8;
unsigned long Subchunk2Size = 0L;
unsigned long recByteSaved = 0L;
unsigned long NumSamples = 0L;
int startHr = 0;
int startMin = 0;
int durationMins = 0;

byte byte1, byte2, byte3, byte4;

AudioAmplifier           amp1;           //xy=367,269
AudioAmplifier           amp2;           //xy=367,269
AudioAmplifier           amp3;           //xy=367,269
AudioAmplifier           amp4;           //xy=367,269
AudioAmplifier           amp5;           //xy=367,269
AudioAmplifier           amp6;           //xy=367,269
AudioAmplifier           amp7;           //xy=367,269
AudioAmplifier           amp8;           //xy=367,269


AudioInputI2SOct         audioInput;


AudioRecordQueue         queue1;
AudioRecordQueue         queue2;    
AudioRecordQueue         queue3;
AudioRecordQueue         queue4;
AudioRecordQueue         queue5;
AudioRecordQueue         queue6;
AudioRecordQueue         queue7;
AudioRecordQueue         queue8;     


AudioConnection          patchCord1(audioInput, 0, amp1, 0);
AudioConnection          patchCord2(audioInput, 1, amp2, 0);
AudioConnection          patchCord3(audioInput, 2, amp3, 0);
AudioConnection          patchCord4(audioInput, 3, amp4, 0);
AudioConnection          patchCord5(audioInput, 4, amp5, 0);
AudioConnection          patchCord6(audioInput, 5, amp6, 0);
AudioConnection          patchCord7(audioInput, 6, amp7, 0);
AudioConnection          patchCord8(audioInput, 7, amp8, 0);




AudioConnection          patchCord11(amp1, queue1);
AudioConnection          patchCord12(amp2, queue2);
AudioConnection          patchCord13(amp3, queue3);
AudioConnection          patchCord14(amp4, queue4);
AudioConnection          patchCord15(amp5, queue5);
AudioConnection          patchCord16(amp6, queue6);
AudioConnection          patchCord17(amp7, queue7);
AudioConnection          patchCord18(amp8, queue8);


byte buffer1[256];
byte buffer2[256];
byte buffer3[256];
byte buffer4[256];
byte buffer5[256];
byte buffer6[256];
byte buffer7[256];
byte buffer8[256];


byte bufferw[512];
byte bufferx[512];
byte buffery[512];
byte bufferz[512];
byte bufferv[512];

float ampgain=1.0;

uint32_t FILE_SIZE=0;
float FILE_SPACE=0;
int LOW_FILE_SPACE=0;
float LOW_FILE_SPACE_THRESHOLD=0;

float BATT_THRESHOLD=4.5;
int LO_BATT=0;

int mode = 0;  // 0=stopped, 1=recording, 2=playing
FsFile frec;
File frec2;
elapsedMillis  msecs;

int filecount=0;

int buttonState =0;

int recmins=15;
unsigned int tsamplemillis = 60000*recmins;


time_t begintime=0;
time_t endtime=0;
time_t offtime=0;

int hiclock=600000000;
//int hiclock=450000000;
//int hiclock=24000000;

#define SDCARD_CS_PIN    BUILTIN_SDCARD


void setup() {

  set_arm_clock(hiclock);
  
  Serial.begin(9600);

  // Set callback
  FsDateTime::setCallback(dateTime);
  

  for (int i=0; i<10; i++){
    delay(50);
    if (Serial){
      i=10;
    }
    Serial.println(i);
  }
  Serial.println("F_CPU_ACTUAL");
  Serial.println(F_CPU_ACTUAL);

  setSyncProvider(getTeensy3Time);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  
  pinMode(PINRED, OUTPUT);
  pinMode(PINGREEN, OUTPUT);
  pinMode(PINBLUE, OUTPUT);


  digitalWrite(PINRED, HIGH);
  digitalWrite(PINGREEN, HIGH);
  digitalWrite(PINBLUE, HIGH);
  
  
  AudioMemory(120); 
  
  if (!(SD.sdfs.begin(SdioConfig(FIFO_SDIO)))) {
    // stop here, but print a message repetitively
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
      delay(250);               // wait for a second
      digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
      delay(250);
    }
  }
  Serial.println("reading config file");
  readconfig();

  Serial.println(devname);
  Serial.println(recmins);
  Serial.println(numChannels);

  tsamplemillis = 60000*recmins;
  
  tmElements_t tm;
  breakTime(now(), tm);

 


  FILE_SIZE=numChannels*60*recmins*44100*2*2;

  Serial.println("Ampgain:");
  Serial.println(ampgain);

  amp1.gain(ampgain);
  amp2.gain(ampgain);
  amp3.gain(ampgain);
  amp4.gain(ampgain);
  amp5.gain(ampgain);
  amp6.gain(ampgain);
  amp7.gain(ampgain);
  amp8.gain(ampgain);


  pinMode(PIN_BUTTON, INPUT_PULLUP);
  
  pinMode(PINRED, OUTPUT);
  pinMode(PINGREEN, OUTPUT);
  pinMode(PINBLUE, OUTPUT);


  digitalWrite(PINRED, LOW);
  digitalWrite(PINGREEN, HIGH);
  digitalWrite(PINBLUE, HIGH);


  set_arm_clock(hiclock);
  
  //set_arm_clock(hiclock);
  delay(1000);
for (int i = 0; i < 10; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
  }
}

bool isItTime() {
  long current = (hour() * 60L) + minute();
  long start = (startHr * 60L) + startMin;
  long end = start + durationMins;

  // Simple check: are we between start and end?
  if (current >= start && current < end) {
    return true;
  }
  return false;
}

void loop() {
  if (isItTime()) {
    set_arm_clock(hiclock); 
    delay(100);
    
    if (verboseOutput);
    recordSimple(tsamplemillis);
  } 
  else {
    set_arm_clock(24000000); 
    
    // 4. indicate it's ON)
    digitalWrite(LED_BUILTIN, HIGH);
    delay(10); 
    digitalWrite(LED_BUILTIN, LOW);

    // Sleep
    delay(10000); 

    if (verboseOutput) {
      Serial.print("Low Power Mode... Current Time: ");
      Serial.print(hour());
      Serial.print(":");
      Serial.println(minute());
    }
  }
}


void recordSimple(int ts){
  delay(50);
  elapsedMillis recordingTime = 0;
  Serial.println("start rec");
  Serial.println(ts);
  char* fn=startRecording();
  Serial.println("cont rec"); 
   Serial.println(fn);

  while((recordingTime<ts)&&(buttonState==HIGH)) {
    continueRecording();
    buttonState = digitalRead(PIN_BUTTON);
  }
  stopRecording(fn);
  buttonState= HIGH;
  
  
  Serial.println("stop rec");
  delay(500);

  digitalWrite(PINRED, LOW);
  digitalWrite(PINGREEN, HIGH);
  digitalWrite(PINBLUE, HIGH);
  
}


char* startRecording() {  
  if (verboseOutput==1){
    Serial.println("verbose");
    digitalWrite(PINRED, HIGH);
    digitalWrite(PINGREEN, LOW);
    digitalWrite(PINBLUE, HIGH);
  
    if (LOW_FILE_SPACE==1){
      digitalWrite(PINRED, LOW);
      digitalWrite(PINGREEN, HIGH);
      digitalWrite(PINBLUE, HIGH);
    }
    if (LO_BATT==1){
      Serial.println("Lo battery");
      digitalWrite(PINRED, HIGH);
      digitalWrite(PINGREEN, HIGH);
      digitalWrite(PINBLUE, LOW);
    }
  }
  Serial.println(FILE_SIZE);
  
  filecount++;

  char *filename = makeFilename();

  if (SD.exists(filename)) {
    SD.remove(filename);
  }
  frec = SD.sdfs.open(filename, O_WRITE | O_CREAT);

  if (!frec.preAllocate(FILE_SIZE)) {
     Serial.println("preAllocate failed\n");
     
  }
  
  if (frec) {
    queue1.begin();
    queue2.begin();
    queue3.begin();
    queue4.begin();
    queue5.begin();
    queue6.begin();
    queue7.begin();
    queue8.begin();
    mode = 1;
    recByteSaved = 0L;
  }

  frec.write(bufferw, 44);

  return filename;
}

void continueRecording() {
  if (queue1.available() > 0 && queue2.available() > 0 && queue3.available() > 0 && queue4.available() > 0 && queue5.available() > 0 && queue6.available() > 0 && queue7.available() > 0 && queue8.available() > 0){
    
    //elapsedMicros usec = 0;

    memcpy(buffer1, queue1.readBuffer(), 256);
    memcpy(buffer2, queue2.readBuffer(), 256);
    memcpy(buffer3, queue3.readBuffer(), 256);
    memcpy(buffer4, queue4.readBuffer(), 256);
    memcpy(buffer5, queue5.readBuffer(), 256);
    memcpy(buffer6, queue6.readBuffer(), 256);
    memcpy(buffer7, queue7.readBuffer(), 256);
    memcpy(buffer8, queue8.readBuffer(), 256);
    queue1.freeBuffer();
    queue2.freeBuffer();
    queue3.freeBuffer();
    queue4.freeBuffer();
    queue5.freeBuffer();
    queue6.freeBuffer();
    queue7.freeBuffer();
    queue8.freeBuffer();

  
    int b = 0;
    int c=0;
    for (int i = 0; i < 2048; i += 16) {
      
      bufferw[c] = buffer1[b];
      c=c+1;
      bufferw[c] = buffer1[b + 1];
      c=c+1;
      bufferw[c] = buffer2[b];
      c=c+1;
      bufferw[c] = buffer2[b + 1];
      c=c+1;

      if (c==512){
        frec.write(bufferw, 512);
        c=0;
      }
      
      bufferw[c] = buffer3[b];
      c=c+1;
      bufferw[c] = buffer3[b + 1];
      c=c+1;
      bufferw[c] = buffer4[b];
      c=c+1;
      bufferw[c] = buffer4[b + 1];
      c=c+1;

      if (c==512){
        frec.write(bufferw, 512);
        c=0;
      }
      
      bufferw[c] = buffer5[b];
      c=c+1;
      bufferw[c] = buffer5[b + 1];
      c=c+1;
      bufferw[c] = buffer6[b];
      c=c+1;
      bufferw[c] = buffer6[b + 1];
      c=c+1;

      if (c==512){
        frec.write(bufferw, 512);
        c=0;
      }

      
      bufferw[c] = buffer7[b];
      c=c+1;
      bufferw[c] = buffer7[b + 1];
      c=c+1;
      bufferw[c] = buffer8[b];
      c=c+1;
      bufferw[c] = buffer8[b + 1];
      c=c+1;

      if (c==512){
        frec.write(bufferw, 512);
        c=0;
      }
      
      b = b+2;
    }
    
    recByteSaved += 2048;


  } 
}


void stopRecording(char* fn) {
  Serial.print("stopRecording ");
  Serial.println(now());
  queue1.end();
  queue2.end();
  queue3.end();
  queue4.end();
  queue5.end();
  queue6.end();
  queue7.end();
  queue8.end();

  if (mode == 1) {
    while (queue1.available() > 0 && queue2.available() > 0 && queue3.available() > 0 && queue4.available() > 0 && queue5.available() > 0 && queue6.available() > 0 && queue7.available() > 0 && queue8.available() > 0 ) {
      frec.write((byte*)queue1.readBuffer(), 256);
      queue1.freeBuffer();
      frec.write((byte*)queue2.readBuffer(), 256);
      queue2.freeBuffer();
      frec.write((byte*)queue3.readBuffer(), 256);
      queue3.freeBuffer();
      frec.write((byte*)queue4.readBuffer(), 256);
      queue4.freeBuffer();
      frec.write((byte*)queue5.readBuffer(), 256);
      queue5.freeBuffer();
      frec.write((byte*)queue6.readBuffer(), 256);
      queue6.freeBuffer();
      frec.write((byte*)queue7.readBuffer(), 256);
      queue7.freeBuffer();
      frec.write((byte*)queue8.readBuffer(), 256);
      queue8.freeBuffer();
      
      recByteSaved += 2048;
    }
    frec.truncate();
    frec.close();
    delay(100);


    frec2 = SD.open(fn, FILE_WRITE);
    //frec = SD.sdfs.open(fn, O_WRITE | O_CREAT);
    Serial.println("closing file");
    writeOutHeader();
    frec2.close();
  }
  mode = 0;
  //digitalWrite(LED_BUILTIN, LOW);
  Serial.print("finishedRecording ");
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);               
  Serial.println(now());
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);   
    delay(250);
  

  if (verboseOutput==1){
    digitalWrite(PINRED, HIGH);
    digitalWrite(PINGREEN, HIGH);
    digitalWrite(PINBLUE, HIGH);
  }
}


void writeOutHeader() { // update WAV header with final filesize/datasize

//  NumSamples = (recByteSaved*8)/bitsPerSample/numChannels;
//  Subchunk2Size = NumSamples*numChannels*bitsPerSample/8; // number of samples x number of channels x number of bytes per sample
  Subchunk2Size = recByteSaved;
  ChunkSize = Subchunk2Size + 36;
  frec2.seek(0);
  frec2.write("RIFF");
  byte1 = ChunkSize & 0xff;
  byte2 = (ChunkSize >> 8) & 0xff;
  byte3 = (ChunkSize >> 16) & 0xff;
  byte4 = (ChunkSize >> 24) & 0xff;  
  frec2.write(byte1);  frec2.write(byte2);  frec2.write(byte3);  frec2.write(byte4);
  frec2.write("WAVE");
  frec2.write("fmt ");
  byte1 = Subchunk1Size & 0xff;
  byte2 = (Subchunk1Size >> 8) & 0xff;
  byte3 = (Subchunk1Size >> 16) & 0xff;
  byte4 = (Subchunk1Size >> 24) & 0xff;  
  frec2.write(byte1);  frec2.write(byte2);  frec2.write(byte3);  frec2.write(byte4);
  byte1 = AudioFormat & 0xff;
  byte2 = (AudioFormat >> 8) & 0xff;
  frec2.write(byte1);  frec2.write(byte2); 
  byte1 = numChannels & 0xff;
  byte2 = (numChannels >> 8) & 0xff;
  frec2.write(byte1);  frec2.write(byte2); 
  byte1 = sampleRate & 0xff;
  byte2 = (sampleRate >> 8) & 0xff;
  byte3 = (sampleRate >> 16) & 0xff;
  byte4 = (sampleRate >> 24) & 0xff;  
  frec2.write(byte1);  frec2.write(byte2);  frec2.write(byte3);  frec2.write(byte4);
  byte1 = byteRate & 0xff;
  byte2 = (byteRate >> 8) & 0xff;
  byte3 = (byteRate >> 16) & 0xff;
  byte4 = (byteRate >> 24) & 0xff;  
  frec2.write(byte1);  frec2.write(byte2);  frec2.write(byte3);  frec2.write(byte4);
  byte1 = blockAlign & 0xff;
  byte2 = (blockAlign >> 8) & 0xff;
  frec2.write(byte1);  frec2.write(byte2); 
  byte1 = bitsPerSample & 0xff;
  byte2 = (bitsPerSample >> 8) & 0xff;
  frec2.write(byte1);  frec2.write(byte2); 
  frec2.write("data");
  byte1 = Subchunk2Size & 0xff;
  byte2 = (Subchunk2Size >> 8) & 0xff;
  byte3 = (Subchunk2Size >> 16) & 0xff;
  byte4 = (Subchunk2Size >> 24) & 0xff;  
  frec2.write(byte1);  frec2.write(byte2);  frec2.write(byte3);  frec2.write(byte4);
  //frec.close();
  Serial.println("header written"); 
  Serial.print("Subchunk2: "); 
  Serial.println(Subchunk2Size); 
}

char *makeFilename(){ 
  static char filename[40];
  sprintf(filename, "%s_%04d_%02d_%02d_%02d_%02d_%02d%s", devname, year(), month(), day(), hour(), minute(), second(), postfix);
  return filename;  
}

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}


void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) {

  // Return date using FS_DATE macro to format fields.
  *date = FS_DATE(year(), month(), day());

  // Return time using FS_TIME macro to format fields.
  *time = FS_TIME(hour(), minute(), second());

  // Return low time bits in units of 10 ms.
  *ms10 = second() & 1 ? 100 : 0;
}



void readconfig(){
  const size_t LINE_DIM = 50;
  char line[LINE_DIM];
  size_t n;
  FsFile file;
  verboseOutput=0;
  if (file.open("config.txt", O_READ)) {
    int ln = 1;
    int x=0;
    while ((n = file.fgets(line, sizeof(line))) > 0) {
      line[strcspn(line, "\n")] = 0;
      if (x>0){
        int a=1;
        String str=String(a);
        if (x==1){
          strcpy(devname, line);
          //devname=line;
        }
        else if (x==5){
          recmins=atoi(line);
        }
        else if (x==13){
          numChannels=atoi(line);
        }
        else if (x==15){
          ampgain=atoi(line)*0.01;
        }
        else if (x == 20) { 
          startHr = atoi(line);  
        }
        else if (x == 21) { 
          startMin = atoi(line); 
        }
        else if (x == 22) { 
          durationMins = atoi(line); }

        x=0;
      }
      
      if (strcmp(line, "DeviceID:")==0){x=1;}
      else if (strcmp(line, "FileLengthMins:")==0){x=5;}
      else if (strcmp(line, "Verbose:")==0){verboseOutput=1;}
      else if (strcmp(line, "Amplifier:")==0){x=15;}
      else if (strcmp(line, "RecordStartHrs:") == 0) { x = 20; }
      else if (strcmp(line, "RecordStartMins:") == 0) { x = 21; }
      else if (strcmp(line, "RecordLengthMins:") == 0) { x = 22; }
  }
  file.close();
  delay(50);
  }


  

  
}
