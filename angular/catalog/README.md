# Anime Catalog

A pnpm workspace MVP for an anime catalog:
- Angular standalone frontend
- NestJS REST API
- MongoDB persistence
- Zod runtime validation
- OpenAI-compatible extraction endpoint
- UUIDv7 anime IDs
- Backend image downloads into local storage

## Requirements

- Node.js version supported by the selected Angular release (use an active LTS release)
- pnpm 10+
- MongoDB 7/8
- An OpenAI-compatible API endpoint and model

## Setup

1. Copy `apps/api/.env.example` to `apps/api/.env` and set values.
2. Install dependencies:

   ```bash
   pnpm install
   ```

3. Start the API:

   ```bash
   pnpm --filter @anime-catalog/api dev
   ```

4. In another terminal, start Angular:

   ```bash
   pnpm --filter @anime-catalog/web dev
   ```

Frontend: http://localhost:4200  
API: http://localhost:3000/api  
Swagger: http://localhost:3000/api/docs

## Add-anime flow

1. The UI submits a source URL to `POST /api/anime/import`.
2. The API validates the URL and fetches the page with SSRF guardrails.
3. The extraction service sends bounded page text and metadata to the configured OpenAI-compatible chat-completions endpoint.
4. The model returns XML matching the anime metadata format.
5. The API parses XML, validates fields with Zod, and downloads image assets.
6. The API saves the record in MongoDB using UUIDv7 as `_id`.

## Important production hardening

- Put the API behind authentication and rate limiting.
- For production, replace local image storage with object storage (S3-compatible) or a persistent volume.
- Use a dedicated HTML parser/readability extraction step for sites that require JavaScript rendering.
- Add DNS-rebinding-resistant SSRF controls at the network layer; the current URL/IP checks are defense-in-depth, not a complete sandbox.
- Add image content scanning, quotas, timeouts, and a background job queue for large imports.
- Review website terms, robots policies, copyright, and applicable law before crawling or storing content.
- Never expose the AI API key to Angular.
