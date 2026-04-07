#!/bin/sh

# Test script for MCP Server using curl over TCP

SERVER_HOST="localhost"
SERVER_PORT="7000"

printf "Testing MCP Server on %s:%s\n" "$SERVER_HOST" "$SERVER_PORT"
printf "================================\n"

# Test initialize
printf "Sending initialize request...\n"
RESPONSE=$(timeout 5 sh -c "printf '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}' | nc -q 0 $SERVER_HOST $SERVER_PORT")
printf "Response: %s\n" "$RESPONSE"
printf "================================\n"

# Test tools/list
printf "Sending tools/list request...\n"
RESPONSE=$(timeout 5 sh -c "printf '{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}' | nc -q 0 $SERVER_HOST $SERVER_PORT")
printf "Response: %s\n" "$RESPONSE"
printf "================================\n"

# Test tools/call echo
printf "Sending tools/call echo request...\n"
RESPONSE=$(timeout 5 sh -c "printf '{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"echo\",\"arguments\":{\"text\":\"Hello, MCP!\"}}}' | nc -q 0 $SERVER_HOST $SERVER_PORT")
printf "Response: %s\n" "$RESPONSE"
printf "================================\n"

# Test invalid method
printf "Sending invalid method request...\n"
RESPONSE=$(timeout 5 sh -c "printf '{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"invalid\",\"params\":{}}' | nc -q 0 $SERVER_HOST $SERVER_PORT")
printf "Response: %s\n" "$RESPONSE"
printf "================================\n"

printf "Test complete.\n"