/**
 * Raw shape of the response from the open.er-api.com "latest" endpoint
 * (ExchangeRate-API's free, no-key, open-access endpoint).
 */
export interface ExchangeRateApiResponse {
  result: string;
  base_code: string;
  time_last_update_utc: string;
  rates: Record<string, number>;
}

/**
 * A validated, converted exchange-rate result — what the app actually
 * hands back once the raw API response has been checked.
 */
export interface ExchangeRate {
  base: string;
  target: string;
  rate: number;
  amount: number;
  converted: number;
  lastUpdated: string;
}

/** Successful outcome of a fallible operation, carrying its data. */
export interface AppSuccess<T> {
  readonly ok: true;
  readonly data: T;
}

/**
 * Failed outcome of a fallible operation. `cause` carries the original
 * error (a caught exception, a bad status code, etc.) for logging, without
 * forcing every caller to deal with `unknown`-typed catch bindings.
 */
export interface AppError {
  readonly ok: false;
  readonly message: string;
  readonly cause?: unknown;
}

/**
 * Discriminated union every fallible operation in this app returns instead
 * of throwing. Narrow with `if (result.ok)`.
 */
export type AppResult<T> = AppSuccess<T> | AppError;

export function ok<T>(data: T): AppSuccess<T> {
  return { ok: true, data };
}

export function err(message: string, cause?: unknown): AppError {
  return { ok: false, message, cause };
}
