#!/bin/bash
echo "Starting Kanban Remote Service on port 8081..."
cd "$(dirname "$0")/kanban-remote"
mvn spring-boot:run
