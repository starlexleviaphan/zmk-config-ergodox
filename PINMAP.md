# Маппинг пинов матрицы ErgoDox для ZMK (nice!nano v2.0)

Заполните соответствие пинов для каждой половины:

- Укажите после знака `=` к какому сигналу подключен пин: `ROW0`..`ROW4` (строки) или `COL0`..`COL8` (столбцы).
- Неиспользуемые пины оставьте пустыми после знака `=`.
- Пины питания (`GND`, `3.3V`, `BATTERY+`, `RESET`) в матрице клавиш не участвуют.

---

## 1. ЛЕВАЯ ПОЛОВИНА (LEFT HALF)

```text
                                ┌──────────────────────────┐
                                │         [USB-C]          │
                                │                          │
(D1  / P0.06 / &pro_micro 1)    │ [D1]        [BATTERY+]   │ (BATTERY+ / RAW)
(D0  / P0.08 / &pro_micro 0)    │ [D0]        [GND]        │ (GND)
(GND)                           │ [GND]       [RESET]      │ (RESET)
(GND)                           │ [GND]       [3.3V]       │ (3.3V / VCC)
(D2  / P0.17 / &pro_micro 2)    │ [D2]        [D21 / P0.31]│ (D21 / P0.31 / &pro_micro 21)
(D3  / P0.20 / &pro_micro 3)    │ [D3]        [D20 / P0.29]│ (D20 / P0.29 / &pro_micro 20)
(D4  / P0.22 / &pro_micro 4)    │ [D4]        [D19 / P0.02]│ (D19 / P0.02 / &pro_micro 19)
(D5  / P0.24 / &pro_micro 5)    │ [D5]        [D18 / P1.15]│ (D18 / P1.15 / &pro_micro 18)
(D6  / P1.00 / &pro_micro 6)    │ [D6]        [D15 / P1.13]│ (D15 / P1.13 / &pro_micro 15)
(D7  / P0.11 / &pro_micro 7)    │ [D7]        [D14 / P1.11]│ (D14 / P1.11 / &pro_micro 14)
(D8  / P1.04 / &pro_micro 8)    │ [D8]        [D16 / P0.10]│ (D16 / P0.10 / &pro_micro 16)
(D9  / P1.06 / &pro_micro 9)    │ [D9]        [D10 / P0.09]│ (D10 / P0.09 / &pro_micro 10)
                                └──────────────────────────┘
```

### Заполните для ЛЕВОЙ половины:

```text
[Левый ряд пинов - сверху вниз]
D1  (P0.06 / &pro_micro 1)  =
D0  (P0.08 / &pro_micro 0)  =
D2  (P0.17 / &pro_micro 2)  = Col 0
D3  (P0.20 / &pro_micro 3)  = Col 1
D4  (P0.22 / &pro_micro 4)  = Col 2
D5  (P0.24 / &pro_micro 5)  = Col 3
D6  (P1.00 / &pro_micro 6)  = Col 4
D7  (P0.11 / &pro_micro 7)  = Col 5
D8  (P1.04 / &pro_micro 8)  = Col 6
D9  (P1.06 / &pro_micro 9)  = Col 7

[Правый ряд пинов - сверху вниз от D21 до D10]
D21 (P0.31 / &pro_micro 21) =
D20 (P0.29 / &pro_micro 20) = Row 4
D19 (P0.02 / &pro_micro 19) = Row 3
D18 (P1.15 / &pro_micro 18) = Row 2
D15 (P1.13 / &pro_micro 15) = Row 1
D14 (P1.11 / &pro_micro 14) = Row 0
D16 (P0.10 / &pro_micro 16) =
D10 (P0.09 / &pro_micro 10) = Col 8
```

---

## 2. ПРАВАЯ ПОЛОВИНА (RIGHT HALF)

```text
                                ┌──────────────────────────┐
                                │         [USB-C]          │
                                │                          │
(D1  / P0.06 / &pro_micro 1)    │ [D1]        [BATTERY+]   │ (BATTERY+ / RAW)
(D0  / P0.08 / &pro_micro 0)    │ [D0]        [GND]        │ (GND)
(GND)                           │ [GND]       [RESET]      │ (RESET)
(GND)                           │ [GND]       [3.3V]       │ (3.3V / VCC)
(D2  / P0.17 / &pro_micro 2)    │ [D2]        [D21 / P0.31]│ (D21 / P0.31 / &pro_micro 21)
(D3  / P0.20 / &pro_micro 3)    │ [D3]        [D20 / P0.29]│ (D20 / P0.29 / &pro_micro 20)
(D4  / P0.22 / &pro_micro 4)    │ [D4]        [D19 / P0.02]│ (D19 / P0.02 / &pro_micro 19)
(D5  / P0.24 / &pro_micro 5)    │ [D5]        [D18 / P1.15]│ (D18 / P1.15 / &pro_micro 18)
(D6  / P1.00 / &pro_micro 6)    │ [D6]        [D15 / P1.13]│ (D15 / P1.13 / &pro_micro 15)
(D7  / P0.11 / &pro_micro 7)    │ [D7]        [D14 / P1.11]│ (D14 / P1.11 / &pro_micro 14)
(D8  / P1.04 / &pro_micro 8)    │ [D8]        [D16 / P0.10]│ (D16 / P0.10 / &pro_micro 16)
(D9  / P1.06 / &pro_micro 9)    │ [D9]        [D10 / P0.09]│ (D10 / P0.09 / &pro_micro 10)
                                └──────────────────────────┘
```

### Заполните для ПРАВОЙ половины:

```text
[Левый ряд пинов - сверху вниз]
D1  (P0.06 / &pro_micro 1)  =
D0  (P0.08 / &pro_micro 0)  =
D2  (P0.17 / &pro_micro 2)  = Row 4
D3  (P0.20 / &pro_micro 3)  = Row 3
D4  (P0.22 / &pro_micro 4)  = Row 2
D5  (P0.24 / &pro_micro 5)  = Row 1
D6  (P1.00 / &pro_micro 6)  = Row 0
D7  (P0.11 / &pro_micro 7)  =
D8  (P1.04 / &pro_micro 8)  =
D9  (P1.06 / &pro_micro 9)  = Col 8

[Правый ряд пинов - сверху вниз от D21 до D10]
D21 (P0.31 / &pro_micro 21) = Col 0
D20 (P0.29 / &pro_micro 20) = Col 1
D19 (P0.02 / &pro_micro 19) = Col 2
D18 (P1.15 / &pro_micro 18) = Col 3
D15 (P1.13 / &pro_micro 15) = Col 4
D14 (P1.11 / &pro_micro 14) = Col 5
D16 (P0.10 / &pro_micro 16) = Col 6
D10 (P0.09 / &pro_micro 10) = Col 7
```
