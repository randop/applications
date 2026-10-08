# QUIC GraphQL SQLite Server

A multi-protocol, high-performance web server built in Rust using **Salvo**, **async-graphql**, and **sqlx**. It natively serves **HTTP/3 (QUIC)** over UDP with automatic ALPN fallback to **HTTP/2** and **HTTP/1.1** over TLS (TCP) on the same port.

---

## Features

- **Multi-Protocol Support**: Dual-listener configuration binding both UDP (`QuinnListener`) and TCP (`TcpListener`) to port `8443`.
- **GraphQL API**: Powered by `async-graphql` with an integrated interactive GraphQL Playground.
- **Async SQLite**: Powered by `sqlx` with an in-memory SQLite pool.
- **TLS via Rustls**: Modern TLS configuration for secure transport negotiation.

---

## Prerequisites

- **Rust toolchain** (Rust 2021 edition or newer)
- **OpenSSL** (if re-generating self-signed certificates)

---

## Project Structure

```text
.
├── Cargo.toml          # Cargo configuration & crate dependencies
├── cert.pem            # Self-signed TLS certificate
├── key.pem             # Self-signed TLS private key
├── README.md           # Documentation
└── src
    └── main.rs         # Database initialization, GraphQL schema, and server listeners
```

---

## Getting Started

1. **Navigate to the project directory:**
   ```bash
   cd quic-graphql-sqlite
   ```

2. **Generate TLS Certificates** *(optional, certificates are pre-included)*:
   ```bash
   openssl req -x509 -newkey rsa:4096 -nodes \
     -keyout key.pem -out cert.pem \
     -days 365 -subj '/CN=localhost'
   ```

3. **Start the server:**
   ```bash
   cargo run
   ```

---

## Usage

### Interactive GraphQL Playground
Open your browser and navigate to:
```text
https://localhost:8443/graphql
```
*(Note: Because the server uses a self-signed TLS certificate, you will need to accept the browser security warning).*

### Sample Operations

#### Add a User (Mutation)
```graphql
mutation {
  addUser(name: "Alice") {
    id
    name
  }
}
```

#### Fetch Users (Query)
```graphql
query {
  users {
    id
    name
  }
}
```

---

## Protocol Verification

You can verify that the server correctly negotiates between HTTP/3 and fallback protocols using `curl`:

- **HTTP/3 (QUIC over UDP)**
  ```bash
  curl --http3 -k -X POST https://localhost:8443/graphql \
    -H "Content-Type: application/json" \
    -d '{"query": "query { users { id name } }"}'
  ```

- **HTTP/2 (TCP / TLS Fallback)**
  ```bash
  curl --http2 -k -X POST https://localhost:8443/graphql \
    -H "Content-Type: application/json" \
    -d '{"query": "query { users { id name } }"}'
  ```
