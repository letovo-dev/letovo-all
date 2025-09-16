import requests
import pytest

USERNAME = "scv-7"
PASSWORD = "7"
TOKEN = "5261aa7439b988c0f93d38f676e3bfd2a070ddd64bf174282f37cfa3348320e9"
URL = "https://127.0.0.1/api/"
ID = 1


@pytest.mark.order1
def test_login():
    url = f"{URL}/auth/login"
    data = {"login": USERNAME, "password": PASSWORD}
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200


def test_am_i_authed():
    response = requests.get(f"{URL}/auth/amiauthed", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert data["status"] == "t"


########################################


def test_am_i_admin():
    response = requests.get("http://127.0.0.1/api/auth/amiadmin", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data and data["status"] == "t"


########################################


def test_is_user_active():
    username = "scv-7"
    response = requests.get(f"{URL}/auth/isactive/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert data["status"] == "t"


########################################


def test_is_user():
    username = "scv-7"
    response = requests.get(f"{URL}/auth/isuser/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert data["status"] == "t"


########################################


def test_auth_isadmin():
    username = USERNAME
    response = requests.get(f"{URL}auth/isadmin/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert data["status"] == "t"


########################################


def test_user_info():
    username = "scv-7"
    response = requests.get(f"{URL}/user/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    assert len(data["result"]) > 0
    user_data = data["result"][0]
    assert user_data["userid"] == "1762"
    assert user_data["username"] == "scv-7"
    assert user_data["userrights"] == "admin"
    assert user_data["jointime"] == "2024-12-05 23:52:49.393567"
    assert user_data["avatar_pic"] == "images/avatars/example_1.png"
    assert user_data["active"] == "t"
    assert user_data["times_visited"] == "1"
    assert user_data["departmentid"] == "0"
    assert user_data["rolename"] == "пипа"
    assert user_data["registered"] == "t"


########################################


def test_full_user_info():
    username = "scv-7"
    response = requests.get(f"{URL}/user/full/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    user_info = data["result"][0]
    assert user_info["userid"] == "1762"
    assert user_info["username"] == username
    assert user_info["userrights"] == "admin"
    assert user_info["balance"] == "262"
    assert user_info["registered"] in ["t", "yes"]
    assert user_info["jointime"] == "2024-12-05 23:52:49.393567"
    assert user_info["avatar_pic"].endswith(".png")
    assert user_info["active"] in ["t", "yes"]
    assert user_info["times_visited"] == "1"
    assert user_info["role"] == "пипа"
    assert user_info["paycheck"] == "1000"
    assert user_info["departmentid"] == "0"
    assert user_info["departmentname"] == "nowhere"


########################################


def test_user_roles():
    username = "scv-7"
    response = requests.get(f"{URL}/user/roles/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    for role in data["result"]:
        assert "username" in role
        assert "roleid" in role
        assert "rolename" in role
        assert "rang" in role
        assert "departmentid" in role
        assert "payment" in role
        assert "departmentname" in role


########################################


def test_user_unactive_roles():
    username = "scv-7"
    response = requests.get(f"{URL}/user/unactive_roles/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    roles = data["result"]
    assert isinstance(roles, list)
    for role in roles:
        assert "roleid" in role
        assert "rolename" in role
        assert "rang" in role
        assert "departmentid" in role
        assert "payment" in role
        assert "departmentname" in role


########################################


def test_department_roles():
    department_id = "1"
    response = requests.get(f"{URL}/user/department/roles/{department_id}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    for role in data["result"]:
        assert isinstance(role, dict)
        assert "roleid" in role
        assert "rolename" in role
        assert "rang" in role
        assert "departmentid" in role
        assert "payment" in role


########################################


def test_department_name():
    department_id = "1"
    response = requests.get(f"{URL}/user/department/name/{department_id}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    for entry in data["result"]:
        assert "department" in entry


########################################


def test_all_departments():
    response = requests.get(f"{URL}/user/department/roles", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    departments = data["result"]
    assert isinstance(departments, list)
    for department in departments:
        assert "departmentid" in department
        assert "departmentname" in department


########################################


def test_all_avatars():
    response = requests.get(f"{URL}/user/all_avatars", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


########################################


def test_media_get():
    filename = "images/avatars/example_1.png"
    response = requests.get(f"{URL}/media/get/{filename}", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    assert "content-type" in response.headers
    content_type = response.headers["content-type"]
    assert content_type.startswith("image/")


########################################


def test_achivement_qr():
    achievement_id = "1"
    response = requests.get(f"{URL}/achivements/qr/{achievement_id}", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    for header in ["Server", "Date", "Content-Type", "Content-Length", "Connection"]:
        assert header in response.headers


########################################


def test_get_post_with_qr_code():
    post_id = "1"
    response = requests.get(f"{URL}/post/qr/{post_id}", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200, "Status code should be 200"
    for header in ["Server", "Date", "Content-Type", "Content-Length", "Connection"]:
        assert header in response.headers, f"Missing header: {header}"


########################################


def test_get_balance():
    response = requests.get(f"{URL}/transactions/balance", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200


########################################


def test_transactions_my():
    response = requests.get(f"{URL}/transactions/my", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    transactions = data["result"]
    assert isinstance(transactions, list)
    for transaction in transactions:
        assert "transactionid" in transaction
        assert "amount" in transaction
        assert "sender" in transaction
        assert "receiver" in transaction
        assert "transactiontime" in transaction


########################################


def test_get_authors_list():
    response = requests.get(f"{URL}/social/authors", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


########################################


def test_get_news():
    start = 0
    size = 10
    response = requests.get(
        f"{URL}/social/news/search?start={start}&size={size}", headers={"Bearer": TOKEN}, verify=False
    )
    assert response.status_code == 200


########################################


def test_get_comments():
    post_id = 123
    start = 0
    size = 10
    response = requests.get(
        f"{URL}/social/comments/search?post_id={post_id}&start={start}&size={size}",
        headers={"Bearer": TOKEN},
        verify=False,
    )
    assert response.status_code == 200
    data = response.json()
    assert "result" in data


########################################


def test_get_post_media():
    post_id = "1"
    response = requests.get(f"{URL}/social/media/pics/{post_id}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


########################################


def test_get_post():
    post_id = "1"
    response = requests.get(f"{URL}/social/new/{post_id}", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


########################################


def test_get_all_posts():
    response = requests.get(f"{URL}/social/posts", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    posts = data["result"]
    for post in posts:
        assert isinstance(post["post_id"], str)
        assert isinstance(post["is_secret"], str)
        assert isinstance(post["likes"], str)
        assert isinstance(post["title"], str)
        assert isinstance(post["author"], str)
        assert isinstance(post["text"], str)
        assert isinstance(post["dislikes"], str)
        assert isinstance(post["parent_id"], str) or post["parent_id"] == ""
        assert isinstance(post["date"], str)
        assert isinstance(post["saved_count"], str)
        assert isinstance(post["category_name"], str)
        assert isinstance(post["avatar_pic"], str)
        assert isinstance(post["is_liked"], str)
        assert isinstance(post["is_disliked"], str)
        assert isinstance(post["saved"], str)


########################################


def test_get_all_titles():
    response = requests.get(f"{URL}/social/titles", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    titles_list = data["result"]
    assert len(titles_list) > 0
    for title_info in titles_list:
        assert "post_id" in title_info
        assert "title" in title_info


########################################


def test_get_saved_posts():
    response = requests.get(f"{URL}/social/saved", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    for post in data["result"]:
        assert "post_id" in post
        assert "post_path" in post
        assert isinstance(post["is_secret"], str)
        assert isinstance(post["likes"], str)
        assert isinstance(post["title"], str)
        assert isinstance(post["author"], str)
        assert isinstance(post["text"], str)
        assert isinstance(post["dislikes"], str)
        assert "parent_id" in post
        assert isinstance(post["date"], str)
        assert isinstance(post["saved_count"], str)
        assert isinstance(post["category"], str)
        assert isinstance(post["category_name"], str)
        assert isinstance(post["avatar_pic"], str)
        assert isinstance(post["is_liked"], str)
        assert isinstance(post["is_disliked"], str)
        assert isinstance(post["saved"], str)


########################################


def test_get_post_categories():
    response = requests.get(f"{URL}/social/categories", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    for category in data["result"]:
        assert isinstance(category["category"], str)
        assert isinstance(category["category_name"], str)


########################################


def test_social_bycat():
    category_id = 0
    response = requests.get(f"{URL}/social/bycat/{category_id}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


########################################


def test_user_achivemets():
    username = "scv-7"
    response = requests.get(f"{URL}/achivements/user/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    for achievement in data["result"]:
        assert isinstance(achievement["id"], str)
        assert achievement["username"] == username
        assert isinstance(achievement["datetime"], str)
        assert isinstance(achievement["stage"], str)
        assert isinstance(achievement["achivement_pic"], str)
        assert isinstance(achievement["achivement_name"], str)
        assert isinstance(achievement["achivement_decsription"], str)
        assert isinstance(achievement["achivement_tree"], str)
        assert isinstance(achievement["level"], str)
        assert isinstance(achievement["stages"], str)
        assert isinstance(achievement["category"], str)


########################################


def test_full_user_achivemets():
    username = USERNAME
    response = requests.get(f"{URL}/achivements/user/full/{username}", verify=False)
    assert response.status_code == 200, "Response status code is not 200"
    data = response.json()
    assert "result" in data, "'result' field missing from the response"
    for item in data["result"]:
        assert isinstance(item.get("id"), str), "Achievement ID should be a string"
        assert isinstance(item.get("username"), str), "Username should be a string"
        assert isinstance(item.get("achivement_id"), str), "Achievement ID should be a string"
        assert isinstance(item.get("datetime"), str), "Date and time should be a string"
        assert isinstance(item.get("stage"), str), "Stage should be a string"
        assert isinstance(item.get("achivement_pic"), str), "Image link should be a string"
        assert isinstance(item.get("achivement_name"), str), "Achievement name should be a string"
        assert isinstance(item.get("achivement_decsription"), str), "Description should be a string"
        assert isinstance(item.get("achivement_tree"), str), "Achievement tree ID should be a string"
        assert isinstance(item.get("level"), str), "Level should be a string"
        assert isinstance(item.get("stages"), str), "Number of stages should be a string"
        assert isinstance(item.get("category"), str), "Category ID should be a string"
        assert isinstance(item.get("category_name"), str), "Category name should be a string"
        assert isinstance(item.get("departmentid"), str), "Department ID should be a string"


########################################


def test_achivements_tree():
    tree_id = "1"
    response = requests.get(f"{URL}/achivements/tree/{tree_id}", verify=False)
    assert response.status_code == 200, "Response status code is not 200"
    data = response.json()
    assert "result" in data, "'result' field missing in the response"
    result = data["result"]
    assert isinstance(result, list), "'result' should be a list"
    for achievement in result:
        assert "achivement_id" in achievement, "Achievement dictionary does not contain 'achivement_id'"
        assert "achivement_pic" in achievement, "Achievement dictionary does not contain 'achivement_pic'"
        assert "achivement_name" in achievement, "Achievement dictionary does not contain 'achivement_name'"
        assert "achivement_decsription" in achievement, (
            "Achievement dictionary does not contain 'achivement_decsription'"
        )
        assert "achivement_tree" in achievement, "Achievement dictionary does not contain 'achivement_tree'"
        assert "level" in achievement, "Achievement dictionary does not contain 'level'"
        assert "stages" in achievement, "Achievement dictionary does not contain 'stages'"
        assert "category" in achievement, "Achievement dictionary does not contain 'category'"
        assert "category_name" in achievement, "Achievement dictionary does not contain 'category_name'"
        assert "departmentid" in achievement, "Achievement dictionary does not contain 'departmentid'"


########################################


def test_achivement_info():
    achievement_id = 1
    response = requests.get(f"{URL}/achivements/info/{achievement_id}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    result = data["result"][0]
    assert result["achivement_id"] == str(achievement_id)
    assert result["achivement_pic"].startswith("images/achivements/")
    assert result["achivement_name"] == "ачивка номер 1"
    assert result["achivement_decsription"] == "чтобы получить ачивку 1 определенно надо что-то сделать"
    assert result["achivement_tree"] == "1"
    assert result["level"] == "0"
    assert result["stages"] == "1"
    assert result["category"] == "1"
    assert result["category_name"] == "дефолт"
    assert result["departmentid"] == "-1"


########################################


def test_achivement_pictures():
    response = requests.get(f"{URL}/achivements/pictures", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    for img_url in data["result"]:
        assert img_url.startswith("images/achivements/")


########################################


def test_achivements_no_dep():
    response = requests.get(f"{URL}/achivements/no_dep", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    for achievement in data["result"]:
        assert isinstance(achievement["id"], str)
        assert isinstance(achievement["username"], str)
        assert isinstance(achievement["achivement_id"], str)
        assert isinstance(achievement["datetime"], str)
        assert isinstance(achievement["stage"], str)
        assert isinstance(achievement["achivement_pic"], str)
        assert isinstance(achievement["achivement_name"], str)
        assert isinstance(achievement["achivement_decsription"], str)
        assert isinstance(achievement["achivement_tree"], str)
        assert isinstance(achievement["level"], str)
        assert isinstance(achievement["stages"], str)
        assert isinstance(achievement["category"], str)
        assert isinstance(achievement["category_name"], str)
        assert achievement["departmentid"] in ["", "-1"]


########################################


def test_achivements_by_user():
    username = USERNAME
    response = requests.get(f"{URL}/achivements/by_user", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    for achivement in data["result"]:
        assert "id" in achivement
        assert "username" in achivement
        assert "achivement_id" in achivement
        assert "datetime" in achivement
        assert "stage" in achivement
        assert "achivement_pic" in achivement
        assert "achivement_name" in achivement
        assert "achivement_decsription" in achivement
        assert "achivement_tree" in achivement
        assert "level" in achivement
        assert "stages" in achivement
        assert "category" in achivement
        assert "category_name" in achivement
        assert "departmentid" in achivement


########################################


def test_achivements_qr_code():
    achievement_id = "1"
    response = requests.get(f"{URL}/achivements/qr_code/{achievement_id}", verify=False)
    assert response.status_code == 200


########################################


def test_get_page_author():
    username = "scv-7"
    response = requests.get(f"{URL}/post/author/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    for author in data["result"]:
        assert "username" in author
        assert isinstance(author["username"], str)
        if author["username"] == username:
            assert "avatar_pic" in author
            assert isinstance(author["avatar_pic"], str)


########################################


def test_get_favourite_posts():
    response = requests.get("http://127.0.0.1/api/post/favourite/", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    headers = ["Server", "Date", "Content-Length", "Connection"]
    for header in headers:
        assert header in response.headers, f"Missing header: {header}"


########################################


def test_authors_list():
    response = requests.get(f"{URL}/authors_list", headers={"Bearer": TOKEN}, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    for author in data["result"]:
        assert "username" in author
        assert "avatar_pic" in author


########################################
