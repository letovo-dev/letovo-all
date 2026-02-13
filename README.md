# Letovo Server

[![Build Status](https://github.com/letovo-dev/letovo-all/actions/workflows/docker-image.yml/badge.svg)](https://github.com/letovo-dev/letovo-all/actions/workflows/docker-image.yml)
[![codecov](https://codecov.io/gh/letovo-dev/letovo-all/branch/main/graph/badge.svg)](https://codecov.io/gh/letovo-dev/letovo-all)
[![Coverage](https://codecov.io/gh/letovo-dev/letovo-all/branch/main/graph/badge.svg?flag=unittests)](https://codecov.io/gh/letovo-dev/letovo-all)
[![Tests](https://img.shields.io/badge/tests-passing-brightgreen.svg)](https://github.com/letovo-dev/letovo-all/actions/workflows/docker-image.yml)

## Статус проекта

| Метрика | Значение |
|---------|----------|
| **Покрытие тестами** | ![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?label=coverage) |
| **Статус сборки** | ![Build](https://img.shields.io/github/actions/workflow/status/letovo-dev/letovo-all/docker-image.yml?branch=main&label=build) |
| **Статус тестов** | ![Tests](https://img.shields.io/github/actions/workflow/status/letovo-dev/letovo-all/docker-image.yml?branch=main&label=tests) |
| **Последний коммит** | ![Last Commit](https://img.shields.io/github/last-commit/letovo-dev/letovo-all) |

---

## Разработка

### Как добавить свои модули - С++

Когда вы написали новый файлик, добавьте путь к файлу .cc относительно папки src в BuildConfig.json. Ваш модуль автоматически добавится в сборку при использовании install-run.sh

### Как добавить своих ботов - Python

В данный момент - никак, но как только появится, сразу опишу, схема будет похожа на плюсовую.

## Немного про стиль

### Добавление функций

Дополнительный файл - дополнительный неймспейс. Функции для новых ручек сервера - дополнительный неймспейс \<namespace\>::server Если вы уверены, что знаете, что делаете - можно и не так, но я спрошу на pr-е. Для примера см auth.cc

### Добавление файлов 

У всех новых файлов обязательно должен быть заголовочник .h и исполняемый файл .cc! Исполняемый файл добавляем в BuildConfig.json, инклюдим заголовочник куда надо, радуемся. Можно смотреть примеры среди уже сделанных файликов, они все стилистически корректные

### Добавление новых путей к api

Для добавления нового пути к api открываем файлик server.cpp, добавляем вызов вашей функции в функцию create по аналогии с уже существующими там вызовами, тестим, плачем, фиксим ошибки, радуемся

За пул реквест с неоттестированной функцией - порка и штраф 50р

За пул реквест с не компилирующейся функцией - показательная порка и штраф 100р

### Конфиги

Все, что может быть вынесено в конфиг обязано быть вынесено в конфиг и использоваться через config.h. Без исключений.
