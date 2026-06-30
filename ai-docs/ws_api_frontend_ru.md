# WebSocket API — для фронта

Короткая справка: где эндпоинт, как с ним работать. Без внутренностей шины.

## Endpoint

```
wss://<host>/ws
```

GET с TLS. Авторизация — токен, такой же как у HTTP-ручек:

- **Bearer-заголовок** — для нативных клиентов / тестов.
- **`?token=<...>` в query-string** — для тестов и небраузерных клиентов.
- **AuthSession cookie** — основной вариант для браузера: `new WebSocket("wss://<host>/ws")`, без чтения токена из JS.

Если локальная разработка разносит frontend/backend по разным site и cookie `Secure; SameSite=Strict` не отправляется, это ограничение окружения. Не сохраняй session token в `localStorage` ради WebSocket.

Ответ при отсутствии/невалидном токене — `401`. При выключенном WS (`WsConfig.enabled=false`) — `503`.

## Что приходит сразу после connect

Все события идут в одинаковом конверте:

```json
{
  "type":  "<event-name>",
  "topic": "<topic>",
  "ts":    "2026-04-29T10:30:00Z",
  "data":  { ... }
}
```

Сразу после успешного connect:

1. Соединение **автоматически** подписано на `inbox:<me>` — все события, адресованные тебе лично, прилетают сюда без действий с твоей стороны.
2. Сервер отправляет `{"type":"welcome", "data":{"username":"<me>"}}`.

## Команды клиента

Текстовые JSON-фреймы:

```json
{"op": "subscribe",   "topic": "chat:alice:bob"}
{"op": "unsubscribe", "topic": "chat:alice:bob"}
```

Сервер отвечает:
- `{"type":"subscribed", "topic":"..."}` — подписка успешна.
- `{"type":"error", "data":{"code":"...","topic":"...","reason":"..."}}` — отказ.

## Темы, на которые можно подписаться (v1)

| Тема | Кто может подписаться | Назначение |
|---|---|---|
| `inbox:<me>` | владелец (авто-подписка) | всё, что адресовано тебе лично |
| `chat:<a>:<b>` (a < b лексикографически) | если ты ∈ {a,b} и `can_chat(me, peer)` истинно | прямые сообщения и удаления в открытом диалоге |

Пары `chat:<a>:<b>` приходится формировать на стороне клиента: отсортируй имена лексикографически и склей.

## События v1

### `chat.message.new`

Приходит в `inbox:<receiver>` (получатель узнаёт о новом DM, даже если чат не открыт) и в `chat:<a>:<b>` (если есть подписчики этого диалога).

```json
{
  "type": "chat.message.new",
  "topic": "inbox:bob",
  "ts": "...",
  "data": {
    "message_id": 42,
    "sender": "alice",
    "receiver": "bob",
    "text": "...",
    "attachments": ["...url..."]
  }
}
```

### `chat.message.deleted`

Приходит только в `chat:<a>:<b>`. В `inbox:` об удалении не уведомляем.

```json
{
  "type": "chat.message.deleted",
  "topic": "chat:alice:bob",
  "ts": "...",
  "data": {
    "message_id": 42,
    "deleted_by": "alice"
  }
}
```

### `transaction.balance.updated`

Приходит в `inbox:<username>` после успешного `POST /transactions/send`. Это только уведомление: переводы по-прежнему создаются, подтверждаются и авторизуются через HTTP.

Для отправителя:

```json
{
  "type": "transaction.balance.updated",
  "topic": "inbox:alice",
  "ts": "...",
  "data": {
    "balance": 990,
    "delta": -10,
    "counterparty": "bob",
    "direction": "outgoing",
    "transaction_id": 12345
  }
}
```

Для получателя:

```json
{
  "type": "transaction.balance.updated",
  "topic": "inbox:bob",
  "ts": "...",
  "data": {
    "balance": 1010,
    "delta": 10,
    "counterparty": "alice",
    "direction": "incoming",
    "transaction_id": 12345
  }
}
```

Для перевода самому себе сервер отправляет одно событие:

```json
{
  "type": "transaction.balance.updated",
  "topic": "inbox:alice",
  "ts": "...",
  "data": {
    "balance": 1000,
    "delta": 0,
    "counterparty": "alice",
    "direction": "self",
    "transaction_id": 12346
  }
}
```

Обновляй отображаемый баланс из `data.balance`, а не локальной арифметикой по `delta`: серверное значение является источником истины.

## Heartbeat и реконнект

- Сервер шлёт `ping` каждые 30 секунд. Браузерный WebSocket автоматически отвечает `pong` — ничего делать не надо.
- Если за 60 секунд от клиента не пришёл pong, сервер закрывает соединение.
- При разрыве (любая причина) — переоткрой `/ws` и заново подпишись на текущий открытый диалог.
- **WS — не replay-источник.** После reconnect обнови состояние через HTTP: `GET /chats/`, `GET /chat/<peer>` и профильные данные/баланс. Между разрывом и reconnect могли быть события, которые ты пропустил, — они есть в БД, и обычные ручки их вернут.

## Ошибки

`{"type":"error", "data":{"code","topic","reason"}}`. Возможные `code`:

| `code` | Когда |
|---|---|
| `forbidden` | Нет права на эту тему (например, `inbox:<other>` или `chat:` где ты не участник, или `block`-override на пару) |
| `unknown_topic` | На префикс не зарегистрировано правил (например, `weather:moscow`) |
| `unknown_op` | `op` не равен `subscribe`/`unsubscribe` |
| `binary_unsupported` | Прислал бинарный фрейм; в v1 не поддерживается |
| `malformed` | Не JSON или нет нужных полей |

## Что НЕ покрывает WS в v1

- Уведомления о новых ачивках, новых постах/комментах — пока нет, добавим позже.
- Replay пропущенных событий по `last_event_id` — нет; используй HTTP catch-up.
- Wildcard-подписки (`chat:*`) — нет.
- Бинарные фреймы — нет.
- Presence (онлайн/оффлайн других юзеров) — нет.
