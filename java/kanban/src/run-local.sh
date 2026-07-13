#!/bin/bash
echo "Starting Kanban Local Service on port 8080..."
cd "$(dirname "$0")/kanban-local"
mvn spring-boot:run
