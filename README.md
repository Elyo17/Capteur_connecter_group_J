# Capteur connecté — Groupe J

## Groupe

- Yanis Gougaud
- Elio Maamari
- Sacha Mahé

Nom de l'appareil (`DEVICE_NAME`) : `sensor_j`

## Contenu du dépôt

| Dossier | Contenu |
|---|---|
| `Projet-qual-dev/` | Croquis Arduino : `Projet-qual-dev.ino` et `config.h` |
| `HMI/` | Projet Godot de l'IHM (`project.godot`), tuile du groupe dans `tiles/sensor_j/` |

## Matériel et câblage

| Élément | Broche ESP32-C3 | Remarque |
|---|---|---|
| Carte | — | ESP32-C3 (« ESP32C3 Dev Module » dans l'IDE Arduino) |
| DHT11 — DATA | GPIO 0 | VCC sur 3,3 V, GND sur GND |
| Entrée analogique | GPIO 1 (ADC1) | Câblée ou non : non câblée, la valeur lue flotte |
| LED RGB adressable intégrée | GPIO 2 | Sur notre carte, la LED est sur la broche 2 (et non 8 comme sur la carte de référence Espressif) |
| Passerelle du TP | — | MAC `90:DA:72:52:EE:1C`, canal 5 (réglés dans `config.h`) |

## Lancer le projet

**Capteur (Arduino IDE 2)**
1. Installer le paquet de cartes « esp32 by Espressif Systems » (3.x).
2. Installer la bibliothèque MPS : *Sketch > Include Library > Add .ZIP Library* (`MPS.zip`). Aucune autre bibliothèque n'est nécessaire (le DHT11 est lu sans bibliothèque externe).
3. Ouvrir `Projet-qual-dev/Projet-qual-dev.ino`, carte « ESP32C3 Dev Module », *Tools > USB CDC On Boot > Disabled*.
4. Téléverser ; moniteur série à 115200 bauds.

**IHM (Godot 4)**
1. Brancher la passerelle au PC.
2. Ouvrir `HMI/project.godot` dans Godot et lancer le projet (F5).

## Conventions

Les conventions de nommage sont celles du sujet :
- C++ : `kSub...` pour les subjects, `k` + PascalCase pour les constantes (unité à la fin), PascalCase pour les types (suffixe `Payload`), camelCase pour le reste, `on` + nom du subject pour les handlers.
- GDScript : UPPER_SNAKE_CASE pour les constantes, snake_case pour le reste, préfixe `_` pour ce qui est privé, pour les callbacks du moteur et pour les paramètres inutilisés.

Toutes les valeurs sont en petit-boutiste (ordre natif de l'ESP32, lu tel quel par `decode_*` en GDScript).

## Fonctionnement

- La LED est éteinte au démarrage, couleur mémorisée : blanc (255, 255, 255).
- Le capteur publie la tension toutes les secondes et la mesure du DHT11 toutes les 2 secondes (seulement si la trame reçue est valide).
- À l'ouverture de sa session, et à chaque `get_sensor_j_state`, le capteur republie tout son état (climat si une mesure valide existe, tension, état et couleur de la LED).
- Chaque commande appliquée est confirmée par le `status_` correspondant ; la tuile ne modifie son affichage qu'à la réception d'un `status_`.
- Tout message dont la taille ne correspond pas au contrat est ignoré, des deux côtés.

## Subjects

| Subject | Sens | ACK | Payload |
|---|---|---|---|
| `hb_sensor_j` | capteur → IHM | non | imposé par la bibliothèque MPS |
| `status_sensor_j_climate` | capteur → IHM | non | `ClimatePayload`, 3 octets |
| `status_sensor_j_voltage` | capteur → IHM | non | `VoltagePayload`, 2 octets |
| `status_sensor_j_led_power` | capteur → IHM | non | `LedPowerPayload`, 1 octet |
| `status_sensor_j_led_color` | capteur → IHM | non | `LedColorPayload`, 3 octets |
| `cmd_sensor_j_toggle_led` | IHM → capteur | oui | vide (0 octet) |
| `set_sensor_j_led_color` | IHM → capteur | oui | `LedColorPayload`, 3 octets |
| `get_sensor_j_state` | IHM → capteur | non | vide (0 octet) |

### hb_sensor_j

Heartbeat publié toutes les secondes par `loop()`. Format imposé par la bibliothèque MPS. Sans heartbeat pendant 3,5 s, la tuile considère le capteur comme déconnecté.

### status_sensor_j_climate

Sens : capteur → IHM. Mesure du DHT11, publiée toutes les 2 s, à l'ouverture de session et en réponse à `get_sensor_j_state`.

| Offset | Champ | Type | Unité | Taille |
|---|---|---|---|---|
| 0 | temperatureCentiDegrees | int16_t | Centième de degré Celsius | 2 octets |
| 2 | humidityPercent | uint8_t | % d'humidité relative | 1 octet |

Taille totale : 3 octets. Exemple : 23,4 °C et 45 % → `temperatureCentiDegrees = 2340`, `humidityPercent = 45`.

### status_sensor_j_voltage

Sens : capteur → IHM. Tension lue sur GPIO 1, publiée toutes les secondes, à l'ouverture de session et en réponse à `get_sensor_j_state`.

| Offset | Champ | Type | Unité | Taille |
|---|---|---|---|---|
| 0 | voltageMillivolts | uint16_t | Millivolt | 2 octets |

Taille totale : 2 octets.

### status_sensor_j_led_power

Sens : capteur → IHM. État réel de la LED, publié après chaque `cmd_sensor_j_toggle_led`, à l'ouverture de session et en réponse à `get_sensor_j_state`.

| Offset | Champ | Type | Unité | Taille |
|---|---|---|---|---|
| 0 | isOn | uint8_t | sans unité | 1 octet |

Valeurs de `isOn` : `0` = éteinte, `1` = allumée.

Taille totale : 1 octet.

### status_sensor_j_led_color

Sens : capteur → IHM. Couleur réellement appliquée (mémorisée même LED éteinte), publiée après chaque `set_sensor_j_led_color`, à l'ouverture de session et en réponse à `get_sensor_j_state`.

| Offset | Champ | Type | Unité | Taille |
|---|---|---|---|---|
| 0 | red | uint8_t | sans unité (0 à 255) | 1 octet |
| 1 | green | uint8_t | sans unité (0 à 255) | 1 octet |
| 2 | blue | uint8_t | sans unité (0 à 255) | 1 octet |

Taille totale : 3 octets.

### cmd_sensor_j_toggle_led

Sens : IHM → capteur, avec ACK. Inverse l'état de la LED (allumée ↔ éteinte). Le capteur répond par `status_sensor_j_led_power`.

Payload vide (0 octet) ; tout message non vide est ignoré.

### set_sensor_j_led_color

Sens : IHM → capteur, avec ACK. Couleur désirée, générée par l'IHM (bouton « Couleur aléatoire »). Le capteur l'applique (ou la mémorise si la LED est éteinte) et répond par `status_sensor_j_led_color`.

| Offset | Champ | Type | Unité | Taille |
|---|---|---|---|---|
| 0 | red | uint8_t | sans unité (0 à 255) | 1 octet |
| 1 | green | uint8_t | sans unité (0 à 255) | 1 octet |
| 2 | blue | uint8_t | sans unité (0 à 255) | 1 octet |

Taille totale : 3 octets (même structure que `status_sensor_j_led_color`).

### get_sensor_j_state

Sens : IHM → capteur, sans ACK. Envoyé par la tuile à chaque (re)connexion du capteur, pour qu'une IHM qui démarre retrouve l'état courant (la passerelle ne garde aucun message). Le capteur répond en republiant tous ses `status_`.

Payload vide (0 octet) ; tout message non vide est ignoré.
