# Project File Structure

## Root Files

```
kanban-app/
├── pom.xml                  Parent Maven POM (defines Spring Boot version, Java 25)
├── build.sh                 Build script (mvn clean package)
├── run-local.sh            Run local service script
├── run-remote.sh           Run remote service script
├── docker-compose.yml      MongoDB instances for both services
├── README.md               Full API documentation & deployment guide
├── ARCHITECTURE.md         Detailed encryption flow, schema, topology
├── QUICKSTART.md           Step-by-step setup & test workflow
├── FILES.md                This file
└── .gitignore              (if using git)
```

## kanban-local Module (Local Service, Port 8080)

### Maven Configuration
```
kanban-local/
├── pom.xml
└── src/main/
    ├── java/com/kanban/local/
    │   │
    │   ├── KanbanLocalApplication.java
    │   │   Main Spring Boot application class
    │   │   Enables @EnableScheduling, @EnableAsync
    │   │
    │   ├── config/
    │   │   ├── SyncProperties.java
    │   │   │   Configuration properties from application.yml
    │   │   │   - remoteBaseUrl: URL of remote service
    │   │   │   - fixedDelayMs: Sync retry interval
    │   │   │   - enabled: Enable/disable sync task
    │   │   │
    │   │   └── RestClientConfig.java
    │   │       Bean providing RestClient for HTTP calls to remote API
    │   │
    │   ├── crypto/
    │   │   ├── EncryptedField.java
    │   │   │   Record: { cipherText, iv }
    │   │   │
    │   │   └── CryptoService.java
    │   │       AES-256-GCM encryption/decryption
    │   │       - generateRawKey(): 32 SecureRandom bytes (base64)
    │   │       - generateSalt(): 16 SecureRandom bytes (base64)
    │   │       - encrypt(plaintext, key, salt): EncryptedField
    │   │       - decrypt(field, key, salt): plaintext
    │   │       Uses PBKDF2-HMAC-SHA256 (120K iterations) for key derivation
    │   │
    │   ├── model/
    │   │   ├── SyncStatus.java
    │   │   │   Enum: PENDING, SYNCED, FAILED
    │   │   │
    │   │   ├── Board.java
    │   │   │   @Document(collection="boards")
    │   │   │   - id: MongoDB _id
    │   │   │   - name: board name
    │   │   │   - createdAt: timestamp
    │   │   │
    │   │   └── Card.java
    │   │       @Document(collection="cards")
    │   │       LOCAL database holds:
    │   │       - titleCipher, titleIv (encrypted title)
    │   │       - descCipher, descIv (encrypted description)
    │   │       - encKey, encSalt (NEVER sent to remote)
    │   │       - status, position, boardId (plaintext metadata)
    │   │       - syncStatus, remoteId (sync tracking)
    │   │       - version (optimistic locking)
    │   │       - timestamps
    │   │
    │   ├── repository/
    │   │   ├── BoardRepository.java
    │   │   │   Spring Data MongoDB: find, save, delete boards
    │   │   │
    │   │   └── CardRepository.java
    │   │       Custom queries:
    │   │       - findByBoardId(String boardId)
    │   │       - findBySyncStatusIn(List<SyncStatus>)
    │   │
    │   ├── dto/
    │   │   ├── BoardRequest.java
    │   │   │   Record: { name }
    │   │   │
    │   │   ├── CardRequest.java
    │   │   │   Record: { boardId, title, description, status, position }
    │   │   │
    │   │   ├── CardResponse.java
    │   │   │   Record: { id, boardId, title, description, status, position, 
    │   │   │            syncStatus, remoteId, createdAt, updatedAt }
    │   │   │   ← Returns PLAINTEXT title/description to local clients
    │   │   │
    │   │   └── RemoteCardPayload.java
    │   │       Record: { cardId, boardId, titleCipher, titleIv, 
    │   │                descCipher, descIv, status, position, updatedAt }
    │   │       ← Sent to remote (ciphertext only, NO keys)
    │   │
    │   ├── service/
    │   │   ├── BoardService.java
    │   │   │   CRUD operations on boards
    │   │   │   - create(BoardRequest)
    │   │   │   - get(id), list(), delete(id)
    │   │   │
    │   │   ├── CardService.java
    │   │   │   CRUD + encryption for cards
    │   │   │   - create(CardRequest): generates encKey, encSalt; encrypts title/desc
    │   │   │   - update(id, CardRequest): re-encrypts with same key, new IV
    │   │   │   - get(id), listByBoard(boardId), delete(id)
    │   │   │   - toResponse(): decrypts plaintext for client response
    │   │   │
    │   │   └── SyncService.java
    │   │       Async push to remote
    │   │       - syncAsync(cardId): @Async, calls RemoteSyncClient
    │   │       - syncDeleteAsync(cardId): @Async, DELETE from remote
    │   │       - @Scheduled syncPending(): retry PENDING/FAILED every fixedDelayMs
    │   │       NEVER sends encKey or encSalt
    │   │
    │   ├── client/
    │   │   └── RemoteSyncClient.java
    │   │       HTTP client for remote API
    │   │       - pushCard(card): PUT /api/sync/cards/{cardId}
    │   │       - deleteCard(cardId): DELETE /api/sync/cards/{cardId}
    │   │       Uses RestClient bean configured in RestClientConfig
    │   │
    │   └── controller/
    │       ├── BoardController.java
    │       │   REST endpoints:
    │       │   - POST /api/boards
    │       │   - GET /api/boards, /api/boards/{id}
    │       │   - DELETE /api/boards/{id}
    │       │
    │       └── CardController.java
    │           REST endpoints (returns plaintext responses):
    │           - POST /api/cards
    │           - PUT /api/cards/{id}
    │           - GET /api/cards/{id}
    │           - GET /api/cards?boardId=...
    │           - DELETE /api/cards/{id}
    │
    └── resources/
        └── application.yml
            spring.data.mongodb.uri: mongodb://localhost:27017/kanban_local
            sync.remote-base-url: http://localhost:8081
            sync.fixed-delay-ms: 30000
            sync.enabled: true
            logging.level.com.kanban: DEBUG
```

## kanban-remote Module (Remote Service, Port 8081)

### Maven Configuration
```
kanban-remote/
├── pom.xml
└── src/main/
    ├── java/com/kanban/remote/
    │   │
    │   ├── KanbanRemoteApplication.java
    │   │   Main Spring Boot application class
    │   │
    │   ├── model/
    │   │   └── RemoteCard.java
    │   │       @Document(collection="remote_cards")
    │   │       REMOTE database holds:
    │   │       - cardId: reference to local Card._id
    │   │       - titleCipher, titleIv (opaque blobs)
    │   │       - descCipher, descIv (opaque blobs)
    │   │       - boardId, status, position (plaintext metadata)
    │   │       ✗ NO encKey, NO encSalt
    │   │       ✗ NO plaintext title/description
    │   │
    │   ├── repository/
    │   │   └── RemoteCardRepository.java
    │   │       Custom queries:
    │   │       - findByBoardId(String boardId)
    │   │       - findByCardId(String cardId): upsert key
    │   │
    │   ├── dto/
    │   │   ├── RemoteCardRequest.java
    │   │   │   Record: input from local service (ciphertext only)
    │   │   │
    │   │   └── RemoteCardDto.java
    │   │       Record: response to local service (still ciphertext)
    │   │
    │   └── controller/
    │       └── SyncController.java
    │           REST endpoints (ciphertext-only):
    │           - PUT /api/sync/cards/{cardId}: upsert card (ciphertext)
    │           - GET /api/sync/cards/{cardId}: retrieve card (ciphertext)
    │           - DELETE /api/sync/cards/{cardId}: delete card
    │
    └── resources/
        └── application.yml
            spring.data.mongodb.uri: mongodb://localhost:27017/kanban_remote
            logging.level.com.kanban: DEBUG
```

## Key Classes Overview

| Class | Location | Purpose |
|-------|----------|---------|
| `CryptoService` | local/crypto | AES-256-GCM encryption, PBKDF2 key derivation |
| `CardService` | local/service | Encrypt/decrypt on create/update, orchestrate sync |
| `SyncService` | local/service | Async/scheduled push to remote |
| `RemoteSyncClient` | local/client | HTTP calls to remote API |
| `CardController` | local/controller | REST API endpoints (local, plaintext) |
| `SyncController` | remote/controller | REST API endpoints (remote, ciphertext) |
| `Card` | local/model | Local database document (plaintext + ciphertext) |
| `RemoteCard` | remote/model | Remote database document (ciphertext only) |

## Configuration Files

- `kanban-local/src/main/resources/application.yml`: Local service config
- `kanban-remote/src/main/resources/application.yml`: Remote service config
- `docker-compose.yml`: MongoDB container definitions

## Build Artifacts

After `./build.sh`:

```
kanban-local/target/kanban-local-1.0.0.jar
kanban-remote/target/kanban-remote-1.0.0.jar
```

These are fat JARs (Spring Boot application) and can be run as:

```bash
java -jar kanban-local/target/kanban-local-1.0.0.jar
java -jar kanban-remote/target/kanban-remote-1.0.0.jar
```

---

**Total Lines of Code**: ~2,500 (all modules combined)
**Dependencies**: Spring Boot 3.4.1, Spring Data MongoDB, javax.crypto
**Java Version**: 25

