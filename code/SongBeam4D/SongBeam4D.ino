#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

#include <Audio.h>
#include <Wire.h>
#include <TimeLib.h>
#include <TimeAlarms.h>

#include <arm_math.h>

extern "C" uint32_t set_arm_clock(uint32_t frequency);

char postfix[5]=".wav";
char devname[5]="R01_";


//write wav
unsigned long ChunkSize = 0L;
unsigned long Subchunk1Size = 16;
unsigned int AudioFormat = 1;
unsigned int numChannels = 4;
unsigned long sampleRate = 44100;
unsigned int bitsPerSample = 16;
unsigned long recByteSaved = 0L;
unsigned long beamByteSaved = 0L;

AudioInputI2S            audioInput;
AudioRecordQueue         queue1;
AudioRecordQueue         queue2;         //xy=389,145
AudioInputI2S2           audioInput2;
AudioRecordQueue         queue1a;
AudioRecordQueue         queue2a;         //xy=389,145

AudioConnection          patchCord1(audioInput, 0, queue1, 0);
AudioConnection          patchCord2(audioInput, 1, queue2, 0);
AudioConnection          patchCord3(audioInput2, 0, queue1a, 0);
AudioConnection          patchCord4(audioInput2, 1, queue2a, 0);
//recod from mic


byte bufferL[256];
byte bufferR[256];
byte bufferLa[256];
byte bufferRa[256];


int recDay[7]={1,1,1,1,1,1,1};


int mode = 0;  // 0=stopped, 1=recording, 2=playing
FsFile frec;
FsFile frecB;
File frec2;
elapsedMillis  msecs;

int filecount=0;

int recmins=15;
unsigned int tsamplemillis = 60000*recmins;

int starttimehour=7;
int starttimemin=0;

time_t begintime=0;
time_t endtime=0;
time_t offtime=0;

int ontime=36*60; //number of seconds before power off. 34*60 is with TPL51110 set with real 100Ohm.
int resistChoice=0;

//THIS IS THE DEFAULT ontime for a 100kOhm resistor - this is for the new machines!

int recordPeriodMins=180;

//int hiclock=450000000;
int hiclock=24000000;

int PIN_POWER=32;

#define SDCARD_CS_PIN    BUILTIN_SDCARD


// ================= TDOA delay-and-sum beamformer =================
//
// Steers the 4-mic array at a target coordinate and writes the beamformed
// mono signal to SD alongside (or instead of) the raw 4-channel file.
//
// Coordinate system (metres internally, config values in cm):
//   origin = centre of the mic line (midpoint between A2 and A3)
//   X = along the mic line, positive towards A4
//   Y = broadside, perpendicular to the mic line (front of the board)
//   Z = vertical
// A linear array cannot distinguish directions on the cone around its axis,
// so only the angle between the target and the array axis matters.
//
// WAV channel order is queue1, queue2, queue1a, queue2a. From the
// SongBeam3.1 netlist: I2S1 (queue1/queue2) is fed by IC5 = mics A3+A4,
// I2S2 (queue1a/queue2a) by IC1 = mics A1+A2. Within each pair the mic with
// SELECT tied to 1V8 (A3, A1) is assumed to land in the LEFT slot.
// VERIFY once by scratching next to mic A1 (x=-60mm) and checking it shows
// up loudest on WAV channel 3; if the pairs are swapped, exchange the two
// entries within each pair below.
//
// Mic X positions per WAV channel [m]: ch1=A3, ch2=A4, ch3=A1, ch4=A2
const float micX[4] = { 0.015f, 0.060f, -0.060f, -0.015f };

#define BEAM_TAPS   64     // per-channel fractional-delay FIR length (even)
#define BEAM_BLOCK  128    // audio library block size, samples
#define BEAM_BULK   20.0f  // shared bulk latency (samples) so filters stay causal

int beamMode  = 0;   // 0 = raw 4ch only (original behaviour)
                     // 1 = beamformed mono only
                     // 2 = raw 4ch + beamformed mono
int targetXcm = 0;   // target coordinates, cm, relative to array centre
int targetYcm = 200;
int targetZcm = 0;
int bus2offset = 0;  // samples that ch3/4 (I2S2) lag ch1/2 in the raw file;
                     // measure with a broadside clap and set here (can be <0)
int beamclock = 150000000; // CPU clock while beamforming (24MHz is too slow)

q15_t beamCoeffs[4][BEAM_TAPS];
q15_t firState[4][BEAM_BLOCK + BEAM_TAPS - 1];
arm_fir_instance_q15 firInst[4];

// Build one fractional-delay FIR per channel: a Hann-windowed sinc whose
// group delay is BEAM_BULK + (latest arrival - this mic's arrival), so the
// four channels sum in phase for a source at the target point. Each filter
// carries a gain of 1/4 so the coherent sum stays within 16 bits.
void beamInit() {
  const float c  = 343.0f;
  const float fs = AUDIO_SAMPLE_RATE_EXACT;
  float tx = targetXcm * 0.01f;
  float ty = targetYcm * 0.01f;
  float tz = targetZcm * 0.01f;

  float d[4];
  float dmax = -1.0e9f;
  for (int ch = 0; ch < 4; ch++) {
    float dx = tx - micX[ch];
    float dist = sqrtf(dx * dx + ty * ty + tz * tz);
    d[ch] = dist / c * fs;              // acoustic arrival time in samples
    if (ch >= 2) d[ch] += bus2offset;   // as recorded, including bus skew
    if (d[ch] > dmax) dmax = d[ch];
  }

  for (int ch = 0; ch < 4; ch++) {
    float delay = BEAM_BULK + (dmax - d[ch]);
    float coef[BEAM_TAPS];
    float sum = 0.0f;
    for (int n = 0; n < BEAM_TAPS; n++) {
      float x = (float)n - delay;
      float w = 0.0f;
      float u = x / (BEAM_TAPS / 2);
      if (u > -1.0f && u < 1.0f) {
        w = 0.5f * (1.0f + cosf(PI * u));            // Hann window
      }
      float s = (fabsf(x) < 1e-6f) ? 1.0f : sinf(PI * x) / (PI * x);
      coef[n] = s * w;
      sum += coef[n];
    }
    // arm_fir wants coefficients time-reversed
    for (int n = 0; n < BEAM_TAPS; n++) {
      float v = coef[n] / sum * 0.25f;
      beamCoeffs[ch][BEAM_TAPS - 1 - n] = (q15_t)lrintf(v * 32768.0f);
    }
    arm_fir_init_q15(&firInst[ch], BEAM_TAPS, beamCoeffs[ch],
                     firState[ch], BEAM_BLOCK);
  }

  Serial.print("beamInit target cm (");
  Serial.print(targetXcm); Serial.print(",");
  Serial.print(targetYcm); Serial.print(",");
  Serial.print(targetZcm); Serial.println(")");
  for (int ch = 0; ch < 4; ch++) {
    Serial.print("  ch"); Serial.print(ch + 1);
    Serial.print(" delay samples: ");
    Serial.println(BEAM_BULK + (dmax - d[ch]));
  }
}

// Filter one 128-sample block per channel and sum into mono.
void beamProcess(const int16_t *in1, const int16_t *in2,
                 const int16_t *in3, const int16_t *in4, int16_t *out) {
  static q15_t f1[BEAM_BLOCK], f2[BEAM_BLOCK], f3[BEAM_BLOCK], f4[BEAM_BLOCK];
  arm_fir_q15(&firInst[0], (q15_t *)in1, f1, BEAM_BLOCK);
  arm_fir_q15(&firInst[1], (q15_t *)in2, f2, BEAM_BLOCK);
  arm_fir_q15(&firInst[2], (q15_t *)in3, f3, BEAM_BLOCK);
  arm_fir_q15(&firInst[3], (q15_t *)in4, f4, BEAM_BLOCK);
  for (int i = 0; i < BEAM_BLOCK; i++) {
    int32_t s = (int32_t)f1[i] + f2[i] + f3[i] + f4[i];
    out[i] = (int16_t)__SSAT(s, 16);
  }
}
// ================= end beamformer =================


void setup() {

  set_arm_clock(hiclock);


  Serial.begin(9600);

  for (int i=0; i<10; i++){
    delay(50);
    if (Serial){
      i=10;
    }
    //Serial.println(i);
  }

  setSyncProvider(getTeensy3Time);

  //setTime(7,59,0,1,2,11);

  pinMode(LED_BUILTIN, OUTPUT);


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

  if (numChannels==2){
    PIN_POWER=32;
    beamMode=0;   // beamforming needs all 4 mics
  }

  if (beamMode>0){
    hiclock=beamclock;
    set_arm_clock(hiclock);
    beamInit();
  }

  pinMode(PIN_POWER, OUTPUT);
  digitalWrite(PIN_POWER, LOW);

  Serial.println(devname);
  Serial.println(starttimemin);
  Serial.println(starttimehour);
  Serial.println(recmins);
  Serial.println(recordPeriodMins);
  Serial.println(numChannels);
  Serial.println(beamMode);
  for (int i=0; i<7; i++){Serial.println(recDay[i]);}

  if (resistChoice==1){
     ontime=285*6; //number of seconds before power off. 285*6 is with TPL51110 set with 91Ohm
     //Most of the first 10 had these resistors
  }
  else if (resistChoice==2){
     ontime=34*60; //number of seconds before power off. 34*60 is with TPL51110 set with 100Ohm.
     //A couple of the first 10 had these resistors.
  }


  tsamplemillis = 60000*recmins;

  tmElements_t tm;
  breakTime(now(), tm);
  tm.Second=0;
  tm.Minute=starttimemin;
  tm.Hour=starttimehour;

  Serial.println("WEEKDAY:");
  Serial.println(tm.Wday);
  int day=tm.Wday-1;


  begintime=makeTime(tm);
  endtime=begintime+60*recordPeriodMins;

 time_t x=now();
 offtime=x+ontime;

  if ((recDay[day]==0)||(x>endtime)||(offtime-60<begintime)){
    digitalWrite(PIN_POWER, HIGH);
    Serial.println("SHUTDOWN");
  }

  offtime=offtime-(recmins+1)*60;

  Serial.println(x);
  Serial.println(begintime);
  Serial.println(endtime);

  //set_arm_clock(hiclock);
  delay(1000);
}

void loop() {
  set_arm_clock(hiclock);
  int x=now();
  //Serial.print("New loop: ");
  //Serial.println(x);
  if ((x>begintime)&&(x<endtime)){
    //Serial.println("Considering recording");

    //set_arm_clock(hiclock);
    if (x>offtime){
      int p=tsamplemillis-1000*(x-offtime);
      if (p>10000){
        recordSimple(p);
      }
      else{
        digitalWrite(PIN_POWER, HIGH);
        delay(1000);
      }
    }
    else{
      recordSimple(tsamplemillis);
    }
  }
  else if (x>endtime){
    //Serial.println("Decided to turn off");
    digitalWrite(PIN_POWER, HIGH);
    delay(1000);
  }
  else{
    //Serial.println("waiting to start");
    //set_arm_clock(loclock);
    delay(1000);
  }
}

void recordSimple(int ts){
  //set_arm_clock(hiclock);
  delay(500);
  //Serial.println("Recording ");
  //Serial.println(now());
  elapsedMillis recordingTime = 0;
  if (numChannels==2){
    char* fn=startRecording2Chan();
    while(recordingTime<ts) continueRecording2Chan();
    stopRecording2Chan(fn);
  }
  else{
    startRecording();
    while(recordingTime<ts) continueRecording();
    stopRecording();
  }
  delay(1000);
}


char rawName[48];
char beamName[48];
bool writeRaw = true;
bool writeBeam = false;

void startRecording() {
  Serial.println("startRecording");
  filecount++;

  writeRaw  = (beamMode != 1);
  writeBeam = (beamMode >= 1);

  makeFilenames();

  if (writeRaw) {
    if (SD.exists(rawName)) {
      SD.remove(rawName);
    }
    frec = SD.sdfs.open(rawName, O_WRITE | O_CREAT);
    int FILE_SIZE=4*60*recmins*44100*2*2;
    if (!frec.preAllocate(FILE_SIZE)) {
       Serial.println("preAllocate failed\n");
    }
  }

  if (writeBeam) {
    if (SD.exists(beamName)) {
      SD.remove(beamName);
    }
    frecB = SD.sdfs.open(beamName, O_WRITE | O_CREAT);
    int FILE_SIZE=60*recmins*44100*2*2;
    if (!frecB.preAllocate(FILE_SIZE)) {
       Serial.println("preAllocate beam failed\n");
    }
  }

  queue1.begin();
  queue2.begin();
  queue1a.begin();
  queue2a.begin();
  mode = 1;
  recByteSaved = 0L;
  beamByteSaved = 0L;
}

char* startRecording2Chan() {
  Serial.println("startRecording");
  filecount++;

  char *filename = makeFilename();

  if (SD.exists(filename)) {
    SD.remove(filename);
  }
  frec = SD.sdfs.open(filename, O_WRITE | O_CREAT);
  int FILE_SIZE=2*60*recmins*44100*2*2;

  if (!frec.preAllocate(FILE_SIZE)) {
     Serial.println("preAllocate failed\n");

  }
  //Serial.print("File opened ");
  //Serial.println(now());

  if (frec) {
    queue1.begin();
    queue2.begin();
    mode = 1;
    recByteSaved = 0L;
  }

  return filename;
}

// Pull one 128-sample block per channel, write the raw interleaved frames
// and/or the beamformed mono block. Shared by continueRecording and the
// drain loop in stopRecording.
void processBlock() {
  byte buffer[1024];
  int16_t beamOut[BEAM_BLOCK];

  memcpy(bufferL, queue1.readBuffer(), 256);
  memcpy(bufferR, queue2.readBuffer(), 256);
  memcpy(bufferLa, queue1a.readBuffer(), 256);
  memcpy(bufferRa, queue2a.readBuffer(), 256);
  queue1.freeBuffer();
  queue2.freeBuffer();
  queue1a.freeBuffer();
  queue2a.freeBuffer();

  if (writeRaw) {
    int b = 0;
    for (int i = 0; i < 1024; i += 8) {
      buffer[i] = bufferL[b];
      buffer[i + 1] = bufferL[b + 1];
      buffer[i + 2] = bufferR[b];
      buffer[i + 3] = bufferR[b + 1];
      buffer[i+4] = bufferLa[b];
      buffer[i + 5] = bufferLa[b + 1];
      buffer[i + 6] = bufferRa[b];
      buffer[i + 7] = bufferRa[b + 1];
      b = b+2;
    }
    frec.write(buffer, 1024);
    recByteSaved += 1024;
  }

  if (writeBeam) {
    beamProcess((int16_t *)bufferL, (int16_t *)bufferR,
                (int16_t *)bufferLa, (int16_t *)bufferRa, beamOut);
    frecB.write((byte *)beamOut, 256);
    beamByteSaved += 256;
  }
}

void continueRecording() {
  if (queue1.available() >= 2 && queue2.available() >= 2 && queue1a.available() >=2 && queue2a.available() >=2) {
    processBlock();
  }
}

void continueRecording2Chan() {

  if (queue1.available() >= 2 && queue2.available() >= 2) {
    byte buffer[512];

    memcpy(bufferL, queue1.readBuffer(), 256);
    memcpy(bufferR, queue2.readBuffer(), 256);

    queue1.freeBuffer();
    queue2.freeBuffer();

    int b = 0;
    for (int i = 0; i < 512; i += 4) {
      buffer[i] = bufferL[b];
      buffer[i + 1] = bufferL[b + 1];
      buffer[i + 2] = bufferR[b];
      buffer[i + 3] = bufferR[b + 1];
      b = b+2;
    }
    //elapsedMicros usec = 0;
    frec.write(buffer, 512);  //256 or 512 (dudes code)
    recByteSaved += 512;
    ////Serial.print("SD write, us=");
    ////Serial.println(usec);
  }
}

void stopRecording() {
  //Serial.print("stopRecording ");
  //Serial.println(now());
  queue1.end();
  queue2.end();
  queue1a.end();
  queue2a.end();
  if (mode == 1) {
    while (queue1.available() > 0 && queue2.available() > 0 && queue1a.available() > 0 && queue2a.available() > 0) {
      processBlock();
    }
    // discard any unmatched leftovers
    while (queue1.available() > 0)  { queue1.freeBuffer(); }
    while (queue2.available() > 0)  { queue2.freeBuffer(); }
    while (queue1a.available() > 0) { queue1a.freeBuffer(); }
    while (queue2a.available() > 0) { queue2a.freeBuffer(); }

    if (writeRaw) {
      frec.truncate();
      frec.close();
      delay(100);
      frec2 = SD.open(rawName, FILE_WRITE);
      writeOutHeader(4, recByteSaved);
      frec2.close();
    }
    if (writeBeam) {
      frecB.truncate();
      frecB.close();
      delay(100);
      frec2 = SD.open(beamName, FILE_WRITE);
      writeOutHeader(1, beamByteSaved);
      frec2.close();
    }
  }
  mode = 0;
  //Serial.print("finishedRecording ");
  //Serial.println(now());
}

void stopRecording2Chan(char* fn) {
  //Serial.print("stopRecording ");
  //Serial.println(now());
  queue1.end();
  queue2.end();
  if (mode == 1) {
    while (queue1.available() > 0 && queue2.available() > 0) {
      frec.write((byte*)queue1.readBuffer(), 256);
      queue1.freeBuffer();
      frec.write((byte*)queue2.readBuffer(), 256);
      queue2.freeBuffer();
      recByteSaved += 256;
    }
    frec.truncate();
    frec.close();
    delay(100);


    frec2 = SD.open(fn, FILE_WRITE);
    //frec = SD.sdfs.open(fn, O_WRITE | O_CREAT);

    writeOutHeader(numChannels, recByteSaved);
    frec2.close();
  }
  mode = 0;
  //Serial.print("finishedRecording ");
  //Serial.println(now());
}


void writeOutHeader(unsigned int nchan, unsigned long dataBytes) { // update WAV header with final filesize/datasize
  byte byte1, byte2, byte3, byte4;
  unsigned long byteRate = sampleRate*nchan*(bitsPerSample/8);
  unsigned int blockAlign = nchan*bitsPerSample/8;
  unsigned long Subchunk2Size = dataBytes;
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
  byte1 = nchan & 0xff;
  byte2 = (nchan >> 8) & 0xff;
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
}

char *makeFilename(){
  static char filename[40];
  sprintf(filename, "%s_%04d_%02d_%02d_%02d_%02d_%02d%s", devname, year(), month(), day(), hour(), minute(), second(), postfix);
  return filename;
}

void makeFilenames(){
  sprintf(rawName, "%s_%04d_%02d_%02d_%02d_%02d_%02d%s", devname, year(), month(), day(), hour(), minute(), second(), postfix);
  sprintf(beamName, "%sBEAM_%04d_%02d_%02d_%02d_%02d_%02d%s", devname, year(), month(), day(), hour(), minute(), second(), postfix);
}

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

void readconfig(){
  const size_t LINE_DIM = 50;
  char line[LINE_DIM];
  size_t n;
  FsFile file;
  for (int i=0; i<7; i++){recDay[i]=0;}
  if (file.open("config.txt", O_READ)) {
    int x=0;

    while ((n = file.fgets(line, sizeof(line))) > 0) {
      line[strcspn(line, "\n")] = 0;
      line[strcspn(line, "\r")] = 0;
      if (x>0){
        if (x==1){
          strcpy(devname, line);
          //devname=line;
        }
        else if (x==2){
          starttimehour=atoi(line);
        }
        else if (x==3){
          starttimemin=atoi(line);
        }
        else if (x==4){
          recordPeriodMins=atoi(line);
        }
        else if (x==5){
          recmins=atoi(line);
        }
        else if (x==6){
          recDay[0]=1;
        }
        else if (x==7){
          recDay[1]=1;
        }
        else if (x==8){
          recDay[2]=1;
        }
        else if (x==9){
          recDay[3]=1;
        }
        else if (x==10){
          recDay[4]=1;
        }
        else if (x==11){
          recDay[5]=1;
        }
        else if (x==12){
          recDay[6]=1;
        }
        else if (x==13){
          numChannels=atoi(line);
        }
        else if (x==14){
          resistChoice=atoi(line);
        }
        else if (x==15){
          beamMode=atoi(line);
        }
        else if (x==16){
          targetXcm=atoi(line);
        }
        else if (x==17){
          targetYcm=atoi(line);
        }
        else if (x==18){
          targetZcm=atoi(line);
        }
        else if (x==19){
          bus2offset=atoi(line);
        }

        x=0;
      }

      if (strcmp(line, "DeviceID:")==0){x=1;}
      else if (strcmp(line, "RecordStartHrs:")==0){x=2;}
      else if (strcmp(line, "RecordStartMins:")==0){x=3;}
      else if (strcmp(line, "RecordLengthMins:")==0){x=4;}
      else if (strcmp(line, "FileLengthMins:")==0){x=5;}
      else if (strcmp(line, "RecSun:")==0){x=6;}
      else if (strcmp(line, "RecMon:")==0){x=7;}
      else if (strcmp(line, "RecTue:")==0){x=8;}
      else if (strcmp(line, "RecWed:")==0){x=9;}
      else if (strcmp(line, "RecThu:")==0){x=10;}
      else if (strcmp(line, "RecFri:")==0){x=11;}
      else if (strcmp(line, "RecSat:")==0){x=12;}
      else if (strcmp(line, "NumChan:")==0){x=13;}
      else if (strcmp(line, "Resist:")==0){x=14;}
      else if (strcmp(line, "BeamMode:")==0){x=15;}
      else if (strcmp(line, "TargetXcm:")==0){x=16;}
      else if (strcmp(line, "TargetYcm:")==0){x=17;}
      else if (strcmp(line, "TargetZcm:")==0){x=18;}
      else if (strcmp(line, "Bus2Offset:")==0){x=19;}

  }
  file.close();
  delay(50);
  }


}

/*
LICENSE:  Creative Commons Attribution 4.0 International License
          Royal Holloway University of London
          Robert Lachlan & Lies Zandberg
*/
