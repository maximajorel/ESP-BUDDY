# Baile de Sofía: baila cuando suena Spotify

Emoción nueva del pedido "nuevas emociones para SOFIA (buddy)", punto 1. Cuando el
dueño escucha música en Spotify, la cara baila: los ojos siguen el compás del
tema y salen notas musicales que suben y se desvanecen. Alcance: la capa pura de
pose/notas, el renderer de `screen_buddy.cpp`, el disparador de consola y el chip
del panel. No cambia el contrato con el bridge.

## Specs

S1 (pedido, literal): "Cuando esté escuchando yo musica en spotify, sofía bailara,
los ojos se moveran a ritmo y saldran notas musicales que se desvanecerán".

S2 (decisión del dueño, quién manda): "Baila si está al pedo (y la música la
despierta)" — contrato literal de la opción elegida: "idle + Spotify → baila /
thinking / tool / waiting / error → su gesto / dormida + Spotify → se despierta
bailando / pelotita encendida → la pelotita manda (no baila)".

S3 (decisión del dueño, el ritmo): "Tempo por tema + fase real del playback" —
contrato literal: "trackId A → 112 BPM, 4/4 faseada desde progressMs / pausa → se
congela el baile / nuevo tema → nuevo BPM, vuelve el beat". El BPM es estable por
`trackId` (84 a 136) y la fase sale de `progressMs` más el tiempo transcurrido
desde el último poll.

S4 (decisión del dueño, cómo se prueba): "Consola + panel" — "consola: dance 10 /
panel Sofía: [Bailando] ← chip nuevo".

S5 (reglas del vidrio que no se tocan): "No eyebrows. Ever." y "No mouth, no text,
no assets". Las notas son glifos procedurales, no imágenes; la cara sigue siendo
dos squircles con párpado negro y la única decoración permitida son las notas del
baile.

S6 (lo que ya anda no cambia): las reacciones nuevas son una capa que se suma a
la pose del estado y a las reacciones existentes (IMU, pelotita, calendario,
concentración, juego web, eventos de idle). Con el baile apagado, cada frame es
exactamente el que se dibujaba antes.

## Tasks

| ID | Qué | Specs | Ruta | Commit |
| --- | --- | --- | --- | --- |
| T1 | `buddy_dance.h` puro (BPM por tema, beat, pose, campo de notas) + `tools/buddy_dance_test.cpp` | S1, S3, S6 | inline | — |
| T2 | Render: notas dithered y pose de baile en `screen_buddy.cpp` | S1, S5 | inline | — |
| T3 | Disparador: Spotify sonando + gate de estado, despertar por música, pin | S2 | inline | — |
| T4 | Consola `dance`, `/api/face` con `dance` y chip "Bailando" del panel | S4 | inline | — |
| T5 | Doc del proyecto, compilación y evidencia en la placa | S1-S6 | inline | — |

## Log

L1 (pedido original, literal): "Vamos con nuevas emociones para SOFIA (buddy)

1.- Cuando esté escuchando yo musica en spotify, sofía bailara, los ojos se
moveran a ritmo y saldran notas musicales que se desvanecerán"

L2 (decisiones del dueño en el cuestionario previo, literales): "Baila si está al
pedo (y la música la despierta)"; "Tempo por tema + fase real del playback";
"Consola + panel".
