// mShuffle firmware (v0.03 - March-2025)
//
// Based on:
// https://github.com/earlephilhower/BackgroundAudio/blob/master/examples/NotSoSimpleMP3Shuffle/NotSoSimpleMP3Shuffle.ino

#include <BackgroundAudioWAV.h>
#include <I2S.h>
I2S audio(OUTPUT);
#include <SD.h>

// List of all MP3 files in the root directory
std::vector<String> wavlist;

File f;
uint8_t filebuff[512];
BackgroundAudioWAV BMP(audio);

double volume = 0.02;

#define MAX_VOLUME 0.10

#define VOLUME_UP_BUTTON 0
#define VOLUME_DOWN_BUTTON 1

// These are GP pins for SPI0 on the Raspberry Pi Pico board, and connect
// to different *board* level pinouts.  Check the PCB while wiring.
// Only certain pins can be used by the SPI hardware, so if you change
// these be sure they are legal or the program will crash.
// See: https://datasheets.raspberrypi.com/picow/PicoW-A4-Pinout.pdf
const int _MISO = 4;  // AKA SPI RX
const int _MOSI = 7;  // AKA SPI TX
const int _CS = 5;
const int _SCK = 6;

void fail() {
  while (1) {
    delay(1000);
#ifdef ESP32
    ESP.restart();
#else
    rp2040.reboot();
#endif
  }
}

// Recursively scan the card and make a list of all MP3 files in all dirs
void scanDirectory(const char *dirname) {
  File root = SD.open(dirname);
  while (true) {
    f = root.openNextFile();
    if (!f) {
      break;
    }
    String n = f.name();
    n.toLowerCase();
    String path = dirname;
    path += f.name();
    if (f.isDirectory()) {
      if (f.name()[0] == '.') {
        // Ignore . and ..
        continue;
      }
      String sub = dirname;
      sub += f.name();
      sub += "/";
      scanDirectory(sub.c_str());
    } else if (strstr(n.c_str(), ".wav") && !strstr(n.c_str(), "marooned")) {
      wavlist.push_back(path);
      Serial.printf("ADD: %s\n", path.c_str());
    } else {
      Serial.printf("SKP: %s\n", path.c_str());
    }
    f.close();
  }
  root.close();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello! :-)");

  // Configure buttons
  pinMode(VOLUME_UP_BUTTON, INPUT_PULLUP);
  pinMode(VOLUME_DOWN_BUTTON, INPUT_PULLUP);

  // PCM5102A configuration
  audio.setBCLK(26);  // LRCLK/word clock will be pin + 1 (27)
  audio.setDATA(29);

  // Initialize the SD card
  bool sdInitialized = false;
#ifdef ESP32
  SPI.begin(_SCK, _MISO, _MOSI, _CS);
  sdInitialized = SD.begin(_CS);
#else
  if (_MISO == 0 || _MISO == 4 || _MISO == 16) {
    SPI.setRX(_MISO);
    SPI.setTX(_MOSI);
    SPI.setSCK(_SCK);
    sdInitialized = SD.begin(_CS);
  } else if (_MISO == 8 || _MISO == 12) {
    SPI1.setRX(_MISO);
    SPI1.setTX(_MOSI);
    SPI1.setSCK(_SCK);
    sdInitialized = SD.begin(_CS, SPI1);
  } else {
    Serial.println(F("ERROR: Unknown SPI Configuration"));
    while (1) {
      delay(1);
    }
  }
#endif

  if (!sdInitialized) {
    Serial.println("SD initialization failed!");
    while (1) {
      Serial.println("SD initialization failed!");
      delay(1000);
    }
  }

  BMP.setGain(volume);
  scanDirectory("/");

  // Start the background player
  BMP.begin();
}

// Debounce code from https://docs.arduino.cc/built-in-examples/digital/Debounce/
int buttonState1;             // the current reading from the input pin
int lastButtonState1 = HIGH;  // the previous reading from the input pin
int buttonState2;
int lastButtonState2 = HIGH;

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime1 = 0;  // the last time the output pin was toggled
unsigned long debounceDelay1 = 50;    // the debounce time; increase if the output
unsigned long lastDebounceTime2 = 0;
unsigned long debounceDelay2 = 50;

void handle_user_input() {

  // read the state of the switch into a local variable:
  int reading1 = digitalRead(VOLUME_UP_BUTTON);
  int reading2 = digitalRead(VOLUME_DOWN_BUTTON);

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH), and you've waited long enough
  // since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading1 != lastButtonState1) {
    // reset the debouncing timer
    lastDebounceTime1 = millis();
  }
  if (reading2 != lastButtonState2) {
    lastDebounceTime2 = millis();
  }

  if ((millis() - lastDebounceTime1) > debounceDelay1) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    // if the button state has changed:
    if (reading1 != buttonState1) {
      buttonState1 = reading1;
      if (buttonState1 == HIGH) {
        volume = volume + 0.01;
        if (volume >= MAX_VOLUME)
          volume = MAX_VOLUME;
        Serial.println(volume);
        BMP.setGain(volume);
      }
    }
  }
  if ((millis() - lastDebounceTime2) > debounceDelay2) {
    if (reading2 != buttonState2) {
      buttonState2 = reading2;
      if (buttonState2 == HIGH) {
        volume = volume - 0.01;
        if (volume <= 0)
          volume = 0;
        Serial.println(volume);
        BMP.setGain(volume);
      }
    }
  }

  // save the reading. Next time through the loop, it'll be the lastButtonState:
  lastButtonState1 = reading1;
  lastButtonState2 = reading2;
}

void loop() {
  if (BOOTSEL) {
    f.close();  // Choose another
    // wait for release
    while (BOOTSEL) {
      delay(1);
    }
  }

  // Choose a song from the list if there's no open file
  if (!f) {
    auto x = random(wavlist.size());
    Serial.print("Playing ");
    Serial.println(wavlist[x]);
    f = SD.open(wavlist[x]);
  }

  // Stuff the buffer with as much as it will take, only doing full sector reads for performance
  while (f && BMP.availableForWrite() > 512) {
    int len = f.read(filebuff, 512);
    BMP.write(filebuff, len);
    if (len != 512) {
      f.close();  // Short reads == EOF
    }
  }

  // Could do things like draw a UI on a LCD, check buttons, etc. here on each loop
  // Just be sure to feed the MP3 raw data at a BPS matching the recording mode
  // (i.e. a 128Kb MP3 only needs 128Kb (16K byte) of reads every second).  Don't disable
  // IRQs for long, either, as the audio DMA and calculations are driven off of them.
  //
  // If you don't send data fast enough (underflow) or you stop sending data completely,
  // the audio will silence and continue when more data is available.

  handle_user_input();
}
