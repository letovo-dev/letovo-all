# Изменения API для фронтенда

Кратко: что поменялось в ответах «ручек», на что обратить внимание при интеграции.

---

## 1. `GET /achivements/user/departments/:username`

**Назначение:** достижения пользователя по отделам, сгруппированные по году, «главе» календаря, отделу и роли.

### Было (старый контракт, для сравнения)

- Корень: `username`, `achivements[]`.
- Элемент массива: `department_id`, `department`, `achivements[]` с полным набором полей включая `username`, `achivement_id`, `achivement_tree`, `level` и т.д.

### Стало (текущий контракт)

Корень:

| Поле | Тип | Описание |
|------|-----|----------|
| `username` | string | Логин |
| `achivements` | array | Группы |

Элемент `achivements[]`:

| Поле | Тип | Описание |
|------|-----|----------|
| `year` | number | Календарный год из `user_achivements.datetime` (если даты нет — `0`) |
| `chapter` | string | Глава из таблицы `calendar` по дате достижения; иначе `""` |
| `department_id` | number | id отдела |
| `department` | string | Название отдела |
| `role` | string | Роль пользователя в этом отделе (по `useroles`/`roles`, макс. `rang`); может быть `""` |
| `post` | string[] | Уникальные `post_name` из `user_posts` + `post` за тот же **год** и ту же **chapter** (календарь по дате поста) |
| `brigade` | string[] | Уникальные имена бригад (`brigade.name`) из `user_brigades` за тот же **год** и **chapter** |
| `achivements` | array | Объекты достижений пользователя в этой группе |

Элемент `achivements[]` (внутренний массив):

| Поле | Тип |
|------|-----|
| `id` | number | id строки в `user_achivements` |
| `new` | boolean | Флаг «новое» с бэка |
| `datetime` | string (ISO-подобный) |
| `stage` | number |
| `achivement_pic` | string |
| `achivement_name` | string |
| `achivement_decsription` | string | Опечатка в API сохранена как в БД |
| `stages` | number \| null |
| `category` | number \| null |
| `category_name` | string \| null |
| `departmentid` | number |

**Убрано** по сравнению со старым вложенным объектом: `username`, `achivement_id`, `achivement_tree`, `level` (для отображения используйте `stage` и прочие поля выше).

**Пример запроса**

```http
GET /achivements/user/departments/GalaxyPulse
Accept: application/json
```

---

## 2. Пользовательские выборки — поле `user_brigades`

В ответах, где уже отдавались `brigade` / `brigadename`, добавлено поле **`user_brigades`**.

Затронутые HTTP-ручки (те же пути, что и раньше):

- `GET /user/:username` — `user_info`;
- `GET /user/full/:username` — `full_user_info`;
- `GET /user/roles/:username`;
- `GET /user/unactive_roles/:username`;
- `best_user_roles` — если есть отдельная ручка в вашей сборке.

**Смысл поля:** история привязок к бригадам — JSON-массив объектов вида:

`{ "id", "datetime", "brigade_id", "name" }` (сортировка по `datetime` по убыванию на стороне БД).

**Важно для парсинга:** общий ответ по-прежнему строится через `cp::serialize`: значения кладутся в строки. Поле `user_brigades` может прийти **как строка, внутри которой лежит JSON** (двойное кодирование). Имеет смысл попробовать `JSON.parse` для этого поля, если тип — строка.

---

## 3. `POST /auth/login`

Тело запроса без изменений: JSON с `login` и `password`.

**Ответ:** прежняя обёртка `{"result":[{ ... пользователь ... }]}` **плюс на том же уровне** (рядом с `result`, не внутри первого элемента массива):

| Поле | Тип | Описание |
|------|-----|----------|
| `last_incoming_payment` | object \| null | Последняя запись в `transactions`, где пользователь — **получатель** (`receiver`), по убыванию `transactionid` |
| `last_outgoing_payment` | object \| null | Последняя запись, где пользователь — **отправитель** (`sender`) |

Объект платежа — строка таблицы `transactions` в JSON (как минимум `transactionid`, `sender`, `receiver`, `amount`; остальные колонки — если есть в БД).

Если подходящих строк нет — **`null`**.

**Замечание:** из-за особенностей сериализации пользователя в `result` весь ответ иногда **формально невалиден как JSON** (кавычки внутри полей). Платёжные поля добавлены конкатенацией без перепарсивания всего тела, чтобы они гарантированно появлялись в ответе. Для надёжного парса имеет смысл либо договориться о фиксе сериализации на бэке, либо вытаскивать `last_*` регуляркой/по подстроке.

---

## 4. Связанные эндпоинты (без смены пути)

- **`POST /auth/reg`** — по-прежнему отдаёт `user_info` (в т.ч. с `user_brigades`), контракт сериализации тот же.
- **`PUT /auth/change_username`**, **`PUT /user/set_avatar`** — ответ с `full_user_info` + `user_brigades`.
- **`GET /achivements/by_user`** (с заголовком `Bearer`) — внутри используется `full_user_info`; состав полей пользователя как у `/user/full/...`.

---

## 5. Чеклист для фронта

1. Экран достижений по отделам: перейти на новую структуру `year` / `chapter` / `post` / `brigade` / вложенные `achivements` и поле `new`.
2. Профиль / роли: при необходимости парсить `user_brigades` как JSON из строки.
3. Логин: отобразить или использовать `last_incoming_payment` / `last_outgoing_payment`; учесть возможные проблемы с парсингом всего тела ответа.

Если нужен один общий OpenAPI-фрагмент или примеры реальных JSON из staging — можно добавить отдельно.
