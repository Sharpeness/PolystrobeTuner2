# Рефакторинг StrobTuner под C++23 и подготовка к режиму Polytune

Комплексная модернизация кодовой базы стробоскопического гитарного и басового тюнера на фреймворке JUCE 8 под стандарт **C++23 (MSVC)** с соблюдением жёстких ограничений реального времени (Real-Time Audio, Lock-Free, AVX2 auto-vectorization) и закладкой фундамента под полифоническое распознавание всех струн (Polytune).

## User Review & Critical Decisions

> [!IMPORTANT]
> Ниже зафиксированы архитектурные решения, подтверждённые на этапе согласования, которые определяют порядок и правила рефакторинга.

- **Confirmed Decision 1 (Порядок рефакторинга — снизу вверх от DSP)**: Начинаем первый этап с математического ядра и таблиц пресетов (`TunerMath`, `TuningPresets`), затем переходим к детектору высоты тона (`MpmPitchDetector`), и далее к `AudioProcessor` и слою отображения.
- **Confirmed Decision 2 (Lock-Free архитектура состояния)**: Сохраняем быстрые неблокирующие атомики (`std::atomic` с явными барьерами памяти `std::memory_order_relaxed` / `acquire` / `release`), сгруппированные в чистые типизированные структуры состояния для монофонического и будущего полифонического режимов, без накладных расходов `AudioProcessorValueTreeState`.
- **Confirmed Decision 3 (C++23 модернизация и готовность к Polytune)**: Переводим таблицы строев и математику на `constexpr`, `std::array`, `std::string_view`, `std::span<const float>` и `std::expected`, избавляясь от сырых C-массивов и динамических аллокаций, а также закладываем структуру многострунного состояния (до 8 струн) для бесшовного подключения Polytune.

---

## 1. Overview & Core Concept

- **What It Does**: Переводит существующий монофонический стробоскопический тюнер со стандарта C++17 на современный идиоматический C++23, повышает безопасность работы с памятью и потоками, улучшает авто-векторизацию циклов DSP компилятором MSVC (`/O2 /arch:AVX2 /fp:fast`) и подготавливает сквозной тракт данных (DSP → Lock-Free State → UI) к отображению строя всех струн одновременно.
- **Target Audience / Persona**: Гитаристы и бас-гитаристы (6–8 струн гитара, 4–6 струн бас), использующие плагин в DAW (VST3/CLAP) или в режиме Standalone с ASIO.
- **Key Value**: Нулевые аллокации и отсутствие исключений в аудиопотоке, компиляция таблиц пресетов и нот на этапе компиляции (`constexpr`), отсутствие копирований строк в DSP и готовый контракт данных для полифонического режима.

---

## 2. User Experience & Visual Design

- **Key User Flows**:
  1. **Монофонический стробоскопический режим (Chromatic / Strobe)**: При извлечении одиночной струны детектор MPM определяет частоту с суб-центовой точностью, применяет поправку выбранного пресета (Peterson GTR/BAS Sweetener, EVH, James Taylor, Buzz Feiten) и сдвиг каподастра (`Capo`), плавно вращая двухслойную дугу стробоскопа и подсвечивая матрицу ноты `5x4` с диезом `5x5` и октавой `5x3`.
  2. **Готовность к полифоническому режиму (Polytune Mode)**: При ударе по всем открытым струнам сразу процессор передаёт в UI массив отклонений в центах и флагов активности для каждой из активных струн (`4..8`) выбранного пресета с допуском `polytuneTolerance`.
  3. **Управление пресетами и параметрами педали**: Переключение `Guitar / Bass`, выбор количества струн (`Strings`), транспозиция (`Capo`), демпфирование (`Accuracy 10..100%`), избранные пресеты (`★`) и бесшумная настройка (`Mute` footswitch).
- **Visual Identity & Theme**:
  - *Aesthetic Direction*: Аппаратная педаль в стиле студийного прибора с литым светло-серым корпусом, 3D-фаской тёмного экрана и физической оптикой светодиодов (трёхслойное свечение Wide Bloom, Tight Halo и LED Core).
  - *Color Palette & Mood*: Корпус педали `rgb(236, 238, 241)`, экран `rgb(23, 26, 37)`, неоновый зелёный `rgb(100, 255, 65)` (верхняя дуга и точное попадание в строй), неоновый голубой `rgb(70, 215, 255)` (нижняя дуга и заголовки секций), рубиново-красный `rgb(255, 30, 25)` (отклонение ноты и индикатор Mute).
  - *Typography & Hierarchy*: Чёткая техническая гарнитура без засечек для подписей органов управления и табличные моноширинные цифры для индикации герц (`Hz`) и центов (`c`) без горизонтального дрожания разрядов.
  - *Component Styling & Layout*: Фиксированная геометрия `448x260` (с поддержкой масштабирования в веб-превью), 3D металлические ручки `SteppedKnobComponent`, тумблер `GuitarBassSwitchComponent` и ножной переключатель `FootswitchComponent`.
- **Interactive Feedback & Motion**: Асимметричная тепловая баллистика разогрева и затухания светодиодов (60 FPS), плавный морфинг цвета матрицы ноты (красный ↔ зелёный) с гистерезисом вокруг порога `inTuneTolerance`, умное отсечение холостых перерисовок (`Smart Dirty-Checking`) и кэширование фоновых слоёв (`setBufferedToImage`).

---

## 3. Key Product Decisions & Trade-Offs

- **Decision 1: Полный перевод `TuningPresets` и `TunerMath` на `constexpr` и `std::array` (C++23)**
  - *Chosen Approach*: Заменить `std::vector<TuningPreset>` и `juce::String` внутри DSP-таблиц пресетов на `constexpr std::array<TuningPreset, N>` и `std::string_view`, а возврат результата расчёта ноты оформить через `std::expected<TunerResult, TunerError>` (или `constexpr` структуру без ветвлений).
  - *Why*: Исключает любые обращения к куче при инициализации и чтении пресетов из `processBlock`, гарантирует размещение таблиц в сегменте `.rdata` (read-only memory) и дружелюбность к кэшу процессора L1.
  - *Alternatives Considered*: Оставить `static const std::vector<TuningPreset>` с `juce::String` — отброшено, так как `juce::String` использует атомарный подсчёт ссылок в куче, что нежелательно при обращении из реального времени.
- **Decision 2: Оптимизация `MpmPitchDetector` под `std::span` и авто-векторизацию MSVC AVX2**
  - *Chosen Approach*: Принимать входной блок через `std::span<const float>`, выровнять рабочие буферы (`alignas(32)`), использовать `[[assume(...)]]` (C++23) и указатели `__restrict` в циклах расчёта автокорреляции NSDF и уточнения периода `computeFineNsdfAt`, полностью исключив ветвления внутри горячих циклов.
  - *Why*: Позволяет компилятору MSVC с флагами `/O2 /Oi /Ot /arch:AVX2 /fp:fast` генерировать чистые векторные инструкции `vfmadd231ps` (FMA3) по 8 `float` за такт.
  - *Alternatives Considered*: Ручные интринсики `<immintrin.h>` — усложняют читаемость и поддержку кода при сопоставимой производительности с чистым развёрнутым циклом на 4 аккумулятора.
- **Decision 3: Единая Lock-Free структура состояния с поддержкой Polytune**
  - *Chosen Approach*: Сгруппировать атомарные поля процессора в логические lock-free блоки (`TunerParameters`, `MonoTunerState`, `PolyTunerState` на `std::array<PolyStringState, 8>`) с явной семантикой `memory_order_relaxed` для телеметрии кадров и `std::expected` для сериализации/десериализации состояния XML.
  - *Why*: Сохраняет мгновенный доступ без блокировок между аудиопотоком и таймером GUI, устраняет «россыпь» из 25 отдельных полей в классе процессора и сразу предоставляет готовый буфер данных для будущего полифонического детектора и экрана Polytune.

---

## 4. Technical Architecture & Data Strategy *(Technical Reference)*

### Architecture & Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     StrobTunerAudioProcessor (Real-Time)                │
│                                                                         │
│  Audio Input (std::span<const float>)                                   │
│       │                                                                 │
│       ├──► MpmPitchDetector (C++23, AVX2-aligned, Zero-Alloc)           │
│       │         └──► Dual-Rate NSDF (12 kHz coarse + 48 kHz fine)       │
│       │                                                                 │
│       ├──► TuningPresets (constexpr std::array, Sweetener LUT)          │
│       │         └──► getSweetenerOffsetForMidi()                        │
│       │                                                                 │
│       ├──► TunerMath (constexpr, std::expected<TunerResult, Error>)     │
│       │                                                                 │
│       └──► Lock-Free Atomic State Bridge (std::memory_order_relaxed)    │
│             ├─► MonoState  (freq, cents, note, sharp, octave, active)   │
│             ├─► PolyState  (std::array<StringState, 8>, isPolyActive)   │
│             └─► Parameters (refPitch, accuracy, preset, strings, capo)  │
└───────────────────────────────────┬─────────────────────────────────────┘
                                    │ 60 FPS Lock-Free Poll
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                       StrobTunerAudioProcessorEditor                    │
│                                                                         │
│  ┌────────────────────────────────────┐  ┌───────────────────────────┐  │
│  │        Main Display Bezel          │  │    Right Control Panel    │  │
│  │  ├─► StrobeArcComponent (Cached)   │  │  ├─► PresetSelector       │  │
│  │  ├─► NoteMatrixComponent (5x4+5x5) │  │  ├─► SteppedKnobs (x3)    │  │
│  │  ├─► Freq & Cents Readout          │  │  ├─► GuitarBassSwitch     │  │
│  │  ├─► RefPitchComponent             │  │  ├─► SettingsButton       │  │
│  │  └─► Overlays (Settings / Presets) │  │  └─► Footswitch (Mute)    │  │
│  └────────────────────────────────────┘  └───────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### Порядок пошагового рефакторинга (4 этапа)

1. **Этап 1 — Математическое ядро и константные пресеты (`TunerMath.h` и `TuningPresets.h`)**:
   - Перевод `TunerMath` на `constexpr` функции, `std::expected<TunerResult, TunerError>` и `std::array<std::string_view, 12>`.
   - Перевод `TuningPresets` на `constexpr std::array<TuningPreset, 27>` (Guitar) и `constexpr std::array<TuningPreset, 14>` (Bass) с `std::string_view` и `std::array<int, 8>` / `std::array<float, 8>` вместо C-массивов и `std::vector`.
   - Добавление `constexpr` методов получения активных струн через структуру фиксированного размера (`ActiveStringsView`), готовую как для моно-тюнера, так и для полифонической сетки.
2. **Этап 2 — Вычислительное ядро детектора (`MpmPitchDetector.h`)**:
   - Перевод `process` на `std::span<const float>`, замена динамических `std::vector` на предвыделенные выровненные буферы фиксированного капасити (`alignas(32) std::array<float, ...>`), что на 100% исключает аллокации даже в `prepare()`.
   - Применение атрибутов C++23 и безветвленного кода во внутренних циклах `runDetection` и `computeFineNsdfAt` для гарантированной векторизации AVX2 в MSVC.
3. **Этап 3 — Аудиопроцессор и Lock-Free мост состояния (`PluginProcessor.h / .cpp`)**:
   - Организация всех атомарных переменных в структурированные Lock-Free блоки (включая `PolyTunerState` под режим Polytune) с явными `memory_order_relaxed`.
   - Безопасная работа с состоянием XML через `std::expected` без риска исключений.
4. **Этап 4 — Графический интерфейс и синхронизация (`Source/ui/*`, `PluginEditor.h / .cpp` + Web Preview)**:
   - Обновление компонентов UI (`PresetSelectorComponent`, `PresetBrowserOverlay`, `NoteMatrixComponent`, `StrobeArcComponent`) для работы с новыми `constexpr` пресетами и структурой состояния процессора, а также синхронизация интерактивного веб-превью.
