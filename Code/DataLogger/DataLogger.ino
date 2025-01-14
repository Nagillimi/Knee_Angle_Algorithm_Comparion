 /* Arduino example for a Teensy Low Latency Logger using DEDICATED SPI
   and formatting the SD card as exFAT (Need a card >= 32GB)
   
   Ben Milligan, July 2020
  
   An implementation of the UM7's SPI mode to allow for logging of 
   2 UM7's @ 250 Hz (will duplicate data)
   (latency of 2000 usec)into Bill Greiman's SdFat-beta library 
   ExFatLogger example. Thanks Bill!!
   
   Tested on:
   - [16MHz] Arduino Uno, slight lag
   - [48MHz] Teensy LC
   - [180MHz] Teensy 3.6, SPI only! Hardware fault in SDIO mode for both 3.5/6
   
   Notes:
   1. You need to format your SD card as exFAT before this example works,
      otherwise you can change SD_FAT_TYPE to match your SD partition.
   2. Should use the built-in bin_to_csv function since it calculates and
      displays missed packets
 */

#include "SdFat.h"
#include "FreeStack.h"
#include "ExFatLogger.h"

//==============================================================================
// Replace logRecord(), printRecord(), and ExFatLogger.h for your sensors.
void logRecord(data_t* data) {
  data->t = (micros() - t0);
  imu1.get_bens_data();
  data->gx_1 = imu1.gyro_x;
  data->gy_1 = imu1.gyro_y;
  data->gz_1 = imu1.gyro_z;
  data->ax_1 = imu1.accel_x;
  data->ay_1 = imu1.accel_y;
  data->az_1 = imu1.accel_z;
  imu2.get_bens_data();
  data->gx_2 = imu2.gyro_x;
  data->gy_2 = imu2.gyro_y;
  data->gz_2 = imu2.gyro_z;
  data->ax_2 = imu2.accel_x;
  data->ay_2 = imu2.accel_y;
  data->az_2 = imu2.accel_z;
  getI2Cdata();
  data->knee_stepper = step_;
  data->knee_angle = calcKneeAngle();
  data->gait_stage = gait_stage_;
  data->impulse_hit = impulse_hit_;
}
//------------------------------------------------------------------------------
void printRecord(Print* pr, data_t* data, bool test_) {
  static uint32_t nr = 0;

  // Print data header when no data is sent to printRecord()
  if (!data) {
    // File info lines
    pr->print(F("LOG INTERVAL,"));
    pr->print(LOG_INTERVAL_USEC);
    pr->print(F(",microseconds"));
    pr->println();
    pr->print(F("TOTAL LOG TIME,"));
    pr->print(log_time);
    pr->print(F(",seconds"));
    pr->println();

    // Dataset Titles
    pr->print(F("TRANSFER #"));
    pr->print(F(",TIME"));
    pr->print(F(",TIME DELTA"));
    pr->print(F(",G1X"));
    pr->print(F(",G1Y"));
    pr->print(F(",G1Z"));
    pr->print(F(",A1X"));
    pr->print(F(",A1Y"));
    pr->print(F(",A1Z"));
    pr->print(F(",G2X"));
    pr->print(F(",G2Y"));
    pr->print(F(",G2Z"));
    pr->print(F(",A2X"));
    pr->print(F(",A2Y"));
    pr->print(F(",A2Z"));
    pr->print(F(",KNEE STEPPER"));
    pr->print(F(",ABS KNEE ANGLE"));
    pr->print(F(",GAIT STAGE"));
    pr->print(F(",IMPULSE HIT"));
    pr->println();
    nr = 0;
    return;
  }

  // Test if the delta is too high. (test_) is used to track whether or
  // not you're just testing the sensors at super low rates.
  if (data->t - delta >= MAX_INTERVAL_USEC && !test_) {
    pr->print(F("Missed Packet(s)\n"));
  }
  
  // Print data packet
  pr->print(nr++);
  pr->write(','); pr->print(data->t);
  pr->write(','); pr->print(data->t - delta);
  pr->write(','); pr->print(data->gx_1);
  pr->write(','); pr->print(data->gy_1);
  pr->write(','); pr->print(data->gz_1);
  pr->write(','); pr->print(data->ax_1);
  pr->write(','); pr->print(data->ay_1);
  pr->write(','); pr->print(data->az_1);
  pr->write(','); pr->print(data->gx_2);
  pr->write(','); pr->print(data->gy_2);
  pr->write(','); pr->print(data->gz_2);
  pr->write(','); pr->print(data->ax_2);
  pr->write(','); pr->print(data->ay_2);
  pr->write(','); pr->print(data->az_2);
  pr->write(','); pr->print(data->knee_stepper);
  pr->write(','); pr->print(data->knee_angle);
  pr->write(','); pr->print(data->gait_stage);
  pr->write(','); pr->print(data->impulse_hit);
  pr->println();

  // Reset delta to hold time for the next packet
  delta = data->t;
}
//==============================================================================
//------------------------------------------------------------------------------
#define error(s) sd.errorHalt(&Serial, F(s))
#define dbgAssert(e) ((e) ? (void)0 : error("assert " #e))
//------------------------------------------------------------------------------
// Convert binary file to csv file.
void binaryToCsv() {
  uint8_t lastPct = 0;
  uint32_t start = millis();
  data_t binData[FIFO_DIM];

  if (!binFile.seekSet(512)) {
    error("binFile.seek failed");
  }
  uint32_t tPct = millis();
  
  // Prints header for csv file
  printRecord(&csvFile, nullptr, test);

  // Loop runs until user types a character or
  // once the conversion is complete
  while (!Serial.available() && binFile.available()) {
    
    // Returns the number of bytes read in binData
    int nb = binFile.read(binData, sizeof(binData));
    if (nb <= 0 ) {
      error("read binFile failed");
    }
    // nr is the number of data_t instances logged
    size_t nr = nb/sizeof(data_t);
    for (size_t i = 0; i < nr; i++) {
      printRecord(&csvFile, &binData[i], test);
    }

    if ((millis() - tPct) > 1000) {
      uint8_t pct = binFile.curPosition()/(binFile.fileSize()/100);
      if (pct != lastPct) {
        tPct = millis();
        lastPct = pct;
        Serial.print(pct, DEC);
        Serial.println('%');
        csvFile.sync();
      }
    }
    if (Serial.available()) {
      break;
    }
  }
  csvFile.close();
  Serial.print(F("Done: "));
  Serial.print(0.001*(millis() - start));
  Serial.println(F(" Seconds"));
}
//-------------------------------------------------------------------------------
void createBinFile() {
  binFile.close();
  while (sd.exists(binName)) {
    char* p = strchr(binName, '.');
    if (!p) {
      error("no dot in filename");
    }
    while (true) {
      p--;
      if (p < binName || *p < '0' || *p > '9') {
        error("Can't create file name");
      }
      if (p[0] != '9') {
        p[0]++;
        break;
      }
      p[0] = '0';
    }
  }
  if (!binFile.open(binName, O_RDWR | O_CREAT)) {
    error("open binary file failed");
  }
  Serial.println(binName);
  if (!binFile.preAllocate(PREALLOCATE_SIZE)) {
    error("preAllocate failed");
  }

  Serial.print(F("preAllocated: "));
  Serial.print(PREALLOCATE_SIZE_MiB);
  Serial.println(F(" MiB"));
}
//-------------------------------------------------------------------------------
bool createCsvFile() {
  char csvName[FILE_NAME_DIM];
  if (!binFile.isOpen()) {
    Serial.println(F("No current binary file"));
    return false;
  }

  // Create a new csvFile.
  binFile.getName(csvName, sizeof(csvName));
  char* dot = strchr(csvName, '.');
  if (!dot) {
    error("no dot in filename");
  }
  strcpy(dot + 1, "csv");
  if (!csvFile.open(csvName, O_WRONLY | O_CREAT | O_TRUNC)) {
    error("open csvFile failed");
  }
  serialClearInput();
  Serial.print(F("Writing: "));
  Serial.print(csvName);
  Serial.println(F(" - type any character to stop"));
  return true;
}
//-------------------------------------------------------------------------------
void logData() {
  int32_t delta;  // Jitter in log time.
  int32_t maxDelta = 0;
  uint32_t maxLogMicros = 0;
  uint32_t maxWriteMicros = 0;
  size_t maxFifoUse = 0;
  size_t fifoCount = 0;
  size_t fifoHead = 0;
  size_t fifoTail = 0;
  uint16_t overrun = 0;
  uint16_t maxOverrun = 0;
  uint32_t totalOverrun = 0;
  uint32_t fifoBuf[128*FIFO_SIZE_SECTORS];
  data_t* fifoData = (data_t*)fifoBuf;

  // Write dummy sector to start multi-block write.
  dbgAssert(sizeof(fifoBuf) >= 512);
  memset(fifoBuf, 0, sizeof(fifoBuf));
  if (binFile.write(fifoBuf, 512) != 512) {
    error("write first sector failed");
  }
  serialClearInput();
  Serial.println(F("Type any character to stop"));

  // Wait until SD is not busy.
  while (sd.card()->isBusy()) {}

  // Start time for log file.
  uint32_t m = millis();

  t0 = micros();
  // Time to log next record.
  uint32_t logTime = micros();
  while (true) {
    
    // Time for next data record.
    logTime += LOG_INTERVAL_USEC;

    // Wait until time to log data.
    delta = micros() - logTime;
    if (delta > 0) {
      Serial.print(F("delta: "));
      Serial.println(delta);
//      error("Rate too fast");
    }
    while (delta < 0) {
      delta = micros() - logTime;
    }
    
    if (fifoCount < FIFO_DIM) {
      
      uint32_t m = micros();
      logRecord(fifoData + fifoHead);
        

      m = micros() - m;
      if (m > maxLogMicros) {
        maxLogMicros = m;
      }
      fifoHead = fifoHead < (FIFO_DIM - 1) ? fifoHead + 1 : 0;
      fifoCount++;
      if (overrun) {
        if (overrun > maxOverrun) {
          maxOverrun = overrun;
        }
        overrun = 0;
      }
    } else {
      totalOverrun++;
      overrun++;
      if (overrun > 0XFFF) {
        error("too many overruns");
      }
      if (ERROR_LED_PIN >= 0) {
        digitalWrite(ERROR_LED_PIN, HIGH);
      }
    }
    // Save max jitter.
    if (delta > maxDelta) {
      maxDelta = delta;
    }
    // Write data if SD is not busy.
    if (!sd.card()->isBusy()) {
      
      size_t nw = fifoHead > fifoTail ? fifoCount : FIFO_DIM - fifoTail;
      // Limit write time by not writing more than 512 bytes.
      const size_t MAX_WRITE = 512/sizeof(data_t);
      if (nw > MAX_WRITE) nw = MAX_WRITE;
      size_t nb = nw*sizeof(data_t);
      uint32_t usec = micros();
      if (nb != binFile.write(fifoData + fifoTail, nb)) {
        error("write binFile failed");
      }
      usec = micros() - usec;
      if (usec > maxWriteMicros) {
        maxWriteMicros = usec;
      }
      fifoTail = (fifoTail + nw) < FIFO_DIM ? fifoTail + nw : 0;
      if (fifoCount > maxFifoUse) {
        maxFifoUse = fifoCount;
      }
      fifoCount -= nw;
      if (Serial.available()) {
        break;
      }
    }
  }
  // Compute total log time in seconds
  log_time = 0.001 * (millis() - m);

  Serial.print(F("\nLog time: "));
  Serial.print(log_time);
  Serial.println(F(" Seconds"));
  binFile.truncate();
  binFile.sync();
  Serial.print(("File size: "));
  // Warning cast used for print since fileSize is uint64_t.
  Serial.print((uint32_t)binFile.fileSize());
  Serial.println(F(" bytes"));
  Serial.print(F("totalOverrun: "));
  Serial.println(totalOverrun);
  Serial.print(F("FIFO_DIM: "));
  Serial.println(FIFO_DIM);
  Serial.print(F("maxFifoUse: "));
  Serial.println(maxFifoUse);
  Serial.print(F("maxLogMicros: "));
  Serial.println(maxLogMicros);
  Serial.print(F("maxWriteMicros: "));
  Serial.println(maxWriteMicros);
  Serial.print(F("Log interval: "));
  Serial.print(LOG_INTERVAL_USEC);
  Serial.print(F(" micros\nmaxDelta: "));
  Serial.print(maxDelta);
  Serial.println(F(" micros"));
}
//------------------------------------------------------------------------------
void openBinFile() {
  char name[FILE_NAME_DIM];
  serialClearInput();
  Serial.println(F("Enter file name"));
  if (!serialReadLine(name, sizeof(name))) {
    return;
  }
  if (!sd.exists(name)) {
    Serial.println(name);
    Serial.println(F("File does not exist"));
    return;
  }
  binFile.close();
  if (!binFile.open(name, O_RDONLY)) {
    Serial.println(name);
    Serial.println(F("open failed"));
    return;
  }
  Serial.println(F("File opened"));
}
//-----------------------------------------------------------------------------
void printData() {
  if (!binFile.isOpen()) {
    Serial.println(F("No current binary file"));
    return;
  }
  // Skip first dummy sector.
  if (!binFile.seekSet(512)) {
    error("seek failed");
  }
  serialClearInput();
  Serial.println(F("type any character to stop\n"));
  delay(1000);
  printRecord(&Serial, nullptr, test);
  while (binFile.available() && !Serial.available()) {
    data_t record;
    if (binFile.read(&record, sizeof(data_t)) != sizeof(data_t)) {
      error("read binFile failed");
    }
    printRecord(&Serial, &record, test);
  }
}
//------------------------------------------------------------------------------
void printUnusedStack() {
#if HAS_UNUSED_STACK  
  Serial.print(F("\nUnused stack: "));
  Serial.println(UnusedStack());
#endif  // HAS_UNUSED_STACK 
}
//------------------------------------------------------------------------------
void serialClearInput() {
  do {
    delay(10);
  } while (Serial.read() >= 0);
}
//------------------------------------------------------------------------------
bool serialReadLine(char* str, size_t size) {
  size_t n = 0;
  while(!Serial.available()) {
    yield();
  }
  while (true) {
    int c = Serial.read();
    if (c < ' ') break;
    str[n++] = c;
    if (n >= size) {
      Serial.println(F("input too long"));
      return false;
    }
    uint32_t m = millis();
    while (!Serial.available() && (millis() - m) < 100){}
    if (!Serial.available()) break;
  }
  str[n] = 0;
  return true;
}
//------------------------------------------------------------------------------
void testSensor() {
  const uint32_t interval = 200000;
  int32_t diff;
  data_t data;
  serialClearInput();
  Serial.println(F("\nTesting - type any character to stop\n"));
  delay(1000);
  printRecord(&Serial, nullptr, false);
  uint32_t m = micros();
  while (!Serial.available()) {
    m += interval;
    do {
      diff = m - micros();
    } while (diff > 0);
    logRecord(&data);
    printRecord(&Serial, &data, test);
  }
}
//------------------------------------------------------------------------------
void setup() {
  if (ERROR_LED_PIN >= 0) {
    pinMode(ERROR_LED_PIN, OUTPUT);
    digitalWrite(ERROR_LED_PIN, HIGH);
  }
  // Init the impulse detector pin
  pinMode(IMPULSE_PIN, INPUT);
  
  // Being I2C as Master
  Wire.begin();
  Wire.setSDA(18);
  Wire.setSCL(19);

  Serial.begin(9600);

  // Wait for USB Serial
  while (!Serial) {
    SysCall::yield();
  }
  delay(1000);
  Serial.println(F("Type any character to begin"));
  while (!Serial.available()) {
    yield();
  }
  FillStack();
#if !ENABLE_DEDICATED_SPI
  Serial.println(F(
    "\nFor best performance edit SdFatConfig.h\n"
    "and set ENABLE_DEDICATED_SPI nonzero"));
#endif  // !ENABLE_DEDICATED_SPI

  Serial.print(FIFO_DIM);
  Serial.println(F(" FIFO entries will be used."));

  // Initialize SD.
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt(&Serial);
  }

  setup_imus(250); // Init with the rate
}
//------------------------------------------------------------------------------
void loop() {
  printUnusedStack();
  // Read any Serial data.
  serialClearInput();

  if (ERROR_LED_PIN >= 0) {
    digitalWrite(ERROR_LED_PIN, LOW);
  }
  Serial.println();
  Serial.println(F("type: "));
  Serial.println(F("b - open existing bin file"));
  Serial.println(F("c - convert file to csv"));
  Serial.println(F("l - list files"));
  Serial.println(F("p - print data to Serial"));
  Serial.println(F("r - record data"));
  Serial.println(F("t - test without logging"));
  while(!Serial.available()) {
    SysCall::yield();
  }
  char c = tolower(Serial.read());
  Serial.println();

  if (c == 'b') {
    openBinFile();
  } else if (c == 'c') {
    if (createCsvFile()) {
      test = false;
      binaryToCsv();
    }
  } else if (c == 'l') {
    Serial.println(F("ls:"));
    sd.ls(&Serial, LS_DATE | LS_SIZE);
  } else if (c == 'p') {
    test = false;
    printData();
  } else if (c == 'r') {
    createBinFile();
    logData();
  } else if (c == 't') {
    test = true;
    testSensor();
  } else {
    Serial.println(F("Invalid entry"));
  }
}
//------------------------------------------------------------------------------
void setup_imus(byte rate_) {
  // Init the SPI bus used for UM7s
  SPI.begin();
  SPI.setMOSI(UM7_MOSI_PIN);
  SPI.setMISO(UM7_MISO_PIN);
  SPI.setSCK(UM7_SCK_PIN);
  // Init UM7 1
  imu1.set_all_processed_rate(rate_);
  imu1.set_orientation_rate(rate_, rate_);
//  imu1.calibrate_accelerometers();
//  imu1.zero_gyros();
  // Init UM7 2
  imu2.set_all_processed_rate(rate_);
  imu2.set_orientation_rate(rate_, rate_);
//  imu2.calibrate_accelerometers();
//  imu2.zero_gyros();
}
