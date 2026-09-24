/**
 * IMAP client authenticated via WorkOS AuthKit CLI Auth
 * (OAuth 2.0 Device Authorization Grant)
 *
 * Docs:
 *   https://workos.com/docs/authkit/cli-auth
 *   https://workos.com/docs/reference/authkit/cli-auth
 */
import { ImapFlow } from 'imapflow';
import { WorkOS } from '@workos-inc/node';
import {
  chmod,
  mkdir,
  readFile,
  rename,
  rm,
  writeFile,
} from 'node:fs/promises';
import { existsSync, readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { config as loadEnv } from 'dotenv';

// ---------------------------------------------------------------------------
// Load .env.local (then .env) so local secrets override process env defaults
// ---------------------------------------------------------------------------
loadEnv({ path: '.env.local' });
loadEnv(); // optional fallback to .env

const execFileAsync = promisify(execFile);

// ---------------------------------------------------------------------------
// Config (prefer env / .env.local)
// ---------------------------------------------------------------------------
const WORKOS_CLIENT_ID = requireEnv('WORKOS_CLIENT_ID');

const IMAP_HOST = process.env.IMAP_HOST ?? 'mail.quizbin.com';
const IMAP_PORT = Number(process.env.IMAP_PORT ?? '993');
const IMAP_USERNAME =
  process.env.IMAP_USERNAME ?? process.env.EMAIL ?? 'randolph@email.ngo';

const IMAP_TLS_SERVERNAME =
  process.env.IMAP_TLS_SERVERNAME ?? IMAP_HOST;
const IMAP_TLS_CA = process.env.IMAP_TLS_CA;
const IMAP_TLS_INSECURE = process.env.IMAP_TLS_INSECURE === '1';

const TOKEN_FILE =
  process.env.IMAP_TOKEN_FILE ??
  join(homedir(), '.cache', 'imap-workos', 'tokens.json');

// Official AuthKit CLI Auth endpoints (User Management API)
const DEVICE_AUTH_URL =
  'https://api.workos.com/user_management/authorize/device';
const AUTHENTICATE_URL =
  'https://api.workos.com/user_management/authenticate';

// Public-client mode: no API key required for device/refresh flows
const workos = new WorkOS({ clientId: WORKOS_CLIENT_ID });

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
interface DeviceAuthorizationResponse {
  device_code: string;
  user_code: string;
  verification_uri: string;
  verification_uri_complete?: string;
  expires_in: number;
  interval?: number;
}

interface TokenResponse {
  access_token: string;
  refresh_token?: string;
  // User Management authenticate responses may also include user, etc.
  [key: string]: unknown;
}

interface StoredTokens {
  access_token: string;
  refresh_token?: string;
  expires_at: number;
}

interface OAuthError {
  error?: string;
  error_description?: string;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
function requireEnv(name: string): string {
  const value = process.env[name];
  if (!value) {
    throw new Error(
      `Missing required environment variable: ${name}\n` +
        `Add it to .env.local (e.g. ${name}=client_01...)`
    );
  }
  return value;
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function postForm(
  url: string,
  params: URLSearchParams
): Promise<{ ok: boolean; status: number; data: unknown }> {
  const response = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
      Accept: 'application/json',
    },
    body: params,
  });

  const text = await response.text();
  let data: unknown = {};

  try {
    data = text ? JSON.parse(text) : {};
  } catch {
    throw new Error(
      `WorkOS returned invalid JSON (${response.status}): ${text}`
    );
  }

  return { ok: response.ok, status: response.status, data };
}

// ---------------------------------------------------------------------------
// 1. Request device authorization
//    POST https://api.workos.com/user_management/authorize/device
// ---------------------------------------------------------------------------
async function requestDeviceAuthorization(): Promise<DeviceAuthorizationResponse> {
  console.log('Starting WorkOS AuthKit device authorization...');

  const { ok, status, data } = await postForm(
    DEVICE_AUTH_URL,
    new URLSearchParams({ client_id: WORKOS_CLIENT_ID })
  );

  if (!ok) {
    const err = data as OAuthError;
    throw new Error(
      `${err.error ?? 'device_authorization_error'}: ${
        err.error_description ?? `HTTP ${status}`
      }`
    );
  }

  const result = data as DeviceAuthorizationResponse;

  console.log('');
  console.log('Authenticate with WorkOS AuthKit:');
  console.log('');
  console.log(`  Code: ${result.user_code}`);
  console.log(`  URL:  ${result.verification_uri}`);

  if (result.verification_uri_complete) {
    console.log('');
    console.log('Complete URL (pre-filled code):');
    console.log(`  ${result.verification_uri_complete}`);
  }

  console.log('');
  console.log(
    `Authorization expires in ${result.expires_in} seconds.`
  );
  console.log('');

  // Optionally open the browser for the user
  if (result.verification_uri_complete) {
    await openBrowser(result.verification_uri_complete);
  }

  return result;
}

// ---------------------------------------------------------------------------
// 2. Poll for tokens
//    POST https://api.workos.com/user_management/authenticate
//    grant_type=urn:ietf:params:oauth:grant-type:device_code
// ---------------------------------------------------------------------------
async function pollForTokens(
  device: DeviceAuthorizationResponse
): Promise<TokenResponse> {
  let interval = Math.max(device.interval ?? 5, 5);
  const deadline = Date.now() + device.expires_in * 1000;

  process.stdout.write('Waiting for authorization');

  while (Date.now() < deadline) {
    await sleep(interval * 1000);

    const { ok, status, data } = await postForm(
      AUTHENTICATE_URL,
      new URLSearchParams({
        grant_type: 'urn:ietf:params:oauth:grant-type:device_code',
        device_code: device.device_code,
        client_id: WORKOS_CLIENT_ID,
      })
    );

    if (ok) {
      process.stdout.write('\n');
      return data as TokenResponse;
    }

    const error = data as OAuthError;

    switch (error.error) {
      case 'authorization_pending':
        process.stdout.write('.');
        continue;

      case 'slow_down':
        interval += 1;
        process.stdout.write('>');
        continue;

      case 'access_denied':
        throw new Error(
          error.error_description ?? 'WorkOS authorization denied'
        );

      case 'expired_token':
        throw new Error(
          error.error_description ?? 'WorkOS device code expired'
        );

      default:
        throw new Error(
          `${error.error ?? 'token_error'}: ${
            error.error_description ?? `HTTP ${status}`
          }`
        );
    }
  }

  throw new Error('WorkOS authorization timed out');
}

// ---------------------------------------------------------------------------
// Token persistence
// ---------------------------------------------------------------------------
async function saveTokens(tokens: {
  access_token: string;
  refresh_token?: string;
  expires_in?: number;
}): Promise<void> {
  const directory = dirname(TOKEN_FILE);

  await mkdir(directory, { recursive: true, mode: 0o700 });

  // Access tokens are typically short-lived; default to 5 min if unknown
  const expiresInSec = tokens.expires_in ?? 300;

  const stored: StoredTokens = {
    access_token: tokens.access_token,
    refresh_token: tokens.refresh_token,
    expires_at: Date.now() + Math.max(expiresInSec - 30, 0) * 1000,
  };

  const temporaryFile = `${TOKEN_FILE}.${process.pid}.tmp`;

  await writeFile(
    temporaryFile,
    JSON.stringify(stored, null, 2) + '\n',
    { mode: 0o600 }
  );
  await chmod(temporaryFile, 0o600);
  await rename(temporaryFile, TOKEN_FILE);
}

async function loadTokens(): Promise<StoredTokens | null> {
  if (!existsSync(TOKEN_FILE)) {
    return null;
  }

  try {
    const data = await readFile(TOKEN_FILE, 'utf8');
    return JSON.parse(data) as StoredTokens;
  } catch (error) {
    console.warn(
      `Unable to read token file ${TOKEN_FILE}; starting login.`
    );
    console.warn(error);
    return null;
  }
}

// ---------------------------------------------------------------------------
// Refresh via WorkOS Node SDK (public client mode)
// ---------------------------------------------------------------------------
async function refreshTokens(
  refreshToken: string
): Promise<{ access_token: string; refresh_token?: string; expires_in: number }> {
  console.log('Refreshing WorkOS access token...');

  const result = await workos.userManagement.authenticateWithRefreshToken({
    clientId: WORKOS_CLIENT_ID,
    refreshToken,
  });

  return {
    access_token: result.accessToken,
    refresh_token: result.refreshToken,
    // SDK does not always surface expires_in; use a conservative default
    expires_in: 300,
  };
}

// ---------------------------------------------------------------------------
// Full auth orchestration
// ---------------------------------------------------------------------------
async function authenticateWorkOS(): Promise<string> {
  const current = await loadTokens();

  if (current) {
    if (current.expires_at > Date.now()) {
      return current.access_token;
    }

    if (current.refresh_token) {
      try {
        const refreshed = await refreshTokens(current.refresh_token);

        await saveTokens({
          access_token: refreshed.access_token,
          refresh_token:
            refreshed.refresh_token ?? current.refresh_token,
          expires_in: refreshed.expires_in,
        });

        return refreshed.access_token;
      } catch (error) {
        console.warn(
          'Refresh token failed; starting a new device authorization.'
        );
        console.warn(error);
        await rm(TOKEN_FILE, { force: true });
      }
    }
  }

  const device = await requestDeviceAuthorization();
  const tokens = await pollForTokens(device);

  if (!tokens.access_token) {
    throw new Error('WorkOS did not return an access_token');
  }

  await saveTokens({
    access_token: tokens.access_token,
    refresh_token: tokens.refresh_token,
    // User Management responses often omit expires_in for device grant
    expires_in: typeof tokens.expires_in === 'number'
      ? tokens.expires_in
      : 300,
  });

  console.log('');
  console.log('WorkOS authentication successful.');

  return tokens.access_token;
}

// ---------------------------------------------------------------------------
// Optional: open verification URL in the system browser
// ---------------------------------------------------------------------------
async function openBrowser(url: string): Promise<void> {
  try {
    if (process.platform === 'linux') {
      await execFileAsync('xdg-open', [url]);
      return;
    }
    if (process.platform === 'darwin') {
      await execFileAsync('open', [url]);
      return;
    }
    if (process.platform === 'win32') {
      await execFileAsync('cmd', ['/c', 'start', '', url]);
    }
  } catch {
    // Browser opening is optional
  }
}

// ---------------------------------------------------------------------------
// IMAP (XOAUTH2 with the WorkOS access token)
// ---------------------------------------------------------------------------
async function connectImap(accessToken: string): Promise<void> {
  if (!IMAP_USERNAME) {
    throw new Error('Set IMAP_USERNAME or EMAIL in .env.local');
  }

  const tlsOptions: {
    servername: string;
    rejectUnauthorized?: boolean;
    ca?: Buffer[];
  } = {
    servername: IMAP_TLS_SERVERNAME,
  };

  if (IMAP_TLS_CA) {
    tlsOptions.ca = [readFileSync(resolve(IMAP_TLS_CA))];
  }

  if (IMAP_TLS_INSECURE) {
    console.warn('WARNING: TLS certificate verification is disabled.');
    tlsOptions.rejectUnauthorized = false;
  }

  const client = new ImapFlow({
    host: IMAP_HOST,
    port: IMAP_PORT,
    secure: true,
    tls: tlsOptions,
    auth: {
      user: IMAP_USERNAME,
      accessToken,
    },
    logger: false,
  });

  try {
    console.log('');
    console.log(`Connecting to ${IMAP_HOST}:${IMAP_PORT}...`);

    await client.connect();

    console.log('TLS connection established.');
    console.log('IMAP authentication successful.');

    const lock = await client.getMailboxLock('INBOX');

    try {
      console.log('');
      console.log('INBOX');
      const mailbox = client.mailbox;
      if (mailbox) {
        console.log(`  messages: ${mailbox.exists ?? 0}`);
        console.log(
          `  UIDVALIDITY: ${mailbox.uidValidity ?? 'unknown'}`
        );
        console.log(`  UIDNEXT: ${mailbox.uidNext ?? 'unknown'}`);
      }

      await client.noop();
      console.log('');
      console.log('NOOP succeeded.');
    } finally {
      lock.release();
    }
  } finally {
    try {
      await client.logout();
    } catch {
      client.close();
    }
  }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
async function main(): Promise<void> {
  console.log('IMAP WorkOS Client (AuthKit CLI Auth)');
  console.log('=====================================');
  console.log('');
  console.log(`Client ID: ${WORKOS_CLIENT_ID}`);
  console.log(`IMAP:      ${IMAP_HOST}:${IMAP_PORT}`);
  console.log('');

  const accessToken = await authenticateWorkOS();
  await connectImap(accessToken);

  console.log('');
  console.log('Test completed successfully.');
}

main().catch((error) => {
  console.error('');
  console.error('ERROR');
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
