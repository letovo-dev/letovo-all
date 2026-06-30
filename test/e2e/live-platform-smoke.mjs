import { createRequire } from 'node:module';
import { setTimeout as delay } from 'node:timers/promises';

const packageDir = process.env.PLAYWRIGHT_PACKAGE_DIR || process.cwd();
const require = createRequire(`${packageDir.replace(/\/+$/, '')}/package.json`);
const { chromium } = require('playwright');

const baseUrl = (process.env.LIVE_E2E_BASE_URL || 'https://ya.sergeiscv.ru').replace(/\/+$/, '');
const waitTimeoutMs = Number(process.env.LIVE_E2E_WAIT_TIMEOUT_SECONDS || 600) * 1000;
const pollIntervalMs = 10_000;
const requireAuth = process.env.LIVE_E2E_REQUIRE_AUTH === 'true';
const username = process.env.LETOVO_E2E_USERNAME || '';
const password = process.env.LETOVO_E2E_PASSWORD || '';

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

  await page.goto(`${baseUrl}/login`, { waitUntil: 'domcontentloaded' });
  await page.locator('#form_login').fill(username);
  await page.locator('#form_password').fill(password);

  const loginResponsePromise = page.waitForResponse(response =>
    response.url().includes('/letovo-api/auth/login'),
  );
  await page.getByRole('button', { name: 'Войти' }).click();
  const loginResponse = await loginResponsePromise;
  assert(loginResponse.status() === 200, `Login API returned HTTP ${loginResponse.status()}`);

  await page.waitForURL(/\/(user|registration)(\/|$)/, { timeout: 20_000 });
  const session = await page.evaluate(async () => {
    const response = await fetch('/letovo-api/auth/amiauthed/', { credentials: 'include' });
    const text = await response.text();
    return { status: response.status, text };
  });

  assert(session.status === 200, `Session API returned HTTP ${session.status}`);
  assert(session.text.includes('"status":"t"'), 'Session API did not confirm authenticated state');
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
