// Verwendete Bibliotheken
//========================================================================
#include <Arduino.h>
#include "DFRobotDFPlayerMini.h"
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <JC_Button.h>
//#include <math.h>
//#include <Preferences.h>

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

int numTracksInFolder;
uint16_t track = 1;
byte lastFolder = 0;
int lastTrack = 0;

// Variablen und Definitionen
//========================================================================

#define buttonPause 25
#define buttonUp 26
#define buttonDown 27
#define busyPin 4

#define LONG_PRESS 1000
#define MINIMUM_TRACK_LENGTH 300    // milliseconds

#define EEPROM_SIZE 10

#define LAST_FOLDER_ADRESS 0x01
#define LAST_TRACK_ADRESS 0x02
#define LAST_VOL_ADRESS 0x03

// boolsche Speicher
bool debug = true;
bool figurePresent = false;
bool doPlaceFigure = false;
bool isStandby = false;
bool cardalreadyRead = false;
bool skipNextTrack = true;
bool currentCardIsEmpty = false;
bool lastSongWasSystemMessage = false;

int jokeCount = 16;
int tooShortSongCount = 0;
int maxTooShortSongs = 5;

// EEPROM Speicher *NVS*
int timeout = 30;
int randomJokesTime = 15; // Seconds till Musik-Box tells a random joke
long randomJokeNumber = 0;
unsigned long lastJoke;  // time the last joke was told
unsigned long lastPlay;  // time the last song was played
unsigned long lastDebug; // time the last debugMsg was printed
byte lastVolume;
int debugTimeout = 1;    // Seconds till next debug msg will be printed

byte min_Volume = 1;
byte max_Volume = 15;
byte akt_Volume = 7;

int control = 0;

// Buttons
Button pauseButton(buttonPause);
Button upButton(buttonUp, 100);
Button downButton(buttonDown, 100);

// detects if mp3-player is playing
bool isPlaying() { return !digitalRead(busyPin); }

HardwareSerial mySoftwareSerial(2);
DFRobotDFPlayerMini myDFPlayer;
//void printDetail(uint8_t type, int value);

// Function decleration
//========================================================================

bool readCard(nfcTagObject *nfcTag);

void debuging(String msg)
{
    if (debug)
    {
        Serial.println("Debug: " + msg);
    }
}

void debugingContinuous(String msg)
{
    if (debug && lastDebug + debugTimeout * 1000 <= millis())
    {
        lastDebug = millis();
        debuging(msg);
    }
}

void handlePause()
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

void nextTrack(bool forward)
{
    if (forward)
    {
        if (track != numTracksInFolder)
        {
            track = track + 1;
            debuging("Playing Track" + String(track) + ", from Folter: " + String(myCard.folder));
            if(myCard.folder)
            {
                myDFPlayer.playFolder(myCard.folder, track);
            }
            else 
            {
                myDFPlayer.playMp3Folder(track);
            }
            lastPlay = millis();
            // Fortschritt im EEPROM abspeichern
            EEPROM.write(LAST_TRACK_ADRESS, track);
        }
        else
        {
            debuging("Kein Song mehr übrig. Player in Standby versetzen.");
            myDFPlayer.pause();
            // Fortschritt zurück setzen
            EEPROM.write(LAST_TRACK_ADRESS, 1);
        }
    }
    else
    {
        if (track > 1)
        {
            track = track - 1;
        }
        debuging("Playing Track" + String(track));
        myDFPlayer.playFolder(myCard.folder, track);
        lastPlay = millis();
        // Fortschritt im EEPROM abspeichern
        EEPROM.write(LAST_TRACK_ADRESS, track);
    }
}

void handleVolume(bool louder)
{
    if (louder)
    {
        debuging("louder");
        akt_Volume = akt_Volume + 1;
        if (akt_Volume > max_Volume)
        {
            akt_Volume = max_Volume;
        }
    }
    else
    {
        debuging("quieter");
        akt_Volume = akt_Volume - 1;
        if (akt_Volume < min_Volume)
        {
            akt_Volume = min_Volume;
        }
    }
    Serial.println("Set and save new volume: " + String(akt_Volume));
    myDFPlayer.volume(akt_Volume);
    EEPROM.write(LAST_VOL_ADRESS,akt_Volume);
    delay(500);
}

void handleJokes()
{
    debuging("Start Random Joke");
    randomJokeNumber = random(jokeCount);
    debuging("test");
    debuging("Playing Song: " + String(randomJokeNumber));
    skipNextTrack = true;
    lastSongWasSystemMessage = true;
    myDFPlayer.playFolder(88, randomJokeNumber);
    delay(1000);
    while (isPlaying())
        ;
    lastJoke = millis();
}

void handleStandby()
{
    isStandby = true;
    Serial.println(F("Starte Standby"));
    playSystem3Message(7);
    delay(500);
    //myDFPlayer.sleep();
    myDFPlayer.pause();
    delay(500);
}

void handleButton()
{
    debugingContinuous("Handling Buttons");

    if (!isPlaying())
    {
        if (myDFPlayer.available())
        {
            printDetail(myDFPlayer.readType(), myDFPlayer.read()); //Print the detail message from DFPlayer to handle different errors and states.
        }
    }

    // Buttons auslesen
    pauseButton.read();
    upButton.read();
    downButton.read();

    // ====== State-Machine ======
    if (pauseButton.pressedFor(LONG_PRESS))
    {
        debuging("State-Machine: Standby");
        handleStandby();
    }
    else if (pauseButton.wasReleased())
    {
        debuging("State-Machine: Pausemodus");
        handlePause();
    }
    else if (upButton.pressedFor(LONG_PRESS))
    {
        debuging("State-Machine: Nächster Song");
        nextTrack(true);
    }
    else if (upButton.wasReleased())
    {
        debuging("State-Machine: Lauter");
        handleVolume(true);
    }
    else if (downButton.pressedFor(LONG_PRESS))
    {
        debuging("State-Machine: Vorheriger Song");
        nextTrack(false);
    }
    else if (downButton.wasReleased())
    {
        debuging("State-Machine: Leiser");
        handleVolume(false);
    }
}

bool isCardPresent()
{
    bool present = false;
    mfrc522.PCD_Init();
    delay(100);

    if (!mfrc522.PICC_IsNewCardPresent())
    {
        debugingContinuous("Card not present");
        return false;
    }
    if (!mfrc522.PICC_ReadCardSerial())
    {
        debugingContinuous("Card not present");
        return false;
    }
        
    debugingContinuous("Card present");
    return true;
}

void handleNoCard()
{
    if(isPlaying())
    {
        myDFPlayer.pause();
    }

    // Handle timeout - standby
    if (lastPlay + 1000 * timeout <= millis() && !isStandby)
    {
        if (!doPlaceFigure)
        {
            doPlaceFigure = true;
            debuging("Please place a figure on the Box");
            playSystem3Message(2);
        }
        else
        {
            doPlaceFigure = false;
            handleStandby();
        }
        lastPlay = millis();
    }
    // if no Card is present, tell random jokes
    if (lastJoke + 1000 * randomJokesTime <= millis() && !isStandby)
    {
        handleJokes();
    }
}

void handlePlayerEvents()
{
    if (myDFPlayer.available())
    {
        printDetail(myDFPlayer.readType(), myDFPlayer.read()); //Print the detail message from DFPlayer to handle different errors and states.
    }
}

int getCurrentFileNumber()
{
    int i = 0;
    int tmp = -1;
    while(i < 3 && tmp == -1)
    {
        i++;
        tmp = myDFPlayer.readCurrentFileNumber();
    }
    if(tmp != -1)
    {
        debuging("CurrentFileNumber: " + String(lastTrack));
        return tmp;
    }
    else
    {
        debuging("CurrentFileNumber not found");
        return lastTrack;
    }
}

void playSystem3Message(int trackNr)
{
    skipNextTrack = true;
    lastSongWasSystemMessage = true;
    myDFPlayer.playMp3Folder(trackNr);
    delay(1000);
    lastTrack = getCurrentFileNumber();
    while (isPlaying())
        ;
    handlePlayerEvents();
    
}

void playUnKnowCardMessage()
{
    playSystem3Message(4);
    skipNextTrack = true;
    myDFPlayer.playFolder(99, myCard.folder);
    delay(1000);
    while (isPlaying())
        ;
    playSystem3Message(5);
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

    // Define EEPROM
    EEPROM.begin(EEPROM_SIZE);

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
    //Set serial communictaion time out 500ms
    myDFPlayer.setTimeOut(500);
    // Check if DFPlayer is available
    if (myDFPlayer.available())
    {
        Serial.println("Verbindung zu MP3 Player konnte hergestellt werden.");
    }
    else
    {
        Serial.println("!!! Verbindung zu MP3 Player konnte NICHT hergestellt werden. !!!");
    }
    
    delay(100);
    //----Set volume----
    lastVolume = EEPROM.read(LAST_VOL_ADRESS);
    if(lastVolume != 255)
    {
        Serial.println("Last Volume was: " + String(lastVolume));
    }
    myDFPlayer.volume(akt_Volume); //Set volume value (0~30).
    delay(100);
    //----Set different EQ----
    myDFPlayer.EQ(DFPLAYER_EQ_BASS);
    delay(100);
    //----Set device we use SD as default----
    myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    delay(100);
    //----Read imformation----
    Serial.println("");
    Serial.println(F("readState--------------------"));
    Serial.println(myDFPlayer.readState()); //read mp3 state
    Serial.println(F("readVolume--------------------"));
    Serial.println(myDFPlayer.readVolume()); //read current volume
    Serial.println(F("readFileCounts--------------------"));
    Serial.println(myDFPlayer.readFileCounts()); //read all file counts in SD card
    Serial.println(F("readCurrentFileNumber--------------------"));
    Serial.println(myDFPlayer.readCurrentFileNumber()); //read current play file number

    //Begrüßung abspielen, das überbrückt auch die Zeit des WLAN Connect
    playSystem3Message(1);
    Serial.println(lastTrack);
    playSystem3Message(2);
    Serial.println(lastTrack);
    // starting time last song and joke were played
    lastPlay = millis();
    lastJoke = millis();

    Serial.println("===============////////////// ================");
    Serial.println("===============/ SETUP ENDE / ================");
    Serial.println("===============/ //////////// ================");
}

// Main-Loop
//========================================================================
void loop()
{
    debugingContinuous("Main loop");

    // State-Machine
    handleButton();

    if(isCardPresent())
    {
        if(!cardalreadyRead)
        {
            if (readCard(&myCard) == true)
            {
                handleCard();
                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();
                cardalreadyRead = true;
            }
            else
            {
                debuging("Reading card wasnt successful");
            }
        }
    }
    else
    {
        if(cardalreadyRead)
        {
            debugingContinuous("Card removed");
            cardalreadyRead = false;
            lastPlay = millis();
            lastJoke = millis();
        }
        handleNoCard();
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
}

// Function declaration
//========================================================================

static void handleCard()
{
    debuging("Entering handleCard");

    if (myCard.cookie == 322417479 && myCard.folder != 0)
    {
        debuging("Known-Card detected.");

        bool isNewCard = false;

        // if lastFolder != 0 and equals currentCard-Folter just start
        if (lastFolder != 0 && lastFolder == myCard.folder)
        {
            debuging("Start playing.");
            if (!lastSongWasSystemMessage)
            {
                myDFPlayer.playFolder(lastFolder, track);
            }
            else
            {
                myDFPlayer.start();
            }
            lastPlay = millis();
            return;
        }
        else
        {
            if (lastFolder == 0)
            {
                debuging("No lastFolder, reading from EEPROM");
                lastFolder = EEPROM.read(LAST_FOLDER_ADRESS);
            }
            if(lastFolder == myCard.folder) // same same
            {
                debuging("lastForder == myCard.folder");
                if (track == 0)
                {
                    debuging("no lastTrack, reading from EEPROM");
                    track = EEPROM.read(LAST_TRACK_ADRESS);
                }
                if (track == 0 || track == 255)
                {
                    debuging("no Track stored in EEPROM, set Track=1");
                    track = 1;
                }
                debuging("Save current Folder to EEPROM: " + String(myCard.folder));
                EEPROM.write(LAST_FOLDER_ADRESS, myCard.folder);
            }
            else // neue Karte
            {
                debuging("detected new Card. Save new Folder and Track=1 to EEPROM");
                //isNewCard = true;
                lastFolder = myCard.folder;
                EEPROM.write(LAST_FOLDER_ADRESS, myCard.folder);
                EEPROM.write(LAST_TRACK_ADRESS, 1);
                track = 1;
            }
        }
        
        debuging("Starting Track: " + String(track) + ", from Folder: " + String(myCard.folder));
        myDFPlayer.playFolder(lastFolder, track);
        handlePlayerEvents();
        lastPlay = millis();
        // if(isNewCard)
        // {
        //     currentCardIsEmpty = false;
        //     if (lastTrack == getCurrentFileNumber())
        //     {
        //         myDFPlayer.stop();
        //         currentCardIsEmpty = true;
        //         playUnKnowCardMessage();
        //     }
        // }
        // else
        // {
        //     if (!currentCardIsEmpty)
        //     {
        //         myDFPlayer.stop();
        //     }
        // }
        
    }
    else
    {
        debuging("Figur nicht kompatibel");
        playSystem3Message(6);
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
        if(skipNextTrack)
        {
            tooShortSongCount = 0;
            skipNextTrack = false;
        }
        else if (!isPlaying())
        {
            tooShortSongCount = 0;
            nextTrack(true);
        }
        lastTrack = value;
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
            long tmp2;
            tmp2 = millis() - lastPlay;
            if (tmp2 <= MINIMUM_TRACK_LENGTH)
            {
                tooShortSongCount ++;
                if (tooShortSongCount >= maxTooShortSongs)
                {
                    playUnKnowCardMessage();
                    tooShortSongCount = 0;
                }
                else
                {
                    debuging("millis() - lastPlay <= MINIMUM_TRACK_LENGTH" + String(tmp2));
                }
            }
            else
            {
                track = 0;
                nextTrack(true);
            }
            break;
        case CheckSumNotMatch:
            Serial.println(F("Check Sum Not Match"));
            break;
        case FileIndexOut:
            Serial.println(F("File Index Out of Bound"));
            track = 1;
            break;
        case FileMismatch:
            Serial.println(F("Cannot Find File"));
            track = 1;
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
    debuging("Start reading card");
    bool returnValue = true;
    // Show some details of the PICC (that is: the tag/card)
    debuging("Card UID:");
    dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
    debuging("PICC type: ");
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    debuging(String(mfrc522.PICC_GetTypeName(piccType)));

    byte buffer[18];
    byte size = sizeof(buffer);

    // Authenticate using key A
    debuging("Authenticating using key A...");
    status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK)
    {
        returnValue = false;
        debuging("PCD_Authenticate() failed: ");
        debuging(String(mfrc522.GetStatusCodeName(status)));
        return returnValue;
    }

    // Show the whole sector as it currently is
    debuging("Current data in sector:");
    mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
    Serial.println();

    // Read data from the block
    debuging("Reading data from block ");
    debuging(String(blockAddr));
    debuging("...");
    status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(blockAddr, buffer, &size);
    if (status != MFRC522::STATUS_OK)
    {
        returnValue = false;
        debuging("MIFARE_Read() failed: ");
        debuging(String(mfrc522.GetStatusCodeName(status)));
    }
    debuging("Data in block ");
    debuging(String(blockAddr));
    debuging(":");
    dump_byte_array(buffer, 20);

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

    debuging("nfcTag->cookie = " + String(tempCookie));
    debuging("nfcTag->version = " + String(buffer[4]));
    debuging("nfcTag->folder = " + String(buffer[5]));

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
