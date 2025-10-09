/***===========================================================================
  Email Server
===========================================================================***/

const log = console;

interface SMTPState {
  greetingSent: boolean;
  inTransaction: boolean;
  from: string | null;
  to: string | null;
  subject: string | null;
  dataBuffer: string[];
}

async function handleConnection(conn: Deno.Conn): Promise<void> {
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  const state: SMTPState = {
    greetingSent: false,
    inTransaction: false,
    from: null,
    to: null,
    subject: null,
    dataBuffer: [],
  };

  // Send initial greeting
  if (!state.greetingSent) {
    await conn.write(encoder.encode("220 localhost ESMTP Ready\r\n"));
    state.greetingSent = true;
  }

  let buffer = new Uint8Array(1024);
  let partialLine = "";

  while (true) {
    const n = await conn.read(buffer);
    if (n === null) break; // Connection closed

    const chunk = decoder.decode(buffer.subarray(0, n));
    partialLine += chunk;

    // Process complete lines
    let lineEnd;
    while ((lineEnd = partialLine.indexOf("\r\n")) !== -1) {
      const line = partialLine.slice(0, lineEnd).trim();
      partialLine = partialLine.slice(lineEnd + 2);

      if (line === "") continue; // Empty line

      log.debug(" ->", line);

      const response = await processCommand(conn, line, state, encoder);
      if (response.endsWith("221")) {
        await conn.write(encoder.encode(response));
        await conn.close();
        return;
      } else {
        await conn.write(encoder.encode(response));
      }
    }
  }

  await conn.close();
}

async function processCommand(
  conn: Deno.Conn,
  line: string,
  state: SMTPState,
  encoder: TextEncoder,
): Promise<string> {
  if (!state.greetingSent) {
    state.greetingSent = true;
    return "220 localhost ESMTP Ready\r\n";
  }

  const parts = line.split(" ");
  const command = parts[0];

  switch (command) {
    case "EHLO":
    case "HELO":
      return "250 localhost Hello\r\n";

    case "MAIL":
      if (state.inTransaction) {
        return "503 Bad sequence of commands\r\n";
      }
      state.from = parts[1];
      state.inTransaction = true;
      return "250 OK\r\n";

    case "RCPT":
      if (!state.inTransaction || state.from === null) {
        return "503 Bad sequence of commands\r\n";
      }
      state.to = parts[1];
      return "250 OK\r\n";

    case "DATA":
      if (!state.inTransaction || state.from === null || state.to === null) {
        return "503 Bad sequence of commands\r\n";
      }
      await conn.write(
        encoder.encode("354 Start mail input; end with <CRLF>.<CRLF>\r\n"),
      );
      return ""; // Wait for data

    case "QUIT":
      return "221 localhost closing connection\r\n";

    default:
      if (state.inTransaction && command === ".") {
        // End of DATA
        const emailBody = state.dataBuffer.join("\r\n");
        state.inTransaction = false;
        state.from = null;
        state.to = null;
        state.dataBuffer = [];
        return "250 OK\r\n";
      } else if (state.inTransaction && line.startsWith(".")) {
        // Data line starting with ., escape it
        state.dataBuffer.push(line.slice(1));
        return ""; // Continue reading data
      } else if (state.inTransaction && state.subject === null) {
        return "";
      } else {
        return "";
      }
  }
}

async function startServer() {
  // Explicitly bind to all interfaces (IPv4).
  const listener = Deno.listen({ hostname: "0.0.0.0", port: 25 });

  log.info("SMTP Server listening on all interfaces, port 25");

  for await (const conn of listener) {
    handleConnection(conn);
  }
}

if (import.meta.main) {
  startServer();
}
