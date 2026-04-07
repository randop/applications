#!/usr/bin/env node

// Simple test script to verify the XMCP Server functionality
// This simulates calling the echo tool

const { spawn } = require('child_process');
const path = require('path');

const serverPath = path.join(__dirname, 'dist', 'index.js');

// Start the server
const server = spawn('node', [serverPath], {
  stdio: ['pipe', 'pipe', 'inherit']
});

// Test messages
const testMessages = [
  {
    jsonrpc: '2.0',
    id: 1,
    method: 'initialize',
    params: {
      protocolVersion: '2024-11-05',
      capabilities: {},
      clientInfo: {
        name: 'test-client',
        version: '1.0.0'
      }
    }
  },
  {
    jsonrpc: '2.0',
    id: 2,
    method: 'tools/list'
  },
  {
    jsonrpc: '2.0',
    id: 3,
    method: 'tools/call',
    params: {
      name: 'echo',
      arguments: {
        message: 'Hello, MCP Server!'
      }
    }
  }
];

let messageIndex = 0;

function sendNextMessage() {
  if (messageIndex < testMessages.length) {
    const message = JSON.stringify(testMessages[messageIndex]) + '\n';
    server.stdin.write(message);
    messageIndex++;
  } else {
    // Send shutdown notification
    const shutdown = JSON.stringify({
      jsonrpc: '2.0',
      method: 'shutdown'
    }) + '\n';
    server.stdin.write(shutdown);
    setTimeout(() => {
      server.kill();
      console.log('Test completed successfully!');
    }, 100);
  }
}

// Handle server responses
server.stdout.on('data', (data) => {
  const response = data.toString().trim();
  if (response) {
    console.log('Server response:', response);
    // Send next message after a short delay
    setTimeout(sendNextMessage, 100);
  }
});

// Start the test
setTimeout(sendNextMessage, 500);