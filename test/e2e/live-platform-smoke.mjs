import { createRequire } from 'node:module';
import { setTimeout as delay } from 'node:timers/promises';

const packageDir = process.env.PLAYWRIGHT_PACKAGE_DIR || process.cwd();
const require = createRequire(`${packageDir.replace(/\/+$/, '')}/package.json`);
const { chromium } = require('playwright');

const baseUrl = (process.env.LIVE_E2E_BASE_URL || 'https://ya.sergeiscv.ru').replace(/\/+$/, '');
const waitTimeoutMs = Number(process.env.LIVE_E2E_WAIT_TIMEOUT_SECONDS || 600) * 1000;
const pollIntervalMs = 10_000;
const requireAuth = process.env.LIVE_E2E_REQUIRE_AUTH === 'true';
const requireExtended = process.env.LIVE_E2E_REQUIRE_EXTENDED === 'true';
const requireOtel = process.env.LIVE_E2E_REQUIRE_OTEL === 'true';
const expectedBackendSha = process.env.LIVE_E2E_EXPECTED_BACKEND_SHA || '';
const expectedFrontendSha = process.env.LIVE_E2E_EXPECTED_FRONTEND_SHA || '';
const username = process.env.LETOVO_E2E_USERNAME || '';
const password = process.env.LETOVO_E2E_PASSWORD || '';
const secondaryUsername = process.env.LETOVO_E2E_SECONDARY_USERNAME || '';
const secondaryPassword = process.env.LETOVO_E2E_SECONDARY_PASSWORD || '';

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

async function fetchText(path) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 10_000);
  try {
    const response = await fetch(`${baseUrl}${path}`, {
      redirect: 'manual',
      signal: controller.signal,
    });
    return {
      status: response.status,
      text: await response.text(),
    };
  } finally {
    clearTimeout(timer);
  }
}

async function postOtelSmokeSpan() {
  const now = BigInt(Date.now()) * 1_000_000n;
  const response = await fetch(`${baseUrl}/otel/v1/traces`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      resourceSpans: [
        {
          resource: {
            attributes: [
              { key: 'service.name', value: { stringValue: 'letovo-live-e2e' } },
              { key: 'service.namespace', value: { stringValue: 'letovocorp' } },
              { key: 'deployment.environment', value: { stringValue: 'production' } },
            ],
          },
          scopeSpans: [
            {
              scope: { name: 'letovo.live-e2e' },
              spans: [
                {
                  traceId: '11111111111111111111111111111111',
                  spanId: '2222222222222222',
                  name: 'live-e2e-otel-smoke',
                  kind: 'SPAN_KIND_INTERNAL',
                  startTimeUnixNano: now.toString(),
                  endTimeUnixNano: (now + 1_000_000n).toString(),
                },
              ],
            },
          ],
        },
      ],
    }),
  });

  assert(
    response.status >= 200 && response.status < 300,
    `Expected /otel/v1/traces to accept OTLP JSON, got HTTP ${response.status}: ${await response.text()}`,
  );
}

async function assertDeploymentMetadata(path, expectedSha, description) {
  if (!expectedSha) {
    return;
  }

  const response = await fetchText(path);
  assert(response.status === 200, `${description} metadata returned HTTP ${response.status}`);
  const metadata = parseJsonResponse(response, `${description} metadata`);
  assert(
    metadata.sha === expectedSha,
    `${description} metadata sha ${metadata.sha || '<empty>'} does not match ${expectedSha}`,
  );
}

function parseJsonResponse(response, description) {
  try {
    return JSON.parse(response.text);
  } catch (error) {
    throw new Error(`${description} returned invalid JSON: ${response.text}`);
  }
}

async function fetchAuthenticated(page, path, options = {}) {
  return page.evaluate(
    async ({ path: requestPath, options: requestOptions }) => {
      const response = await fetch(requestPath, {
        credentials: 'include',
        ...requestOptions,
        headers: {
          ...(requestOptions.headers || {}),
        },
      });
      const text = await response.text();
      return { status: response.status, text };
    },
    { path, options },
  );
}

async function fillLoginForm(page, login, loginPassword) {
  const loginInput = page.locator('#form_login');
  const passwordInput = page.locator('#form_password');
  const button = page.getByRole('button', { name: 'Войти' });

  for (let attempt = 0; attempt < 20; attempt += 1) {
    await loginInput.fill(login);
    await passwordInput.fill(loginPassword);
    if (await button.isEnabled()) return button;
    await page.waitForTimeout(250);
  }

  throw new Error('Login form did not become enabled after hydration');
}

async function loginAs(page, login, loginPassword) {
  await page.context().clearCookies();
  await page.goto(baseUrl, { waitUntil: 'domcontentloaded' });
  await page.evaluate(() => {
    window.localStorage.clear();
    window.sessionStorage.clear();
  });
  await page.context().clearCookies();
  await page.goto(`${baseUrl}/login`, { waitUntil: 'domcontentloaded' });
  await page.locator('#form_login').waitFor({ state: 'visible' });
  await page.locator('#form_password').waitFor({ state: 'visible' });
  const loginButton = await fillLoginForm(page, login, loginPassword);

  const [loginResponse] = await Promise.all([
    page.waitForResponse((response) => {
      const url = new URL(response.url());
      return url.pathname.endsWith('/auth/login');
    }, { timeout: 60_000 }),
    loginButton.click(),
  ]);
  assert(
    loginResponse.status() === 200,
    `Login API ${loginResponse.url()} returned HTTP ${loginResponse.status()}`,
  );

  await page.waitForURL(/\/(user|registration)(\/|$)/, { timeout: 20_000 });
}

async function apiLoginAs(context, login, loginPassword) {
  const loginResponse = await context.request.post(`${baseUrl}/letovo-api/auth/login`, {
    data: { login, password: loginPassword },
    headers: { 'Content-Type': 'application/json' },
  });
  assert(
    loginResponse.status() === 200,
    `API login returned HTTP ${loginResponse.status()}: ${await loginResponse.text()}`,
  );
}

async function assertAuthenticatedSession(page) {
  const session = await fetchAuthenticated(page, '/letovo-api/auth/amiauthed/');
  assert(session.status === 200, `Session API returned HTTP ${session.status}`);
  const sessionJson = parseJsonResponse(session, 'Session API');
  assert(
    sessionJson.status === 't',
    `Session API did not confirm authenticated state: ${session.text}`,
  );
}

async function assertAdminSession(page) {
  const admin = await fetchAuthenticated(page, '/letovo-api/auth/amiadmin/');
  assert(admin.status === 200, `Admin API returned HTTP ${admin.status}`);
  const adminJson = parseJsonResponse(admin, 'Admin API');
  assert(adminJson.status === 't', `Configured e2e account is not admin: ${admin.text}`);
}

async function assertUploaderSession(page) {
  const uploader = await fetchAuthenticated(page, '/letovo-api/auth/amiuploader/');
  assert(uploader.status === 200, `Uploader API returned HTTP ${uploader.status}`);
  const uploaderJson = parseJsonResponse(uploader, 'Uploader API');
  assert(uploaderJson.status === 't', `Configured e2e account cannot upload: ${uploader.text}`);
}

async function getAuthSessionCookieValue(page) {
  const cookies = await page.context().cookies(baseUrl);
  const authCookie = cookies.find(cookie => cookie.name === 'AuthSession');
  assert(authCookie?.value, 'Authenticated browser context is missing AuthSession cookie');
  return authCookie.value;
}

async function checkMoneyTransfer(page, receiver) {
  const prepare = await fetchAuthenticated(page, '/letovo-api/transactions/prepare', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ receiver, amount: 0 }),
  });
  assert(
    prepare.status === 200,
    `Transfer prepare returned HTTP ${prepare.status}: ${prepare.text}`,
  );
  assert(prepare.text.length > 0, 'Transfer prepare returned an empty transaction id');

  const send = await fetchAuthenticated(page, '/letovo-api/transactions/send', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ tr_id: prepare.text }),
  });
  assert(send.status === 200, `Transfer send returned HTTP ${send.status}: ${send.text}`);
  assert(send.text === 'ok', `Transfer send returned unexpected body: ${send.text}`);
}

async function uploadSmokeFile(page) {
  const authSession = await getAuthSessionCookieValue(page);
  const upload = await page.evaluate(
    async ({ authSession: bearer }) => {
      const formData = new FormData();
      formData.append(
        'file',
        new Blob(['letovo live e2e upload smoke\n'], { type: 'text/plain' }),
        `live-e2e-${Date.now()}.txt`,
      );
      const response = await fetch('/upload/', {
        method: 'POST',
        credentials: 'include',
        headers: { Bearer: bearer },
        body: formData,
      });
      const text = await response.text();
      return { status: response.status, text };
    },
    { authSession },
  );

  assert(upload.status === 200, `Upload API returned HTTP ${upload.status}: ${upload.text}`);
  const uploadJson = parseJsonResponse(upload, 'Upload API');
  assert(
    typeof uploadJson.file === 'string' && uploadJson.file.length > 0,
    `Upload API returned no file path: ${upload.text}`,
  );
  return uploadJson.file;
}

async function createSmokePost(page, mediaPath) {
  const marker = `live-e2e-${Date.now()}`;
  const create = await fetchAuthenticated(page, '/letovo-api/post/add_page', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      title: `Live e2e smoke ${marker}`,
      text: `Automated live e2e post creation smoke ${marker}`,
      media: [mediaPath],
    }),
  });

  assert(create.status === 200, `Post create returned HTTP ${create.status}: ${create.text}`);
  const createJson = parseJsonResponse(create, 'Post create API');
  assert(
    Array.isArray(createJson.result),
    `Post create response did not include result array: ${create.text}`,
  );
  assert(
    createJson.result.length > 0,
    `Post create response returned an empty result: ${create.text}`,
  );
  const postId = Number(createJson.result[0].post_id);
  assert(Number.isInteger(postId), `Post create response returned no post_id: ${create.text}`);

  const remove = await fetchAuthenticated(page, '/letovo-api/post/delete', {
    method: 'DELETE',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ post_id: postId }),
  });
  assert(remove.status === 200, `Post cleanup returned HTTP ${remove.status}: ${remove.text}`);
  assert(remove.text === 'ok', `Post cleanup returned unexpected body: ${remove.text}`);
}

async function editSmokeArticle(page) {
  const marker = `live-e2e-article-${Date.now()}`;
  const categoryName = 'Медиа';
  let postId = undefined;

  try {
    const create = await fetchAuthenticated(page, '/letovo-api/post/add_page', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        post_path: `/media/${marker}.md`,
        category_name: categoryName,
        title: `Live e2e article ${marker}`,
        is_secret: 'f',
      }),
    });

    assert(create.status === 200, `Article create returned HTTP ${create.status}: ${create.text}`);
    const createJson = parseJsonResponse(create, 'Article create API');
    assert(
      Array.isArray(createJson.result) && createJson.result.length > 0,
      `Article create response did not include a result row: ${create.text}`,
    );
    postId = Number(createJson.result[0].post_id);
    assert(Number.isInteger(postId), `Article create response returned no post_id: ${create.text}`);

    const updatedTitle = `Live e2e article updated ${marker}`;
    const updatedPath = `/media/${marker}-updated.md`;
    const update = await fetchAuthenticated(page, '/letovo-api/post/update', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        post_id: String(postId),
        is_secret: 'f',
        likes: '0',
        dislikes: '0',
        saved_count: '0',
        title: updatedTitle,
        author: null,
        parent_id: null,
        text: '',
        category_name: categoryName,
        post_path: updatedPath,
      }),
    });

    assert(update.status === 200, `Article update returned HTTP ${update.status}: ${update.text}`);
    const updateJson = parseJsonResponse(update, 'Article update API');
    assert(
      Array.isArray(updateJson.result) && updateJson.result.length > 0,
      `Article update response did not include a result row: ${update.text}`,
    );
    assert(
      updateJson.result[0].title === updatedTitle,
      `Article update returned unexpected title: ${update.text}`,
    );
    assert(
      updateJson.result[0].post_path === updatedPath,
      `Article update returned unexpected post_path: ${update.text}`,
    );
    assert(
      updateJson.result[0].category_name === categoryName,
      `Article update returned unexpected category_name: ${update.text}`,
    );

    const read = await fetchText(`/letovo-api/post/${postId}`);
    assert(read.status === 200, `Article read returned HTTP ${read.status}: ${read.text}`);
    const readJson = parseJsonResponse(read, 'Article read API');
    assert(
      Array.isArray(readJson.result) && readJson.result.length > 0,
      `Article read response did not include a result row: ${read.text}`,
    );
    assert(
      readJson.result[0].title === updatedTitle,
      `Article read returned unexpected title: ${read.text}`,
    );
  } finally {
    if (postId !== undefined) {
      const remove = await fetchAuthenticated(page, '/letovo-api/post/delete', {
        method: 'DELETE',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ post_id: postId }),
      });
      assert(remove.status === 200, `Article cleanup returned HTTP ${remove.status}: ${remove.text}`);
      assert(remove.text === 'ok', `Article cleanup returned unexpected body: ${remove.text}`);
    }
  }
}

async function probeDeployment() {
  const root = await fetchText('/');
  assert(root.status === 200, `Expected / to return 200, got ${root.status}`);
  assert(root.text.includes('<title>Letovo</title>'), 'Expected / to serve the Letovo frontend');

  const login = await fetchText('/login');
  assert(login.status === 200, `Expected /login to return 200, got ${login.status}`);
  assert(login.text.includes('id="form_login"'), 'Expected /login to render the login input');
  assert(login.text.includes('id="form_password"'), 'Expected /login to render the password input');

  const api = await fetchText('/letovo-api/auth/login');
  assert(
    api.status === 501,
    `Expected /letovo-api/auth/login GET to return 501, got ${api.status}`,
  );

  if (requireOtel) {
    await postOtelSmokeSpan();
  } else {
    console.log('Skipping OTEL trace smoke because LIVE_E2E_REQUIRE_OTEL is not true.');
  }

  await assertDeploymentMetadata(
    '/letovo-api/deployment/metadata',
    expectedBackendSha,
    'Backend deployment',
  );
  await assertDeploymentMetadata(
    '/api/deployment/metadata',
    expectedFrontendSha,
    'Frontend deployment',
  );

  return true;
}

async function waitForLiveDeployment() {
  const startedAt = Date.now();
  let lastError = undefined;

  while (Date.now() - startedAt <= waitTimeoutMs) {
    try {
      await probeDeployment();
      return;
    } catch (error) {
      lastError = error;
      console.log(`Live deployment is not ready yet: ${error.message}`);
      await delay(pollIntervalMs);
    }
  }

  throw new Error(
    `Timed out waiting for live deployment after ${waitTimeoutMs}ms: ${lastError?.message}`,
  );
}

async function checkPublicBrowserFlow(page) {
  await page.goto(`${baseUrl}/`, { waitUntil: 'domcontentloaded' });
  assert((await page.title()) === 'Letovo', 'Expected root page title to be Letovo');

  await page.goto(`${baseUrl}/login`, { waitUntil: 'domcontentloaded' });
  await page.waitForLoadState('networkidle');
  const loginInput = page.locator('#form_login');
  const passwordInput = page.locator('#form_password');
  const submitButton = page.getByRole('button', { name: 'Войти' });

  await loginInput.waitFor({ state: 'visible' });
  await passwordInput.waitFor({ state: 'visible' });
  await submitButton.waitFor({ state: 'visible' });
  assert(await submitButton.isDisabled(), 'Expected login button to be disabled before input');

  await loginInput.click();
  await loginInput.pressSequentially('smoke-user');
  await passwordInput.click();
  await passwordInput.pressSequentially('smoke-password');
  await page.waitForFunction(() => {
    const submit = document.querySelector('button[type="submit"]');
    return submit && !submit.disabled;
  });
}

async function checkAuthenticatedBrowserFlow(page) {
  if (!username || !password) {
    assert(
      !requireAuth,
      'LIVE_E2E_REQUIRE_AUTH=true requires LETOVO_E2E_USERNAME and LETOVO_E2E_PASSWORD',
    );
    console.log('Skipping authenticated flow because LETOVO_E2E_* secrets are not configured.');
    return;
  }

  await loginAs(page, username, password);
  await assertAuthenticatedSession(page);

  if (!requireExtended) {
    console.log('Skipping extended authenticated flow because LIVE_E2E_REQUIRE_EXTENDED is not true.');
    return;
  }

  assert(
    requireAuth,
    'LIVE_E2E_REQUIRE_EXTENDED=true requires LIVE_E2E_REQUIRE_AUTH=true',
  );
  assert(
    secondaryUsername && secondaryPassword,
    'LIVE_E2E_REQUIRE_EXTENDED=true requires LETOVO_E2E_SECONDARY_USERNAME and LETOVO_E2E_SECONDARY_PASSWORD',
  );

  await assertAdminSession(page);
  await assertUploaderSession(page);
  await checkMoneyTransfer(page, secondaryUsername);
  await editSmokeArticle(page);
  const mediaPath = await uploadSmokeFile(page);
  await createSmokePost(page, mediaPath);

  const secondaryBrowser = page.context().browser();
  assert(secondaryBrowser, 'Could not create isolated browser context for secondary e2e login');
  const secondaryContext = await secondaryBrowser.newContext({ viewport: { width: 1366, height: 900 } });
  const secondaryPage = await secondaryContext.newPage();
  try {
    await apiLoginAs(secondaryContext, secondaryUsername, secondaryPassword);
    await secondaryPage.goto(baseUrl, { waitUntil: 'domcontentloaded' });
    await assertAuthenticatedSession(secondaryPage);
    await assertAdminSession(secondaryPage);
  } finally {
    await secondaryContext.close();
  }
}

async function main() {
  await waitForLiveDeployment();

  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1366, height: 900 } });

  try {
    await checkPublicBrowserFlow(page);
    await checkAuthenticatedBrowserFlow(page);
  } finally {
    await browser.close();
  }
}

await main();
