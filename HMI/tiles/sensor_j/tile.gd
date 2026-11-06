extends Tile

# ---- CONSTANTES ----
## Même valeur que DEVICE_NAME dans config.h côté Arduino.
const DEVICE_NAME = "sensor_j"
const SUB_HB = "hb_" + DEVICE_NAME
const SUB_STATUS_CLIMATE = "status_" + DEVICE_NAME + "_climate"
const SUB_STATUS_VOLTAGE = "status_" + DEVICE_NAME + "_voltage"
const SUB_STATUS_LED_POWER = "status_" + DEVICE_NAME + "_led_power"
const SUB_STATUS_LED_COLOR = "status_" + DEVICE_NAME + "_led_color"
const SUB_CMD_TOGGLE_LED = "cmd_" + DEVICE_NAME + "_toggle_led"
const SUB_SET_LED_COLOR = "set_" + DEVICE_NAME + "_led_color"
const SUB_GET_STATE = "get_" + DEVICE_NAME + "_state"

## Contrat de status_sensor_j_climate (ClimatePayload) : int16 temperatureCentiDegrees, uint8 humidityPercent.
const CLIMATE_PAYLOAD_SIZE := 3
const TEMPERATURE_OFFSET := 0
const HUMIDITY_OFFSET := 2
## Contrat de status_sensor_j_voltage (VoltagePayload) : uint16 voltageMillivolts.
const VOLTAGE_PAYLOAD_SIZE := 2
const VOLTAGE_OFFSET := 0
## Contrat de status_sensor_j_led_power (LedPowerPayload) : uint8 isOn.
const LED_POWER_PAYLOAD_SIZE := 1
const LED_POWER_OFFSET := 0
## Valeur de isOn quand la LED est allumée (0 : éteinte).
const LED_ON := 1
## Contrat de status_ et set_sensor_j_led_color (LedColorPayload) : uint8 red, green, blue.
const LED_COLOR_PAYLOAD_SIZE := 3
const RED_OFFSET := 0
const GREEN_OFFSET := 1
const BLUE_OFFSET := 2
## Valeur maximale d'une composante de couleur.
const MAX_COLOR_COMPONENT := 255

## Conversion des centièmes de degré reçus en degrés affichés.
const CENTIDEGREES_PER_DEGREE := 100.0
## Teinte des valeurs périmées (capteur déconnecté) et des valeurs à jour.
const STALE_MODULATE := Color.DIM_GRAY
const FRESH_MODULATE := Color.WHITE

## Valeur de _node_id tant que la passerelle n'a pas annoncé le capteur.
const UNKNOWN_NODE_ID := -1
## Délai sans heartbeat au-delà duquel le capteur est considéré comme bloqué (3,5 périodes de 1 s).
const HB_TIMEOUT_S := 3.5
## Durée d'allumage de l'icône de heartbeat à chaque réception.
const HB_BLINK_S := 0.5

# ---- VARIABLES ----
var _node_id: int = UNKNOWN_NODE_ID
var _seconds_since_hb := INF
# Le setter met à jour l'affichage à chaque changement d'état de connexion
var _connected: bool = false:
	set(value):
		if value == _connected: return
		_connected = value
		if _connected:
			_on_sensor_connected()
		else:
			_on_sensor_disconnected()


## Abonne la tuile (via super), relie les boutons à l'envoi des commandes et part de l'état déconnecté.
func _ready() -> void:
	super()
	%LedButton.pressed.connect(_on_led_toggle_button_pressed)
	%CouleurAléatoireButton.pressed.connect(_on_random_color_button_pressed)
	_on_sensor_disconnected()


## Fait clignoter l'icône de heartbeat et détecte un capteur devenu muet.
func _process(delta: float) -> void:
	_seconds_since_hb += delta
	%HeartbeatIcon.state = _seconds_since_hb < HB_BLINK_S
	if _connected and _seconds_since_hb >= HB_TIMEOUT_S:
		_connected = false  # Plus de heartbeat : code du capteur bloqué ou capteur hors de portée


## Liste des subjects reçus par la tuile : un subject absent d'ici n'arrive jamais dans on_message.
func get_subscriptions() -> PackedStringArray:
	var subscriptions: PackedStringArray = []
	subscriptions.append(SUB_HB)
	subscriptions.append(SUB_STATUS_CLIMATE)
	subscriptions.append(SUB_STATUS_VOLTAGE)
	subscriptions.append(SUB_STATUS_LED_POWER)
	subscriptions.append(SUB_STATUS_LED_COLOR)
	return subscriptions


## Mémorise l'identifiant du capteur dès que la passerelle l'annonce, avant ses premières publications.
func on_node_online(node_id: int, _mac_address: String, device_name: String) -> void:
	if device_name != DEVICE_NAME: return
	_node_id = node_id  # L'identifiant peut changer après un redémarrage du capteur
	%IDLabel.text = "node_id: %d" % _node_id


## Passe le capteur en déconnecté quand la passerelle le déclare hors ligne.
func on_node_offline(node_id: int, _reason_code: int) -> void:
	if node_id == _node_id: _connected = false


## Oublie le capteur quand la passerelle devient injoignable.
func on_gateway_reachability_changed(is_gateway_reachable: bool) -> void:
	if is_gateway_reachable: return
	_connected = false
	_node_id = UNKNOWN_NODE_ID  # La passerelle peut redistribuer les identifiants en redémarrant


## Aiguille les messages reçus de notre capteur vers leur traitement ; l'affichage ne change qu'ici.
func on_message(subject_name: String, source_id: int, payload: PackedByteArray) -> void:
	if subject_name == SUB_HB: _on_hb(source_id)
	if source_id != _node_id: return
	match subject_name:
		SUB_STATUS_CLIMATE: _on_status_climate(payload)
		SUB_STATUS_VOLTAGE: _on_status_voltage(payload)
		SUB_STATUS_LED_POWER: _on_status_led_power(payload)
		SUB_STATUS_LED_COLOR: _on_status_led_color(payload)


## Affiche la température (en °C) et l'humidité (en %) mesurées par le DHT11.
func _on_status_climate(payload: PackedByteArray) -> void:
	if payload.size() != CLIMATE_PAYLOAD_SIZE: return
	var temperature_degrees := payload.decode_s16(TEMPERATURE_OFFSET) / CENTIDEGREES_PER_DEGREE
	%Temperature.text = "Température : %.1f °C" % temperature_degrees
	%Humidité.text = "Humidité : %d %%" % payload.decode_u8(HUMIDITY_OFFSET)


## Affiche la tension lue sur la broche analogique, en millivolts.
func _on_status_voltage(payload: PackedByteArray) -> void:
	if payload.size() != VOLTAGE_PAYLOAD_SIZE: return
	%Tension.text = "Tension : %d mV" % payload.decode_u16(VOLTAGE_OFFSET)


## Affiche l'état réel de la LED et adapte le libellé du bouton à l'action qu'il déclenchera.
func _on_status_led_power(payload: PackedByteArray) -> void:
	if payload.size() != LED_POWER_PAYLOAD_SIZE: return
	var is_on := payload.decode_u8(LED_POWER_OFFSET) == LED_ON
	%Led.text = "LED : allumée" if is_on else "LED : éteinte"
	%LedButton.text = "Éteindre" if is_on else "Allumer"


## Affiche la couleur réellement appliquée par le capteur.
func _on_status_led_color(payload: PackedByteArray) -> void:
	if payload.size() != LED_COLOR_PAYLOAD_SIZE: return
	%LedColorSwatch.color = Color8(
		payload.decode_u8(RED_OFFSET),
		payload.decode_u8(GREEN_OFFSET),
		payload.decode_u8(BLUE_OFFSET))


## Demande au capteur d'inverser l'état de la LED ; l'affichage attend son status_.
func _on_led_toggle_button_pressed() -> void:
	publish(SUB_CMD_TOGGLE_LED, PackedByteArray(), true)


## Tire une couleur au hasard et la demande au capteur ; l'aperçu attend son status_.
func _on_random_color_button_pressed() -> void:
	var payload := PackedByteArray()
	payload.resize(LED_COLOR_PAYLOAD_SIZE)
	payload.encode_u8(RED_OFFSET, randi_range(0, MAX_COLOR_COMPONENT))
	payload.encode_u8(GREEN_OFFSET, randi_range(0, MAX_COLOR_COMPONENT))
	payload.encode_u8(BLUE_OFFSET, randi_range(0, MAX_COLOR_COMPONENT))
	publish(SUB_SET_LED_COLOR, payload, true)


## Affiche le capteur comme connecté et lui demande tout son état : la passerelle ne garde rien,
## une IHM qui démarre ne connaît donc pas encore l'état de la LED.
func _on_sensor_connected() -> void:
	%IDLabel.text = "node_id: %d" % _node_id
	%ConnectedIcon.state = true
	_set_values_fresh(true)
	publish(SUB_GET_STATE, PackedByteArray())


## Affiche le capteur comme déconnecté : valeurs grisées et commandes indisponibles.
func _on_sensor_disconnected() -> void:
	%IDLabel.text = "node_id: ∅"
	%ConnectedIcon.state = false
	_set_values_fresh(false)


## Grise ou non les valeurs affichées, et active ou non les boutons de commande.
func _set_values_fresh(is_fresh: bool) -> void:
	%Values.modulate = FRESH_MODULATE if is_fresh else STALE_MODULATE
	%LedButton.disabled = not is_fresh
	%CouleurAléatoireButton.disabled = not is_fresh


## Traite un heartbeat : (re)connecte le capteur et relance la temporisation.
func _on_hb(source_id: int) -> void:
	# Secours si la tuile n'a pas vu on_node_online (IHM lancée après le capteur, par exemple)
	if _node_id == UNKNOWN_NODE_ID: _node_id = source_id
	if source_id != _node_id: return
	_seconds_since_hb = 0.0
	_connected = true
