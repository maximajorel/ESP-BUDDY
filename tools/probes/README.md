# Secuencias de sondeo

Cada `.txt` de esta carpeta es una lista de comandos de la consola serie, una por
línea, que `gadget_probe.py` ejecuta en orden. La línea especial `sleep <ms>` la
espera el script del lado de la PC, no el firmware.

```powershell
python tools\gadget_probe.py @tools\probes\rotation.txt
```

Se usan porque la pantalla no tiene MISO: la única verificación honesta sin una
cámara es el comando `dump`, que escribe el framebuffer en ASCII.

| Archivo | Qué prueba |
| --- | --- |
| `status_after_boot.txt` | Estado básico después del arranque. |
| `dump_screens.txt` | Render ASCII de las pantallas de red y música. |
| `screens_and_menu.txt` | Recorre las pantallas, abre el menú y verifica la API. |
| `config_screen.txt` | Las filas de CONFIG: brillo, rotación y bloqueo. |
| `rotation.txt` | Modo automático, bloqueo e imagen fijada. |
| `rotation_restore.txt` | Devuelve el equipo al estado de arranque tras la prueba. |
| `steer_calibration.txt` | Calibración de ejes del juego con `imufake`. |
| `steer_swap.txt` | Intercambio de ejes desde la API. |
| `game_fire.txt` | Disparos y estado del juego. |
| `game_difficulty.txt` | Curva de dificultad con el paso del tiempo. |
| `wifi_recovery.txt` | Credenciales incorrectas y `wifi/forget`. |
| `imu.txt` | Lecturas crudas del sensor. |
