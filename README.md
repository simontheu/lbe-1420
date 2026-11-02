# lbe-1420

Utilidad de línea de comandos para Linux que configura el **Leo Bodnar LBE-1420 GPS Locked Clock Source**
por USB (`hidraw`): consulta de estado, frecuencia de salida, activar/desactivar la salida y parpadeo del LED.

- Original: [simontheu/lbe-1420](https://github.com/simontheu/lbe-1420)
- Modificaciones: Copyright (c) 2026 Manuel - EA7JRS

> No actualiza firmware: para eso está el *Firmware Doctor* de Leo Bodnar (Windows). Ver "Actualizar el firmware".

## Cómo funciona

1. **Busca el equipo**: recorre `/sys/class/hidraw` y se queda con los dispositivos USB 1DD2:2443
   (LBE-1420). Si hay uno, lo usa; si hay varios, pide elegir con `--device` o `--serial`.
2. **Lo abre y lo comprueba**: confirma por `ioctl` que es un LBE-1420 de verdad antes de enviarle nada.
3. **Lee la identificación**: nombre, número de serie y versión de firmware (campo `bcdDevice` del USB).
4. **Lee el estado** (informe HID 9): enganche GPS, antena, salida 1, frecuencia actual y byte de estado en crudo.
5. **Aplica los cambios pedidos**, en este orden: frecuencia (y la vuelve a leer para confirmarla), activar o
   desactivar la salida, y parpadeo del LED. Cada orden se envía en un informe de 60 bytes puesto a cero.
6. **Termina con un código de salida** que dice cómo fue (ver la tabla de códigos).

Sin opciones de cambio, solo lee: no modifica nada del equipo.

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
lbe-142x --list                 # lista los LBE-1420 conectados (ruta, nº de serie y firmware)
lbe-142x --f1 10M               # 10 MHz, guardado en flash
lbe-142x --f1_nosave 25k        # 25 kHz, sin guardar (se pierde al apagar)
lbe-142x --out1 0               # desactiva la salida 1
lbe-142x --blink1               # parpadea el LED de la salida 1 durante 3 s
lbe-142x --serial 0673ED0E4101 --f1 27.12M   # con varios equipos conectados
lbe-142x /dev/hidraw1 --f1 10000000          # sintaxis original, sigue funcionando
```

Frecuencia: de 1 Hz a 1,6 GHz, el límite que marca el configurador oficial v1.07 (el datasheet v1.1 dice 1,4 GHz
y el original limitaba a 1,1 GHz). Se puede escribir en Hz (`10000000`) o con sufijo `k`, `M`, `G` y `Hz` opcional
(`10M`, `2.048MHz`, `1.6G`). Si el firmware de tu equipo no admite el valor pedido, la verificación tras el cambio
lo detecta (código 4).

El datasheet indica una resolución de 1 µHz, pero el informe HID que usa esta utilidad lleva un entero en Hz,
así que desde aquí la resolución es de 1 Hz.

Después de cambiar la frecuencia, la utilidad vuelve a leerla del equipo para confirmar el cambio.

## Actualizar el firmware

1. Mira la versión actual: `lbe-142x --list` o `lbe-142x` (línea `Firmware:`).
2. En Windows, ejecuta el *Firmware Doctor* de Leo Bodnar. Junto al equipo indica "update to vX.XX" si hay
   versión nueva o "no online firmware" si no la hay. Pulsa *Update Firmware*.
3. Precauciones: mejor Windows nativo que máquina virtual, conexión directa sin hub, portátil enchufado, y no
   desconectar hasta que termine. Apunta antes la frecuencia configurada.
4. Al terminar, comprueba aquí la nueva versión con `lbe-142x`.

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

Mejoras: versión de firmware, rango hasta 1,6 GHz según el configurador oficial, modo `--dump` de solo lectura, byte de estado en crudo, detección automática del equipo, `--list`, `--serial`, validación de la frecuencia y de `--out1`,
sufijos k/M/G, verificación tras el cambio, códigos de salida, `--check`, regla udev y objetivos `install`.

## Volcado de diagnóstico (`--dump`)

```
lbe-142x --dump        # 3 s de captura
lbe-142x --dump=10     # 10 s
```

Solo lee; no envía nada al equipo. Muestra el descriptor HID (qué informes existen y su tamaño), el informe de
estado completo en hexadecimal y los informes de entrada que el equipo emite por su cuenta.

## Lo que hace el configurador oficial (v1.07) y esta utilidad aún no

Según las cadenas del programa *LBE-1420 Configuration v1.07* (Windows, VB6, 2025), el configurador ofrece:

| Función | Texto en el configurador |
|---|---|
| Baja potencia | "Low power output – Reduce OUT power to +1dBm" |
| Modo ADEV | "Enable better ADEV but lower absolute precision" |
| Constelaciones | GPS, GLONASS, Galileo, BeiDou, SBAS (activables por separado) |
| Modelo dinámico | Stationary, Portable, Airborne |
| Restaurar valores de fábrica | "Factory reset" |
| Satélites | Gráfica de C/N0 (dB) por satélite |
| Posición y hora | Posición GPS y ajuste del reloj del PC desde el GPS |
| Escritura de registros | "Register write" (función de depuración) |

Por los campos que envía su enlace de diagnóstico, el equipo informa además de: satélites, C/N0, TCXO, estado
del FLL, potencia, constelaciones, modelo dinámico, corriente de antena, posición, altitud y versión de firmware.

Estos informes no están documentados públicamente, así que no se envían a ciegas: un informe mal formado podría
cambiar la configuración del equipo. La forma segura de añadirlos es:

1. Ejecutar `lbe-142x --dump` y guardar la salida.
2. Cambiar **una sola** opción con el configurador oficial en Windows.
3. Repetir el volcado y comparar qué bytes cambian.

Para las órdenes de escritura (por ejemplo, baja potencia), lo fiable es capturar el tráfico USB del configurador
con Wireshark + USBPcap mientras se pulsa cada opción.

**Privacidad**: el botón *Diagnostics* del configurador abre una URL de leobodnar.com que incluye tu número de
serie, tu posición GPS (latitud, longitud y altitud) y tu versión de Windows.

## Otras notas del datasheet (v1.1)

- **Baja potencia**: el datasheet habla de +6 dBm (por debajo de 400 MHz) y el configurador v1.07, de +1 dBm.
  Seguramente cambió con el firmware.
- Los bits `0x02` y `0x08` del byte de estado no están documentados; se pueden identificar con `--dump`.
- **NMEA por puerto serie virtual**: el equipo también aparece como `/dev/ttyACM*` con las tramas NMEA del
  receptor GNSS. Se puede usar con `gpsd` o leer directamente (`cat /dev/ttyACM0`).
- **Corriente de antena**: el datasheet se contradice (30 mA máximo en especificaciones, 50 mA en características).
  Lo prudente es no pasar de 30 mA con antenas activas.
