#include "weather_code.h"

const char *weatherCodeLabel(int code) {
  switch (code) {
    case 0: return "Despejado";
    case 1: return "Mayormente despejado";
    case 2: return "Parcialmente nublado";
    case 3: return "Nublado";
    case 45:
    case 48: return "Niebla";
    case 51: return "Llovizna leve";
    case 53: return "Llovizna";
    case 55: return "Llovizna intensa";
    case 56:
    case 57: return "Llovizna helada";
    case 61: return "Lluvia leve";
    case 63: return "Lluvia";
    case 65: return "Lluvia intensa";
    case 66:
    case 67: return "Lluvia helada";
    case 71: return "Nieve leve";
    case 73: return "Nieve";
    case 75: return "Nieve intensa";
    case 77: return "Granos de nieve";
    case 80: return "Chaparrones leves";
    case 81: return "Chaparrones";
    case 82: return "Chaparrones fuertes";
    case 85: return "Chaparrones de nieve";
    case 86: return "Nevadas fuertes";
    case 95: return "Tormenta";
    case 96:
    case 99: return "Tormenta con granizo";
    default: return "Sin datos";
  }
}

uint8_t weatherCodeIcon(int code) {
  switch (code) {
    case 0: return WEATHER_ICON_CLEAR;
    case 1:
    case 2: return WEATHER_ICON_PARTLY;
    case 3: return WEATHER_ICON_CLOUDY;
    case 45:
    case 48: return WEATHER_ICON_FOG;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57: return WEATHER_ICON_DRIZZLE;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82: return WEATHER_ICON_RAIN;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86: return WEATHER_ICON_SNOW;
    case 95:
    case 96:
    case 99: return WEATHER_ICON_STORM;
    default: return WEATHER_ICON_UNKNOWN;
  }
}
