# lbe-1420

Utilidad de línea de comandos para Linux que configura el **Leo Bodnar LBE-1420 GPS Locked Clock Source**
por USB (`hidraw`): consulta de estado, frecuencia de salida, activar/desactivar la salida y parpadeo del LED.

- Original: [simontheu/lbe-1420](https://github.com/simontheu/lbe-1420)
- Modificaciones: Copyright (c) 2026 Manuel - EA7JRS

> No actualiza firmware. Para eso sigue siendo necesario el *Firmware Doctor* de Leo Bodnar (Windows).

## Compilar e instalar

Solo hace falta `gcc` (o `cc`) y `make`; no tiene dependencias externas.

```
make
sudo make install          # /usr/local/bin/lbe-142x
sudo make install-udev     # opcional: permite usarlo sin root
```

Tras instalar la regla udev, desconecta y vuelve a conectar el LBE-1420. Si tu distribución no usa el grupo
`plugdev`, la etiqueta `uaccess` da acceso al usuario con sesión local igualmente.

## Uso

El dispositivo se detecta automáticamente; ya no hace falta buscar el `/dev/hidrawN` a mano.

```
lbe-142x                        # muestra el estado
lbe-142x --list                 # lista los LBE-1420 conectados (ruta y nº de serie)
lbe-142x --f1 10M               # 10 MHz, guardado en flash
lbe-142x --f1_nosave 25k        # 25 kHz, sin guardar (se pierde al apagar)
lbe-142x --out1 0               # desactiva la salida 1
lbe-142x --blink1               # parpadea el LED de la salida 1 durante 3 s
lbe-142x --serial 0673ED0E4101 --f1 27.12M   # con varios equipos conectados
lbe-142x /dev/hidraw1 --f1 10000000          # sintaxis original, sigue funcionando
```

Frecuencia: de 1 Hz a 1,4 GHz (según el datasheet v1.1; el original limitaba a 1,1 GHz). Se puede escribir en Hz
(`10000000`) o con sufijo `k`, `M`, `G` y `Hz` opcional (`10M`, `2.048MHz`, `1.4G`). Si el firmware de tu equipo
es antiguo y no acepta frecuencias por encima de 1,1 GHz, la verificación tras el cambio lo detecta (código 4).

El datasheet indica una resolución de 1 µHz, pero el informe HID que usa esta utilidad lleva un entero en Hz,
así que desde aquí la resolución es de 1 Hz.

Después de cambiar la frecuencia, la utilidad vuelve a leerla del equipo para confirmar el cambio.

## Monitorización

`--check` devuelve el código 2 si no hay enganche GPS, hay fallo de antena o la salida está desactivada.
Sirve para cron, Nagios/Zabbix o un script de la estación:

```
lbe-142x --check > /dev/null || echo "LBE-1420 sin lock GPS"
```

| Código | Significado |
|---|---|
| 0 | OK |
| 1 | Error de uso o de E/S |
| 2 | Alarma (con `--check`) |
| 3 | No se encuentra el dispositivo o no hay permiso |
| 4 | La frecuencia leída tras el cambio no coincide |

## Cambios respecto al original

Correcciones:
- **Desbordamiento de buffer**: las lecturas `HIDIOCGFEATURE(256)` y `HIDIOCGRAWNAME(256)` escribían hasta 256 bytes en un buffer de 60.
- **`--f1` no guardaba en flash** si la frecuencia en marcha ya coincidía. Así, `--f1_nosave X` seguido de `--f1 X` nunca dejaba X guardada.
- Los informes enviados al equipo llevaban bytes basura de lecturas anteriores; ahora se ponen a cero antes de enviarlos.
- Con la salida 1 desactivada aparecía el mensaje "GPS antenna short circuit".
- Si fallaba la lectura de estado, la frecuencia actual se usaba sin inicializar.
- `getopt` estaba mal definido (`-a` sin argumento provocaba un fallo por puntero nulo) y las opciones desconocidas se ignoraban.
- El `Makefile` tenía un `clean` roto; se eliminan el binario compilado y el `hidapi.h`, que no se usaba.

Mejoras: rango hasta 1,4 GHz según el datasheet, byte de estado en crudo, detección automática del equipo, `--list`, `--serial`, validación de la frecuencia y de `--out1`,
sufijos k/M/G, verificación tras el cambio, códigos de salida, `--check`, regla udev y objetivos `install`.

## Notas del datasheet (v1.1) y pendientes

- **Modo de baja potencia** (+6 dBm frente a +11 dBm por debajo de 400 MHz): el datasheet lo menciona, pero su
  informe HID no está documentado y esta utilidad no lo controla todavía. Los bits `0x02` y `0x08` del byte de
  estado tampoco están documentados. Para identificarlos: activa y desactiva el modo con la utilidad oficial de
  Leo Bodnar y compara el valor de `Raw flags`.
- **NMEA por puerto serie virtual**: el equipo también aparece como `/dev/ttyACM*` con las tramas NMEA del
  receptor GNSS. Se puede usar con `gpsd` o leer directamente (`cat /dev/ttyACM0`).
- **Corriente de antena**: el datasheet se contradice (30 mA máximo en especificaciones, 50 mA en características).
  Lo prudente es no pasar de 30 mA con antenas activas.
