#include <ArduCAM.h> // https://docs.arducam.com/Arduino-SPI-camera/Legacy-SPI-camera/Software/API-Functions/
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>

// Chip select for data logging shield
#define SD_CS 10
// Chip select for ArduCAM
#define SPI_CS 7
// Input pin for button
#define buttonPin 3

ArduCAM myCAM(OV2640, SPI_CS);
char imageName[12];
int nFiles = 0;
RTC_DS1307 rtc;
DateTime now;
DateTime lastPicTime;

// Return the number of files saved to the SD card
int countFilesSaved() {
  int nFiles = 0;
  File dir = SD.open('/'); // open root
  while (File f = dir.openNextFile()) { // keep going until no files left
    if (!f.isDirectory()) { // Increment n files if the opened file is not a dir
      nFiles++;
    }
    f.close();
  }
  dir.close();
  return nFiles;
}

// Update the imageName variable, which stores the name
// of an image file. Should be called before creating a new
// image to overwrite the previous image file's name.
void updateImageName(int name) {
  sprintf(imageName, "%08d.jpg", name);
}

// Verify that the ArduCAM is present
void testArduCAM() {
  Serial.println(F("Testing ArduCAM"));

  // Test SPI connection to ArduCAM
  byte temp;
  while (true) {
    // Write and then read a value to the chip test register
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    temp = myCAM.read_reg(ARDUCHIP_TEST1);

    // Check if correct value was written
    if (temp == 0x55) {
      Serial.println(F("ArduCAM connected"));
      break;
    } else {
      Serial.println(F("!> ArduCAM not detected"));
    }

    // Wait 1 second before trying again
    delay(1000);
  }
}

// Ensure the ArduCAM uses the OV2640 module
void detectOV2640() {
  Serial.println(F("Detecting OV2640"));
  byte vid, pid;
  while(1){
    //Check if the camera module type is OV2640
    myCAM.wrSensorReg8_8(0xff, 0x01);
    myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
    myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
    if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))){
      Serial.println(F("Can't find OV2640 module!"));
      delay(1000);continue;
    }
    else{
      Serial.println(F("OV2640 detected."));break;
    } 
  }
}

// Take a picture using the ArduCAM OV2640 module
void takePicture() {
  // Length of the FIFO queue, used later
  int length = 0;
  // index of pixel 
  int i = 0;
  // Store the single-byte response from the SPI bus
  // while we read in the FIFO queue from the ArduCAM.
  // Track current and previous byte to determine end
  byte resp = 0;
  byte prev_resp = 0;
  // I don't know what is_header is supposed to be doing
  bool is_header = false;
  // The file object for the image we'll be reading in
  File imgFile;
  // A buffer to read ArduCAM data into
  byte imgBuff[256];

  // Flush the FIFO queue, which is list a stream of pixels
  // coming from the camera
  myCAM.flush_fifo();
  // Reset the flag indicating if a capture has started
  myCAM.clear_fifo_flag();

  // Start the image capture
  myCAM.start_capture();
  Serial.println(F("Starting capture..."));
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
    Serial.println(F("!> Unable to fetch bit"));
  }
  Serial.println(F("Capture complete."));

  // Check size of the pixel queue
  length = myCAM.read_fifo_length();
  if (length == 0) {
    Serial.println(F("!> FIFO size 0"));
    return ; // Don't continue if image can't be read
  } else if (length >= MAX_FIFO_SIZE) {
    Serial.println(F("!> FIFO overflow"));
    return ; // Don't continue if image was too big
  } else {
    Serial.print(F("FIFO length :"));
    Serial.println(length, DEC);
  }

  // Set imageName to the name of the image file we're creating,
  // overwriting the previous image's name.
  updateImageName(nFiles); // Give images a unique interger name
  Serial.print(F("Creating image: "));
  Serial.println(imageName);
  // Open the new image file, setting the write, create, and truncate
  // flags to true.
  // O_WRTIE: Set write mode
  // O_CREAT: Create file if doesn't exist
  // O_TRUNC: Erase the file upon opening
  imgFile = SD.open(imageName, O_WRITE | O_CREAT | O_TRUNC);
  // imgFile = SD.open("imageOne.jpg", O_WRITE | O_CREAT | O_TRUNC);
  // Check if the file was opened
  if (!imgFile) {
    Serial.println(F("!> Unable to create image"));
    return ; // Don't continue if image file wasn't opened
  }

  // Select ArduCAM module and tell it to prepare for data transfer
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();

  // Read each pixel into a buffer, store the buffer when full,
  // and stop when FIFO queue is empty.
  while ( length-- ) {
    prev_resp = resp;
    // Transfer an empty byte, recieve the next byte in the FIFO queue
    resp = SPI.transfer(0x00);

    // Check if previous SPI response was 0xFF and current response was 0xD9,
    // indicating image file is over
    if (resp == 0xD9 && prev_resp == 0xFF) {
      imgBuff[i++] = resp;
      myCAM.CS_HIGH();
      // Write remaining data from the buffer and close image file
      imgFile.write(imgBuff, i);
      imgFile.close();
      nFiles++; // Increment number of files saved
      Serial.println(F("Image saved"));
      is_header = false;
      i = 0;
    }

    // No idea what is_header is supposed to mean actually
    if (is_header) {
      // Add data to buffer until it's full, then write it to file
      if (i < 256) {
        // Store recieved byte
        imgBuff[i++] = resp;
      } else {
        // Unselect ArduCAM since we're not using it for a second
        myCAM.CS_HIGH();
        // Write buffer to image file
        imgFile.write(imgBuff, 256);
        // Store byte now that buffer is cleared
        i = 0;
        imgBuff[i++] = resp;
        // Select ArduCAM again to resume filling the buffer
        myCAM.CS_LOW();
        myCAM.set_fifo_burst();
      }
    // 0xFF 0xD8 indicates EOI, end of image, in JPG standard
    } else if (resp == 0xD8 && prev_resp == 0xFF) {
      is_header = true;
      // Write both bytes to image buffer
      imgBuff[i++] = prev_resp;
      imgBuff[i++] = resp;
    }
  }
}

// Set up DS1307
void clockInit() {
  Serial.println(F("Initializing DS1307"));
  while (!rtc.begin()) {
    Serial.println(F("!> DS1307 not detected"));
  }
  Serial.println(F("DS1307 connected"));
}

// Sync the DS1307 time to a computer's time
void syncDS1307() {
  Serial.println(F("Syncing DS1307"));
  if (!Serial) {
    Serial.println(F(">! Unable to sync DS1307, no computer connected"));
    return ;
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  now = rtc.now();
  Serial.print(F("DS1307 time set: "));
  Serial.print(now.year());
  Serial.print('/');
  Serial.print(now.month());
  Serial.print('/');
  Serial.print(now.day());
  Serial.print(" ");
  Serial.print(now.hour());
  Serial.print(':');
  Serial.print(now.minute());
  Serial.print(':');
  Serial.println(now.second());
}

// SD library calls this when creating new files
void dateTime(uint16_t* date, uint16_t* time) {
  now = rtc.now();
  *date = FAT_DATE(now.year(), now.month(), now.day());
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}

void setup() {
  Wire.begin();

  Serial.begin(9600);

  // Set the button as input
  pinMode(buttonPin, INPUT);

  // Init SPI
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);  // CS is active low, so deativate it here
  SPI.begin();

  // Init DS1307
  clockInit();
  // if (Serial) { // Only sync in connected to a computer
  //   syncDS1307();
  // } else { // Otherwise set time to beginning of time
  rtc.adjust(DateTime(2000,1,1));
  // }
  now = rtc.now();
  lastPicTime = now - TimeSpan(0,0,15,0);

  //Reset the CPLD -- not sure what this means
  myCAM.write_reg(0x07, 0x80);
  delay(100);
  myCAM.write_reg(0x07, 0x00);
  delay(100);

  // Make sure the connection to the ArduCAM is functional
  testArduCAM();

  // Initialize SD card
  while(!SD.begin(SD_CS)) {
    Serial.println(F("!> SD card not detected"));
    delay(1000); // Wait a second before trying again
  }
  SdFile::dateTimeCallback(dateTime); // Attach datetime lookup method
  nFiles = countFilesSaved(); // Count files already saved
  Serial.println(F("SD card connected"));
  
  // Initialize ArduCAM
  detectOV2640();
  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  myCAM.OV2640_set_JPEG_size(OV2640_320x240);
  delay(1000);
}


void loop() {
  // Update the time tracked through this program
  now = rtc.now();
  // Get time since last picutre
  TimeSpan delta = now - lastPicTime;
  // Take another picture if it's been more than 15 minutes
  if (delta.totalseconds() >= TimeSpan(0,0,15,0).totalseconds()) {
      // takePicture();
      // Note now as the most recent time a pic was taken
      lastPicTime = rtc.now();
      delay(5*1000); // wait 5 seconds
  }
  delay(1000);
}
