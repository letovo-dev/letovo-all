from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUTH_CC = ROOT / "src/basic/auth.cc"
USER_DATA_CC = ROOT / "src/basic/user_data.cc"
LIVE_E2E = ROOT / "test/e2e/live-platform-smoke.mjs"


def _handler(source: str, start: str, end: str) -> str:
    start_index = source.index(start)
    return source[start_index : source.index(end, start_index)]


def _assert_private_no_store(source: str) -> None:
    assert '.append_header("Cache-Control", "no-store, private")' in source
    assert '.append_header("Pragma", "no-cache")' in source


def test_auth_and_private_profile_responses_disable_browser_caching():
    auth = AUTH_CC.read_text()
    user_data = USER_DATA_CC.read_text()

    login = _handler(auth, "void enable_auth(", "void enable_reg(")
    auth_check = _handler(auth, "void am_i_authed(", "void am_i_admin(")
    logout = _handler(auth, "void logout(", "void enable_delete(")
    private_profile = _handler(user_data, "void full_user_info(", "void user_roles(")

    _assert_private_no_store(login)
    assert auth_check.count('"Cache-Control", "no-store, private"') == 2
    assert auth_check.count('"Pragma", "no-cache"') == 2
    _assert_private_no_store(logout)
    _assert_private_no_store(private_profile)


def test_live_browser_flow_switches_accounts_without_reusing_persisted_state():
    source = LIVE_E2E.read_text()
    flow = _handler(
        source,
        "async function checkAccountSwitchCacheIsolation(",
        "async function checkAuthenticatedBrowserFlow(",
    )

    assert "['userStore', 'chat-store', 'comments-store', 'articles-store']" in source
    workflow = (ROOT / ".github/workflows/docker-image.yml").read_text()
    assert 'LIVE_E2E_REQUIRE_ACCOUNT_SWITCH: "true"' in workflow
    assert "issue176AccountMarker" in flow
    assert "getByRole('button', { name: 'Выйти' })" in flow
    assert "await submitLogin(page, secondaryUsername, secondaryPassword)" in flow
    assert "await page.reload" in flow
    assert "await page.goBack" in flow
    assert "await page.goForward" in flow
    assert "assertNoPreviousAccountCache" in flow
