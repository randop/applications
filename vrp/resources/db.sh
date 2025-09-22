#!/bin/sh
docker compose up -d --renew-anon-volumes &&
  docker compose logs -f
