#include <MPS.h>
#include "config.h"

// ---- Constantes ----
/// Débit du moniteur série.
constexpr uint32_t kSerialBaudRate = 115200;
/// Période d'envoi du heartbeat.
constexpr uint32_t kHeartbeatPeriodMs = 1000;
/// Période de lecture et de publication de la tension de la broche analogique.
constexpr uint32_t kVoltagePeriodMs = 1000;
/// Période entre deux mesures du DHT11 (le capteur n'accepte pas plus d'une mesure par seconde).
constexpr uint32_t kClimatePeriodMs = 2000;
/// Adresse nulle : indique que kGatewayMacAddress n'a pas été renseignée dans config.h.
static const uint8_t kUnsetMacAddress[sizeof(kGatewayMacAddress)] = {0};

/// Broche analogique lue pour l'objectif 6 (ADC1, câblée ou non).
constexpr uint8_t kAnalogInputPin = 1;
/// Broche de données du DHT11.
constexpr uint8_t kDhtPin = 0;
/// LED RGB adressable intégrée à la carte du TP (broche 2, et non 8 comme sur la carte de référence Espressif).
constexpr uint8_t kRgbLedPin = 2;

/// Valeurs du champ isOn de LedPowerPayload.
constexpr uint8_t kLedOff = 0;
constexpr uint8_t kLedOn = 1;
/// Valeur maximale d'une composante de couleur.
constexpr uint8_t kMaxColorComponent = 255;

// Protocole du DHT11 : l'hôte tient la ligne basse au moins 18 ms, puis le capteur répond par
// 40 bits. Chaque bit commence par un front descendant ; sa durée jusqu'au front suivant vaut
// environ 78 µs pour un 0 et 120 µs pour un 1.
/// Durée du signal de départ envoyé par l'ESP32.
constexpr uint32_t kDhtStartSignalMs = 20;
/// Temps laissé au capteur pour envoyer sa trame (elle dure environ 5 ms).
constexpr uint32_t kDhtResponseTimeoutMs = 10;
/// Durée de bit au-delà de laquelle le bit vaut 1.
constexpr uint32_t kDhtBitOneThresholdUs = 100;
/// Nombre de bits d'une trame.
constexpr uint8_t kDhtFrameBits = 40;
/// Nombre de bits dans un octet.
constexpr uint8_t kBitsPerByte = 8;
/// Nombre d'octets d'une trame.
constexpr uint8_t kDhtFrameBytes = kDhtFrameBits / kBitsPerByte;
/// Fronts descendants nécessaires pour mesurer les 40 bits : un de plus que le nombre de bits.
constexpr uint8_t kDhtRequiredFallingEdges = kDhtFrameBits + 1;
/// Fronts descendants d'une trame complète : début de réponse, puis un par bit, puis la fin.
constexpr uint8_t kDhtMaxFallingEdges = kDhtRequiredFallingEdges + 1;
/// Position des octets dans la trame.
constexpr uint8_t kDhtHumidityIntegerIndex = 0;
constexpr uint8_t kDhtTemperatureIntegerIndex = 2;
constexpr uint8_t kDhtTemperatureDecimalIndex = 3;
constexpr uint8_t kDhtChecksumIndex = 4;
/// Bit de signe de l'octet décimal de la température (température négative).
constexpr uint8_t kDhtNegativeTemperatureMask = 0x80;
/// Masque du chiffre des dixièmes dans l'octet décimal de la température.
constexpr uint8_t kDhtTenthsMask = 0x7F;
/// Conversions vers les centièmes de degré.
constexpr int16_t kCentiDegreesPerDegree = 100;
constexpr int16_t kCentiDegreesPerTenth = 10;

// ---- Structures de payload ----
/// Température et humidité mesurées par le DHT11.
struct __attribute__((packed)) ClimatePayload {
  int16_t temperatureCentiDegrees;  ///< Température, en centièmes de degré Celsius (signée)
  uint8_t humidityPercent;          ///< Humidité relative, en %
};
static_assert(sizeof(ClimatePayload) == 3, "ClimatePayload = 3 octets");

/// Tension lue sur la broche analogique.
struct __attribute__((packed)) VoltagePayload {
  uint16_t voltageMillivolts;  ///< Tension, en millivolts
};
static_assert(sizeof(VoltagePayload) == 2, "VoltagePayload = 2 octets");

/// État d'allumage de la LED RGB.
struct __attribute__((packed)) LedPowerPayload {
  uint8_t isOn;  ///< kLedOff (0) : éteinte, kLedOn (1) : allumée
};
static_assert(sizeof(LedPowerPayload) == 1, "LedPowerPayload = 1 octet");

/// Couleur de la LED RGB, partagée par le set_ et le status_ correspondant.
struct __attribute__((packed)) LedColorPayload {
  uint8_t red;    ///< Composante rouge, de 0 à 255
  uint8_t green;  ///< Composante verte, de 0 à 255
  uint8_t blue;   ///< Composante bleue, de 0 à 255
};
static_assert(sizeof(LedColorPayload) == 3, "LedColorPayload = 3 octets");

/// Couleur de la LED au démarrage, avant tout set_ reçu : blanc.
constexpr LedColorPayload kDefaultLedColor = {kMaxColorComponent, kMaxColorComponent, kMaxColorComponent};

// ---- Variables globales ----
/// Dernière mesure valide du DHT11.
static ClimatePayload climate = {0, 0};
/// Faux tant qu'aucune trame du DHT11 n'a été validée : rien à publier.
static bool hasClimateReading = false;
/// Dernière tension lue.
static VoltagePayload voltage = {0};
/// État courant de la LED.
static LedPowerPayload ledPower = {kLedOff};
static LedColorPayload ledColor = kDefaultLedColor;

/// Étapes de la lecture non bloquante du DHT11.
enum class DhtState : uint8_t {
  Idle,         ///< Attente de la prochaine mesure
  StartSignal,  ///< L'ESP32 tient la ligne basse
  Receiving,    ///< Le capteur envoie sa trame, les fronts sont relevés par interruption
};
static DhtState dhtState = DhtState::Idle;
/// Instant d'entrée dans l'étape courante.
static uint32_t dhtStateStartTimeMs = 0;
/// Instants des fronts descendants de la trame, remplis par l'interruption.
static volatile uint32_t dhtFallingEdgeTimesUs[kDhtMaxFallingEdges];
static volatile uint8_t dhtFallingEdgeCount = 0;

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);
constexpr mps::Subject kSubStatusClimate = mps::makeSubject("status_" DEVICE_NAME "_climate");
constexpr mps::Subject kSubStatusVoltage = mps::makeSubject("status_" DEVICE_NAME "_voltage");
constexpr mps::Subject kSubStatusLedPower = mps::makeSubject("status_" DEVICE_NAME "_led_power");
constexpr mps::Subject kSubStatusLedColor = mps::makeSubject("status_" DEVICE_NAME "_led_color");
constexpr mps::Subject kSubCmdToggleLed = mps::makeSubject("cmd_" DEVICE_NAME "_toggle_led");
constexpr mps::Subject kSubSetLedColor = mps::makeSubject("set_" DEVICE_NAME "_led_color");
constexpr mps::Subject kSubGetState = mps::makeSubject("get_" DEVICE_NAME "_state");

// ---- LED ----
/// Recopie l'état et la couleur mémorisés sur la LED physique.
static void applyLed() {
  if (ledPower.isOn == kLedOn) {
    rgbLedWrite(kRgbLedPin, ledColor.red, ledColor.green, ledColor.blue);
  } else {
    rgbLedWrite(kRgbLedPin, 0, 0, 0);
  }
}

// ---- Publication de l'état ----
/// Republie tout l'état du capteur, car la passerelle ne garde aucun message.
static void publishState() {
  if (hasClimateReading) MPS.publishValue(kSubStatusClimate, climate);
  MPS.publishValue(kSubStatusVoltage, voltage);
  MPS.publishValue(kSubStatusLedPower, ledPower);
  MPS.publishValue(kSubStatusLedColor, ledColor);
}

// ---- Callbacks ----
/// Ouverture de session (au démarrage ou après un redémarrage de la passerelle) : republie tout l'état.
static void onSessionOpened() {
  Serial.println("Session opened!");
  publishState();
}

/// Demande de l'état courant par l'IHM (à son démarrage ou à la reconnexion du capteur) : republie tout l'état.
static void onGetState(const mps::FrameView& message, void*) {
  if (message.payloadLength != 0) return;  // Ce subject n'a pas de payload : tout autre format est ignoré
  publishState();
}

/// Commande d'inversion de l'état de la LED, confirmée par le nouvel état réel.
static void onCmdToggleLed(const mps::FrameView& message, void*) {
  if (message.payloadLength != 0) return;  // Ce subject n'a pas de payload : tout autre format est ignoré
  ledPower.isOn = (ledPower.isOn == kLedOn) ? kLedOff : kLedOn;
  applyLed();
  MPS.publishValue(kSubStatusLedPower, ledPower);
}

/// Nouvelle couleur demandée par l'IHM, confirmée par la couleur réellement appliquée.
static void onSetLedColor(const mps::FrameView& message, void*) {
  LedColorPayload requestedColor;
  if (!mps::readPayloadAs(message, &requestedColor)) return;  // Taille inattendue : message ignoré
  ledColor = requestedColor;
  applyLed();  // Si la LED est éteinte, la couleur est mémorisée pour le prochain allumage
  MPS.publishValue(kSubStatusLedColor, ledColor);
}

/// Signale sur le moniteur série un message que la passerelle n'a pas pu distribuer.
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus) {
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}

// ---- DHT11 ----
/// Interruption : relève l'instant de chaque front descendant de la trame du DHT11.
static void IRAM_ATTR recordDhtFallingEdge() {
  if (dhtFallingEdgeCount < kDhtMaxFallingEdges) {
    dhtFallingEdgeTimesUs[dhtFallingEdgeCount] = micros();
    dhtFallingEdgeCount = dhtFallingEdgeCount + 1;
  }
}

/// Change d'étape de lecture et note l'instant du changement.
static void enterDhtState(DhtState nextState) {
  dhtState = nextState;
  dhtStateStartTimeMs = millis();
}

/// Démarre une mesure : la ligne est tenue basse pour réveiller le capteur.
static void startDhtSignal() {
  pinMode(kDhtPin, OUTPUT);
  digitalWrite(kDhtPin, LOW);
  enterDhtState(DhtState::StartSignal);
}

/// Relâche la ligne pour laisser le capteur répondre, en relevant ses fronts par interruption.
static void releaseDhtLine() {
  dhtFallingEdgeCount = 0;
  // L'interruption est armée avant de relâcher la ligne : la réponse du capteur suit de 20 à 40 µs
  attachInterrupt(digitalPinToInterrupt(kDhtPin), recordDhtFallingEdge, FALLING);
  pinMode(kDhtPin, INPUT_PULLUP);
  enterDhtState(DhtState::Receiving);
}

/// Convertit les fronts relevés en mesure ; renvoie false si la trame est incomplète ou corrompue.
static bool decodeDhtFrame(ClimatePayload* reading) {
  const uint8_t edgeCount = dhtFallingEdgeCount;
  if (edgeCount < kDhtRequiredFallingEdges) return false;

  // Les 41 derniers fronts encadrent les 40 bits, que le front de début de réponse ait été vu ou non
  const uint8_t firstBitEdge = edgeCount - kDhtRequiredFallingEdges;
  uint8_t frame[kDhtFrameBytes] = {0};
  for (uint8_t bitIndex = 0; bitIndex < kDhtFrameBits; bitIndex++) {
    const uint8_t edgeIndex = firstBitEdge + bitIndex;
    const uint32_t bitDurationUs = dhtFallingEdgeTimesUs[edgeIndex + 1] - dhtFallingEdgeTimesUs[edgeIndex];
    uint8_t& frameByte = frame[bitIndex / kBitsPerByte];
    frameByte = frameByte << 1;
    if (bitDurationUs > kDhtBitOneThresholdUs) frameByte = frameByte | 1;
  }

  uint8_t checksum = 0;
  for (uint8_t byteIndex = 0; byteIndex < kDhtChecksumIndex; byteIndex++) checksum += frame[byteIndex];
  if (checksum != frame[kDhtChecksumIndex]) return false;

  const uint8_t temperatureDecimal = frame[kDhtTemperatureDecimalIndex];
  int16_t temperatureCentiDegrees = frame[kDhtTemperatureIntegerIndex] * kCentiDegreesPerDegree
                                    + (temperatureDecimal & kDhtTenthsMask) * kCentiDegreesPerTenth;
  if (temperatureDecimal & kDhtNegativeTemperatureMask) temperatureCentiDegrees = -temperatureCentiDegrees;

  reading->temperatureCentiDegrees = temperatureCentiDegrees;
  reading->humidityPercent = frame[kDhtHumidityIntegerIndex];
  return true;
}

/// Termine une mesure : décode la trame et publie la nouvelle valeur si elle est valide.
static void finishDhtReading() {
  detachInterrupt(digitalPinToInterrupt(kDhtPin));
  enterDhtState(DhtState::Idle);

  ClimatePayload reading;
  if (!decodeDhtFrame(&reading)) return;  // Trame perdue (capteur absent, parasite) : on garde l'ancienne valeur
  climate = reading;
  hasClimateReading = true;
  MPS.publishValue(kSubStatusClimate, climate);
}

/// Fait avancer la lecture du DHT11 d'une étape, sans jamais attendre : appelée à chaque tour de loop().
static void updateClimate() {
  const uint32_t elapsedMs = millis() - dhtStateStartTimeMs;
  switch (dhtState) {
    case DhtState::Idle:
      if (elapsedMs >= kClimatePeriodMs) startDhtSignal();
      break;
    case DhtState::StartSignal:
      if (elapsedMs >= kDhtStartSignalMs) releaseDhtLine();
      break;
    case DhtState::Receiving:
      if (elapsedMs >= kDhtResponseTimeoutMs) finishDhtReading();
      break;
  }
}

// ---- Fonctions appelées par loop() ----
/// Publie un heartbeat toutes les kHeartbeatPeriodMs, sans bloquer : il prouve à l'IHM que loop() tourne.
static void publishHeartbeat() {
  static uint32_t lastHeartbeatTimeMs = 0;
  if (millis() - lastHeartbeatTimeMs < kHeartbeatPeriodMs) return;
  lastHeartbeatTimeMs = millis();
  MPS.publishHeartbeat(kSubHb);
}

/// Lit et publie la tension de la broche analogique toutes les kVoltagePeriodMs, sans bloquer.
static void updateVoltage() {
  static uint32_t lastVoltageTimeMs = 0;
  if (millis() - lastVoltageTimeMs < kVoltagePeriodMs) return;
  lastVoltageTimeMs = millis();
  voltage.voltageMillivolts = analogReadMilliVolts(kAnalogInputPin);
  MPS.publishValue(kSubStatusVoltage, voltage);
}

// ---- Fonctions Arduino ----
/// Initialise les broches et la LED, déclare les callbacks puis démarre le client MPS.
void setup() {
  Serial.begin(kSerialBaudRate);
  MPS.setLogOutput(&Serial);

  pinMode(kDhtPin, INPUT_PULLUP);  // Ligne au repos à l'état haut entre deux mesures
  applyLed();                      // La LED physique part du même état que celui qui sera publié

  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);
  MPS.on(kSubCmdToggleLed, onCmdToggleLed);
  MPS.on(kSubSetLedColor, onSetLedColor);
  MPS.on(kSubGetState, onGetState);

  // Sans adresse de passerelle, le client ne peut joindre personne : inutile de le démarrer
  if (memcmp(kGatewayMacAddress, kUnsetMacAddress, sizeof(kGatewayMacAddress)) == 0) {
    Serial.println("MPS: renseignez kGatewayMacAddress dans config.h (adresse affichee par l'IHM)");
    return;
  }

  if (!MPS.begin(kGatewayMacAddress, DEVICE_NAME, kWifiChannel)) Serial.println("MPS: demarrage impossible");
}

/// Boucle principale, jamais bloquante : MPS.loop() doit tourner au moins toutes les 10 ms.
void loop() {
  MPS.loop();
  publishHeartbeat();
  updateVoltage();
  updateClimate();
}
