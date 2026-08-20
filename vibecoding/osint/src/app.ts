import blessed from "blessed"

export function startApp(): void {
  const screen = blessed.screen({
    smartCSR: true,
    title: "osint",
    fullUnicode: true,
  })

  // ── Main log (left) ──────────────────────────────────────────
  const main = blessed.box({
    parent: screen,
    top: 0,
    left: 0,
    width: "70%",
    height: "100%-3",
    label: " osint ",
    tags: true,
    border: { type: "line" },
    style: {
      fg: "white",
      bg: "black",
      border: { fg: "blue" },
      label: { fg: "blue" },
    },
    scrollable: true,
    alwaysScroll: true,
    scrollbar: {
      ch: "│",
      style: { fg: "blue" },
    },
  })

  const log = blessed.log({
    parent: main,
    top: 0,
    left: 0,
    width: "100%-2",
    height: "100%-2",
    tags: true,
    scrollable: true,
    alwaysScroll: true,
    style: { fg: "white", bg: "black" },
  })

  // ── Sidebar (right) ──────────────────────────────────────────
  const sidebar = blessed.box({
    parent: screen,
    top: 0,
    right: 0,
    width: "30%",
    height: "100%-3",
    label: " context ",
    tags: true,
    border: { type: "line" },
    style: {
      fg: "gray",
      bg: "black",
      border: { fg: "gray" },
      label: { fg: "gray" },
    },
    content: [
      "{bold}Session{/bold}",
      "  osint-demo",
      "",
      "{bold}Status{/bold}",
      "  ready",
      "",
      "{bold}Shortcuts{/bold}",
      "  Enter  submit",
      "  Ctrl+C quit",
      "  ↑/↓    scroll",
    ].join("\n"),
  })

  // ── Prompt bar (bottom, full width) ─────────────────────────
  const promptBox = blessed.box({
    parent: screen,
    bottom: 0,
    left: 0,
    width: "100%",
    height: 3,
    border: { type: "line" },
    style: {
      border: { fg: "cyan" },
      bg: "black",
    },
  })

  const input = blessed.textbox({
    parent: promptBox,
    top: 0,
    left: 2,
    width: "100%-4",
    height: 1,
    keys: true,
    inputOnFocus: true,
    style: {
      fg: "white",
      bg: "black",
    },
  })

  // Prefix ">"
  blessed.text({
    parent: promptBox,
    top: 0,
    left: 0,
    width: 2,
    height: 1,
    content: "{cyan-fg}>{/cyan-fg}",
    tags: true,
  })

  // ── Bootstrap messages ───────────────────────────────────────
  log.log("{cyan-fg}Welcome to osint.{/cyan-fg}")
  log.log("Layout: main · right sidebar · bottom prompt.")
  log.log("Type a command and press Enter. Ctrl+C to quit.")
  log.log("")

  // ── Submit ───────────────────────────────────────────────────
  input.on("submit", (value: string) => {
    const text = (value ?? "").trim()
    input.clearValue()
    input.focus()

    if (!text) {
      screen.render()
      return
    }

    log.log(`{blue-fg}> ${text}{/blue-fg}`)
    log.log(`  {gray-fg}(stub) received: ${text}{/gray-fg}`)
    log.log("")

    sidebar.setContent(
      [
        "{bold}Session{/bold}",
        "  osint-demo",
        "",
        "{bold}Status{/bold}",
        `  last: ${text}`,
        "",
        "{bold}Shortcuts{/bold}",
        "  Enter  submit",
        "  Ctrl+C quit",
        "  ↑/↓    scroll",
      ].join("\n"),
    )

    screen.render()
  })

  // ── Keys ─────────────────────────────────────────────────────
  screen.key(["C-c", "q"], () => {
    process.exit(0)
  })

  input.focus()
  screen.render()
}
