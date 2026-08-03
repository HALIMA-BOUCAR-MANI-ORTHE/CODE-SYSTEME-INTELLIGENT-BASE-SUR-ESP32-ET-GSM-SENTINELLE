/*

 SYSTEME INTELLIGENT DE SURVEILLANCE ET SECURITE
 VERSION FINALE COMPLETE - FIX ERREUR DIGITALREAD

*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <MPU6050_tockn.h>
#include <HardwareSerial.h>


// LCD (Broches GPIO21/SDA, GPIO22/SCL)

LiquidCrystal_I2C lcd(0x27, 16, 2);


// DHT11 (GPIO13 avec Pull-up 4.7k)

#define DHTPIN 13
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);


// MPU6050 (I2C Partagé)

MPU6050 mpu6050(Wire);


// SIM800L (Hardware Serial 2 : RX=16, TX=17)

HardwareSerial sim800(2);


// DEFINITION DES BROCHES (Strictement conforme au tableau)

#define MQ2_PIN        14
#define PIR_PIN        27
#define SOUND_PIN      36

#define LED_PIN        33
#define BUZZER_PIN     32
#define FAN_PIN        25

#define SD_CS          5


// CONFIGURATION TELEPHONE

String phoneNumber = "+212600000000";


// ETATS DES ALERTES ET MODULES

bool mqAlert = false;
bool pirAlert = false;
bool soundAlert = false;
bool mpuAlert = false;
bool systemAlert = false;

bool simReady = false;
bool sdReady = false;

int totalAlerts = 0;
String lastAlertType = "";

// Variables de mesures
float temperature = 0;
float humidity = 0;
float referenceX = 0, referenceY = 0;
float angleX = 0, angleY = 0;

// Compteurs anti-parasites (Sécurisation fausses alertes)
int mqCounter = 0;
int pirCounter = 0;
int soundCounter = 0;
int mpuCounter = 0;

// Gestion du cadencement Temporel
unsigned long previousMillis = 0;
const long interval = 200; // Analyse toutes les 200ms
unsigned long lastLCDChange = 0;
unsigned long lastLog = 0;
int currentPage = 0;

// Variables globales de synchronisation pour l'alerte
unsigned long globalBlinkTimer = 0;
bool globalBlinkState = false;


// SETUP

void setup()
{
  Serial.begin(115200);

  // Configuration des Entrées/Sorties
  pinMode(MQ2_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  // Initialisation des états électriques de sécurité
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);

  // Initialisation de l'Écran LCD (Pages de Boot 1 à 4)
  lcd.init();
  lcd.backlight();
  bootPages();

  // Initialisation DHT11
  dht.begin();

  // Initialisation I2C et calibrage du gyroscope MPU6050
  Wire.begin();
  mpu6050.begin();
  mpu6050.calcGyroOffsets(true);
  referenceX = mpu6050.getAngleX();
  referenceY = mpu6050.getAngleY();

  // Initialisation SIM800L (9600 Baud conforme)
  sim800.begin(9600, SERIAL_8N1, 16, 17);
  initSIM800();

  // Initialisation Carte MicroSD (SPI standard CS=5)
  SPI.begin(18, 19, 23, SD_CS);
  initSD();

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SYSTEME PRET");
  delay(2000);
}


// BOUCLE PRINCIPALE (LOOP)

void loop()
{
  unsigned long currentMillis = millis();

  // Exécution prioritaire et directe des Actionneurs mécaniques/lumineux
  periodicFan();
  runBlinkAlert();

  // Cadencement régulier pour les capteurs, affichage et journalisation
  if(currentMillis - previousMillis >= interval)
  {
    previousMillis = currentMillis;

    readSensors();
    processAlerts();
    handleLCD();

    // Enregistrement périodique toutes les 15 secondes
    if(millis() - lastLog > 15000)
    {
      saveRealtimeStatus();
      lastLog = millis();
    }
  }
}


// AFFICHAGE DES PAGES DE DEMARRAGE (BOOT)

void bootPages()
{
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("PAGE1 SYSTEME   ");
  lcd.setCursor(0,1); lcd.print("ILLDSCESV       ");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("   BIENVENUE    ");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("PAGE2: SYSTEME  ");
  lcd.setCursor(0,1); lcd.print("PRET / INITIALIS");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("PAGE4: MQ-2     ");
  lcd.setCursor(0,1); lcd.print("CHAUFFAGE 20S   ");
  delay(20000);

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("PIR STABLE      ");
  delay(10000);
}


// INITIALISATIONS MATÉRIELLES

void initSIM800()
{
  sim800.println("AT");
  delay(1000);
  sim800.println("AT+CMGF=1"); 
  delay(1000);
  simReady = true;
}

void initSD()
{
  Serial.println("INITIALISATION SD...");
  if(SD.begin(SD_CS))
  {
    sdReady = true;
    Serial.println("CARTE SD DETECTEE");

    File file = SD.open("/journal.txt", FILE_APPEND);
    if(file)
    {
      file.println("[SYSTEME] : DEMARRE ET OPERATIONNEL 24H/24");
      file.close();
    }
  }
  else
  {
    sdReady = false;
    Serial.println("MICRO SD NON TROUVEE");
  }
}


// GESTION DES CAPTEURS ET FILTRAGE ANTI-FAUSSES ALERTES

void readSensors()
{
  // 1. MQ-2 (Logique inversée : LOW = Détection)
  if(digitalRead(MQ2_PIN) == LOW) {
    mqCounter++;
    if(mqCounter >= 5) mqAlert = true;
  } else {
    mqCounter = 0;
    mqAlert = false;
  }

  // 2. PIR (Logique : HIGH = Détection)
  if(digitalRead(PIR_PIN) == HIGH) {
    pirCounter++;
    if(pirCounter >= 2) pirAlert = true;
  } else {
    pirCounter = 0;
    pirAlert = false;
  }

  // 3. CAPTEUR SON (Logique inversée : LOW = Détection)
  if(digitalRead(SOUND_PIN) == LOW) {
    soundCounter++;
    if(soundCounter >= 2) soundAlert = true;
  } else {
    soundCounter = 0;
    soundAlert = false;
  }

  // 4. MPU6050 (Détection de choc / inclinaison)
  mpu6050.update();
  angleX = abs(mpu6050.getAngleX() - referenceX);
  angleY = abs(mpu6050.getAngleY() - referenceY);

  if(angleX > 20 || angleY > 20) {
    mpuCounter++;
    if(mpuCounter >= 3) mpuAlert = true;
  } else {
    mpuCounter = 0;
    mpuAlert = false;
  }

  // 5. DHT11
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if(!isnan(t) && !isnan(h)) {
    temperature = t;
    humidity = h;
  }
}


// TRAITEMENT LOGIQUE ET INTEGRATION DE L'ALERTE

void processAlerts()
{
  int activeAlerts = (mqAlert ? 1 : 0) + (pirAlert ? 1 : 0) + (soundAlert ? 1 : 0) + (mpuAlert ? 1 : 0);
  systemAlert = (activeAlerts > 0);

  if(systemAlert)
  {
    String currentType = getCurrentAlertType();

    if(currentType != lastAlertType)
    {
      totalAlerts++;
      saveAlertToSD(); 

      // Déclenchement séquentiel basé sur vos fonctions natives fonctionnelles
      sendGlobalAlert();
      makeCall();

      lastAlertType = currentType;
    }
  }
  else
  {
    lastAlertType = "";
  }
}

String getCurrentAlertType()
{
  String type = "";
  if(mqAlert) type += "MQ2 ";
  if(pirAlert) type += "PIR ";
  if(soundAlert) type += "SON ";
  if(mpuAlert) type += "MPU ";
  return type;
}


// ACTIONS SYNCHRONES DISCONTINUES (LED + BUZZER)

void runBlinkAlert()
{
  if(systemAlert)
  {
    // Clignotement discontinu synchrone (Alternance toutes les 300ms)
    if(millis() - globalBlinkTimer >= 300)
    {
      globalBlinkTimer = millis();
      globalBlinkState = !globalBlinkState;
    }
    digitalWrite(LED_PIN, globalBlinkState);
    digitalWrite(BUZZER_PIN, globalBlinkState);
  }
  else
  {
    globalBlinkState = false;
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}


// VENTILATEUR D'URGENCE AUTOMATIQUE (SANS DELAIS CYCLIQUES)

void periodicFan()
{
  // Lecture matérielle directe pour ignorer l'impact des micro-délais
  int directMQ2 = digitalRead(MQ2_PIN);

  // Le ventilateur s'active automatiquement si : gaz instantané OU mqAlert confirmée OU température > 32°C
  if (directMQ2 == LOW || mqAlert || temperature > 32)
  {
    // CORRIGE : Utilisation de digitalRead pour vérifier l'état actuel de la broche
    if(digitalRead(FAN_PIN) == LOW)
    {
      digitalWrite(FAN_PIN, HIGH);
      Serial.println("[VENTILATEUR] : ALLUMAGE AUTOMATIQUE (GAZ OU TEMPERATURE)");
    }
  }
  else
  {
    // CORRIGE : Utilisation de digitalRead pour vérifier l'état actuel de la broche
    if(digitalRead(FAN_PIN) == HIGH)
    {
      digitalWrite(FAN_PIN, LOW);
      Serial.println("[VENTILATEUR] : ARRET (RETOUR AU CALME)");
    }
  }
}


// NOTIFICATIONS ET GESTIONNAIRES GSM NATIFS PRESERVES

void sendGlobalAlert()
{
  String msg = " ALERTE \n\n";
  msg += "TYPE : ";
  msg += getCurrentAlertType();
  msg += "\n\n";

  msg += "MQ2 : ";
  msg += mqAlert ? "GAZ/FUMEE\n" : "NORMAL\n";

  msg += "PIR : ";
  msg += pirAlert ? "MOUVEMENT\n" : "NORMAL\n";

  msg += "SON : ";
  msg += soundAlert ? "BRUIT\n" : "NORMAL\n";

  msg += "MPU6050 : ";
  msg += mpuAlert ? "CHOC\n" : "NORMAL\n";

  msg += "\nTEMP : ";
  msg += String(temperature);
  msg += "C";

  msg += "\nHUM : ";
  msg += String(humidity);
  msg += "%";

  msg += "\n\nALERTES : ";
  msg += String(totalAlerts);

  sendSMS(msg);
}

void sendSMS(String message)
{
  sim800.println("AT+CMGF=1");
  delay(1000);
  sim800.print("AT+CMGS=\"");
  sim800.print(phoneNumber);
  sim800.println("\"");
  delay(1000);
  sim800.print(message);
  delay(500);
  sim800.write(26);
  delay(5000);
  Serial.println("SMS ENVOYE");
}

void makeCall()
{
  sim800.print("ATD");
  sim800.print(phoneNumber);
  sim800.println(";");
  Serial.println("APPEL EN COURS");
  
  // Clignotement discontinu forcé pendant le délai d'établissement de l'appel (15s)
  for(int i = 0; i < 25; i++) {
    digitalWrite(LED_PIN, HIGH); digitalWrite(BUZZER_PIN, HIGH); delay(300);
    digitalWrite(LED_PIN, LOW);  digitalWrite(BUZZER_PIN, LOW);  delay(300);
  }
  
  sim800.println("ATH");
  Serial.println("APPEL TERMINE");
}


// DEFILEMENT DES 18 PAGES DE L'ECRAN LCD

void handleLCD()
{
  if(millis() - lastLCDChange < 2500) return;
  lastLCDChange = millis();
  lcd.clear();

  switch(currentPage)
  {
    case 0:
      lcd.setCursor(0,0); lcd.print("PAGE1 SYSTEME   ");
      lcd.setCursor(0,1); lcd.print("ILLDSCESV       ");
      break;
    case 1:
      lcd.setCursor(0,0); lcd.print("PAGE2: SYSTEME  ");
      lcd.setCursor(0,1); lcd.print("PRET            ");
      break;
    case 2:
      lcd.setCursor(0,0); lcd.print("PAGE3:          ");
      lcd.setCursor(0,1); lcd.print("INITIALISATION  ");
      break;
    case 3:
      lcd.setCursor(0,0); lcd.print("PAGE4: MQ-2     ");
      lcd.setCursor(0,1); lcd.print("CHAUFFAGE 20S   ");
      break;
    case 4:
      lcd.setCursor(0,0); lcd.print("PAGE5: SIM800L  ");
      lcd.setCursor(0,1); lcd.print(simReady ? "CONNECTE        " : "NON-CONNECTE    ");
      break;
    case 5:
      lcd.setCursor(0,0); lcd.print("PAGE6: MICRO-SD ");
      lcd.setCursor(0,1); lcd.print(sdReady ? "PRET            " : "NON-TROUVE      ");
      break;
    case 6:
      lcd.setCursor(0,0); lcd.print("PAGE7: NUMERO   ");
      lcd.setCursor(0,1); lcd.print("0700000000      ");
      break;
    case 7:
      lcd.setCursor(0,0); lcd.print("PAGES: ETATS SYS");
      lcd.setCursor(0,1); lcd.print(systemAlert ? "ALERTE !!!      " : "NORMAL          ");
      break;
    case 8:
      lcd.setCursor(0,0); lcd.print("PAGE9: NOMBRES  ");
      lcd.setCursor(0,1); lcd.print("ALERTES: " + String(totalAlerts));
      break;
    case 9:
      lcd.setCursor(0,0); lcd.print("PAGE10: MQ-2 VAL");
      lcd.setCursor(0,1); lcd.print(mqAlert ? "ALERTE!!!       " : "NORMAL          ");
      break;
    case 10:
      lcd.setCursor(0,0); lcd.print("PAGE11: VENTILAT");
      lcd.setCursor(0,1); lcd.print(digitalRead(FAN_PIN) ? "ALLUME          " : "ETEINT          ");
      break;
    case 11:
      lcd.setCursor(0,0); lcd.print("PAGE12: DHT:ACTU");
      lcd.setCursor(0,1); lcd.print("T:" + String((int)temperature) + "C H:" + String((int)humidity) + "%");
      break;
    case 12:
      lcd.setCursor(0,0); lcd.print("PAGE13: PIR ACTU");
      lcd.setCursor(0,1); lcd.print(pirAlert ? "ALERTE!!!       " : "NORMAL          ");
      break;
    case 13:
      lcd.setCursor(0,0); lcd.print("PAGE14: CAP-SON ");
      lcd.setCursor(0,1); lcd.print(soundAlert ? "ALERTE!!!       " : "NORMAL          ");
      break;
    case 14:
      lcd.setCursor(0,0); lcd.print("PAGE15: CAP-MPU ");
      lcd.setCursor(0,1); lcd.print(mpuAlert ? "ALERTE!!!       " : "NORMAL          ");
      break;
    case 15:
      lcd.setCursor(0,0); lcd.print("PAGE16: LED-BUZZ");
      lcd.setCursor(0,1); lcd.print(systemAlert ? "ACTIVE          " : "NON-ACTIVE      ");
      break;
    case 16:
      lcd.setCursor(0,0); lcd.print("PAGE17: SMS: VAL");
      lcd.setCursor(0,1); lcd.print(systemAlert ? "ENVOYE          " : "VALEURS         ");
      break;
    case 17:
      lcd.setCursor(0,0); lcd.print("PAGE18: APPEL   ");
      lcd.setCursor(0,1); lcd.print(systemAlert ? "LANCE           " : "VALEURS         ");
      break;
  }

  currentPage++;
  if(currentPage > 17) currentPage = 0;
}


// JOURNALISATION SUR CARTE MICROSD

void saveAlertToSD()
{
  if(!sdReady) return;

  File file = SD.open("/journal.txt", FILE_APPEND);
  if(file)
  {
    file.println("SYSTEME HI !");
    file.print("[ALERTE DECLENCHEE] No: "); file.println(totalAlerts);
    file.print("MODULES EN FAUTE : "); file.println(getCurrentAlertType());
    file.print("VALEURS -> T: "); file.print(temperature); file.print("C | H: "); file.println(humidity);
    file.println("SITUATION INTERNE SIGNALEE PAR SMS ET APPEL");
    file.println("BONNE RECEPTION !");
    file.close();
  }
}

void saveRealtimeStatus()
{
  if(!sdReady) return;

  File file = SD.open("/etat.txt", FILE_APPEND);
  if(file)
  {
    file.print("TEMP:"); file.print(temperature);
    file.print(" HUM:");  file.print(humidity);
    file.print(" MQ2:");  file.print(mqAlert);
    file.print(" PIR:");  file.print(pirAlert);
    file.print(" SON:");  file.print(soundAlert);
    file.print(" MPU:");  file.println(mpuAlert);
    file.close();
  }
}