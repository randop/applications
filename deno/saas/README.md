# SaaS

## Development

```sh
deno task dev
```

### TypeScript type-check support

This project supports instant reload with type-check on development.

```
Task dev deno run --check --watch --unstable-sloppy-imports --allow-env --allow-net src/main.ts
Watcher Process started.
Check file:///applications/deno/saas/main.ts
TS2322 [ERROR]: Type 'string' is not assignable to type 'number'.
const a:number = "2";
      ^
    at file:///applications/deno/saas/src/app.module.ts:13:7

error: Type checking failed.

  info: The program failed type-checking, but it still might work correctly.
  hint: Re-run with --no-check to skip type-checking.
Watcher Process failed. Restarting on file change...
```
