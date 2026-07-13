# Kanban: Dual-Service Zero-Knowledge Architecture

Two-module Spring Boot 3.4 + Java 25 kanban application with split data sovereignty.

## Architecture

### Local Service (port 8080)
- **Responsibility**: Plaintext card data, key management
- **Storage**: MongoDB `kanban_local` database
- **Per-card encryption**: Each card owns a unique `encKey` + `encSalt` (never transmitted)
- **Cipher**: AES-256-GCM with PBKDF2-HMAC-SHA256 key derivation (120K iterations)
- **API Contract**: Returns plaintext `title` and `description` to local clients

### Remote Service (port 8081)
- **Responsibility**: Ciphertext-only relay and archival
- **Storage**: MongoDB `kanban_remote` database
- **Visibility**: Only encrypted `titleCipher` / `descCipher` + `titleIv` / `descIv` fields
- **Crypto**: Zero capability to decrypt (no encKey, no encSalt ever received)
- **API Contract**: PUT `/api/sync/cards/{cardId}` accepts ciphertext blobs; returns DTO

## Encryption Model

Each card encrypts independently:

```
title_ciphertext = AES-256-GCM(
    plaintext=title,
    key=PBKDF2(password=encKey, salt=encSalt, iterations=120K),
    iv=random_12_bytes
)
```

The `encKey` and `encSalt` are:
- Generated at card creation via `SecureRandom`
- Stored in the local MongoDB only
- Never serialized in sync payloads
- Never transmitted to the remote API

### Key Derivation
```
AES_key = PBKDF2-HMAC-SHA256(
    password=encKey (base64),
    salt=encSalt (base64-decoded),
    iterations=120000,
    keyLength=256 bits
)
```

This two-factor model means the remote service sees only raw ciphertext and has zero ability to derive the AES key without both factors.

## Running

### Prerequisites
```bash
docker run -d --name mongo-local -p 27017:27017 mongo:latest
docker run -d --name mongo-remote -p 27018:27017 mongo:latest
```

OR two separate MongoDB instances on different ports/hosts.

### Build
```bash
cd /path/to/kanban-app
mvn clean package -DskipTests
```

### Run Local Service
```bash
cd kanban-local
mvn spring-boot:run
```

### Run Remote Service (separate terminal)
```bash
cd kanban-remote
mvn spring-boot:run
```

## API Examples

### Create Board (Local)
```bash
curl -X POST http://localhost:8080/api/boards \
  -H "Content-Type: application/json" \
  -d '{"name":"Sprint 1"}'
```

### Create Card (Local)
```bash
curl -X POST http://localhost:8080/api/cards \
  -H "Content-Type: application/json" \
  -d '{
    "boardId":"<board_id>",
    "title":"Fix login bug",
    "description":"Users report session timeout",
    "status":"TODO",
    "position":0
  }'
```

Response includes plaintext title/description:
```json
{
  "id":"<card_id>",
  "boardId":"<board_id>",
  "title":"Fix login bug",
  "description":"Users report session timeout",
  "status":"TODO",
  "position":0,
  "syncStatus":"PENDING",
  "remoteId":null,
  "createdAt":"2026-07-02T...",
  "updatedAt":"2026-07-02T..."
}
```

### Automatic Sync
On creation/update, `CardService` calls `SyncService.syncAsync()`:
1. Extracts `titleCipher`, `titleIv`, `descCipher`, `descIv` from the card
2. Sends ciphertext-only `RemoteCardPayload` to remote at `/api/sync/cards/{cardId}`
3. Remote stores the opaque blob in its own database
4. Local card's `syncStatus` → `SYNCED`, `remoteId` → remote `_id`

Pending syncs retry every 30 seconds (configurable via `sync.fixed-delay-ms`).

### Update Card (Local)
```bash
curl -X PUT http://localhost:8080/api/cards/<card_id> \
  -H "Content-Type: application/json" \
  -d '{
    "boardId":"<board_id>",
    "title":"Fix login bug - In Progress",
    "description":"Found race condition in auth middleware",
    "status":"IN_PROGRESS",
    "position":1
  }'
```

New IV generated per GCM requirement. `SyncService` retransmits ciphertext.

### Get Card (Local)
```bash
curl http://localhost:8080/api/cards/<card_id>
```

Returns decrypted plaintext (encKey + encSalt used locally).

### List Cards by Board (Local)
```bash
curl "http://localhost:8080/api/cards?boardId=<board_id>"
```

### Delete Card (Local)
```bash
curl -X DELETE http://localhost:8080/api/cards/<card_id>
```

Async call to remote `DELETE /api/sync/cards/{cardId}`.

### Remote API (GET ciphertext)
```bash
curl http://localhost:8081/api/sync/cards/<card_id>
```

Returns ciphertext-only:
```json
{
  "id":"<remote_mongo_id>",
  "cardId":"<local_card_id>",
  "boardId":"<board_id>",
  "titleCipher":"<base64_encrypted>",
  "titleIv":"<base64_iv>",
  "descCipher":"<base64_encrypted>",
  "descIv":"<base64_iv>",
  "status":"TODO",
  "position":0,
  "createdAt":"...",
  "updatedAt":"..."
}
```

No plaintext, no key material.

## Deployment Notes

### Multi-Instance Local
If you need local service HA, use MongoDB's built-in replica set to keep key material replicated within your trust boundary, but never replicate to the remote database.

### Network Segregation
- Local service → Remote API should use TLS + mutual cert auth
- Remote API logs should not expose ciphertext if breached
- Backups of `kanban_local` stay on-premise; `kanban_remote` can live anywhere

## Security Model

**Threat Model:**
- Remote API / database is potentially compromised
- Local service / database is trusted
- Attacker has ciphertext + metadata but no plaintext or key material

**Guarantees:**
- Plaintext never leaves the local service
- Each card's key is independent (no key reuse across cards)
- PBKDF2 with 120K iterations hardens against brute-force on leaked `encKey` or `encSalt` individually
- AES-256-GCM provides authenticated encryption (AEAD)

**Limitations:**
- If local service is compromised, plaintext is exposed
- If both encKey and encSalt are compromised, ciphertext is decryptable
- No forward secrecy; card deletion doesn't retroactively remove remote ciphertext

## Development

### Testing

```bash
# Run tests (requires MongoDB)
mvn test
```

Add test fixtures in `src/test/java` per module.

### Configuration

**Local (`kanban-local/src/main/resources/application.yml`)**
- `spring.data.mongodb.uri`: Local MongoDB connection
- `sync.remote-base-url`: Remote API address
- `sync.fixed-delay-ms`: Sync retry interval
- `sync.enabled`: Enable/disable sync task

**Remote (`kanban-remote/src/main/resources/application.yml`)**
- `spring.data.mongodb.uri`: Remote MongoDB connection

### Logging

Set `logging.level.com.kanban=DEBUG` in both services to see sync flow:
- `SyncService`: push attempts, status updates
- `RemoteSyncClient`: HTTP calls
- `CryptoService`: encryption/decryption ops

---

**License**: MIT
