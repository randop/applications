FROM node:22-slim

WORKDIR /app

CMD ["node", "-e", "require('http').createServer((req,res)=>res.end('hello world')).listen(process.env.PORT||8080)"]
