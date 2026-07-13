#!/bin/bash
set -e

echo "Building Kanban Dual-Service Application..."
cd "$(dirname "$0")"

mvn clean package -DskipTests

echo ""
echo "✓ Build complete"
echo ""
echo "Artifacts:"
echo "  kanban-local:  kanban-local/target/kanban-local-1.0.0.jar"
echo "  kanban-remote: kanban-remote/target/kanban-remote-1.0.0.jar"
