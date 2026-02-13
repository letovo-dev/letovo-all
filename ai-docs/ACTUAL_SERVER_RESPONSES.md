# Actual Server Response Documentation

Based on testing with user "test" and valid authentication:

## Social Endpoints
- `GET /social/authors` -> **200** (no auth required)
- `GET /social/news` -> **401** (auth header not processed correctly)
- `GET /social/posts` -> **401** (auth header not processed correctly)
- `GET /social/new/:id` -> **401** (auth header not processed correctly)
- `GET /social/titles` -> **401** (auth header not processed correctly)
- `GET /social/search` -> **401** (auth header not processed correctly)
- `GET /social/comments` -> **401** (auth header not processed correctly)

## User Endpoints  
- `GET /user/roles/test` -> **502** (user exists but has no roles)
- `GET /user/unactive_roles/test` -> **502** (user has no inactive roles)
- `GET /user/department/roles/1` -> **200** (department exists)
- `GET /user/department/roles` -> **200** (returns all departments)

## Achievement Endpoints
- `GET /achivements/user/test` -> **502** (user has no achievements)
- `GET /achivements/user/full/test` -> **200** (returns all achievements)
- `GET /achivements/tree/1` -> **502** (tree is empty or doesn't exist)
- `GET /achivements/info/1` -> **200** (achievement exists)

## Key Finding
Social endpoints return 401 even with Bearer token - authentication header might not be processed correctly by the server!
