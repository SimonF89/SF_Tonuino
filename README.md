Based on https://github.com/CKUL/TonUINO-ESP32

Without wifi, just basic functionality.

# Text2Mp3
https://voicemaker.in/
https://ttsmp3.com/

# Write/Read RFID-Cards
Use test_write for writing and testing the RFID-Cards

# Layout
- To reduce noise, add a 100uF capacity parallely to the speaker
![image](https://user-images.githubusercontent.com/8984410/124837231-bc22b380-df84-11eb-9362-05a5ca6fccdb.png)

# Pins DFPlayer
![image](https://user-images.githubusercontent.com/8984410/124837238-c04ed100-df84-11eb-8781-986065f39ca6.png)

# Pins ESP32
![image](https://user-images.githubusercontent.com/8984410/124837275-d197dd80-df84-11eb-8cff-1737979d7c81.png)

# Detecting low Battery
https://www.pangodream.es/esp32-getting-battery-charging-level


# offene Todos
- [x] prüfen ob aktuelle Karte == letzte Karte
  - [x] wenn ja und playing - lasse laufen und mache nichts
  - [x] wenn ja und nicht playing - mache weiter bei letzen nächsten Song
- [ ] numInFolder häufig -1 --> mehrfach ordner auslesen --> Leider nicht möglich, Funktion geht nicht.
- [x] read and save last Song on EEPROM --> noch nicht ausreichend getestet
- [ ] save last jokes in string and check if already played. 
'''
String playedJokes = "";
for(int i =0; i < strlen(string); i++ ) {
  char c = string[i];
  // do something with c
}

// Store last song via
playedJokes += String(track);
'''
- [ ] wenn wrong stack und Track==1 --> diese figur hat noch keine Musi!