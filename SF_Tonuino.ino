// Verwendete Bibliotheken
//========================================================================
#include <Arduino.h>
#include "DFRobotDFPlayerMini.h"
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <JC_Button.h>
#include <math.h>
#include <Preferences.h>

// RC522 Module
//========================================================================
// Set Pins for RC522 Module
const int resetPin = 22; // Reset pin
const int ssPin = 21;    // Slave select pin

// this object stores nfc tag data
struct nfcTagObject
{
    uint32_t cookie;
    uint8_t version;
    uint8_t folder;
    uint8_t mode;
    uint8_t special;
    uint8_t color;
};
nfcTagObject myCard;

// Objekt zur kommunikation mit dem Modul anlegen
MFRC522 mfrc522 = MFRC522(ssPin, resetPin); // Create instance
MFRC522::MIFARE_Key key;
bool successRead;
byte sector = 1;
byte blockAddr = 4;
byte trailerBlock = 7;
MFRC522::StatusCode status;

uint16_t numTracksInFolder;
uint16_t track = 1;
uint8_t lastFolder = 0;

// Variablen und Definitionen
//========================================================================

#define buttonPause 25
#define buttonUp 26
#define buttonDown 27
#define busyPin 4

#define LONG_PRESS 1000

// EEPROM Speicher *NVS*
Preferences preferences;
int timeout = 20;
int randomJokesTime = 5; // Seconds till Musik-Box tells a random joke
long randomJokeNumber = 0;
unsigned long lastJoke; // time the last joke was told
unsigned long lastPlay; // time the last song was played
bool placeFigure = false;
bool debug = true;
unsigned long lastDebug; // time the last debugMsg was printed
int debugTimeout = 1;    // Seconds till next debug msg will be printed

unsigned long last_color = 0xFFFFFF;
unsigned int last_Volume;
unsigned int last_max_Volume;
unsigned int max_Volume = 5;
signed int akt_Volume = 2;

// Buttons
Button pauseButton(buttonPause);
Button upButton(buttonUp, 100);
Button downButton(buttonDown, 100);

uint8_t numberOfCards = 0;

bool isPlaying() { return !digitalRead(busyPin); }

HardwareSerial mySoftwareSerial(2);
DFRobotDFPlayerMini myDFPlayer;
void printDetail(uint8_t type, int value);

// Function decleration
//========================================================================

bool readCard(nfcTagObject *nfcTag);
void setupCard(void);
static void nextTrack(bool);
static void handleVolume(bool);
static void handleCard();
static void handleJokes();
static void standbyMode();
int voiceMenu(int numberOfOptions, int startMessage, int messageOffset,
              bool preview = false, int previewFromFolder = 0);

static void handlePause()
{
    if (isPlaying())
    {
        Serial.println(F("Pausiere"));
        myDFPlayer.pause();
    }
    else
    {
        Serial.println(F("Starte"));
        myDFPlayer.start();
        lastPlay = millis();
    }
    delay(2000);
}

static void nextTrack(bool forward)
{
    if (forward)
    {
        Serial.println(F("Debug1"));
        if (track != numTracksInFolder)
        {
            Serial.println(F("Debug2"));
            track = track + 1;
            Serial.println(F("Spiele Track" + track));
            myDFPlayer.playFolder(myCard.folder, track);
            lastPlay = millis();
            // Fortschritt im EEPROM abspeichern
            EEPROM.write(myCard.folder, track);
        }
        else
        {
            Serial.println(F("Debug3"));
            Serial.println(F("Kein Song mehr übrig. Player in Standby versetzen."));
            myDFPlayer.sleep();
            // Fortschritt zurück setzen
            EEPROM.write(myCard.folder, 1);
        }
    }
    else
    {
        Serial.println(F("Debug1_a"));
        if (track > 1)
        {
            track = track - 1;
        }
        Serial.println(F("Debug2_a"));
        Serial.println("Spiele Track " + String(track));
        Serial.println(F("Debug3_a"));
        myDFPlayer.playFolder(myCard.folder, track);
        lastPlay = millis();
        // Fortschritt im EEPROM abspeichern
        Serial.println(F("Debug4_a"));
        EEPROM.write(myCard.folder, track);
    }
    delay(2000);
}

static void handleVolume(bool louder)
{
    if (louder)
    {
        akt_Volume = akt_Volume + 1;
    }
    else
    {
        akt_Volume = akt_Volume - 1;
    }
    if (akt_Volume > max_Volume)
    {
        akt_Volume = max_Volume;
    }
    else if (akt_Volume < 0)
    {
        akt_Volume = 0;
    }
    myDFPlayer.volume(akt_Volume);
    Serial.println("Aktuelles Volumen" + String(akt_Volume));
    delay(500);
}

static void handleJokes()
{
    numTracksInFolder = myDFPlayer.readFileCountsInFolder(88);
    randomJokeNumber = random(1, numTracksInFolder + 1);
    myDFPlayer.playFolder(88, randomJokeNumber);
}

static void handleStandby()
{
    Serial.println(F("Starte Standby"));
    myDFPlayer.pause();
    myDFPlayer.sleep();
}

// Setup
//========================================================================
void setup()
{
    // Allgemeine Settings
    Serial.begin(115200);
    SPI.begin();
    mySoftwareSerial.begin(9600, SERIAL_8N1, 16, 17); // speed, type, RX, TX
    delay(3000);

    randomSeed(analogRead(33)); // Zufallsgenerator initialisieren
    mfrc522.PCD_Init();
    mfrc522.PCD_DumpVersionToSerial();

    // Pin-Setup
    pinMode(buttonPause, INPUT_PULLUP);
    pinMode(buttonUp, INPUT_PULLUP);
    pinMode(buttonDown, INPUT_PULLUP);
    pinMode(busyPin, INPUT);

    for (byte i = 0; i < 6; i++)
    {
        key.keyByte[i] = 0xFF;
    }

    // RESET --- ALLE DREI KNÖPFE BEIM STARTEN GEDRÜCKT HALTEN -> alle bekannten
    // Karten werden gelöscht
    if (digitalRead(buttonPause) == LOW && digitalRead(buttonUp) == LOW &&
        digitalRead(buttonDown) == LOW)
    {
        Serial.println(F("Reset -> EEPROM wird gelöscht"));
        for (int i = 0; i < EEPROM.length(); i++)
        {
            EEPROM.write(i, 0);
        }
    }

    Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));
    myDFPlayer.begin(mySoftwareSerial, false, true);

    if (myDFPlayer.available())
    {
        Serial.println("Verbindung zu MP3 Player konnte hergestellt werden.");
    }
    else
    {
        Serial.println("!!! Verbindung zu MP3 Player konnte NICHT hergestellt werden. !!!");
    }

    myDFPlayer.setTimeOut(500); //Set serial communictaion time out 500ms
    delay(100);
    //----Set volume----
    myDFPlayer.volume(akt_Volume); //Set volume value (0~30).
    //myDFPlayer.volumeUp(); //Volume Up
    //myDFPlayer.volumeDown(); //Volume Down
    delay(100);
    //----Set different EQ----
    //myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
    //  myDFPlayer.EQ(DFPLAYER_EQ_POP);
    //  myDFPlayer.EQ(DFPLAYER_EQ_ROCK);
    //  myDFPlayer.EQ(DFPLAYER_EQ_JAZZ);
    //  myDFPlayer.EQ(DFPLAYER_EQ_CLASSIC);
    myDFPlayer.EQ(DFPLAYER_EQ_BASS);
    delay(100);
    //----Set device we use SD as default----
    //  myDFPlayer.outputDevice(DFPLAYER_DEVICE_U_DISK);
    myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    //  myDFPlayer.outputDevice(DFPLAYER_DEVICE_AUX);
    //  myDFPlayer.outputDevice(DFPLAYER_DEVICE_SLEEP);
    //  myDFPlayer.outputDevice(DFPLAYER_DEVICE_FLASH);

    //----Mp3 control----
    //  myDFPlayer.sleep();     //sleep
    //  myDFPlayer.reset();     //Reset the module
    //  myDFPlayer.enableDAC();  //Enable On-chip DAC
    //  myDFPlayer.disableDAC();  //Disable On-chip DAC
    //  myDFPlayer.outputSetting(true, 15); //output setting, enable the output and set the gain to 15

    //----Read imformation----
    Serial.println("");
    Serial.println(F("readState--------------------"));
    Serial.println(myDFPlayer.readState()); //read mp3 state
    Serial.println(F("readVolume--------------------"));
    Serial.println(myDFPlayer.readVolume()); //read current volume
    //Serial.println(F("readEQ--------------------"));
    //Serial.println(myDFPlayer.readEQ()); //read EQ setting
    Serial.println(F("readFileCounts--------------------"));
    Serial.println(myDFPlayer.readFileCounts()); //read all file counts in SD card
    Serial.println(F("readCurrentFileNumber--------------------"));
    Serial.println(myDFPlayer.readCurrentFileNumber()); //read current play file number
    Serial.println(F("readFileCountsInFolder--------------------"));
    Serial.println(myDFPlayer.readFileCountsInFolder(3)); //read fill counts in folder SD:/03
    Serial.println(F("--------------------"));
    //Begrüßung abspielen, das überbrückt auch die Zeit des WLAN Connect
    //myDFPlayer.playMp3Folder(1);
    myDFPlayer.playFolder(1, 1);
    delay(500);
    while (isPlaying())
        ;
    delay(2000);

    // starting time last song and joke were played
    lastPlay = millis();
    lastJoke = millis();

    Serial.println("===============////////////// ================");
    Serial.println("===============/ SETUP ENDE / ================");
    Serial.println("===============/ //////////// ================");
}

void debuging(String msg)
{
    if (debug && lastDebug + debugTimeout * 1000 <= millis())
    {
        lastDebug = millis();
        Serial.println("Debug: " + msg);
    }
}

// Main-Loop
//========================================================================
void loop()
{
    debuging("Main loop");

    // Button Handling
    do
    {
        placeFigure = false;

        debuging("Do loop");

        // Detailed error handling
        if (myDFPlayer.available() && !isPlaying())
        {
            printDetail(myDFPlayer.readType(), myDFPlayer.read()); //Print the detail message from DFPlayer to handle different errors and states.
        }

        // Buttons auslesen
        pauseButton.read();
        upButton.read();
        downButton.read();

        // ====== State-Machine ======
        if (pauseButton.pressedFor(LONG_PRESS))
        {
            Serial.println(F("Starte Standby"));
            myDFPlayer.pause();
            myDFPlayer.sleep();
            delay(2000);
        }
        else if (pauseButton.wasReleased())
        {
            Serial.println(F("Pausemodus"));
            handlePause();
        }
        else if (upButton.pressedFor(LONG_PRESS))
        {
            Serial.println(F("nächster Song"));
            nextTrack(true);
        }
        else if (upButton.wasReleased())
        {
            Serial.println(F("Lauter"));
            handleVolume(true);
        }
        else if (downButton.pressedFor(LONG_PRESS))
        {
            Serial.println(F("vorheriger Song"));
            nextTrack(false);
        }
        else if (downButton.wasReleased())
        {
            Serial.println(F("Leiser"));
            handleVolume(false);
        }

    } while (!mfrc522.PICC_IsNewCardPresent());

    // RFID Karte wurde aufgelegt
    if (!mfrc522.PICC_ReadCardSerial())
        return;

    if (readCard(&myCard) == true)
    {
        Serial.println(F("Handling Card"));
        handleCard();
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    // Handle timeout - standby
    if (lastPlay + 1000 * timeout <= millis())
    {
        if(!placeFigure)
        {
            placeFigure = true;
            Serial.println(F("Please place a figure on the Box"));
            myDFPlayer.playMp3Folder(2);
            delay(200);
            while (isPlaying());
        }
        else
        {
            placeFigure = false;
            handleStandby();
        }
    }
    // if no Card is present, tell random jokes
    if (lastJoke + 1000 * randomJokesTime <= millis())
    {
        handleJokes();
        lastJoke = millis();
    }
}

// Function declaration
//========================================================================

static void handleCard()
{
    Serial.println("Entering handleCard");

    if (myCard.cookie == 322417479 && myCard.folder != 0)
    {
        Serial.println("Known-Card detected.");

        lastFolder = EEPROM.read(lastFolder);
        numTracksInFolder = myDFPlayer.readFileCountsInFolder(myCard.folder);

        Serial.println("numTracksInFolder = " + numTracksInFolder);

        if (numTracksInFolder > 0)
        {
            if (myCard.folder == lastFolder)
            {
                track = lastFolder = EEPROM.read(track);
                if (track == 0 || track == 255)
                    track = 1;
            }
            else
            {
                track = 1;
            }
            myDFPlayer.playFolder(myCard.folder, track);
        }
        else
        {
            Serial.println("No Song found for folder: " + myCard.folder);
            myDFPlayer.playMp3Folder(4);
            myDFPlayer.playFolder(99, myCard.folder);
            myDFPlayer.playMp3Folder(5);
        }
    }
    else
    {
        myDFPlayer.playMp3Folder(1);
    }
}

// Detailed error handling
void printDetail(uint8_t type, int value)
{
    switch (type)
    {
    case TimeOut:
        Serial.println(F("Time Out!"));
        break;
    case WrongStack:
        Serial.println(F("Stack Wrong!"));
        break;
    case DFPlayerCardInserted:
        Serial.println(F("Card Inserted!"));
        break;
    case DFPlayerCardRemoved:
        Serial.println(F("Card Removed!"));
        break;
    case DFPlayerCardOnline:
        Serial.println(F("Card Online!"));
        break;
    case DFPlayerPlayFinished:
        Serial.print(F("Number:"));
        Serial.print(value);
        Serial.println(F(" Play Finished!"));
        break;
    case DFPlayerError:
        Serial.print(F("DFPlayerError:"));
        switch (value)
        {
        case Busy:
            Serial.println(F("Card not found"));
            break;
        case Sleeping:
            Serial.println(F("Sleeping"));
            break;
        case SerialWrongStack:
            Serial.println(F("Get Wrong Stack"));
            break;
        case CheckSumNotMatch:
            Serial.println(F("Check Sum Not Match"));
            break;
        case FileIndexOut:
            Serial.println(F("File Index Out of Bound"));
            break;
        case FileMismatch:
            Serial.println(F("Cannot Find File"));
            break;
        case Advertise:
            Serial.println(F("In Advertise"));
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

bool readCard(nfcTagObject *nfcTag)
{
    bool returnValue = true;
    // Show some details of the PICC (that is: the tag/card)
    Serial.print(F("Card UID:"));
    dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
    Serial.println();
    Serial.print(F("PICC type: "));
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    Serial.println(mfrc522.PICC_GetTypeName(piccType));

    byte buffer[18];
    byte size = sizeof(buffer);

    // Authenticate using key A
    Serial.println(F("Authenticating using key A..."));
    status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK)
    {
        returnValue = false;
        Serial.print(F("PCD_Authenticate() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
        return returnValue;
    }

    // Show the whole sector as it currently is
    Serial.println(F("Current data in sector:"));
    mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
    Serial.println();

    // Read data from the block
    Serial.print(F("Reading data from block "));
    Serial.print(blockAddr);
    Serial.println(F(" ..."));
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(blockAddr, buffer, &size);
    if (status != MFRC522::STATUS_OK)
    {
        returnValue = false;
        Serial.print(F("MIFARE_Read() failed: "));
        Serial.println(mfrc522.GetStatusCodeName(status));
    }
    Serial.print(F("Data in block "));
    Serial.print(blockAddr);
    Serial.println(F(":"));
    dump_byte_array(buffer, 20);
    Serial.println();
    Serial.println();

    uint32_t tempCookie;
    tempCookie = (uint32_t)buffer[0] << 24;
    tempCookie += (uint32_t)buffer[1] << 16;
    tempCookie += (uint32_t)buffer[2] << 8;
    tempCookie += (uint32_t)buffer[3];

    uint32_t tempColor;
    tempColor = (uint32_t)buffer[8] << 24;
    tempColor += (uint32_t)buffer[9] << 16;
    tempColor += (uint32_t)buffer[10] << 8;
    tempColor += (uint32_t)buffer[11];

    nfcTag->cookie = tempCookie;
    nfcTag->version = buffer[4];
    nfcTag->folder = buffer[5];
    nfcTag->mode = buffer[6];
    nfcTag->special = buffer[7];
    nfcTag->color = tempColor;

    Serial.println("nfcTag->cookie = " + String(tempCookie));
    Serial.println("nfcTag->version = " + String(buffer[4]));
    Serial.println("nfcTag->folder = " + String(buffer[5]));

    return returnValue;
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize)
{
    for (byte i = 0; i < bufferSize; i++)
    {
        Serial.print(buffer[i] < 0x10 ? " 0" : " ");
        Serial.print(buffer[i], HEX);
    }
}
