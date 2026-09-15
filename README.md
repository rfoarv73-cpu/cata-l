# cata! — Showcase Bot

Макро/реплей-бот для Geometry Dash (Geode-мод) с ImGui-интерфейсом.

## Что умеет

- Запись и воспроизведение инпутов (Record / Stop / Play)
- **Практика с чекпоинтами**: чекпоинт = коммит макроса, смерть = откат к последнему чекпоинту
- Блокировка живого ввода во время плейбека
- Сохранение / загрузка макроса
- ImGui-меню: сайдбар Main / Assist / Render / Visuals, панели Replay и Settings (Delta / Tweaks)
- Плавающий шарик-лаунчер (тап — открыть меню, перетаскивается)
- Поддержка мобильных устройств (увеличенный UI, вход через шарик или кнопку на паузе)

## Как открыть меню

- ПК: клавиша **K**, шарик, или кнопка `cata!` на паузе
- Мобилка: шарик или кнопка `cata!` на паузе

## Сборка через GitHub Actions (без установки SDK)

1. Залить содержимое репозитория на GitHub
2. **Actions → Build Geode Mod → Run workflow** (или просто запушить)
3. Готовый `.geode` — в артефактах рана, скачать и положить в `geode/mods/`

## Локальная сборка

Нужен [Geode SDK](https://docs.geode-sdk.org) и переменная `GEODE_SDK`.

```
cmake -B build
cmake --build build --config Release
```

## Структура

```
.github/workflows/build.yml   CI: Windows / macOS / Android32 / Android64
src/main.cpp                  всё: макро-ядро + хуки + интерфейс
mod.json                      метаданные мода и ресурсы
CMakeLists.txt                сборка + зависимость imgui-cocos
resources/*.png               иконки вкладок
```

## Заметки / TODO

- Счётчик кадров сейчас на `PlayLayer::update` — не физ-точный на высоком FPS.
  Для кадро-точности нужен TPS-lock (панель Delta пока только UI).
- Frame Advance, Intentional death, Speedhack, SSB Fix — UI готов, бэкенда нет.
- Перед стабильными сборками запинить коммит `gd-imgui-cocos` в `CMakeLists.txt`
  вместо `#main`.
