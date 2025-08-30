# iOS-style Calculator for PSP

Простое калькулятор-приложение в текстовом UI (iOS-like).

**Управление**: D-Pad — перемещать выделение, **X** — нажать кнопку, **O** — AC (сброс), **[]** — Backspace, **△** — равно, **L/R** — смена знака, **SELECT** — CE (очистить ввод), **START** — выход.

## Сборка в GitHub Actions
Используй workflow из репозитория (контейнер `ghcr.io/pspdev/pspdev:latest`).

## Сборка локально через Docker
```bash
docker run --rm -v "$PWD":/work -w /work ghcr.io/pspdev/pspdev:latest bash -lc "make clean && make && mkdir -p /work/out && cp EBOOT.PBP /work/out/EBOOT.PBP"
```

## Запуск
- PPSSPP: открой `out/EBOOT.PBP`
- PSP (CFW): скопируй `EBOOT.PBP` в `ms0:/PSP/GAME/CalcIOS/`
