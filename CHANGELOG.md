# Cambios

## 0.3.0 — 30 de septiembre de 2026 — preliminar

- Instalador único en C++ con todos los componentes embebidos, consola de progreso, permiso de Windows y acceso al escritorio.
- Selección de micrófonos activos de cualquier marca; selección automática si solo hay uno.
- Procesamiento de 8–192 kHz y de 1–8 canales mediante remuestreo interno SpeexDSP y RNNoise a 48 kHz.
- Conservación del estado al reiniciar y al actualizar. Abrir la bandeja no activa el filtro; salir lo desactiva.
- Comprobación de captura real sin guardar voz, registros persistentes y restauración ante fallos de captura.
- Registro APO con una sola interfaz de procesamiento y agregación COM correcta.
- Pruebas privadas de DSP, formatos, continuidad, COM y paquete; prueba física con Wavo POD y confirmación del usuario.

La instalación gestiona un micrófono por equipo. Otros APOs existentes se conservan y los conflictos detienen la instalación. Versión x64 para Intel/AMD, sin firma digital; necesita comprobarse en otros controladores y equipos.
