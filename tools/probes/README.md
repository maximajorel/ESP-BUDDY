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
| `config_knobs.txt` | CONFIG con `−`/`+`: un toque aprieta el control marcado, doble toque mueve el foco, la rotación no pierde la fila y la pantalla no se apaga. |
| `rotation.txt` | Modo automático, bloqueo e imagen fijada. |
| `rot_auto_check.txt` | La política automática con `imufake`: gira en el agarre parado y en el de costado, y un rolido de timón no la mueve. |
| `rotation_restore.txt` | Devuelve el equipo al estado de arranque tras la prueba. |
| `steer_calibration.txt` | Calibración de ejes del juego con `imufake`. |
| `steer_swap.txt` | Intercambio de ejes desde la API. |
| `game_fire.txt` | Disparos y estado del juego. |
| `game_difficulty.txt` | Curva de dificultad con el paso del tiempo. |
| `wifi_recovery.txt` | Credenciales incorrectas y `wifi/forget`. |
| `imu.txt` | Lecturas crudas del sensor. |
| `hardening_verify.txt` | Humo después de flashear: arranque, `help`, estado, CONFIG y API. |
| `buddy_life.txt` | La cara de Sofía: microsaccadas, cejas neutrales, anatomía de enojo/mareo y el tilde sintetizado del HUD. |
| `buddy_ball_gravity.txt` | La pelotita de Sofía con gravedad calibrada: `imufake` inclina a un lado y al otro y `buddydraw` muestra hacia dónde cae y a qué velocidad. |
| `calibration_config.txt` | El wizard de calibración en CONFIG: lo abre el botón del panel, se completan centro + cuatro direcciones con `imufake`, y se verifica `steer.calibrated` y los ejes en `/api/status`. |
| `buddy_game_face.txt` | El ánimo del ta-te-ti web llega a la cara: `focus` (lid 0.18), `thinking` (parpadeo asimétrico), `anger` (`hot=1.00`, `grow=1.12`, ojos inclinados ±8°), `sleepy` (ojo cerrado) y el limpiado que devuelve la pose del bridge. |
| `buddy_sleep_events.txt` | Sueño local y `/events`: el reloj de silencio, el reset por IMU, `sleep_hint`, el cursor y un run de punta a punta. |
| `buddy_sleep_shake.txt` | La sacudida contra el reloj de sueño, sin `sleep` de por medio (el `wakeUntil` dura 4 s y una espera larga lo tapa). |
| `buddy_ball.txt` | La cara feliz con squircle y curva bajo los ojos, la pelotita roja empujada por `imufake`, sus rebotes y el doble toque que la prende y apaga. Requiere el bridge en idle. |
| `console_smoke.txt` | Ejercita todos los comandos de consola que movió el refactor (toques, menú, rotación, ejes, LED, buddy, IMU falso, assets, fuente, parser y framebuffer). Deja el reloj en 12 h si el doble toque entra: volvé a tocarlo en RELOJ para restaurar 24 h. |
| `all_screens.txt` | `dump` de las siete pantallas, una por una. |
| `ap_policy.txt` | La política del punto de acceso: arranca arriba, se apaga solo a los 30 s de enlace de casa probado, y `ap on`/`off`/`auto` lo levantan y lo bajan desde la consola sin tirar el enlace STA. Requiere el WiFi de casa configurado. |
