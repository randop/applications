# XMCP Server

A minimal Model Context Protocol (MCP) server implementation in TypeScript that provides a simple echo tool.

## Features

- **Echo Tool**: A basic tool that echoes back any text message sent to it
- **Stdio Transport**: Communicates via standard input/output for MCP protocol
- **TypeScript**: Fully typed implementation

## Installation

```bash
npm install
```

## Building

```bash
npm run build
```

## Running

```bash
npm start
```

Or for development:

```bash
npm run dev
```

## Usage

This server implements a single tool called `echo` that takes a `message` parameter and returns it prefixed with "Echo: ".

Example interaction:
- Tool call: `{"method": "tools/call", "params": {"name": "echo", "arguments": {"message": "Hello World"}}}`
- Response: `{"content": [{"type": "text", "text": "Echo: Hello World"}]}`

## Project Structure

```
src/
  index.ts          # Main server implementation
package.json        # Dependencies and scripts
tsconfig.json       # TypeScript configuration
```

## Dependencies

- `@modelcontextprotocol/sdk`: MCP protocol implementation
- `typescript`: TypeScript compiler
- `@types/node`: Node.js type definitions
- `ts-node`: TypeScript execution for development