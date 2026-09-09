# Slack

## compile
```bash
g++ -std=c++17 -O2 \
  -I$HOME/opt/boost/current/include \
  src/main.cpp -o slack_bot \
  -lssl -lcrypto -lpthread
```
