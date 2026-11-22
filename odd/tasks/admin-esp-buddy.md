# ESP BUDDY: el panel con el diseño de `design/admin-espejo`

Implementación del panel de administración tal como quedó aprobado en el canvas:
`design/admin-espejo/` (diez planchas, una por sección) sobre el panel real de
`esp32_gadget/web_ui/`. El diseño es la especificación visual; este documento
guarda los pedidos del dueño, literales, y el reparto del trabajo.

Alcance: sólo el panel (`admin.html`, `styles.css`, `app.js`) y el header de
assets que el firmware sirve. La landing y el login comparten `styles.css`: no se
tocan, y ninguna regla puede cambiar cómo se ven.

## Specs

S1 (marca): "Aqui le llamaremos ESP BUDDY al proyecto" y "Aca podemos simular en
negro los ojitos de sofia" — el logo del header son dos ojos negros (squircles,
38% del semieje) sobre el sticker amarillo, sin glow. Se va la línea
`esp32-gadget 1.0.0` ("Saquemos esto").

S2 (limpieza de chrome): "Saquemos eto" (el eyebrow de cada sección), "Esto no va
en ninguna de las pantallas" (el `<h2>` grande que repetía la pestaña), "Esto no
va en ninguna pantalla" (la segunda línea del footer).

S3 (navegación): "Este menu a la izquiera de todo, cambiando el layout, como un
navbar tipo aside a la izquierda... y a la derecha el contenido, eliminamos esta
sección" y "todos los botones deben tener el mismo ancho, adaptandose al que tiene
la palabra mas larga".

S4 (tema): "Implementemos toggle para tema dark" — botón redondo en el header.

S5 (aplicaciones): "acá el orden de las aplicaciones pueda reordenar arrastrando
los items con el mouse" y "Saquemos esta funcion" (el botón "Nombres y orden
originales").

S6 (pantalla): "Compactar por secciones, en un desplegable" + "Color de interfaz,
rotacion y brillo son secciones separadas verticalmente" + "Aca el checkbox va a
la derecha".

S7 (luz RGB): "Saquemos esto" (los atajos de paleta) y "Aqui un texto o algo para
que no quede tan aislado" (el color con etiqueta y su hex).

S8 (música): "Chau esto" ×3 (el lead y los dos párrafos de ayuda) y "Agreguemos la
portada tambien y aprovechemos mejor el espacio".

S9 (integraciones y red): "Dividir aca en pestañas tipo secciones, segun la
integracion", "Aca tambien en pestañas digamos, una para red remota y la otra el
punto de acceso", "Texto: Conectarse a una red", "Hagamos un input nativo CSS para
aprovechar el lenguaje de diseño".

S10 (Sofía): "Todo esto lo saquemos" (las cinco tarjetas de estado), "Aqui a la
derecha de todo de la pantalla poner si esta conectado al bridge o no", "Aqui
poner restablecer color", "Saquemos esto, y pongamos un selector con botones con
cada tipo de reaccion y estado para simularlo desde aqui".

S11 (lo que no puede romperse): el panel vive en la placa, sin internet: cero
URLs externas, cero fuentes, cero imágenes raster. La misma hoja sirve landing y
login. El estado sigue siendo del equipo: el panel no inventa valores.

## Tasks

| ID | Qué | Specs | Commit |
| --- | --- | --- | --- |
| T1 | Marca, tema oscuro, aside, sin eyebrows ni títulos repetidos | S1-S4, S11 | `feat(web): ESP BUDDY shell` |
| T2 | Aplicaciones: reordenar arrastrando | S5 | `feat(web): drag the screen order` |
| T3 | Pantalla en desplegables, interruptor a la derecha, campos de color | S6, S7 | `feat(web): compact screen settings` |
| T4 | Música con portada, Integraciones y Red en sub-pestañas | S8, S9 | `feat(web): tab the integrations` |
| T5 | Sofía: fuera las tarjetas, pill del bridge, simulador de estados | S10 | `feat(web): simulate Sofia's face` |

## Log

L1 (verbatim): "Ahi quedo! Implementa todo" — sobre el diseño aprobado en
`design/admin-espejo/`, que arranca de los 60+ comentarios del canvas listados en
las Specs.
