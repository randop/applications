# picobot

## compile
```bash
g++ -std=c++17 -O2 \
  -I$HOME/opt/boost/current/include \
  src/main.cpp -o picobot \
  -lssl -lcrypto -lpthread -lboost_json -lboost_context
```
