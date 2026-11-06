#pragma once

#include <stdint.h>

/// Adresse MAC de la passerelle du TP, affichée par l'IHM au démarrage.
static const uint8_t kGatewayMacAddress[6] = {0x90, 0xDA, 0x72, 0x52, 0xEE, 0x1C};
/// Canal Wi-Fi de la passerelle, affiché par l'IHM au démarrage.
constexpr uint8_t kWifiChannel = 5;

// Seule macro du croquis : elle est collée aux préfixes pour former les noms de subjects.
// Nom unique dans la classe : deux appareils du même nom recevraient les messages l'un de l'autre.
#define DEVICE_NAME "sensor_j"
