# Ultimate Vocal

Plugin **VST3** para Windows (probado en el flujo de Ableton Live) que reúne en un solo lugar la
afinación automática tipo Auto-Tune y un editor de tono visual inspirado en Melodyne, más una pequeña
cadena vocal de estudio. Interfaz estilo HUD futurista.

## Funciones

| Módulo | Qué hace |
|---|---|
| **100 · Nota actual** | Anillo con la nota objetivo, Hz detectados y desviación en cents |
| **200 · Editor de tono** | Traza de tono de entrada y corregida en tiempo real, bloques de nota; clic en la columna de notas para activar/desactivar notas |
| **300 · Afinación** | Tonalidad, escala (9 tipos), velocidad de retune (0–400 ms), cantidad, transposición, ajuste fino, bypass |
| **400 · Escala** | Teclado para elegir manualmente qué notas están permitidas |
| **500 · Entrada** | Ganancia, filtro paso alto, puerta de ruido |
| **600 · Voz** | Desplazamiento de formantes (±6 st), mezcla seco/húmedo |
| **700 · Dinámica** | Compresor y ganancia de salida |
| **800 · Niveles** | Medidores IN / OUT / GR |

## Motor

- Detección de tono **YIN** (90–1000 Hz) con interpolación parabólica
- Cuantización a escala con histéresis y constante de tiempo de retune
- Síntesis **TD-PSOLA** sobre retícula de épocas, con control independiente de formantes
- Latencia fija (~39 ms), informada al DAW para compensación automática
- Probado con señales sintéticas a 44.1 / 48 / 96 kHz (`Tests/dsp_test.cpp`): error < 1 cent

## Límites conocidos (honestidad técnica)

- Es un procesador **monofónico en tiempo real**. No incluye edición polifónica (DNA) ni la edición
  offline de notas sobre audio ya grabado de Melodyne (requiere ARA, que no existe en el SDK libre de VST3).
- Para una voz sola por pista. Con coros o armonías el detector puede equivocarse de octava.

## Compilar

GitHub Actions compila automáticamente en Windows con cada *push* (pestaña **Actions** → artefacto
`UltimateVocal-Windows-VST3`). Para compilar en local:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target UltimateVocal_VST3
```

El VST3 queda en `build/UltimateVocal_artefacts/Release/VST3/Ultimate Vocal.vst3`.
Instalación: ver [INSTALL.md](INSTALL.md).

Hecho con [JUCE](https://juce.com) 8 (licencia AGPLv3/comercial: revisa sus términos si distribuyes el plugin).
