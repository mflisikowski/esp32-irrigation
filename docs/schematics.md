# Schemat połączeń — ESP32 Irrigation Controller

## Zaciski ESP32

```
                  ESP32 DevKit
            ┌─────────────────────┐
            │                     │
    SENSOR  │ GPIO34 ← czujnik deszczu (NO/NC)
    PRZEPLYW│ GPIO35 ← czujnik przepływu (opcjonalnie)
            │                     │
    PRZEKAŹNIKI (active LOW — przekaźnik włącza się przy LOW)
            │
    Z1 S2a  │ GPIO32 → moduł przekaźników IN1
    Z2 S2b  │ GPIO33 → IN2
    Z3 S1S3 │ GPIO25 → IN3
    Z4 S4   │ GPIO26 → IN4
    Z5 S5   │ GPIO27 → IN5
    Z6 rabat│ GPIO14 → IN6
            │
    ZASILANIE│
    5V      │ VIN ← 5V z zasilacza USB (lub HLK-PM01)
    GND     │ GND ← masa wspólna
            └─────────────────────┘
```

## Moduł przekaźników 8ch 5V

```
    ┌─────────────────────────┐
    │ MODUŁ PRZEKAŹNIKÓW 8CH │
    │                         │
    │ IN1 ← GPIO32           │
    │ IN2 ← GPIO33           │
    │ IN3 ← GPIO25           │
    │ IN4 ← GPIO26           │
    │ IN5 ← GPIO27           │
    │ IN6 ← GPIO14           │
    │ IN7 — nieużywane       │
    │ IN8 — nieużywane       │
    │                         │
    │ VCC → 5V               │
    │ GND → GND              │
    │                         │
    │ COM → 24V AC (+)       │
    │ NO1 → zawór DV-100 Z1  │
    │ NO2 → zawór DV-100 Z2  │
    │ NO3 → zawór DV-100 Z3  │
    │ NO4 → zawór DV-100 Z4  │
    │ NO5 → zawór DV-100 Z5  │
    │ NO6 → zawór DV-100 Z6  │
    └─────────────────────────┘
```

## Zawory elektromagnetyczne Rain Bird DV-100

```
    ┌─────────────────────────────┐
    │  Rain Bird DV-100 1"       │
    │                             │
    │  Cewka 24V AC (~400mA)     │
    │                             │
    │  Jeden przewód → NO z      │
    │  modułu przekaźników        │
    │                             │
    │  Drugi przewód → wspólny    │
    │  (COM) 24V AC (-)           │
    └─────────────────────────────┘

    UWAGA: Wszystkie zawory dzielą jeden
    przewód wspólny (common). Z modułu
    przekaźników: COM → 24V AC (+)
    NO1..NO6 → do każdego zaworu.
    Drugi koniec każdego zaworu →
    do 24V AC (-) wspólnie.
```

## Zasilanie

```
    ┌─────────────────────┐
    │ Zasilacz 24V AC 2A  │
    │ (np. do dzwonka)    │
    ├─────────────────────┤
    │ (+) → COM modułu    │
    │ (-) → common zaworów│
    └─────────────────────┘

    ┌─────────────────────┐
    │ Zasilacz 5V 2A USB  │
    │ (lub HLK-PM01 230V  │
    │  → 5V)              │
    ├─────────────────────┤
    │ 5V → VIN ESP32      │
    │ GND → GND ESP32     │
    └─────────────────────┘
```

## Czujnik deszczu

```
    ┌─────────────────────┐
    │ FC-37 / RSD-BEx      │
    │                     │
    │ Wyjście NC/GND      │
    │                     │
    │ NC → GPIO34          │
    │ GND → GND           │
    │                     │
    │ (Pull-up wewn. EN)  │
    │ GPIO34 = LOW → pada │
    └─────────────────────┘
```

## Schemat blokowy

```
  230V AC
    │
    ├── Zasilacz 24V AC ──┬── COM modułu przekaźników
    │                     └── common (-) wszystkich zaworów
    │
    └── Zasilacz 5V DC ──── VIN ESP32

  ESP32 GPIO ──→ Moduł przekaźników ──→ Zawory DV-100 ──→ Linie wodne
  ESP32 GPIO ──→ Czujnik deszczu
```

## Uwagi

- Przekrój przewodów do zaworów: min 0.75mm² (1.0mm² zalecane) dla odległości do 20m
- Dla dłuższych odcinków: 1.5mm²
- Obudowa IP65 (sterownik + przekaźniki + zasilacz) montowana w budce gospodarczej
- Skrzynka zaworowa (valve box) przy rozdzielaczu w ogrodzie
- Kabel sterujący z budki do skrzynki zaworowej: YDYp 7×1mm² (6 stref + common)
