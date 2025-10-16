const socket = io();
const todoList = document.getElementById("todoList");
const taskInput = document.getElementById("taskInput");

fetch("/todos")
  .then((res) => res.json())
  .then((todos) => renderTodos(todos));

function renderTodos(todos) {
  todoList.innerHTML = todos.map((todo) => `
      <li data-id="${todo.id}">
        <span style="text-decoration: ${
    todo.completed ? "line-through" : "none"
  }">${todo.task}</span>
        <button onclick="toggleTodo(${todo.id})">${
    todo.completed ? "Undo" : "Complete"
  }</button>
        <button onclick="deleteTodo(${todo.id})">Delete</button>
      </li>
    `).join("");
}

socket.on("todosLoaded", renderTodos);
socket.on("todoCreated", (todo) => {
  const li = document.createElement("li");
  li.setAttribute("data-id", todo.id);
  li.innerHTML =
    `<span>${todo.task}</span> <button onclick="toggleTodo(${todo.id})">Complete</button> <button onclick="deleteTodo(${todo.id})">Delete</button>`;
  todoList.appendChild(li);
});

socket.on("todoUpdated", (todo) => {
  const li = todoList.querySelector(`li[data-id="${todo.id}"]`);
  if (li) {
    li.innerHTML = `<span style="text-decoration: ${
      todo.completed ? "line-through" : "none"
    }">${todo.task}</span>
                      <button onclick="toggleTodo(${todo.id})">${
      todo.completed ? "Undo" : "Complete"
    }</button>
                      <button onclick="deleteTodo(${todo.id})">Delete</button>`;
  }
});
socket.on("todoDeleted", (id) => {
  const li = todoList.querySelector(`li[data-id="${id}"]`);
  if (li) li.remove();
});

async function createTodo() {
  const task = taskInput.value;
  if (!task) return;
  await fetch("/todos", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ task }),
  });
  taskInput.value = "";
}

async function toggleTodo(id) {
  const res = await fetch(`/todos/${id}`);
  const todo = await res.json();
  const updated = { ...todo, completed: !todo.completed };
  await fetch(`/todos/${id}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ completed: updated.completed }),
  });
}

async function deleteTodo(id) {
  await fetch(`/todos/${id}`, { method: "DELETE" });
}

taskInput.addEventListener("keypress", (e) => {
  if (e.key === "Enter") createTodo();
});
