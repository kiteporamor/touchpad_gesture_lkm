# touchpad_gesture_lkm
Загружаемый модуль ядра Linux для регулирования громкости и яркости с помощью тачпада

##  Запуск

1. Собрать модуль:

```bash
make
```

2. Загрузить модуль в ядро:

```bash
make install
```

3. Проверить, что модуль загрузился:

```bash
lsmod | grep touchpad_gesture_lkm
```

4. Посмотреть логи:

```bash
make info
make debug
```

5. Выгрузить модуль:

```bash
make uninstall
```

## Требования

- Linux
- Установленные headers для текущего ядра
- `make` и `gcc`
- Права `sudo` для `insmod`/`rmmod`
