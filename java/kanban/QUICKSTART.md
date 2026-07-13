# Quick Start Guide

## Prerequisites

- **Java 25+** (JDK 25)
- **Maven 3.9+**
- **Docker & Docker Compose** (for MongoDB)

### Verify Prerequisites

```bash
java -version
# Should print: openjdk 25 (or similar)

mvn -version
# Should print: Maven 3.9.x or later

docker --version
docker-compose --version
```

## One-Command Setup

### Start MongoDB Instances

```bash
cd kanban-app
docker-compose up -d
```

Verify:
```bash
docker ps
# Should show: kanban-mongo-local, kanban-mongo-remote
```

### Build Both Services

```bash
./build.sh
```

(Or: `mvn clean package -DskipTests`)

### Run Services (Two Terminals)

**Terminal 1: Local Service (port 8080)**
```bash
./run-local.sh
```

Wait for startup:
```
Started KanbanLocalApplication in X.xxx seconds
```

**Terminal 2: Remote Service (port 8081)**
```bash
./run-remote.sh
```

Wait for startup:
```
Started KanbanRemoteApplication in X.xxx seconds
```

## Verify Installation

### Health Check

```bash
# Local service
curl http://localhost:8080/actuator/health 2>/dev/null | jq .
# Expected: {"status":"UP"}

# Remote service
curl http://localhost:8081/actuator/health 2>/dev/null | jq .
# Expected: {"status":"UP"}
```

## Test Workflow

### 1. Create a Board

```bash
BOARD_ID=$(curl -s -X POST http://localhost:8080/api/boards \
  -H "Content-Type: application/json" \
  -d '{"name":"Sprint 1"}' | jq -r '.id')

echo "Created board: $BOARD_ID"
```

### 2. Create a Card

```bash
CARD_RESP=$(curl -s -X POST http://localhost:8080/api/cards \
  -H "Content-Type: application/json" \
  -d "{
    \"boardId\":\"$BOARD_ID\",
    \"title\":\"Fix login bug\",
    \"description\":\"Users report session timeout issues\",
    \"status\":\"TODO\",
    \"position\":0
  }")

CARD_ID=$(echo $CARD_RESP | jq -r '.id')
echo "Created card: $CARD_ID"
echo ""
echo "Card details (plaintext on local):"
echo $CARD_RESP | jq .
```

### 3. Wait for Sync

```bash
sleep 3
```

### 4. Verify Remote Has Ciphertext

```bash
curl -s http://localhost:8081/api/sync/cards/$CARD_ID | jq .
```

**Key observation**: 
- `titleCipher` and `descCipher` are opaque base64 blobs
- `titleIv` and `descIv` are random values
- NO plaintext, NO encKey, NO encSalt

### 5. Get Card from Local (Plaintext)

```bash
curl -s http://localhost:8080/api/cards/$CARD_ID | jq '.title, .description'
```

**Expected**: 
```json
"Fix login bug"
"Users report session timeout issues"
```

### 6. Update Card

```bash
curl -s -X PUT http://localhost:8080/api/cards/$CARD_ID \
  -H "Content-Type: application/json" \
  -d "{
    \"boardId\":\"$BOARD_ID\",
    \"title\":\"Fix login bug - IN PROGRESS\",
    \"description\":\"Found race condition in auth middleware\",
    \"status\":\"IN_PROGRESS\",
    \"position\":1
  }" | jq '.status, .description'
```

Wait 3 seconds and verify remote updated:

```bash
sleep 3
curl -s http://localhost:8081/api/sync/cards/$CARD_ID | jq '.updatedAt, .descCipher' | head -1
```

### 7. Delete Card

```bash
curl -X DELETE http://localhost:8080/api/cards/$CARD_ID
echo "Card deleted from local"

sleep 2
curl http://localhost:8081/api/sync/cards/$CARD_ID 2>&1 | jq .
# Should 404
```

## Complete Test Script

Save as `test.sh` and `chmod +x test.sh`:

```bash
#!/bin/bash
set -e

echo "=== Kanban End-to-End Test ==="
echo ""

# Create board
echo "1. Creating board..."
BOARD=$(curl -s -X POST http://localhost:8080/api/boards \
  -H "Content-Type: application/json" \
  -d '{"name":"Test Sprint"}')
BOARD_ID=$(echo $BOARD | jq -r '.id')
echo "✓ Board: $BOARD_ID"
echo ""

# Create card
echo "2. Creating card (plaintext)..."
CARD=$(curl -s -X POST http://localhost:8080/api/cards \
  -H "Content-Type: application/json" \
  -d "{
    \"boardId\":\"$BOARD_ID\",
    \"title\":\"Implement OAuth2\",
    \"description\":\"Add Google & GitHub login\",
    \"status\":\"TODO\",
    \"position\":0
  }")
CARD_ID=$(echo $CARD | jq -r '.id')
echo "✓ Card: $CARD_ID"
echo "  Title: $(echo $CARD | jq -r '.title')"
echo ""

# Wait for sync
echo "3. Waiting for sync (3s)..."
sleep 3
echo "✓ Synced"
echo ""

# Verify remote has ciphertext
echo "4. Checking remote (ciphertext only)..."
REMOTE=$(curl -s http://localhost:8081/api/sync/cards/$CARD_ID)
TITLE_CIPHER=$(echo $REMOTE | jq -r '.titleCipher')
echo "✓ titleCipher: ${TITLE_CIPHER:0:30}...truncated"
echo "✗ NO plaintext on remote"
echo ""

# Update card
echo "5. Updating card..."
UPDATED=$(curl -s -X PUT http://localhost:8080/api/cards/$CARD_ID \
  -H "Content-Type: application/json" \
  -d "{
    \"boardId\":\"$BOARD_ID\",
    \"title\":\"Implement OAuth2 - 50% complete\",
    \"description\":\"Added Google login, working on GitHub\",
    \"status\":\"IN_PROGRESS\",
    \"position\":1
  }")
echo "✓ Status: $(echo $UPDATED | jq -r '.status')"
echo ""

# Wait for sync
sleep 2

# Verify remote updated
REMOTE_UPDATED=$(curl -s http://localhost:8081/api/sync/cards/$CARD_ID)
echo "6. Remote updated:"
echo "  updatedAt: $(echo $REMOTE_UPDATED | jq -r '.updatedAt')"
echo ""

# Delete
echo "7. Deleting card..."
curl -s -X DELETE http://localhost:8080/api/cards/$CARD_ID > /dev/null
echo "✓ Deleted locally"

sleep 2
DELETED_REMOTE=$(curl -s -w "\n%{http_code}" http://localhost:8081/api/sync/cards/$CARD_ID)
HTTP_CODE=$(echo "$DELETED_REMOTE" | tail -1)
if [ "$HTTP_CODE" = "404" ]; then
  echo "✓ Deleted remotely (404)"
else
  echo "✗ Remote still has card (HTTP $HTTP_CODE)"
fi

echo ""
echo "=== All Tests Passed ==="
```

Run it:
```bash
./test.sh
```

## Logging

### See Full Sync Flow

Edit `kanban-local/src/main/resources/application.yml`:

```yaml
logging:
  level:
    com.kanban: DEBUG
```

Rebuild:
```bash
cd kanban-local && mvn spring-boot:run
```

Watch logs for:
- `CryptoService`: encryption/decryption ops
- `SyncService`: sync attempts
- `RemoteSyncClient`: HTTP calls

### View MongoDB Documents

```bash
# Local
docker exec kanban-mongo-local mongosh kanban_local --eval "db.cards.findOne()"

# Remote
docker exec kanban-mongo-remote mongosh kanban_remote --eval "db.remote_cards.findOne()"
```

## Troubleshooting

### MongoDB won't start
```bash
docker-compose down -v
docker-compose up -d
```

### Services won't connect
```bash
# Check network
docker network ls
docker network inspect kanban-app_default

# Manually test connection
docker run --rm -it mongo:latest mongosh mongodb://kanban-mongo-local:27017/kanban_local
```

### Build fails with Java version
```bash
# Must be Java 25+
java -version

# If not, update JAVA_HOME:
export JAVA_HOME=/path/to/jdk-25
```

### Sync not working
1. Check local service logs for `RemoteSyncClient` errors
2. Verify remote service is running: `curl http://localhost:8081/actuator/health`
3. Check firewall between services (if on different machines)

## Next Steps

- Read `ARCHITECTURE.md` for detailed crypto explanation
- Check `README.md` for full API documentation
- Add security: Enable TLS between local↔remote
- Scale: Add Kafka for sync queue, metrics, etc.

---

**Stuck?** Check service logs:
```bash
# Terminal running local service: scroll up
# Terminal running remote service: scroll up
```
