# Wavo Filter

Reducción de ruido nativa para micrófonos en Windows, escrita en C++ y basada en RNNoise. Instala un efecto APO propio y ofrece un icono para activar, desactivar y ajustar el filtro. A pesar del nombre, permite elegir micrófonos de cualquier marca.

**[Descargar el instalador para Windows x64](https://github.com/dean6609/wavo-filter/releases/download/v0.3.0/WavoFilter-Setup.exe)** · [Releases](https://github.com/dean6609/wavo-filter/releases)

Versión preliminar 0.3.0. La captura física se comprobó con un Wavo POD. Otras frecuencias y canales tienen pruebas automatizadas; otros micrófonos necesitan comprobarse en su equipo. El instalador conserva los efectos existentes de otros fabricantes.

## Instalar

1. Descarga **WavoFilter-Setup.exe**, haz doble clic y acepta el permiso de administrador de Windows.
2. Solo aparecen micrófonos conectados y habilitados. Si hay uno, continúa automáticamente; si hay varios, pide elegir su número.
3. Una consola explica el funcionamiento, muestra el progreso, guarda una copia de restauración y comprueba la captura sin guardar grabaciones.
4. Al terminar encontrarás **Wavo Filter** en el escritorio. Abre ese acceso cuando quieras mostrar el icono y controlar el filtro.

El modelo y todos los componentes vienen dentro del instalador. No necesitas internet durante la instalación, compilar nada, Clownfish, un cable virtual ni un anfitrión VST. No hay asistente con «Siguiente» ni elección de carpeta. Los archivos quedan en `%ProgramFiles%\WavoFilter` y el estado en `%ProgramData%\WavoFilter`.

El ejecutable preliminar no está firmado digitalmente y Windows puede mostrar un aviso de editor desconocido. No requiere desactivar Secure Boot ni cambiar las protecciones de audio. Es un APO de usuario, sin controlador de kernel; no es un paquete INF certificado para distribución general.

## Cómo funciona después

| Acción | Resultado |
| --- | --- |
| Primera instalación | Activa RNNoise, sin puerta adicional de silencio. |
| Abrir Wavo Filter | Muestra el icono; conserva el ajuste actual. |
| Un clic en el icono | Alterna entre filtrado y audio original. |
| «Salir (dejar audio original)» | Desactiva el filtro y cierra el programa. |
| Reiniciar Windows | Conserva el ajuste activado o desactivado. |
| No abrir el programa | Windows procesa el micrófono con el ajuste guardado. |
| Actualizar | Conserva el estado y el perfil elegidos. |

El programa de la bandeja solo controla el efecto. Windows carga el motor cuando una aplicación utiliza el micrófono. «Iniciar con Windows» permite mostrar el icono al iniciar sesión; no es necesario para que funcione el filtro.

En el menú puedes consultar **Diagnóstico**, elegir perfiles de detección y cambiar la mezcla entre filtrado y original. **Filtro confirmado** indica actividad reciente del APO y formato compatible. Cuando ninguna aplicación usa el micrófono, el efecto queda esperando; no graba audio por su cuenta.

Para retirar el efecto: clic derecho → **Retirar efecto y restaurar configuración…** y después reconecta el micrófono o reinicia Windows. El programa queda disponible. Esta versión administra un micrófono por instalación; retira el efecto anterior y ejecuta de nuevo el instalador para elegir otro.

## Compatibilidad y sonido

Se aceptan formatos internos de Windows en flotante de 32 bits, de 1 a 8 canales y entre 8 y 192 kHz. El hardware puede trabajar a otra profundidad de bits; el motor compartido de Windows hace su conversión. SpeexDSP adapta internamente la frecuencia a 48 kHz para RNNoise y devuelve el resultado a la frecuencia original. No se cambia el formato configurado del dispositivo.

La integración necesita que el controlador y la aplicación utilicen la ruta de efectos APO de Windows. Algunas aplicaciones RAW o en modo exclusivo pueden evitarla. Si hay otros APOs en el mismo punto de la cadena, la instalación se detiene y conserva su configuración. Si la captura falla tras instalar, intenta restaurar la asociación anterior. Si hay captura pero aún no se observan callbacks propios, informa que la instalación está pendiente de confirmar y pide reconectar o reiniciar; no anuncia que funciona sin comprobarlo.

El motor y los pesos RNNoise son los incluidos en Werman v1.21. No hay entrenamiento personalizado ni descarga de un modelo «latest» que cambie entre instalaciones. El perfil inicial desactiva el silenciamiento adicional; RNNoise continúa reduciendo ruido. Los perfiles del 60% y 85% agregan una puerta basada en la probabilidad de voz.

A 48 kHz hay un retardo algorítmico nominal de 20 ms, además del dispositivo y la aplicación. A otras frecuencias se suman los filtros de remuestreo y un pequeño margen de cola. El APO informa su retardo a Windows; no es una medición acústica completa. En bypass el audio original pasa sin cambiar sus muestras ni ejecutar la red. El cambio de retardo al activar/desactivar puede percibirse como un salto breve.

Evalúa consonantes y finales de palabras, además del ruido. La actividad del filtro no garantiza por sí sola la calidad del dictado.

## Compilar y empaquetar

En Windows x64, con PowerShell:

```powershell
.\bootstrap-toolchain.ps1
.\build.ps1 -RunTests
.\package.ps1 -RunTests
```

El primer comando prepara LLVM-MinGW portátil desde su fuente oficial y comprueba su SHA-256. No instala un compilador en Windows. Los siguientes producen la aplicación, la DLL y **dist\WavoFilter-Setup.exe**, con los componentes como recursos embebidos. **SHA256SUMS.txt** permite comprobar el instalador descargado.

Las pruebas usan controles y telemetría privados; no contaminan el estado instalado. Comprueban bypass exacto, equivalencia directa con RNNoise a 48 kHz, callbacks variables, mezcla alineada, procesamiento in situ, formatos de 8/16/22,05/44,1/48/96/192 kHz, 1/2/4/8 canales, continuidad del remuestreo, frecuencia fundamental de voz, cambios de estado, agregación COM, negociación del APO y límites de buffers. El paquete verifica extracción aislada, recursos embebidos, scripts compatibles con PowerShell 5.1 y checksum. Las pruebas sintéticas no certifican todos los controladores de audio.

Comandos de diagnóstico y control, sin abrir el icono:

```powershell
.\dist\WavoFilter.exe --status
.\dist\WavoFilter.exe --enable
.\dist\WavoFilter.exe --disable
.\dist\WavoFilter.exe --check-audio
.\dist\WavoFilter.exe --probe-audio
.\dist\WavoFilter-Setup.exe --list-devices
.\dist\WavoFilter-Setup.exe --self-test
```

`--check-audio` abre una captura sin iniciarla. `--probe-audio` inicia 2,5 segundos de captura y muestra contadores y nivel, sin guardar muestras. Los registros del instalador están en `%ProgramData%\WavoFilter\logs`; las operaciones desde la bandeja guardan sus registros en `%LOCALAPPDATA%\WavoFilter\logs`.

## Procedencia y licencia

- Código propio: GPL-3.0, ver [LICENSE](LICENSE).
- [Werman v1.21](https://github.com/werman/noise-suppression-for-voice/tree/v1.21): commit `4b0a6f76fc8bcfb5a3603ae2cb6619dfcb0e19f2`.
- RNNoise incluido: commit `70f1d256acd4b34a572f999a05c87bf00b67730d`; licencia BSD conservada en `vendor/rnnoise/COPYING` y en el instalador.
- [SpeexDSP 1.2.1](https://github.com/xiph/speexdsp/tree/SpeexDSP-1.2.1): commit `1b28a0f61bc31162979e1f26f3981fc3637095c8`; remuestreador sin modificaciones y licencias BSD conservadas.
- Encabezados APO de Microsoft [win32metadata](https://github.com/microsoft/win32metadata), conservando su copyright. Compilador [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw), fijado en `compiler-source.json`.

Documentación: [APOs de Windows](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects) y [agregación COM](https://learn.microsoft.com/en-us/windows/win32/com/aggregation).
