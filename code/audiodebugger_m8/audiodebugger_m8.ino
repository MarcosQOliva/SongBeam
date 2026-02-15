#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
 
// --- Audio Objects (8 Channels) ---
AudioInputI2SOct         audioInput;     // The Octo input (8 channels)
 
// We need 8 queues to capture the data
AudioRecordQueue         queue1;
AudioRecordQueue         queue2;
AudioRecordQueue         queue3;
AudioRecordQueue         queue4;
AudioRecordQueue         queue5;
AudioRecordQueue         queue6;
AudioRecordQueue         queue7;
AudioRecordQueue         queue8;
 
// --- Patch Cords (Connecting Input -> Queue) ---
AudioConnection          patchCord1(audioInput, 0, queue1, 0);
AudioConnection          patchCord2(audioInput, 1, queue2, 0);
AudioConnection          patchCord3(audioInput, 2, queue3, 0);
AudioConnection          patchCord4(audioInput, 3, queue4, 0);
AudioConnection          patchCord5(audioInput, 4, queue5, 0);
AudioConnection          patchCord6(audioInput, 5, queue6, 0);
AudioConnection          patchCord7(audioInput, 6, queue7, 0);
AudioConnection          patchCord8(audioInput, 7, queue8, 0);
 
// --- Global Variables ---
File frec;
bool isRecording = false;
unsigned long recByteSaved = 0L;
int fileNumber = 0;
 
// WAV File Standard Settings
unsigned long sampleRate = 44100;
unsigned int numChannels = 8;     // STRICTLY 8 CHANNELS
unsigned int bitsPerSample = 16;  // 16-bit audio
 
// Buffers
// 128 samples * 2 bytes = 256 bytes per channel block
byte buffer1[256]; byte buffer2[256]; byte buffer3[256]; byte buffer4[256];
byte buffer5[256]; byte buffer6[256]; byte buffer7[256]; byte buffer8[256];
 
// Output Buffer: 128 samples * 8 channels * 2 bytes = 2048 bytes per cycle
byte outputBuffer[2048];
 
void setup() {
  Serial.begin(9600);
  
  // Audio Memory: 8 channels * 10 blocks buffer = ~80 blocks minimum
  AudioMemory(100);
 
  // Initialize SD Card
  if (!(SD.begin(BUILTIN_SDCARD))) {
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
  
  Serial.println("System Ready. Press 'R' to Record, 'S' to Save.");
}
 
void loop() {
  // --- Input Handling ---
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'R' || c == 'r') startRecording();
    if (c == 'S' || c == 's') stopRecording();
  }
 
  // --- Recording Logic ---
  if (isRecording) {
    // Check if ALL 8 queues have a block of audio ready
    if (queue1.available() >= 1 && queue2.available() >= 1 &&
        queue3.available() >= 1 && queue4.available() >= 1 &&
        queue5.available() >= 1 && queue6.available() >= 1 &&
        queue7.available() >= 1 && queue8.available() >= 1) {
      
      // 1. Read data from queues into temporary buffers
      memcpy(buffer1, queue1.readBuffer(), 256); queue1.freeBuffer();
      memcpy(buffer2, queue2.readBuffer(), 256); queue2.freeBuffer();
      memcpy(buffer3, queue3.readBuffer(), 256); queue3.freeBuffer();
      memcpy(buffer4, queue4.readBuffer(), 256); queue4.freeBuffer();
      memcpy(buffer5, queue5.readBuffer(), 256); queue5.freeBuffer();
      memcpy(buffer6, queue6.readBuffer(), 256); queue6.freeBuffer();
      memcpy(buffer7, queue7.readBuffer(), 256); queue7.freeBuffer();
      memcpy(buffer8, queue8.readBuffer(), 256); queue8.freeBuffer();
 
      // 2. Interleave the data
      // We take 2 bytes (1 sample) from each channel sequentially
      int outIndex = 0;
      int inIndex = 0; // Steps by 2 (low byte, high byte)
 
      // Loop through the 128 samples in the block
      for (int i = 0; i < 128; i++) {
        // Channel 1
        outputBuffer[outIndex++] = buffer1[inIndex];
        outputBuffer[outIndex++] = buffer1[inIndex+1];
        // Channel 2
        outputBuffer[outIndex++] = buffer2[inIndex];
        outputBuffer[outIndex++] = buffer2[inIndex+1];
        // Channel 3
        outputBuffer[outIndex++] = buffer3[inIndex];
        outputBuffer[outIndex++] = buffer3[inIndex+1];
        // Channel 4
        outputBuffer[outIndex++] = buffer4[inIndex];
        outputBuffer[outIndex++] = buffer4[inIndex+1];
        // Channel 5
        outputBuffer[outIndex++] = buffer5[inIndex];
        outputBuffer[outIndex++] = buffer5[inIndex+1];
        // Channel 6
        outputBuffer[outIndex++] = buffer6[inIndex];
        outputBuffer[outIndex++] = buffer6[inIndex+1];
        // Channel 7
        outputBuffer[outIndex++] = buffer7[inIndex];
        outputBuffer[outIndex++] = buffer7[inIndex+1];
        // Channel 8
        outputBuffer[outIndex++] = buffer8[inIndex];
        outputBuffer[outIndex++] = buffer8[inIndex+1];
        
        inIndex += 2;
      }
 
      // 3. Write the interleaved block to SD
      frec.write(outputBuffer, 2048);
      recByteSaved += 2048;
    }
  }
}
 
void startRecording() {
  if (isRecording) return;
  
  Serial.println("Starting Recording...");
  char filename[15];
  sprintf(filename, "REC_%d.WAV", fileNumber++);
  
  if (SD.exists(filename)) SD.remove(filename);
  frec = SD.open(filename, FILE_WRITE);
 
  if (frec) {
    // Write 44 bytes of 0 as a placeholder for the header
    for (int i=0; i<44; i++) frec.write((byte)0);
    
    // Clear and Start Queues
    queue1.clear(); queue2.clear(); queue3.clear(); queue4.clear();
    queue5.clear(); queue6.clear(); queue7.clear(); queue8.clear();
    
    queue1.begin(); queue2.begin(); queue3.begin(); queue4.begin();
    queue5.begin(); queue6.begin(); queue7.begin(); queue8.begin();
    
    recByteSaved = 0;
    isRecording = true;
    Serial.print("Recording to "); Serial.println(filename);
  }
}
 
void stopRecording() {
  if (!isRecording) return;
  
  Serial.println("Stopping Recording...");
  
  // Stop Queues
  queue1.end(); queue2.end(); queue3.end(); queue4.end();
  queue5.end(); queue6.end(); queue7.end(); queue8.end();
 
  // Write the real WAV header
  writeWavHeader();
  
  frec.close();
  isRecording = false;
  Serial.println("File Saved.");
}
 
void writeWavHeader() {
  // Calculate file sizes
  unsigned long Subchunk2Size = recByteSaved;
  unsigned long ChunkSize = Subchunk2Size + 36;
  unsigned long byteRate = sampleRate * numChannels * (bitsPerSample / 8);
  unsigned int blockAlign = numChannels * bitsPerSample / 8;
 
  frec.seek(0); // Go back to the start of the file
  
  frec.write("RIFF");
  write4Bytes(ChunkSize);
  frec.write("WAVE");
  frec.write("fmt ");
  write4Bytes(16); // Subchunk1Size (16 for PCM)
  write2Bytes(1);  // AudioFormat (1 for PCM)
  write2Bytes(numChannels);
  write4Bytes(sampleRate);
  write4Bytes(byteRate);
  write2Bytes(blockAlign);
  write2Bytes(bitsPerSample);
  frec.write("data");
  write4Bytes(Subchunk2Size);
}
 
// --- Helper functions to write raw bytes ---
void write4Bytes(unsigned long value) {
  frec.write(value & 0xFF);
  frec.write((value >> 8) & 0xFF);
  frec.write((value >> 16) & 0xFF);
  frec.write((value >> 24) & 0xFF);
}
 
void write2Bytes(unsigned int value) {
  frec.write(value & 0xFF);
  frec.write((value >> 8) & 0xFF);
}