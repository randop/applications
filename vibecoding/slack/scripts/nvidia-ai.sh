#!/usr/bin/env bash
stream=false
if [ "$stream" = true ]; then
    accept_header='Accept: text/event-stream'
else
    accept_header='Accept: application/json'
fi

cat > payload.json <<JSON
{"messages":[{"role":"user","content":[{"type":"text","text":"What is 1+1"}]}],"model":"meta/muse-glimmer-30b","max_tokens":16384,"seed":0,"stream":false,"temperature":1,"reasoning_effort":"max"}
JSON

curl https://integrate.api.nvidia.com/v1/chat/completions \
  -H "Authorization: Bearer $NVIDIA_API_KEY" \
  -H "Content-Type: application/json" \
  -H "$accept_header" \
  -d @payload.json
