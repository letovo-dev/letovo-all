## инструкции к постам

### новости

для добавления новости: 

1. загружаем все медиа, которые нужно добавить в новость при помощи http://ip:8880/
2. POST ручка /api/post/add_page, формат боди:
```json
{
	"likes": 100,
	"dislikes": 100,
	"saved": 100,
	"title": "new header",
	"author": "scv-7",
	"text": "test text",
	"media": ["/images/avatars/example_2.png", "/images/avatars/example_1.png"]
}
```

для добавления wiki статьи:
1. загружаем все медиа, которые нужно добавить в статью  при помощи http://ip:8880/
2. добавляем md файл таким же образом
3. POST ручка  /api/post/add_page, формат боди:
```json
{
	"post_path": "posts/example_post_3.md",
	"category": "дефолт",
	"title": "some"
}
```

с категориями сейчас может произойти лажа, баг известен, в процессе фикса
