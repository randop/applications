// src/db/turso.js
//
// Minimal client for Turso's HTTP API (the "v2/pipeline" Hrana-over-HTTP
// protocol: https://docs.turso.tech/sdk/http/reference). This is used
// instead of a native libsql client because Cloudflare Workers' WASM/JS
// sandbox has no raw TCP socket access -- only `fetch`. The pipeline
// endpoint is a plain HTTPS POST/JSON API, which fits the Workers model
// exactly and needs no extra dependency.

export class TursoError extends Error {
  constructor(message, details) {
    super(message);
    this.name = "TursoError";
    this.details = details;
  }
}

export class TursoClient {
  /**
   * @param {string} url   e.g. "https://your-db-org.turso.io" (no trailing slash, no /v2/pipeline)
   * @param {string} authToken
   */
  constructor(url, authToken) {
    if (!url || !authToken) {
      throw new TursoError("Turso client requires both a database URL and an auth token");
    }
    // Accept either an http(s) URL or a libsql:// URL and normalize to https.
    this.baseUrl = url.replace(/^libsql:\/\//, "https://").replace(/\/+$/, "");
    this.authToken = authToken;
  }

  /**
   * Runs one or more SQL statements in a single HTTP round trip.
   * @param {{sql: string, args?: any[]}[]} statements
   * @returns {Promise<Array<{columns: string[], rows: any[][]}>>} one result per statement
   */
  async batch(statements) {
    const requests = [
      ...statements.map((s) => ({
        type: "execute",
        stmt: { sql: s.sql, args: (s.args ?? []).map(toHranaValue) },
      })),
      { type: "close" },
    ];

    const res = await fetch(`${this.baseUrl}/v2/pipeline`, {
      method: "POST",
      headers: {
        "content-type": "application/json",
        authorization: `Bearer ${this.authToken}`,
      },
      body: JSON.stringify({ requests }),
    });

    if (!res.ok) {
      const body = await res.text().catch(() => "");
      throw new TursoError(`Turso HTTP ${res.status}: ${res.statusText}`, body);
    }

    const payload = await res.json();
    const results = [];

    for (let i = 0; i < statements.length; i++) {
      const entry = payload.results?.[i];
      if (!entry) {
        throw new TursoError(`Turso response missing result for statement ${i}`, payload);
      }
      if (entry.type === "error") {
        throw new TursoError(
          `Turso query failed: ${entry.error?.message ?? "unknown error"}`,
          entry.error
        );
      }
      const execResult = entry.response?.result;
      results.push({
        columns: (execResult?.cols ?? []).map((c) => c.name),
        rows: (execResult?.rows ?? []).map((row) => row.map(fromHranaValue)),
        lastInsertRowid: execResult?.last_insert_rowid ?? null,
        affectedRowCount: execResult?.affected_row_count ?? 0,
      });
    }

    return results;
  }

  /** Runs a single statement and returns rows as an array of plain objects. */
  async query(sql, args = []) {
    const [result] = await this.batch([{ sql, args }]);
    return result.rows.map((row) => rowToObject(result.columns, row));
  }

  /** Runs a single statement for its side effects (INSERT/UPDATE/DELETE/DDL). */
  async execute(sql, args = []) {
    const [result] = await this.batch([{ sql, args }]);
    return { lastInsertRowid: result.lastInsertRowid, affectedRowCount: result.affectedRowCount };
  }
}

function rowToObject(columns, row) {
  const obj = {};
  for (let i = 0; i < columns.length; i++) obj[columns[i]] = row[i];
  return obj;
}

// --- Hrana value (de)serialization -----------------------------------------

function toHranaValue(v) {
  if (v === null || v === undefined) return { type: "null" };
  if (typeof v === "bigint") return { type: "integer", value: v.toString() };
  if (typeof v === "number") {
    return Number.isInteger(v)
      ? { type: "integer", value: v.toString() }
      : { type: "float", value: v };
  }
  if (typeof v === "boolean") return { type: "integer", value: v ? "1" : "0" };
  if (v instanceof Uint8Array) return { type: "blob", base64: bytesToBase64(v) };
  return { type: "text", value: String(v) };
}

function fromHranaValue(v) {
  if (!v || v.type === "null") return null;
  switch (v.type) {
    case "integer":
      return Number.isSafeInteger(Number(v.value)) ? Number(v.value) : BigInt(v.value);
    case "float":
      return v.value;
    case "text":
      return v.value;
    case "blob":
      return base64ToBytes(v.base64);
    default:
      return v.value ?? null;
  }
}

function bytesToBase64(bytes) {
  let binary = "";
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary);
}

function base64ToBytes(b64) {
  const binary = atob(b64);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}
