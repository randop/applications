const log = console;
const express = require("express");
const http = require("http");
const socketIo = require("socket.io");

const app = express();
const server = http.createServer(app);
const io = socketIo(server, { cors: { origin: "*" } });

let todos = []; // In-memory store

app.use(express.static("public")); // Serve frontend
app.use(express.json());

// HTTP endpoints for initial CRUD (fallback for non-WebSocket)
app.get("/todos", (req, res) => res.json(todos));

app.get("/todos/:id", (req, res) => {
  const id = parseInt(req.params.id);
  const index = todos.findIndex((t) => t.id === id);
  if (index !== -1) {
    res.json(todos[index]);
  } else {
    res.status(404).send();
  }
});

app.post("/todos", (req, res) => {
  const todo = { id: Date.now(), ...req.body, completed: false };
  todos.push(todo);
  io.emit("todoCreated", todo); // Broadcast event
  res.json(todo);
});

app.put("/todos/:id", (req, res) => {
  const id = parseInt(req.params.id);
  const index = todos.findIndex((t) => t.id === id);
  if (index !== -1) {
    todos[index] = { ...todos[index], ...req.body };
    io.emit("todoUpdated", todos[index]); // Broadcast delta
    res.json(todos[index]);
  } else {
    res.status(404).send();
  }
});

app.delete("/todos/:id", (req, res) => {
  const id = parseInt(req.params.id);
  const index = todos.findIndex((t) => t.id === id);
  if (index !== -1) {
    const deletedTodo = todos.splice(index, 1)[0];
    io.emit("todoDeleted", deletedTodo.id); // Broadcast ID only
    res.json(deletedTodo);
  } else {
    res.status(404).send();
  }
});

// Socket.io for real-time
io.on("connection", (socket) => {
  log.debug("User connected");
  socket.emit("todosLoaded", todos); // Initial sync on connect

  socket.on("disconnect", () => log.debug("User disconnected"));
});

const PORT = 3000;
const HOST = "0.0.0.0";
server.listen(
  PORT,
  HOST,
  () => console.log(`Server running ${HOST} on port ${PORT}`),
);
