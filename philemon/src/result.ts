export class AppError extends Error {
  constructor(
    message: string,
    public readonly code?: string,
  ) {
    super(message);
    this.name = "AppError";
  }
}

export interface Success<T> {
  readonly ok: true;
  readonly value: T;
}

export interface Failure<E> {
  readonly ok: false;
  readonly error: E;
}

export type Result<T, E = AppError> = Success<T> | Failure<E>;

export const SuccessResult = <T>(value: T): Success<T> =>
  ({ ok: true, value }) as const;

export const FailureResult = <E>(error: E): Failure<E> =>
  ({ ok: false, error }) as const;
