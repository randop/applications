# Kanban Architecture: Detailed Breakdown

## System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Local Trust Boundary                        │
│                                                                     │
│  ┌────────────────────┐         ┌──────────────────────────────┐  │
│  │ Spring Boot 8080   │◄───────►│  MongoDB kanban_local        │  │
│  │  (Plaintext)       │         │  ✓ title (plaintext)        │  │
│  └────────────────────┘         │  ✓ description (plaintext)  │  │
│         ▲                        │  ✓ encKey (base64)          │  │
│         │                        │  ✓ encSalt (base64)         │  │
│         │                        │  ✓ titleCipher (b64)        │  │
│         │                        │  ✓ titleIv (b64)            │  │
│         │                        │  ✓ descCipher (b64)         │  │
│         │                        │  ✓ descIv (b64)             │  │
│         │                        └──────────────────────────────┘  │
│         │                                                           │
│         └───────────────────────────────────────────────────────┘  │
│
│                         ▼ TLS (ideally mTLS)
│
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                    Remote Trust Boundary (Untrusted)                │
│                                                                     │
│  ┌────────────────────┐         ┌──────────────────────────────┐  │
│  │ Spring Boot 8081   │◄───────►│  MongoDB kanban_remote       │  │
│  │  (Ciphertext Only) │         │  ✗ NO plaintext             │  │
│  └────────────────────┘         │  ✗ NO encKey                │  │
│                                 │  ✗ NO encSalt               │  │
│         ▲                        │  ✓ cardId (reference)       │  │
│         │                        │  ✓ boardId (metadata)       │  │
│         │                        │  ✓ titleCipher (b64)        │  │
│         │                        │  ✓ titleIv (b64)            │  │
│         │                        │  ✓ descCipher (b64)         │  │
│         │                        │  ✓ descIv (b64)             │  │
│         │                        │  ✓ status (plaintext)       │  │
│         │                        │  ✓ position (plaintext)     │  │
│         │                        │  ✓ updatedAt (plaintext)    │  │
│         │                        └──────────────────────────────┘  │
│         │                                                           │
│         └───────────────────────────────────────────────────────┘  │
│
└─────────────────────────────────────────────────────────────────────┘
```

## Data Flow: Create Card

### Step 1: User creates card on local client
```
POST /api/cards
{
  "boardId": "board-1",
  "title": "Fix login bug",
  "description": "Session timeout issue",
  "status": "TODO",
  "position": 0
}
```

### Step 2: Local CardService processes create
```
CardService.create(req):
  1. Generate encKey = SecureRandom(32 bytes) → Base64
  2. Generate encSalt = SecureRandom(16 bytes) → Base64
  3. Call CryptoService.encrypt(title, encKey, encSalt):
     a. Derive AES key:
        AES_key = PBKDF2-HMAC-SHA256(
          password = encKey (as char[]),
          salt = Base64.decode(encSalt),
          iterations = 120,000,
          keyLength = 256 bits
        )
     b. Generate random IV = 12 bytes
     c. Cipher = AES-256-GCM
     d. ciphertext = Cipher.encrypt(plaintext + IV)
     e. return EncryptedField(base64(ciphertext), base64(IV))
  4. Repeat step 3 for description
  5. Store Card document in kanban_local:
     {
       _id: "card-1",
       boardId: "board-1",
       titleCipher: "...",
       titleIv: "...",
       descCipher: "...",
       descIv: "...",
       encKey: "...",        ◄─── LOCAL ONLY
       encSalt: "...",       ◄─── LOCAL ONLY
       status: "TODO",
       position: 0,
       syncStatus: "PENDING",
       remoteId: null,
       createdAt: "...",
       updatedAt: "..."
     }
  6. Save card to MongoDB kanban_local
  7. Call SyncService.syncAsync("card-1")
  8. Return CardResponse with decrypted plaintext
```

### Step 3: Async sync to remote
```
SyncService.syncAsync("card-1"):
  ┌─ Card is loaded from kanban_local
  │
  ├─ RemoteSyncClient.pushCard(card):
  │  └─ Build RemoteCardPayload:
  │     {
  │       cardId: "card-1",
  │       boardId: "board-1",
  │       titleCipher: "<encrypted>",
  │       titleIv: "<iv>",
  │       descCipher: "<encrypted>",
  │       descIv: "<iv>",
  │       status: "TODO",
  │       position: 0,
  │       updatedAt: "..."
  │     }
  │     ✗ encKey NOT included
  │     ✗ encSalt NOT included
  │
  │  └─ PUT /api/sync/cards/card-1 + payload
  │     (TLS recommended)
  │
  └─ Remote SyncController.upsertCard():
     1. Check if RemoteCard with cardId="card-1" exists
     2. Create or update RemoteCard:
        {
          _id: ObjectId(...),
          cardId: "card-1",
          boardId: "board-1",
          titleCipher: "<opaque blob>",  ◄─── Never decrypted
          titleIv: "<opaque blob>",
          descCipher: "<opaque blob>",
          descIv: "<opaque blob>",
          status: "TODO",
          position: 0,
          createdAt: "...",
          updatedAt: "..."
        }
     3. Store in kanban_remote
     4. Return RemoteCardDto with remote _id
     
  ├─ Back in local SyncService:
  │  card.setRemoteId(remoteId)
  │  card.setSyncStatus(SYNCED)
  │  Save to kanban_local
  │
  └─ Complete
```

## Encryption Details

### PBKDF2-HMAC-SHA256 Derivation

**Why two secrets (encKey + encSalt)?**
- **encKey**: Raw random material (32 bytes), acts like a "password"
- **encSalt**: Additional random material (16 bytes), acts like a salt
- Together they produce a strong AES key via PBKDF2
- Remote attacker with ciphertext alone cannot derive AES key without both

**Example flow:**
```
encKey  = "aBc123XyZ..." (base64, 32 bytes decoded)
encSalt = "salt456pqr..." (base64, 16 bytes decoded)

PBKDF2(
  password = encKey.toCharArray(),
  salt = Base64.decode(encSalt),
  iterations = 120_000,
  keyLength = 256 bits
)
  ↓
AES_key = 32-byte key for AES-256
```

### GCM Cipher

```
Plaintext: "Fix login bug"
AES key: (256-bit derived)
IV: 12 random bytes (per NIST GCM spec)

Cipher.ENCRYPT_MODE + GCMParameterSpec(128 bits tag)
  ↓
Ciphertext with auth tag: ~13 + auth_tag bytes

Base64 encode both ciphertext and IV separately
```

**IV Freshness:**
- New IV generated per encryption operation
- IV is non-secret (sent alongside ciphertext)
- Each plaintext-key-IV combo must be unique (never reused)

### Remote Service Cannot Decrypt

If remote MongoDB is compromised:
- Attacker gets: cardId, boardId, titleCipher, titleIv, descCipher, descIv, status, position
- Attacker lacks: encKey, encSalt
- Attack cost: Brute force encSalt (2^128 possibilities) × encKey (2^256 possibilities) × PBKDF2 (120K iterations)
- Result: Infeasible

## Sync Flow: Update & Delete

### Update Card

```
PUT /api/cards/card-1
{
  "boardId": "board-1",
  "title": "Fix login bug - IN PROGRESS",
  "description": "Found race condition",
  "status": "IN_PROGRESS",
  "position": 1
}

CardService.update():
  1. Load Card from kanban_local (includes encKey, encSalt)
  2. Re-encrypt plaintext with SAME encKey/encSalt (new IV auto-generated)
  3. Set syncStatus = PENDING
  4. Save to kanban_local
  5. Call SyncService.syncAsync()
  6. Same push-to-remote flow as create
  7. Remote RemoteCard is updated (upsert on cardId)
```

### Delete Card

```
DELETE /api/cards/card-1

CardService.delete():
  1. Delete from kanban_local
  2. Call SyncService.syncDeleteAsync(cardId)

SyncService.syncDeleteAsync():
  (Async)
  └─ RemoteSyncClient.deleteCard(cardId)
     └─ DELETE /api/sync/cards/card-1
        └─ Remote removes RemoteCard with cardId="card-1"
```

## Database Schema

### kanban_local Collection: cards

```javascript
{
  _id: ObjectId,
  boardId: String,
  
  // PLAINTEXT (local only)
  titleCipher: String,     // AES-256-GCM(title, key, iv)
  titleIv: String,         // base64(12 random bytes)
  descCipher: String,      // AES-256-GCM(description, key, iv)
  descIv: String,          // base64(12 random bytes)
  
  // SECRETS (local only, NEVER sent to remote)
  encKey: String,          // base64(32 SecureRandom bytes)
  encSalt: String,         // base64(16 SecureRandom bytes)
  
  // METADATA
  status: String,          // "TODO", "IN_PROGRESS", "DONE"
  position: Number,
  remoteId: String,        // ObjectId of remote RemoteCard
  syncStatus: String,      // "PENDING", "SYNCED", "FAILED"
  
  // TRACKING
  version: Number,         // @Version for optimistic locking
  createdAt: Date,
  updatedAt: Date
}
```

### kanban_remote Collection: remote_cards

```javascript
{
  _id: ObjectId,           // Remote-generated; never sent to local
  cardId: String,          // Reference to local Card._id (index)
  boardId: String,         // Metadata only
  
  // CIPHERTEXT (opaque blobs)
  titleCipher: String,     // Base64(AES ciphertext)
  titleIv: String,         // Base64(12 bytes)
  descCipher: String,      // Base64(AES ciphertext)
  descIv: String,          // Base64(12 bytes)
  
  // METADATA (plaintext)
  status: String,
  position: Number,
  
  // TRACKING
  version: Number,
  createdAt: Date,
  updatedAt: Date
}
```

## Configuration

### Local (kanban-local/application.yml)

```yaml
spring:
  data:
    mongodb:
      uri: mongodb://localhost:27017/kanban_local

sync:
  remote-base-url: http://localhost:8081
  fixed-delay-ms: 30000        # Retry pending syncs every 30s
  enabled: true
```

### Remote (kanban-remote/application.yml)

```yaml
spring:
  data:
    mongodb:
      uri: mongodb://localhost:27017/kanban_remote
```

## Security Considerations

### Assumptions
- **Local service + database**: Trusted, within your control
- **Remote service + database**: Potentially compromised
- **Network**: Eavesdropper or MITM possible (use TLS)

### Threat Mitigations

| Threat | Mitigation |
|--------|-----------|
| Remote DB breach | Ciphertext only; no key material |
| Network eavesdrop | TLS/mTLS between local↔remote |
| Individual encKey leak | encSalt still required; PBKDF2 cost |
| Individual encSalt leak | encKey still required; 2^256 space |
| Both leaked | Ciphertext decryptable; mitigate via local security |
| Timing attacks on crypto | Use standard JDK javax.crypto |

### Limitations

- **No forward secrecy**: Deleted cards remain encrypted on remote forever
- **Metadata visible**: status, position, boardId, timestamps readable on remote
- **Key rotation**: Not implemented; would require re-encryption of all cards
- **Sync failures**: Retry-on-schedule; no queue or fallback strategy

## Performance Notes

- **PBKDF2(120K iterations)**: ~50ms per encryption/decryption on modern CPU
- **AES-256-GCM**: Hardware-accelerated (AES-NI) if available
- **Sync**: Async by default; does not block card create/update
- **Scaling**: Local MongoDB replica set works within trust boundary; remote can scale independently

## Deployment Topology

### Development
```
Local: Spring 8080 ← mongodb:27017
Remote: Spring 8081 ← mongodb:27017 (different DB instance)
```

### Production (Suggested)
```
On-Premise (Trusted):
  ├─ Local Spring Boot HA (load balanced)
  └─ MongoDB replica set (kanban_local DB)
       ↓ TLS + mTLS
Remote Cloud / Untrusted Zone:
  ├─ Remote Spring Boot HA (load balanced)
  └─ MongoDB replica set (kanban_remote DB)
```

### Network Rules
- Local ↔ Local MongoDB: Private network or localhost
- Local → Remote: TLS + certificate auth
- Remote ← Local: Rate-limit to prevent DoS if compromised
- Remote ↔ Remote MongoDB: Private network (cloud VPC / private link)

---

**Next**: See README.md for API examples and quick-start.
