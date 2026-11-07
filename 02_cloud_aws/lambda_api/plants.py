"""Catalogo de plantas de PotAI.

El modelo de IA recibe el tipo de planta como un NUMERO (columna `localname`
del dataset). Regla: id = posicion de la especie en esta lista (1..8).
El firmware (menuconfig -> Tipo de planta) usa exactamente los mismos ids.

Los datos son los de la tabla `species` del proyecto:
  common_name, scientific_name, optimal_water_frequency_days,
  optimal_light_hours, optimal_humidity_percent
"""

PLANTS = {
    1: {
        "common_name": "ajo",
        "scientific_name": "Allium sativum",
        "optimal_water_frequency_days": 3,
        "optimal_light_hours": 6,
        "optimal_humidity_percent": 60,
    },
    2: {
        "common_name": "geranio",
        "scientific_name": "Pelargonium",
        "optimal_water_frequency_days": 4,
        "optimal_light_hours": 6,
        "optimal_humidity_percent": 50,
    },
    3: {
        "common_name": "hierbabuena",
        "scientific_name": "Mentha spicata",
        "optimal_water_frequency_days": 2,
        "optimal_light_hours": 5,
        "optimal_humidity_percent": 70,
    },
    4: {
        "common_name": "menta",
        "scientific_name": "Mentha",
        "optimal_water_frequency_days": 2,
        "optimal_light_hours": 5,
        "optimal_humidity_percent": 70,
    },
    5: {
        "common_name": "oregano",
        "scientific_name": "Origanum vulgare",
        "optimal_water_frequency_days": 5,
        "optimal_light_hours": 6,
        "optimal_humidity_percent": 40,
    },
    6: {
        "common_name": "orquidea",
        "scientific_name": "Orchidaceae",
        "optimal_water_frequency_days": 7,
        "optimal_light_hours": 4,
        "optimal_humidity_percent": 60,
    },
    7: {
        "common_name": "rosachina",
        "scientific_name": "Hibiscus rosa-sinensis",
        "optimal_water_frequency_days": 3,
        "optimal_light_hours": 6,
        "optimal_humidity_percent": 60,
    },
    8: {
        "common_name": "tomatecherry",
        "scientific_name": "Solanum lycopersicum var. cerasiforme",
        "optimal_water_frequency_days": 2,
        "optimal_light_hours": 8,
        "optimal_humidity_percent": 65,
    },
}


def plant_info(plant_id):
    """Devuelve el dict de la planta (con su id) o None si no existe."""
    if type(plant_id) is not int or plant_id not in PLANTS:
        return None
    return {"id": plant_id, **PLANTS[plant_id]}


def plant_catalog():
    return [{"id": pid, **data} for pid, data in sorted(PLANTS.items())]
